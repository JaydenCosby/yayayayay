// Scenario.h - the user-facing controls (distance, power, terrain, antenna, weather...) and the
// translation of those into the FM / AM engine parameters through the link-budget model.
#pragma once
#include "Physics.h"
#include "FmEngine.h"
#include "AmEngine.h"

namespace radiosim
{

enum Band { BandFM = 0, BandAM = 1 };

struct UserParams
{
    int    band = BandFM;
    int    era = 6;
    int    region = 0;            // 0 = Americas (75 us, 200 kHz, 10 kHz, 60 Hz)  1 = Europe/Asia (50 us, 9 kHz, 50 Hz)
    double fmFreqMHz = 98.7;
    double amFreqKHz = 1010.0;
    double distanceKm = 25.0;
    double powerKw = 50.0;        // FM: ERP.  AM: carrier power
    int    env = EnvSuburban;
    int    antenna = 0;           // 0 = era default, 1.. = AntennaType + 1
    int    ground = 1;            // AM ground conductivity: 0 poor, 1 average, 2 good, 3 sea water
    double timeOfDay = 0.0;       // AM: 0 = midday, 1 = deep night
    double storm = 0.0;           // thunderstorm activity 0..1
    double speedKmh = 0.0;
    double multipath = 0.30;      // FM
    double adjDb = -60.0;         // -60 = off
    double adj2Db = -60.0;
    double coDb = -60.0;
    double impulse = 0.0;         // FM ignition / AM atmospheric crackle
    double buzz = 0.0;            // AM power-line buzz
    double detune = 0.0;          // -1..1 of the tuning window
    bool   stereo = true;
    bool   rds = true;
    double drive = 0.85;
    int    proc = 0;              // 0 = era, 1 = off, 2 = light, 3 = medium, 4 = heavy
    bool   speaker = true;
    bool   smart = true;
    double trimDb = 0.0;          // fine adjustment of the carrier-to-noise ratio
    double mix = 1.0;
    double outDb = 0.0;
};

struct LinkInfo
{
    double fieldDbuv = 0, cnrDb = 0, wavelengthM = 0, horizonKm = 0, skyRatio = 0, quarterWaveCm = 0;
    double freq = 0;
    bool   am = false;
};

inline double procAmount (int proc)
{
    switch (proc) { case 1: return 0.0; case 2: return 0.25; case 3: return 0.55; case 4: return 0.90; default: return -1.0; }
}

inline double groundSigma (int g)
{
    static const double s[4] = { 0.001, 0.005, 0.010, 5.0 };
    return s[std::max (0, std::min (3, g))];
}

inline void buildFm (const UserParams& u, FmParams& fm, LinkInfo& li)
{
    const int era = std::max (1, u.era);                  // 1920s had no FM -> behaves like the 1940s
    const FmEra& E = kFmEras[era];
    const double f = u.fmFreqMHz;
    const int antType = u.antenna <= 0 ? -1 : u.antenna - 1;
    FmLink L = computeFmLink (u.distanceKm, u.powerKw, 250.0, u.env, antType < 0 ? AntTelescopic : antType,
                              f, E.nfDb, E.ifBwHz);
    double cnr = L.cnrDb;
    if (antType < 0) cnr += E.antGainDbi - antennaGainDbi (AntTelescopic, f);   // era-typical antenna instead
    cnr += u.trimDb;

    fm.era = era;
    fm.cnrDb = clampd (cnr, -10.0, 90.0);
    fm.stereo = u.stereo;
    fm.modDepth = u.drive;
    fm.region = u.region;
    fm.txProc = procAmount (u.proc);
    fm.rds = u.rds;
    fm.multipath = clampd (u.multipath, 0.0, 1.0);
    fm.speedKmh = u.speedKmh;
    fm.fMHz = f;
    fm.shadowSigmaDb = kEnv[u.env].shadowSigmaDb;
    fm.adjLevelDb = u.adjDb > -59.0 ? u.adjDb : -100.0;
    fm.adj2LevelDb = u.adj2Db > -59.0 ? u.adj2Db : -100.0;
    fm.coLevelDb = u.coDb > -59.0 ? u.coDb : -100.0;
    fm.impulse = u.impulse;
    fm.detuneKHz = u.detune * 120.0;
    fm.smartFeatures = u.smart;

    li.am = false;
    li.fieldDbuv = L.fieldDbuv;
    li.cnrDb = fm.cnrDb;
    li.wavelengthM = L.wavelengthM;
    li.horizonKm = L.horizonKm;
    li.skyRatio = 0.0;
    li.freq = f;
    li.quarterWaveCm = 25.0 * L.wavelengthM;
}

inline void buildAm (const UserParams& u, AmParams& am, LinkInfo& li)
{
    const AmEra& E = kAmEras[std::max (0, std::min (kNumEras - 1, u.era))];
    const double fMHz = u.amFreqKHz / 1000.0;
    AmLink L = computeAmLink (u.distanceKm, u.powerKw, fMHz, groundSigma (u.ground), u.timeOfDay, u.storm,
                              u.env, E.rxNoiseDbuv, E.ifBwHz, 0.0);
    am.era = u.era;
    am.cnrDb = clampd (L.cnrDb + u.trimDb, -10.0, 90.0);
    am.skyRatio = clampd (L.skyRatio, 0.0, 60.0);
    am.skyDelayMs = clampd (L.skyDelayMs, 0.02, 0.6);
    am.skyFadeHz = 0.25 + 0.9 * clampd (u.distanceKm / 1500.0, 0.0, 1.0) + 0.4 * u.storm;
    am.modDepth = u.drive;
    am.amDepth = 1.0;
    am.txProc = procAmount (u.proc);
    am.region = u.region;
    am.adjLevelDb = u.adjDb > -59.0 ? u.adjDb : -100.0;
    am.adj2LevelDb = u.adj2Db > -59.0 ? u.adj2Db : -100.0;
    am.coLevelDb = u.coDb > -59.0 ? u.coDb : -100.0;
    am.staticAmt = clampd (u.impulse + 0.45 * u.storm, 0.0, 1.0);
    am.buzzAmt = u.buzz;
    am.detuneKHz = u.detune * 4.0;
    am.smartFeatures = u.smart;

    li.am = true;
    li.fieldDbuv = L.totalDbuv;
    li.cnrDb = am.cnrDb;
    li.wavelengthM = L.wavelengthM;
    li.horizonKm = 0.0;
    li.skyRatio = L.skyRatio;
    li.freq = u.amFreqKHz;
    li.quarterWaveCm = 25.0 * L.wavelengthM;
}

} // namespace radiosim
