// Eras.h - era "profiles": how broadcasters and receivers actually behaved in each decade.
// Every number here is a deliberate modelling choice, documented in docs/RADIO_NOTES.md.
#pragma once

namespace radiosim
{

constexpr int kNumEras = 9;

inline const char* const kEraNames[kNumEras] = {
    "1920s Crystal / Regen (AM only)",
    "1940s Tube Console",
    "1950s Bakelite Table Radio",
    "1960s Transistor Pocket Radio",
    "1970s Hi-Fi Receiver / Car Stereo",
    "1980s Boombox / Walkman",
    "1990s CD Boombox / RDS Car Stereo",
    "2000s Digital Tuner",
    "2020s Modern DSP Radio" };

//------------------------------------------------------------- speaker / cabinet
struct EqBand { double f, q, gDb; };

struct Voicing
{
    double hpHz; int hpOrder;
    double lpHz; int lpOrder;
    EqBand eq[3];
    double satDrive;   // soft saturation amount 0..1
    double satAsym;    // even-harmonic bias (tube-style) 0..1
    double crossover;  // class-B dead zone (transistor sets)
    double width;      // stereo width of the speaker system, 0 = mono
};

inline const Voicing kVoicings[kNumEras] = {
    // 1920s: crystal set + earphones (resonant diaphragm, no bass)
    { 350, 4, 2800, 4, { { 1100, 2.0, 7 }, { 2200, 3.0, 4 }, { 500, 1.0, -3 } }, 0.10, 0.0, 0.0, 0.0 },
    // 1940s: big wooden console, tube output stage, boomy cabinet
    { 90, 2, 5200, 4, { { 140, 1.2, 5 }, { 1400, 1.0, 3 }, { 3800, 2.0, 2 } }, 0.25, 0.30, 0.0, 0.0 },
    // 1950s: Bakelite table set, small oval speaker
    { 160, 2, 4800, 4, { { 220, 1.5, 4 }, { 1800, 1.2, 4 }, { 3500, 2.0, 3 } }, 0.20, 0.25, 0.0, 0.0 },
    // 1960s: transistor pocket radio, 2 inch speaker, class-B crossover grit
    { 380, 4, 3600, 4, { { 900, 1.2, 3 }, { 2500, 1.5, 4 }, { 3300, 3.0, 3 } }, 0.50, 0.0, 0.020, 0.0 },
    // 1970s: hi-fi receiver / dash speakers
    { 45, 2, 13000, 2, { { 90, 0.9, 2 }, { 3000, 1.0, 1.5 }, { 8000, 0.8, 0.0 } }, 0.05, 0.0, 0.0, 0.90 },
    // 1980s: boombox, closely spaced small speakers (narrow stereo)
    { 70, 2, 12000, 2, { { 100, 0.9, 5 }, { 2500, 1.0, 1 }, { 9000, 0.8, 2 } }, 0.12, 0.0, 0.0, 0.55 },
    // 1990s: CD boombox, bass boost button, a little sheen
    { 50, 2, 15000, 2, { { 80, 0.8, 6 }, { 3500, 1.0, 1 }, { 11000, 0.7, 3 } }, 0.08, 0.0, 0.0, 0.80 },
    // 2000s: mini hi-fi
    { 35, 2, 16000, 2, { { 60, 0.8, 2 }, { 3000, 1.0, 0 }, { 10000, 0.8, 0 } }, 0.03, 0.0, 0.0, 1.0 },
    // 2020s: wide-range, near flat
    { 20, 2, 19000, 2, { { 100, 1.0, 0 }, { 3000, 1.0, 0 }, { 10000, 1.0, 0 } }, 0.0, 0.0, 0.0, 1.0 }
};

//------------------------------------------------------------- FM
struct FmEra
{
    bool   txStereo;       // was stereo FM broadcasting around?
    double txProc;         // station processing (compression/limiting), 0..1
    double rds;            // 57 kHz RDS injection (fraction of full deviation)
    double ifBwHz;         // IF -3 dB bandwidth (full width)
    int    ifOrder;
    double nfDb;           // receiver noise figure
    double antGainDbi;     // typical antenna of that receiver class
    int    decoder;        // 0 = mono, 1 = matrix, 2 = PLL
    double separationDb;   // stereo separation
    double decLpHz; int decLpOrder;   // composite / audio low-pass
    double pilotNotchQ;    // 19 kHz trap (0 = none; lower Q = leakier)
    bool   softMute, blend, hcc;      // "smart" features
    double smartLoDb, smartHiDb;      // estimated-CNR window for mute/blend/HCC
    double detNonlin;      // discriminator S-curve cubic term
    double driftHz;        // local-oscillator drift amplitude
    double hiss;           // amplifier hiss (linear, relative to full scale)
    double humAmp;         // mains hum (linear)
    double pllBwHz;        // stereo PLL loop bandwidth
};

inline const FmEra kFmEras[kNumEras] = {
    // 0: no FM in the 1920s -> falls back to 1940s in the engine
    { false, 0.10, 0.0, 240e3, 4, 14, -4, 0, 0, 9000, 2, 0.0, false, false, false, 0, 0, 0.10, 12000, 0.0040, 0.020, 0 },
    // 1940s (first FM sets: mono, drifting tubes)
    { false, 0.10, 0.0, 240e3, 4, 14, -4, 0, 0, 9000, 2, 0.0, false, false, false, 0, 0, 0.10, 12000, 0.0040, 0.020, 0 },
    // 1950s
    { false, 0.15, 0.0, 225e3, 4, 12, -4, 0, 0, 10000, 2, 0.0, false, false, false, 0, 0, 0.08, 9000, 0.0030, 0.015, 0 },
    // 1960s pocket radio (mono receiver)
    { true, 0.25, 0.0, 210e3, 4, 11, -10, 0, 0, 8000, 2, 0.0, false, false, false, 0, 0, 0.06, 6000, 0.0040, 0.004, 0 },
    // 1970s hi-fi / car: PLL decoder, analogue dial drift, leaky pilot trap
    { true, 0.35, 0.0, 195e3, 6, 8, -2, 2, 30, 14000, 2, 1.5, false, false, false, 0, 0, 0.03, 3500, 0.0012, 0.004, 60 },
    // 1980s boombox / Walkman: simple stereo blend
    { true, 0.50, 0.0, 185e3, 6, 7, -8, 2, 34, 14500, 4, 3.0, false, true, false, 14, 24, 0.02, 1500, 0.0008, 0.002, 50 },
    // 1990s car stereo: RDS, soft-mute, blend, high-cut, synthesised tuning
    { true, 0.60, 0.03, 180e3, 6, 6, -5, 2, 38, 15000, 4, 5.0, true, true, true, 12, 24, 0.01, 300, 0.0005, 0.001, 40 },
    // 2000s digital tuner (the "loudness war" peak)
    { true, 0.85, 0.03, 175e3, 6, 5, -3, 2, 42, 15500, 6, 8.0, true, true, true, 11, 22, 0.005, 50, 0.0003, 0.0005, 40 },
    // 2020s DSP radio chip
    { true, 0.70, 0.03, 170e3, 8, 4, -8, 2, 50, 16000, 8, 10.0, true, true, true, 10, 20, 0.0, 0, 0.0001, 0.0, 30 }
};

//------------------------------------------------------------- AM
struct AmEra
{
    double txBwHz;         // station audio bandwidth (Americas)
    double txProc;         // station processing 0..1
    bool   asym;           // +125% positive peaks allowed
    double ifBwHz; int ifOrder;
    double rxNoiseDbuv;    // receiver internal noise as an equivalent field (dBuV/m in 10 kHz)
    double agcStrength, agcAttackMs, agcReleaseMs;
    int    detector;       // 0 = diode envelope, 1 = synchronous
    double detRcUs;        // diode detector RC time constant
    double audioLpHz; int audioLpOrder;
    double audioHpHz;
    bool   whistleNotch;   // 9/10 kHz trap
    bool   noiseBlanker;
    double driftHz;
    double hiss, humAmp;
    double detNonlin;      // square-law component of the detector
};

inline const AmEra kAmEras[kNumEras] = {
    // 1920s crystal / regen: broad tuning, no AGC, no mains hum (no power supply!)
    { 3500, 0.05, false, 14000, 2, 52, 0.0, 30, 1000, 0, 80, 3000, 2, 150, false, false, 300, 0.000, 0.000, 0.15 },
    // 1940s tube console
    { 5000, 0.15, false, 9000, 4, 38, 0.6, 30, 1500, 0, 40, 4200, 2, 100, false, false, 150, 0.003, 0.025, 0.08 },
    // 1950s
    { 5500, 0.20, false, 7500, 4, 34, 0.7, 25, 1200, 0, 35, 4500, 2, 100, false, false, 90, 0.003, 0.020, 0.06 },
    // 1960s transistor pocket radio
    { 6000, 0.30, true, 6000, 4, 38, 0.7, 25, 800, 0, 30, 3500, 2, 200, false, false, 60, 0.004, 0.003, 0.08 },
    // 1970s wideband hi-fi AM tuner: wide IF, no whistle filter yet -> 9/10 kHz heterodyne whistles are audible
    { 7500, 0.45, true, 12000, 4, 30, 0.85, 20, 600, 0, 25, 9500, 2, 60, false, false, 40, 0.0015, 0.004, 0.04 },
    // 1980s (NRSC-1, whistle filters appear)
    { 9500, 0.60, true, 8000, 4, 27, 0.90, 20, 500, 0, 22, 7500, 4, 50, true, false, 20, 0.0010, 0.002, 0.02 },
    // 1990s car radios with noise blankers
    { 8500, 0.70, true, 8000, 6, 24, 0.95, 15, 400, 0, 20, 7000, 4, 50, true, true, 5, 0.0006, 0.001, 0.01 },
    // 2000s: synchronous detection arrives
    { 8000, 0.80, true, 9000, 6, 21, 1.00, 15, 300, 1, 15, 7500, 4, 40, true, true, 1, 0.0004, 0.0005, 0.0 },
    // 2020s
    { 8000, 0.75, true, 10000, 8, 18, 1.00, 10, 250, 1, 10, 9000, 6, 30, true, true, 0, 0.0001, 0.0, 0.0 }
};

} // namespace radiosim
