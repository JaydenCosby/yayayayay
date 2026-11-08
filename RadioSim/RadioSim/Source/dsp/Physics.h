// Physics.h - propagation, noise and link-budget maths for the FM and AM models.
// Sources: ITU-R P.372 (radio noise), ITU-R BS.450 (FM stereo), Norton surface-wave
// approximation for AM groundwave, free-space + clutter + horizon model for VHF.
// All of it is a *model* chosen to be physically plausible and cheap, not a coverage tool.
#pragma once
#include "RadioCommon.h"

namespace radiosim
{

enum Environment { EnvUrban = 0, EnvSuburban, EnvRural, EnvOpen, kNumEnv };
inline const char* const kEnvNames[kNumEnv] = { "City centre", "Suburb", "Rural", "Open country" };

enum AntennaType { AntTelescopic = 0, AntCarWhip, AntHeadphoneCord, AntWireDipole, AntRooftopYagi, kNumAnt };
inline const char* const kAntNames[kNumAnt] = { "Telescopic whip", "Car whip", "Headphone cord", "T-dipole", "Rooftop Yagi" };

struct EnvData
{
    double clutterLossDb;       // VHF clutter / building loss
    double manMadeC, manMadeD;  // ITU-R P.372 Fam = c - d*log10(f_MHz)
    double multipathBase;       // default multipath richness 0..1
    double shadowSigmaDb;       // slow (log-normal) shadowing std-dev when moving
};
inline const EnvData kEnv[kNumEnv] = {
    { 18.0, 76.8, 27.7, 0.90, 7.0 }, // business
    {  9.0, 72.5, 27.7, 0.55, 5.0 }, // residential
    {  3.0, 67.2, 27.7, 0.25, 3.0 }, // rural
    {  0.0, 53.6, 28.6, 0.10, 2.0 }  // quiet rural
};

//----------------------------------------------------------------------- FM link
struct FmLink
{
    double fieldDbuv = 0;   // median field strength at the receiver, dBuV/m
    double pinDbm = -100;   // available power at the antenna terminals
    double cnrDb = 0;       // carrier-to-noise ratio in the IF bandwidth
    double wavelengthM = 3.0;
    double horizonKm = 70;
};

inline double antennaGainDbi (int type, double fMHz)
{
    // gains for real-world, mostly poorly matched antennas
    const double f0 = 98.0;
    const double mis = 1.0 + 3.0 * std::pow ((fMHz - f0) / f0, 2.0) * 25.0; // gentle resonance droop
    switch (type)
    {
        case AntTelescopic:     return -4.0 - 10.0 * std::log10 (mis);
        case AntCarWhip:        return -2.0 - 10.0 * std::log10 (mis);
        case AntHeadphoneCord:  return -14.0 - 10.0 * std::log10 (mis);   // ~1 m wire, low-impedance loop
        case AntWireDipole:     return  1.0 - 10.0 * std::log10 (mis);
        case AntRooftopYagi:    return 10.0 - 10.0 * std::log10 (mis);
        default:                return -4.0;
    }
}

inline double antennaHeightM (int type)
{
    switch (type) { case AntCarWhip: return 1.5; case AntRooftopYagi: return 10.0; default: return 1.5; }
}

inline FmLink computeFmLink (double distKm, double erpKw, double txHeightM, int env, int antenna,
                             double fMHz, double rxNfDb, double ifBwHz)
{
    FmLink r;
    distKm = std::max (distKm, 0.05);
    const EnvData& e = kEnv[env];
    r.wavelengthM = kSpeedOfLight / (fMHz * 1e6);
    // free-space field for a half-wave dipole ERP: E = 7 sqrt(P_W)/d  (V/m)  -> dBuV/m
    double E = 106.9 + 10.0 * std::log10 (std::max (erpKw, 1e-6)) - 20.0 * std::log10 (distKm);
    // radio horizon (k = 4/3 earth)
    const double rxH = antennaHeightM (antenna);
    const double dh = 4.12 * (std::sqrt (txHeightM) + std::sqrt (rxH));
    r.horizonKm = dh;
    double loss = e.clutterLossDb;
    if (distKm > dh)
    {
        // diffraction beyond the horizon: ~0.3 dB/km near the horizon, steepening gently with distance
        // (consistent in shape with FCC F(50,50) curves: roughly -45 dB between 70 and 180 km)
        const double x = distKm - dh;
        loss += 0.3 * (std::sqrt (x * x + 4.0) - 2.0) + 0.0012 * x * x;
    }
    // a bit of extra "two-ray" loss at close range is ignored on purpose
    E -= loss;
    r.fieldDbuv = E;
    const double gain = antennaGainDbi (antenna, fMHz);
    r.pinDbm = E + gain - 20.0 * std::log10 (fMHz) - 77.2;
    // noise: external (man-made + galactic) combined with receiver noise figure
    const double lf = std::log10 (fMHz);
    const double famMan = e.manMadeC - e.manMadeD * lf;
    const double famGal = 52.0 - 23.0 * lf;
    const double faLin = dbToPow (famMan) + dbToPow (famGal);
    const double F = faLin + dbToPow (rxNfDb) - 1.0;
    const double noiseDbm = -174.0 + 10.0 * std::log10 (ifBwHz) + powToDb (F);
    r.cnrDb = r.pinDbm - noiseDbm;
    return r;
}

//----------------------------------------------------------------------- AM link
struct AmLink
{
    double groundDbuv = 0;
    double skyDbuv = -100;
    double totalDbuv = 0;
    double noiseDbuv = 0;
    double cnrDb = 0;
    double skyRatio = 0;   // skywave amplitude / groundwave amplitude
    double skyDelayMs = 0;
    double wavelengthM = 300;
};

// Norton flat-earth approximation of surface-wave attenuation A(p), p = pi d /(lambda x).
inline double nortonAtten (double distKm, double fMHz, double sigmaSm)
{
    const double lambdaKm = 0.299792458 / fMHz;
    const double x = 18000.0 * sigmaSm / fMHz;
    const double p = kPi * distKm / (lambdaKm * x);
    return (2.0 + 0.3 * p) / (2.0 + p + 0.6 * p * p);
}

// nightFactor: 0 = full daylight (D-layer absorbs skywave), 1 = deep night.
// storm: 0..1 thunderstorm activity.  userNoiseDb: extra noise.
inline AmLink computeAmLink (double distKm, double powerKw, double fMHz, double sigmaSm, double nightFactor,
                             double storm, int env, double rxNoiseDbuv10k, double ifBwHz, double userNoiseDb)
{
    AmLink r;
    distKm = std::max (distKm, 0.05);
    r.wavelengthM = kSpeedOfLight / (fMHz * 1e6);
    const double E0 = 109.7 + 10.0 * std::log10 (std::max (powerKw, 1e-6)); // inverse-distance field at 1 km
    const double A = nortonAtten (distKm, fMHz, sigmaSm);
    r.groundDbuv = E0 - 20.0 * std::log10 (distKm) + 20.0 * std::log10 (A);

    // skywave (E-layer at ~100 km): slant range, reflection + absorption losses
    const double h = 100.0;
    const double slant = std::sqrt (distKm * distKm + 4.0 * h * h);
    const double dayAbsorb = 55.0 * (1.0 - nightFactor);
    const double hopLoss = 9.0 + 0.012 * distKm;
    const double skyUnclamped = E0 - 20.0 * std::log10 (slant) - hopLoss - dayAbsorb;
    r.skyDbuv = (nightFactor < 0.001 ? -100.0 : skyUnclamped);
    r.skyDelayMs = (slant - distKm) / 299.792458;   // km / (km/ms)

    const double ampG = dbToAmp (r.groundDbuv), ampS = dbToAmp (r.skyDbuv);
    r.skyRatio = ampS / std::max (ampG, 1e-12);
    r.totalDbuv = ampToDb (std::sqrt (ampG * ampG + ampS * ampS));

    // noise, expressed as an equivalent field strength in the IF bandwidth (ITU-R P.372 style)
    const EnvData& e = kEnv[env];
    const double lf = std::log10 (fMHz);
    const double famMan = e.manMadeC - e.manMadeD * lf;
    const double famAtm = lerpd (52.0, 72.0, nightFactor) + 14.0 * storm;      // approx. P.372 atmospheric trend at ~1 MHz
    const double B = 10.0 * std::log10 (ifBwHz);
    const double conv = 20.0 * lf + B - 95.5;
    const double enMan = famMan + conv, enAtm = famAtm + conv;
    const double enRx = rxNoiseDbuv10k + 10.0 * std::log10 (ifBwHz / 10000.0);
    double nPow = dbToPow (enMan) + dbToPow (enAtm) + dbToPow (enRx);
    r.noiseDbuv = powToDb (nPow) + userNoiseDb;
    r.cnrDb = r.totalDbuv - r.noiseDbuv;
    return r;
}

} // namespace radiosim
