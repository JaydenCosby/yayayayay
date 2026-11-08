// AmEngine.h - end-to-end simulation of an amplitude-modulated medium-wave broadcast link.
//
//   audio (mono) -> station processing -> asymmetric clipper -> audio band-limit (NRSC-style)
//        -> AM modulator  s(t) = 1 + m x(t)
//        -> channel: groundwave + two-ray ionospheric skywave (Rayleigh fading, selective fading),
//                    adjacent-channel carriers (heterodyne whistles), co-channel beat, atmospheric
//                    crackle, power-line buzz, receiver noise
//        -> noise blanker -> tuner offset -> IF filter -> AGC -> envelope (diode) or synchronous detector
//        -> whistle notch -> audio filters -> speaker
#pragma once
#include "RadioCommon.h"
#include "Eras.h"

namespace radiosim
{

struct AmParams
{
    int    era = 4;
    double cnrDb = 40.0;        // total carrier (ground + sky) to noise in the IF bandwidth
    double skyRatio = 0.0;      // skywave / groundwave amplitude ratio
    double skyDelayMs = 0.2;    // skywave excess delay
    double skyFadeHz = 0.4;     // fading rate of the ionospheric path
    double modDepth = 0.9;      // input gain into the station processor
    double amDepth = 1.0;       // modulation index at 100% program level
    double txProc = -1.0;       // <0 -> era default
    int    region = 0;          // 0 = 10 kHz channel spacing & 60 Hz, 1 = 9 kHz & 50 Hz
    double adjLevelDb = -100.0; // +1 channel, dB relative to the wanted carrier
    double adj2LevelDb = -100.0;// -2 channels
    double coLevelDb = -100.0;  // co-channel
    double staticAmt = 0.0;     // 0..1 atmospheric crackle (lightning, solar)
    double buzzAmt = 0.0;       // 0..1 power-line buzz
    double detuneKHz = 0.0;
    double noiseScale = 1.0;
    bool   smartFeatures = true;
    double floorScale = 1.0;
};

class AmEngine
{
public:
    static constexpr int kMaxUp = 8;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        up = std::min (kMaxUp, std::max (1, (int) std::ceil (110000.0 / sr)));
        fsi = sr * up;
        dt = 1.0 / fsi;
        interp.init (up, 24, 0.9);
        decim.init (up, 24, 0.85);
        rng.reseed (7771);
        skyFad[0].init (rng);
        skyFad[1].init (rng);
        impDecay = std::exp (-1.0 / (40e-6 * fsi));
        buzzDecay = std::exp (-1.0 / (70e-6 * fsi));
        tuner.reset (0.0);
        designed = false;
        p = AmParams();
        setParams (p);
        reset();
    }

    void reset()
    {
        interp.reset(); decim.reset();
        txLp.reset(); ifFilt.reset(); hp.reset(); lp.reset(); whistle.reset(); hccLp.reset();
        sky.reset();
        compEnv = 0; compGain = 1;
        cHat = 1.0; avgPre = 1.0; nbHold = 0; lastGood = { 1.0, 0.0 };
        detY = 1.0;
        syncC = { 1.0, 0.0 };
        imp = { 0, 0 }; buzzImp = { 0, 0 };
        buzzCount = 0; humPhase = 0; fadeCount = 0;
        rssi = 1.0;
        driftState = 0; driftCount = 0;
        for (auto& o : oscs) o.reset (0.0);
        for (auto& st : intSt) { st.lp1 = st.lp2 = 0; }
    }

    void setParams (const AmParams& np)
    {
        const bool redesign = ! designed || np.era != p.era || np.region != p.region;
        p = np;
        eraIdx = std::max (0, std::min (kNumEras - 1, p.era));
        E = kAmEras[eraIdx];
        if (redesign) designFilters();
        designed = true;

        txProcAmt = p.txProc >= 0.0 ? p.txProc : E.txProc;
        const double totalPow = 1.0;
        const double cnrLin = dbToPow (p.cnrDb);
        sigmaTarget = std::sqrt (0.5 * totalPow * fsi / (E.ifBwHz * cnrLin)) * p.noiseScale;
        const double sr2 = p.skyRatio * p.skyRatio;
        groundAmp = 1.0 / std::sqrt (1.0 + sr2);
        skyAmp = p.skyRatio * groundAmp;
        const double maxDelay = (FracDelayC::kSize - 6.0) / fsi;
        const double d1 = clampd (p.skyDelayMs * 1e-3, 1.2 / fsi, maxDelay * 0.7);
        skyD1 = d1 * fsi;
        skyD2 = std::min ((FracDelayC::kSize - 5.0), skyD1 + 0.00012 * fsi);

        const double spacing = (p.region == 0 ? 10000.0 : 9000.0);
        adjTarget = p.adjLevelDb > -90.0 ? dbToAmp (p.adjLevelDb) : 0.0;
        adj2Target = p.adj2LevelDb > -90.0 ? dbToAmp (p.adj2LevelDb) : 0.0;
        coTarget = p.coLevelDb > -90.0 ? dbToAmp (p.coLevelDb) : 0.0;
        const double wAdj = kTwoPi * (spacing + 1.7) * dt;
        const double wAdj2 = -kTwoPi * (2.0 * spacing - 2.3) * dt;
        const double wCo = kTwoPi * 6.5 * dt;
        adjRot = { std::cos (wAdj), std::sin (wAdj) };
        adj2Rot = { std::cos (wAdj2), std::sin (wAdj2) };
        coRot = { std::cos (wCo), std::sin (wCo) };

        const double st = clampd (p.staticAmt, 0.0, 1.0);
        impProb = 260.0 * st * st * dt;
        impLevel = 0.02 + 0.55 * st;
        buzzLevel = 0.30 * p.buzzAmt;
        buzzPeriod = (int) std::lround (fsi / (p.region == 0 ? 120.0 : 100.0));
        detuneHz = p.detuneKHz * 1000.0;
        // real blankers stand down when the signal is already noisy (they would just gate the audio)
        nbActive = p.smartFeatures && E.noiseBlanker && p.cnrDb > 14.0;
    }

    void process (float* l, float* r, int n)
    {
        double upBuf[kMaxUp];
        for (int i = 0; i < n; ++i)
        {
            double a = 0.5 * ((double) l[i] + (double) r[i]) * p.modDepth;
            {
                const double pk = std::fabs (a);
                compEnv = pk > compEnv ? compEnv + compAtk * (pk - compEnv) : compEnv + compRel * (pk - compEnv);
                const double thr = dbToAmp (-3.0 - 20.0 * txProcAmt);
                const double ratio = 1.4 + 6.0 * txProcAmt;
                double g = 1.0;
                if (compEnv > thr) g = std::pow (compEnv / thr, 1.0 / ratio - 1.0);
                const double makeup = dbToAmp ((3.0 + 20.0 * txProcAmt) * (1.0 - 1.0 / ratio) * 0.9);
                compGain += 0.02 * (g * makeup - compGain);
                a *= compGain;
            }
            interp.process (a, upBuf);
            double y = 0.0, o = 0.0;
            for (int k = 0; k < up; ++k)
            {
                o = tick (upBuf[k]);
                decim.process (o, y);
            }
            double out = y;
            const double hiss = E.hiss * p.floorScale;
            if (hiss > 0.0) out += hiss * rng.gauss();
            const double hum = E.humAmp * p.floorScale;
            if (hum > 0.0)
            {
                humPhase += kTwoPi * (p.region == 0 ? 60.0 : 50.0) / sr;
                if (humPhase > kTwoPi) humPhase -= kTwoPi;
                out += hum * (std::sin (humPhase) + 0.5 * std::sin (2.0 * humPhase + 0.4) + 0.25 * std::sin (3.0 * humPhase));
            }
            l[i] = r[i] = (float) out;
        }
    }

    double meterCnrDb() const { return p.cnrDb; }
    double meterRssiDb() const { return ampToDb (rssi); }
    double meterAgcDb() const { return -20.0 * E.agcStrength * std::log10 (std::max (cHat, 1e-6)); }
    double internalRate() const { return fsi; }
    const AmEra& era() const { return E; }

private:
    void designFilters()
    {
        txLp.designLowpass (fsi, std::min (E.txBwHz, 0.45 * sr), 4);
        ifFilt.design (fsi, E.ifBwHz * 0.5, E.ifOrder);
        hp.designHighpass (fsi, std::max (E.audioHpHz, 5.0), 2);
        lp.designLowpass (fsi, E.audioLpHz, E.audioLpOrder);
        const double spacing = (p.region == 0 ? 10000.0 : 9000.0);
        if (E.whistleNotch) whistle.setNotch (fsi, spacing, 6.0); else whistle.setIdentity();
        hccLp.setIdentity();
        compAtk = onePoleCoef (0.003, sr);
        compRel = onePoleCoef (0.250, sr);
        agcAtk = onePoleCoef (std::max (E.agcAttackMs, 1.0) * 1e-3, fsi);
        agcRel = onePoleCoef (std::max (E.agcReleaseMs, 1.0) * 1e-3, fsi);
        detDecay = std::exp (-1.0 / (E.detRcUs * 1e-6 * fsi));
        syncCoef = onePoleCoef (1.0 / (kTwoPi * 18.0), fsi);
    }

    struct IntState { double lp1 = 0, lp2 = 0, env = 0; };

    // Other AM station: carrier with a program-like modulation (about 60 % depth, ~4 kHz wide).
    inline Cx amStation (Oscillator& osc, IntState& st, const Cx& rot, double amp)
    {
        osc.rotate (rot.re, rot.im);
        const double n = rng.gauss();
        st.lp1 += 0.12 * (n - st.lp1);
        st.lp2 += 0.12 * (st.lp1 - st.lp2);
        st.env += 0.0004 * (std::fabs (rng.gauss()) - st.env);
        const double prog = clampd (st.lp2 * 4.2 * (0.4 + 1.6 * st.env), -1.0, 1.25);
        return osc.z * (amp * (1.0 + 0.7 * prog));
    }

    inline double tick (double xin)
    {
        // ---------------- transmitter ----------------
        double x = xin * (1.0 + 0.8 * txProcAmt);
        x = clampd (x, -1.0, E.asym ? 1.25 : 1.0);   // asymmetric modulation: carrier can't be cut, peaks may reach +125 %
        x = txLp.process (x);
        const double env = std::max (0.0, 1.0 + p.amDepth * x);
        const Cx s { env, 0.0 };

        // ---------------- channel ----------------
        if (++fadeCount >= 64)
        {
            fadeCount = 0;
            const double step = 64.0 * dt;
            fadeV[0] = skyFad[0].step (p.skyFadeHz, step);
            fadeV[1] = skyFad[1].step (p.skyFadeHz * 1.35, step);
        }
        sky.push (s);
        Cx y = s * groundAmp;
        if (skyAmp > 1e-4)
        {
            const Cx r1 = sky.tap (skyD1) * fadeV[0];
            const Cx r2 = sky.tap (skyD2) * fadeV[1];
            y = y + (r1 * 0.85 + r2 * 0.55) * skyAmp;
        }
        adjAmp += 0.0008 * (adjTarget - adjAmp);
        adj2Amp += 0.0008 * (adj2Target - adj2Amp);
        coAmp += 0.0008 * (coTarget - coAmp);
        if (adjAmp > 1e-5) y = y + amStation (oscs[0], intSt[0], adjRot, adjAmp);
        if (adj2Amp > 1e-5) y = y + amStation (oscs[1], intSt[1], adj2Rot, adj2Amp);
        if (coAmp > 1e-5) y = y + amStation (oscs[2], intSt[2], coRot, coAmp);

        sigma += 0.0008 * (sigmaTarget - sigma);
        y.re += sigma * rng.gauss();
        y.im += sigma * rng.gauss();

        if (impProb > 0.0)
        {
            if (rng.uniform() < impProb)
                imp = imp + expj (kTwoPi * rng.uniform()) * (impLevel * std::exp (0.9 * rng.gauss()));
            imp = imp * impDecay;
            y = y + imp;
        }
        if (buzzLevel > 0.0)
        {
            if (--buzzCount <= 0)
            {
                buzzCount = buzzPeriod;
                buzzImp = buzzImp + expj (kTwoPi * rng.uniform()) * (buzzLevel * (0.5 + rng.uniform()));
            }
            buzzImp = buzzImp * buzzDecay;
            y = y + buzzImp;
        }

        // ---------------- receiver ----------------
        // noise blanker works on the wideband signal ahead of the narrow IF filter
        {
            const double m = std::sqrt (y.re * y.re + y.im * y.im);
            if (nbActive)
            {
                if (nbHold > 0) { --nbHold; y = lastGood; }
                else if (m > 3.4 * avgPre) { nbHold = (int) (60e-6 * fsi) + 1; y = lastGood; }
                else { avgPre += 0.002 * (m - avgPre); lastGood = y; }
            }
        }
        if (--driftCount <= 0)
        {
            driftCount = 64;
            driftState += 64.0 * dt / 1.5 * (rng.gauss() * 5.0 - driftState);
            const double f = detuneHz + E.driftHz * driftState * 0.3;
            const double w = kTwoPi * f * dt;
            rotC = std::cos (w); rotS = std::sin (w);
        }
        tuner.rotate (rotC, rotS);
        y = ifFilt.process (y * tuner.z);

        const double mag = std::sqrt (y.re * y.re + y.im * y.im);
        rssi += 2e-5 * (mag - rssi);
        // AGC
        cHat += (mag > cHat ? agcAtk : agcRel) * (mag - cHat);
        const double gain = E.agcStrength > 0.0 ? std::pow (std::max (cHat, 1e-4), -E.agcStrength) : 1.0;

        double d;
        if (E.detector == 0)
        {
            const double e = mag * gain;
            detY = std::max (e, detY * detDecay);
            d = detY;
        }
        else
        {
            syncC.re += syncCoef * (y.re - syncC.re);
            syncC.im += syncCoef * (y.im - syncC.im);
            const double cm = std::sqrt (syncC.re * syncC.re + syncC.im * syncC.im) + 1e-9;
            d = (y.re * syncC.re + y.im * syncC.im) / cm * gain;
        }
        d += E.detNonlin * d * d;
        d = hp.process (d);
        d = whistle.process (d);
        d = lp.process (d);
        return d;
    }

    double sr = 44100, fsi = 132300, dt = 1.0 / 132300;
    int up = 3;
    AmParams p;
    int eraIdx = 4;
    AmEra E = kAmEras[4];
    bool designed = false;

    PolyInterp interp;
    PolyDecim decim;
    ButterChain txLp, hp, lp;
    ComplexLowpass ifFilt;
    Biquad whistle, hccLp;
    FracDelayC sky;
    FadingGen skyFad[2];
    Cx fadeV[2] { { 1, 0 }, { 1, 0 } };
    Rng rng { 1 };

    Oscillator oscs[3], tuner;
    IntState intSt[3];
    Cx adjRot, adj2Rot, coRot;

    double txProcAmt = 0.4, compEnv = 0, compGain = 1, compAtk = 0.01, compRel = 0.0001;
    double sigma = 0, sigmaTarget = 0, groundAmp = 1, skyAmp = 0, skyD1 = 10, skyD2 = 20;
    double adjAmp = 0, adj2Amp = 0, coAmp = 0, adjTarget = 0, adj2Target = 0, coTarget = 0;
    double impProb = 0, impLevel = 0, impDecay = 0.9, buzzLevel = 0, buzzDecay = 0.9;
    int buzzPeriod = 1000, buzzCount = 0;
    Cx imp { 0, 0 }, buzzImp { 0, 0 };
    bool nbActive = false; int nbHold = 0; double avgPre = 1.0; Cx lastGood { 1, 0 };
    double detuneHz = 0, driftState = 0, rotC = 1, rotS = 0; int driftCount = 0;
    double cHat = 1.0, agcAtk = 0.001, agcRel = 0.0001, detDecay = 0.99, detY = 1.0;
    Cx syncC { 1, 0 }; double syncCoef = 0.001;
    double rssi = 1.0, humPhase = 0.0;
    int fadeCount = 0;
};

} // namespace radiosim
