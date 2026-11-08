// FmEngine.h - end-to-end simulation of an FM broadcast link.
//
//   audio -> station processing -> pre-emphasis -> 15 kHz LP -> stereo MPX (19k pilot, 38k DSB-SC, 57k RDS)
//         -> FM modulator (75 kHz deviation) -> RF channel (multipath, fading, interferers, noise, impulses)
//         -> receiver IF filter -> limiter -> quadrature discriminator -> stereo PLL decoder
//         -> soft-mute / blend / high-cut -> de-emphasis -> audio
//
// The whole chain runs at complex baseband at ~420-580 kHz. FM "noise triangle", threshold clicks,
// capture effect, stereo hiss penalty and multipath distortion all emerge from the simulation.
#pragma once
#include "RadioCommon.h"
#include "Eras.h"

namespace radiosim
{

struct FmParams
{
    int    era = 6;
    double cnrDb = 40.0;        // median carrier-to-noise ratio in the IF bandwidth
    bool   stereo = true;
    double modDepth = 0.85;     // input gain into the station processor (1.0 = full scale)
    int    region = 0;          // 0 = 75 us pre-emphasis & 60 Hz, 1 = 50 us & 50 Hz
    double txProc = -1.0;       // <0: use the era's typical station processing
    bool   rds = true;
    double multipath = 0.3;     // 0..1
    double speedKmh = 0.0;      // receiver motion (Doppler fading rate)
    double fMHz = 98.7;
    double shadowSigmaDb = 0.0; // slow log-normal fading when moving
    double adjLevelDb = -100.0; // first-adjacent (+200 kHz) station, dB relative to wanted carrier
    double adj2LevelDb = -100.0;// second-adjacent (-400 kHz)
    double coLevelDb = -100.0;  // co-channel station
    double impulse = 0.0;       // 0..1 ignition / electrical impulse noise
    double detuneKHz = 0.0;     // tuning error
    double noiseScale = 1.0;    // 0 disables channel noise (used for tests / latency measurement)
    bool   smartFeatures = true;// era-appropriate soft-mute / blend / high-cut
    double floorScale = 1.0;    // receiver hiss & hum amount
};

class FmEngine
{
public:
    static constexpr int kMaxUp = 24;
    static constexpr double kDevHz = 75000.0;
    static constexpr double kNoiseDetCal = -10.9; // dB, calibrated against forced-CNR renders (tools/analyze_fm.py)

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        up = std::min (kMaxUp, std::max (1, (int) std::ceil (420000.0 / sr)));
        fsi = sr * up;
        dt = 1.0 / fsi;
        for (int c = 0; c < 2; ++c)
        {
            interp[c].init (up, 16, 0.8);   // images are removed again by the 15 kHz MPX low-pass
            decim[c].init (up, 32, 0.8);
        }
        rng.reseed (20251);
        for (auto& f : fad) f.init (rng);
        const double w19 = kTwoPi * 19000.0 / fsi;
        c19 = std::cos (w19);
        s19 = std::sin (w19);
        impDecay = std::exp (-1.0 / (6e-6 * fsi));
        rdsC = std::cos (kTwoPi * 1187.5 * dt);
        rdsS = std::sin (kTwoPi * 1187.5 * dt);
        tunerOsc.reset (0.0);
        driftCount = 0;
        designed = false;
        p = FmParams();
        setParams (p);
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            interp[c].reset(); decim[c].reset();
            txPre[c].reset(); txLp[c].reset(); deemph[c].reset(); hcc[c].reset();
            lpOut[c].reset();
        }
        lpMid.reset(); lpSide.reset(); pilotBpf.reset(); pilotNotch.reset();
        nd1.reset(); nd2.reset(); ifFilt.reset();
        pilotOsc.reset (0.0);
        txOsc.reset (0.0);
        pllOsc.reset (0.0);
        pllI = 0.0; pllPhaseInt = 0.0;
        pilotLvl = 0.0; pilotQ = 0.0; stereoGate = 0.0;
        zPrev = { 1.0, 0.0 };
        dcX = dcY = 0.0;
        for (auto& d : delay) d.reset();
        compEnv = 0.0; compGain = 1.0;
        noiseDetPow = 1e-6; cnrEst = 40.0;
        muteGain = 1.0; blendAmt = 1.0; hccFc = 16000.0;
        rssi = 1.0; humPhase = 0.0;
        imp = { 0.0, 0.0 };
        rdsT = 0.0; rdsBit = 1.0;
        fadeCount = 0;
    }

    void setParams (const FmParams& np)
    {
        const bool redesign = ! designed || np.era != p.era || np.region != p.region;
        p = np;
        eraIdx = std::max (0, std::min (kNumEras - 1, p.era));
        E = kFmEras[eraIdx];
        if (redesign) designFilters();
        designed = true;

        txStereo = p.stereo && E.txStereo;
        rxStereo = txStereo && E.decoder >= 1;
        txProcAmt = p.txProc >= 0.0 ? p.txProc : E.txProc;
        rdsAmp = (p.rds && txStereo) ? E.rds : 0.0;

        const double cnrLin = dbToPow (p.cnrDb);
        sigmaTarget = std::sqrt (0.5 * fsi / (E.ifBwHz * cnrLin)) * p.noiseScale;

        fdHz = p.speedKmh / 3.6 * p.fMHz * 1e6 / kSpeedOfLight;
        mp = clampd (p.multipath, 0.0, 1.0);
        const double moving = clampd (p.speedKmh / 15.0, 0.0, 1.0);
        shadowSigma = p.shadowSigmaDb * moving;

        adjTarget = p.adjLevelDb > -90.0 ? dbToAmp (p.adjLevelDb) : 0.0;
        adj2Target = p.adj2LevelDb > -90.0 ? dbToAmp (p.adj2LevelDb) : 0.0;
        coTarget = p.coLevelDb > -90.0 ? dbToAmp (p.coLevelDb) : 0.0;
        adjRot = { std::cos (kTwoPi * 200000.0 * dt), std::sin (kTwoPi * 200000.0 * dt) };
        adj2Rot = { std::cos (-kTwoPi * 400000.0 * dt), std::sin (-kTwoPi * 400000.0 * dt) };
        coRot = { std::cos (kTwoPi * 37.0 * dt), std::sin (kTwoPi * 37.0 * dt) };

        impRate = 700.0 * p.impulse * p.impulse;
        impProb = impRate * dt;
        detuneHz = p.detuneKHz * 1000.0;
    }

    // In-place stereo processing at the host sample rate.
    void process (float* l, float* r, int n)
    {
        double upL[kMaxUp], upR[kMaxUp];
        for (int i = 0; i < n; ++i)
        {
            double a = (double) l[i] * p.modDepth, b = (double) r[i] * p.modDepth;
            // ---- station compressor (linked stereo, feed-forward)
            {
                const double pk = std::max (std::fabs (a), std::fabs (b));
                compEnv = pk > compEnv ? compEnv + compAtk * (pk - compEnv) : compEnv + compRel * (pk - compEnv);
                const double thr = dbToAmp (-4.0 - 22.0 * txProcAmt);
                const double ratio = 1.5 + 6.5 * txProcAmt;
                double g = 1.0;
                if (compEnv > thr) g = std::pow (compEnv / thr, 1.0 / ratio - 1.0);
                const double makeup = dbToAmp ((4.0 + 22.0 * txProcAmt) * (1.0 - 1.0 / ratio) * 0.85);
                compGain += 0.02 * (g * makeup - compGain);
                a *= compGain; b *= compGain;
            }
            interp[0].process (a, upL);
            interp[1].process (b, upR);
            double oL = 0, oR = 0, yl = 0, yr = 0;
            for (int k = 0; k < up; ++k)
            {
                tick (upL[k], upR[k], oL, oR);
                decim[0].process (oL, yl);
                decim[1].process (oR, yr);
            }
            // decimators emit exactly once per host sample, on the last phase
            double outL = yl, outR = yr;
            postProcess (outL, outR);
            l[i] = (float) outL;
            r[i] = (float) outR;
        }
    }

    // ---- metering (read from the UI thread; benign races)
    double meterCnrDb() const { return cnrEst; }
    double meterRssiDb() const { return ampToDb (rssi); }
    double meterStereo() const { return rxStereo ? stereoGate * blendAmt : 0.0; }
    double meterMute() const { return muteGain; }
    double meterHighCutHz() const { return hccFc; }
    double internalRate() const { return fsi; }

private:
    //------------------------------------------------------------------ design
    void designFilters()
    {
        const double tau = (p.region == 0 ? 75e-6 : 50e-6);
        for (int c = 0; c < 2; ++c)
        {
            txPre[c].setPoleZero (fsi, 1.0 / tau, kTwoPi * 100e3);
            txLp[c].designLowpass (fsi, 15000.0, 8);
            deemph[c].setLowpassHz (fsi, 1.0 / (kTwoPi * tau));
        }
        ifFilt.design (fsi, E.ifBwHz * 0.5, E.ifOrder);
        lpMid.designLowpass (fsi, E.decLpHz, E.decLpOrder);
        lpSide.designLowpass (fsi, E.decLpHz, E.decLpOrder);
        pilotBpf.setBandpass (fsi, 19000.0, 12.0);
        if (E.pilotNotchQ > 0.0) pilotNotch.setNotch (fsi, 19000.0, E.pilotNotchQ); else pilotNotch.setIdentity();
        nd1.setHighpass (fsi, 70000.0, 0.7071);
        nd2.setLowpass (fsi, 90000.0, 0.7071);
        const double wn = kTwoPi * std::max (E.pllBwHz, 10.0) * 0.5;
        pllKp = 2.0 * 0.7071 * wn * dt;
        pllKi = wn * wn * dt * dt;
        dcR = 1.0 - kTwoPi * 12.0 * dt;
        compAtk = onePoleCoef (0.004, sr);
        compRel = onePoleCoef (0.180, sr);
        const double fin = (double) up;
        (void) fin;
        for (auto& d : lpOut) d.setIdentity();
    }

    //------------------------------------------------------------------ one internal-rate sample
    inline void tick (double l, double r, double& outL, double& outR)
    {
        // ======================= transmitter =======================
        l = softLimit (txPre[0].process (l), 0.75 - 0.35 * txProcAmt);
        r = softLimit (txPre[1].process (r), 0.75 - 0.35 * txProcAmt);
        l = txLp[0].process (l);
        r = txLp[1].process (r);
        pilotOsc.rotate (c19, s19);
        const double pc = pilotOsc.z.re, ps = pilotOsc.z.im;
        const double s38 = 2.0 * ps * pc;
        const double mid = 0.5 * (l + r), side = 0.5 * (l - r);
        double m = 0.9 * mid;
        if (txStereo) m += 0.9 * side * s38 + 0.09 * ps;
        if (rdsAmp > 0.0)
        {
            rdsT += 1187.5 * dt;
            if (rdsT >= 1.0) { rdsT -= 1.0; rdsBit = rng.uniform() < 0.5 ? -1.0 : 1.0; rdsOsc.reset (0.0); }
            rdsOsc.rotate (rdsC, rdsS);
            const double c57 = pc * (4.0 * pc * pc - 3.0);
            m += rdsAmp * rdsBit * rdsOsc.z.im * c57;
        }
        txOsc.advance (kTwoPi * kDevHz * dt * m);
        Cx s = txOsc.z;

        // ======================= RF channel =======================
        if (++fadeCount >= 256)
        {
            fadeCount = 0;
            const double step = 256.0 * dt;
            for (int g = 0; g < 4; ++g) fadeV[g] = fad[g].step (fdHz, step);
            shadowTick();
        }
        delay[0].push (s);
        static const double kEchoUs[3] = { 2.8, 5.6, 10.5 };
        static const double kEchoAmp[3] = { 0.60, 0.45, 0.30 };
        Cx y;
        {
            // direct path: Rician-style, fading depth grows with multipath richness
            const Cx f0 = fadeV[0];
            const double c1 = 0.9 * mp;
            y = { s.re + c1 * (s.re * f0.re - s.im * f0.im), s.im + c1 * (s.re * f0.im + s.im * f0.re) };
        }
        if (mp > 0.001)
        {
            for (int e = 0; e < 3; ++e)
            {
                const double d = std::max (1.1, kEchoUs[e] * 1e-6 * fsi);
                const Cx t = delay[0].tap (d);
                const Cx g = fadeV[e + 1] * (mp * kEchoAmp[e]);
                y = y + t * g;
            }
        }
        y = y * shadowAmp;

        // interferers (smooth level changes)
        adjAmp += 0.0005 * (adjTarget - adjAmp);
        adj2Amp += 0.0005 * (adj2Target - adj2Amp);
        coAmp += 0.0005 * (coTarget - coAmp);
        if (adjAmp > 1e-5) y = y + interferer (adjOsc, adjMod, adjRot, adjAmp, 0);
        if (adj2Amp > 1e-5) y = y + interferer (adj2Osc, adj2Mod, adj2Rot, adj2Amp, 1);
        if (coAmp > 1e-5) y = y + interferer (coOsc, coMod, coRot, coAmp, 2);

        // thermal + galactic + man-made noise (Gaussian)
        sigma += 0.0005 * (sigmaTarget - sigma);
        y.re += sigma * rng.gauss();
        y.im += sigma * rng.gauss();

        // impulse noise (ignition, motors, switching): short, random-phase, heavy-tailed bursts
        if (impProb > 0.0)
        {
            if (rng.uniform() < impProb)
            {
                const double amp = std::exp (-1.5 + 3.0 * rng.uniform());
                imp = imp + expj (kTwoPi * rng.uniform()) * amp;
            }
            imp = imp * impDecay;
            y = y + imp;
        }

        // local-oscillator tuning error + slow drift
        if (--driftCount <= 0)
        {
            driftCount = 64;
            driftState += 64.0 * dt / 0.8 * (rng.gauss() * 6.0 - driftState);
            const double f = detuneHz + E.driftHz * driftState * 0.4;
            const double w = kTwoPi * f * dt;
            rotC = std::cos (w); rotS = std::sin (w);
        }
        tunerOsc.rotate (rotC, rotS);
        y = y * tunerOsc.z;

        // ======================= receiver =======================
        y = ifFilt.process (y);
        const double mag2 = y.re * y.re + y.im * y.im;
        const double mag = std::sqrt (mag2);
        rssi += 2e-5 * (mag - rssi);
        const double inv = 1.0 / (mag + 1e-12);
        const Cx zl { y.re * inv, y.im * inv };        // hard limiter
        const Cx dd = zl * cconj (zPrev);
        zPrev = zl;
        double x = fastAtan2 (dd.im, dd.re) * (fsi / (kTwoPi * kDevHz));
        x -= E.detNonlin * x * x * x;
        // DC blocker (removes tuning offset)
        {
            const double yv = x - dcX + dcR * dcY;
            dcX = x; dcY = yv;
            x = yv;
        }

        // ---- noise detector (70..90 kHz composite energy) for soft-mute / blend / high-cut
        {
            const double nb = nd2.process (nd1.process (x));
            noiseDetPow += 2e-5 * (nb * nb - noiseDetPow);
            cnrEst = kNoiseDetCal - 10.0 * std::log10 (std::max (noiseDetPow, 1e-12));
            cnrEst = clampd (cnrEst, -5.0, 70.0);
        }

        // ---- stereo decoder
        double M = lpMid.process (x) / 0.9;
        if (E.pilotNotchQ > 0.0) M = pilotNotch.process (M);
        double L = M, R = M;
        if (rxStereo)
        {
            const double pb = pilotBpf.process (x);
            const double e = pb * pllOsc.z.re * (1.0 / 0.045);
            pllI += pllKi * e;
            pllOsc.advance (kTwoPi * 19000.0 * dt + pllKp * e + pllI);
            const double sn = pllOsc.z.im, cs = pllOsc.z.re;
            pilotLvl += 0.0004 * (pb * sn * (2.0 / 0.09) - pilotLvl);
            pilotQ += 0.0004 * (pb * cs * (2.0 / 0.09) - pilotQ);
            const bool good = pilotLvl > 0.45 && std::fabs (pilotQ) < 0.45 * pilotLvl;
            stereoGate += (good ? 0.0006 : 0.003) * ((good ? 1.0 : 0.0) - stereoGate);
            const double D = lpSide.process (x * 2.0 * sn * cs) * 2.0 / 0.9;
            const double S = D * stereoGate * blendAmt;
            L = M + S;
            R = M - S;
            if (E.separationDb < 48.0)
            {
                const double k = dbToAmp (-E.separationDb);
                const double l2 = L + k * R, r2 = R + k * L;
                L = l2; R = r2;
            }
        }
        outL = deemph[0].process (L);
        outR = deemph[1].process (R);
    }

    // ------------------------------------------------------------------ post (host rate)
    inline void postProcess (double& l, double& r)
    {
        // smart-receiver behaviour (updated every 16 host samples)
        if ((postCount++ & 15) == 0)
        {
            const double lo = E.smartLoDb, hi = E.smartHiDb;
            double tMute = 1.0, tBlend = 1.0, tHcc = 17000.0;
            if (p.smartFeatures)
            {
                if (E.softMute) tMute = dbToAmp (-28.0 * (1.0 - smoothstep (lo - 4.0, lo + 3.0, cnrEst)));
                if (E.blend) tBlend = smoothstep (lo + 3.0, hi, cnrEst);
                if (E.hcc) tHcc = lerpd (2200.0, 17000.0, smoothstep (lo, hi + 6.0, cnrEst));
            }
            muteGain += 0.15 * (tMute - muteGain);
            blendAmt += 0.15 * (tBlend - blendAmt);
            hccFc += 0.15 * (tHcc - hccFc);
            if (hccFc < 16000.0) { hcc[0].setLowpass (sr, hccFc, 0.7071); hcc[1].setLowpass (sr, hccFc, 0.7071); hccActive = true; }
            else hccActive = false;
        }
        if (hccActive) { l = hcc[0].process (l); r = hcc[1].process (r); }
        l *= muteGain; r *= muteGain;
        // receiver noise floor: amplifier hiss + mains hum
        const double hiss = E.hiss * p.floorScale;
        if (hiss > 0.0) { l += hiss * rng.gauss(); r += hiss * rng.gauss(); }
        const double hum = E.humAmp * p.floorScale;
        if (hum > 0.0)
        {
            const double f = (p.region == 0 ? 60.0 : 50.0);
            humPhase += kTwoPi * f / sr;
            if (humPhase > kTwoPi) humPhase -= kTwoPi;
            const double h = hum * (std::sin (humPhase) + 0.45 * std::sin (2.0 * humPhase + 0.7) + 0.2 * std::sin (3.0 * humPhase));
            l += h; r += h;
        }
    }

    //------------------------------------------------------------------ helpers
    struct IntState { Oscillator mod; double lp1 = 0, lp2 = 0, env = 0; };

    // Synthetic "other station": FM carrier with speech/music-like noise modulation (~+-20 kHz rms).
    inline Cx interferer (Oscillator& off, IntState& st, const Cx& rot, double amp, int)
    {
        off.rotate (rot.re, rot.im);
        const double n = rng.gauss();
        st.lp1 += 0.020 * (n - st.lp1);          // ~1.4 kHz corner at 440 kHz
        st.lp2 += 0.020 * (st.lp1 - st.lp2);
        st.env += 0.00003 * (std::fabs (rng.gauss()) - st.env);   // syllabic envelope
        const double u = st.lp2 * 7.0 * (0.3 + 2.0 * st.env);
        st.mod.advance (kTwoPi * kDevHz * 0.30 * clampd (u, -2.5, 2.5) * dt);   // ~22 kHz rms, peaks ~56 kHz
        return (off.z * st.mod.z) * amp;
    }

    void shadowTick()
    {
        if (shadowSigma <= 0.0) { shadowAmp += 0.01 * (1.0 - shadowAmp); return; }
        const double alpha = 256.0 * dt / 1.2;
        shadowState += alpha * (rng.gauss() - shadowState);
        const double stdev = std::sqrt (alpha / (2.0 - alpha));
        const double dB = shadowSigma * shadowState / stdev;
        shadowAmp += 0.02 * (dbToAmp (dB) - shadowAmp);
    }

public:
    // exposed so tests can reach the era table in use
    const FmEra& era() const { return E; }

private:
    double sr = 44100, fsi = 441000, dt = 1.0 / 441000;
    int up = 10;
    FmParams p;
    int eraIdx = 6;
    FmEra E = kFmEras[6];
    bool designed = false;

    PolyInterp interp[2];
    PolyDecim decim[2];
    FirstOrder txPre[2], deemph[2];
    ButterChain txLp[2];
    ComplexLowpass ifFilt;
    ButterChain lpMid, lpSide;
    Biquad pilotBpf, pilotNotch, nd1, nd2, hcc[2], lpOut[2];
    FracDelayC delay[1];
    FadingGen fad[4];
    Cx fadeV[4] { { 1, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
    Rng rng { 1 };

    Oscillator pilotOsc, txOsc, pllOsc, tunerOsc, adjOsc, adj2Osc, coOsc, rdsOsc;
    double rdsC = 1, rdsS = 0;
    IntState adjMod, adj2Mod, coMod;
    Cx adjRot, adj2Rot, coRot;
    double c19 = 1, s19 = 0;
    double pllKp = 0, pllKi = 0, pllI = 0, pllPhaseInt = 0;
    double pilotLvl = 0, pilotQ = 0, stereoGate = 0;
    double dcX = 0, dcY = 0, dcR = 0.999;
    Cx zPrev { 1, 0 };

    bool txStereo = true, rxStereo = true;
    double txProcAmt = 0.5, rdsAmp = 0.0;
    double compEnv = 0, compGain = 1, compAtk = 0.01, compRel = 0.0001;
    double sigma = 0, sigmaTarget = 0;
    double fdHz = 0, mp = 0;
    int fadeCount = 0;
    double shadowSigma = 0, shadowState = 0, shadowAmp = 1.0;
    double adjAmp = 0, adj2Amp = 0, coAmp = 0, adjTarget = 0, adj2Target = 0, coTarget = 0;
    double impRate = 0, impProb = 0;
    double impDecay = std::exp (-1.0 / (6e-6 * 441000.0));
    Cx imp { 0, 0 };
    double detuneHz = 0, driftState = 0, rotC = 1, rotS = 0;
    int driftCount = 0;
    double rdsT = 0, rdsBit = 1;
    double noiseDetPow = 1e-6, cnrEst = 40;
    double muteGain = 1, blendAmt = 1, hccFc = 17000;
    bool hccActive = false;
    unsigned postCount = 0;
    double rssi = 1;
    double humPhase = 0;
};

} // namespace radiosim
