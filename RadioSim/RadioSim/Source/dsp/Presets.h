// Presets.h - factory presets. Every preset is just a UserParams snapshot, so the plugin applies
// them by setting parameters (they stay editable), and the CLI/tests can use them directly.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "Scenario.h"

namespace radiosim
{

struct Preset
{
    std::string group, name;
    UserParams p;
};

inline const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> list = []
    {
        std::vector<Preset> v;
        auto add = [&v] (const char* group, const char* name, const std::function<void (UserParams&)>& f)
        {
            Preset p; p.group = group; p.name = name; f (p.p); v.push_back (std::move (p));
        };
        auto fm = [] (UserParams& u, int era, double dist, double kw, int env)
        { u.band = BandFM; u.era = era; u.distanceKm = dist; u.powerKw = kw; u.env = env; };
        auto am = [] (UserParams& u, int era, double dist, double kw, int env)
        { u.band = BandAM; u.era = era; u.distanceKm = dist; u.powerKw = kw; u.env = env; };

        //=========================================================== init
        add ("Init", "Init (clean modern FM)", [&] (UserParams& u) { fm (u, 8, 20, 50, EnvSuburban); u.multipath = 0.1; });

        //=========================================================== FM distance ladder
        const char* G1 = "FM / Distance";
        add (G1, "Next to the Tower (2 km)",        [&] (UserParams& u) { fm (u, 8, 2, 50, EnvSuburban); u.multipath = 0.1; u.fmFreqMHz = 98.7; });
        add (G1, "City Strong Signal (15 km)",      [&] (UserParams& u) { fm (u, 8, 15, 50, EnvUrban); u.multipath = 0.45; });
        add (G1, "Suburb, Clean (40 km)",           [&] (UserParams& u) { fm (u, 8, 40, 50, EnvSuburban); u.multipath = 0.30; });
        add (G1, "Edge of the Horizon (70 km)",     [&] (UserParams& u) { fm (u, 8, 70, 50, EnvSuburban); u.multipath = 0.30; });
        add (G1, "Beyond the Horizon (137 km)",     [&] (UserParams& u) { fm (u, 8, 137, 50, EnvRural); u.multipath = 0.2; u.fmFreqMHz = 95.3; });
        add (G1, "Fringe Reception (160 km)",       [&] (UserParams& u) { fm (u, 8, 160, 50, EnvRural); u.multipath = 0.2; u.fmFreqMHz = 95.3; });
        add (G1, "Deep Fringe, Hissy Mono (178 km)",[&] (UserParams& u) { fm (u, 8, 178, 50, EnvRural); u.multipath = 0.25; u.smart = false; u.fmFreqMHz = 95.3; });
        add (G1, "Threshold Crackle (192 km)",      [&] (UserParams& u) { fm (u, 8, 192, 50, EnvRural); u.multipath = 0.25; u.smart = false; });
        add (G1, "Tropo Opening DX (230 km)",       [&] (UserParams& u) { fm (u, 8, 230, 50, EnvOpen); u.trimDb = 24; u.multipath = 0.7; u.speedKmh = 3; u.coDb = -9; u.smart = false; });

        //=========================================================== FM on the move
        const char* G2 = "FM / On the Move";
        add (G2, "Highway Drive (suburbs, 100 km/h)", [&] (UserParams& u) { fm (u, 6, 45, 50, EnvSuburban); u.antenna = 1 + AntCarWhip; u.speedKmh = 100; u.multipath = 0.5; });
        add (G2, "Downtown Canyon Drive",             [&] (UserParams& u) { fm (u, 6, 12, 50, EnvUrban); u.antenna = 1 + AntCarWhip; u.speedKmh = 35; u.multipath = 1.0; u.impulse = 0.15; });
        add (G2, "Rural Road, Fading Out",            [&] (UserParams& u) { fm (u, 6, 172, 50, EnvRural); u.antenna = 1 + AntCarWhip; u.speedKmh = 90; u.multipath = 0.35; });
        add (G2, "Walking, Headphone-Cord Antenna",   [&] (UserParams& u) { fm (u, 5, 28, 50, EnvSuburban); u.antenna = 1 + AntHeadphoneCord; u.speedKmh = 5; u.multipath = 0.5; });
        add (G2, "Old Car, Ignition Noise",           [&] (UserParams& u) { fm (u, 4, 50, 50, EnvSuburban); u.antenna = 1 + AntCarWhip; u.speedKmh = 60; u.multipath = 0.4; u.impulse = 0.55; });

        //=========================================================== FM interference
        const char* G3 = "FM / Interference";
        add (G3, "Multipath Garble (parked)",        [&] (UserParams& u) { fm (u, 8, 18, 50, EnvUrban); u.multipath = 1.0; u.speedKmh = 0; });
        add (G3, "Adjacent Station Bleed (+34 dB)",  [&] (UserParams& u) { fm (u, 7, 60, 50, EnvSuburban); u.adjDb = 34; u.adj2Db = 20; });
        add (G3, "Co-channel Ghost (-10 dB)",        [&] (UserParams& u) { fm (u, 8, 55, 50, EnvSuburban); u.coDb = -10; u.multipath = 0.2; });
        add (G3, "Fighting for Capture (D/U 4 dB)",  [&] (UserParams& u) { fm (u, 8, 55, 50, EnvSuburban); u.coDb = -4; });
        add (G3, "Mistuned Between Stations",        [&] (UserParams& u) { fm (u, 6, 30, 50, EnvSuburban); u.detune = 0.42; });
        add (G3, "Electrical Interference (motors)", [&] (UserParams& u) { fm (u, 6, 70, 50, EnvUrban); u.impulse = 0.7; });

        //=========================================================== eras
        const char* G4 = "Eras / Devices";
        add (G4, "1920s Crystal Set (local AM)", [&] (UserParams& u) { am (u, 0, 20, 1, EnvOpen); u.amFreqKHz = 780; u.timeOfDay = 0.0; u.ground = 1; u.drive = 0.7; u.trimDb = -6; });
        add (G4, "1940s Tube Console (AM, evening)", [&] (UserParams& u) { am (u, 1, 55, 5, EnvSuburban); u.amFreqKHz = 880; u.timeOfDay = 0.4; });
        add (G4, "1950s Bakelite Table Radio (AM)", [&] (UserParams& u) { am (u, 2, 40, 5, EnvSuburban); u.amFreqKHz = 1010; });
        add (G4, "1950s First FM Tuner (mono tube)", [&] (UserParams& u) { fm (u, 2, 35, 20, EnvSuburban); u.stereo = false; u.antenna = 1 + AntWireDipole; u.region = 0; });
        add (G4, "1960s Pocket Transistor (AM)", [&] (UserParams& u) { am (u, 3, 30, 5, EnvSuburban); u.amFreqKHz = 1230; u.drive = 0.9; });
        add (G4, "1960s Transistor FM (mono)", [&] (UserParams& u) { fm (u, 3, 30, 50, EnvSuburban); u.stereo = false; });
        add (G4, "1970s Hi-Fi Receiver (FM stereo)", [&] (UserParams& u) { fm (u, 4, 40, 50, EnvSuburban); u.antenna = 1 + AntWireDipole; u.multipath = 0.2; });
        add (G4, "1970s Wideband AM Tuner", [&] (UserParams& u) { am (u, 4, 35, 10, EnvSuburban); u.amFreqKHz = 1100; u.adjDb = 8; });
        add (G4, "1980s Boombox FM (telescopic)", [&] (UserParams& u) { fm (u, 5, 38, 50, EnvSuburban); u.antenna = 1 + AntTelescopic; u.multipath = 0.4; });
        add (G4, "1980s Walkman (headphone cord)", [&] (UserParams& u) { fm (u, 5, 22, 50, EnvSuburban); u.antenna = 1 + AntHeadphoneCord; u.speedKmh = 4; u.multipath = 0.4; });
        add (G4, "1990s CD Boombox (RDS)", [&] (UserParams& u) { fm (u, 6, 38, 50, EnvSuburban); u.multipath = 0.3; });
        add (G4, "1990s Car Stereo (blend + soft-mute)", [&] (UserParams& u) { fm (u, 6, 168, 50, EnvRural); u.antenna = 1 + AntCarWhip; u.speedKmh = 80; u.multipath = 0.4; });
        add (G4, "1990s Car AM (noise blanker)", [&] (UserParams& u) { am (u, 6, 45, 10, EnvSuburban); u.amFreqKHz = 1370; u.impulse = 0.25; });
        add (G4, "2000s Digital Tuner (loud)", [&] (UserParams& u) { fm (u, 7, 30, 50, EnvSuburban); u.proc = 4; u.drive = 1.0; });
        add (G4, "2020s DSP Radio", [&] (UserParams& u) { fm (u, 8, 30, 50, EnvSuburban); u.proc = 3; });

        //=========================================================== AM distance ladder
        const char* G5 = "AM / Distance";
        add (G5, "Local Station (5 km)",             [&] (UserParams& u) { am (u, 6, 5, 10, EnvSuburban); u.amFreqKHz = 1010; });
        add (G5, "City Station (25 km)",             [&] (UserParams& u) { am (u, 6, 25, 10, EnvUrban); u.amFreqKHz = 1010; });
        add (G5, "Daytime Groundwave (80 km)",       [&] (UserParams& u) { am (u, 6, 80, 10, EnvRural); u.amFreqKHz = 1010; });
        add (G5, "Daytime Fringe (250 km)",          [&] (UserParams& u) { am (u, 6, 250, 10, EnvRural); u.amFreqKHz = 700; });
        add (G5, "Dusk, Skywave Creeping In (120 km)", [&] (UserParams& u) { am (u, 4, 120, 10, EnvRural); u.amFreqKHz = 1100; u.timeOfDay = 0.55; });
        add (G5, "Night Skywave (400 km)",           [&] (UserParams& u) { am (u, 4, 400, 50, EnvRural); u.amFreqKHz = 880; u.timeOfDay = 1.0; });
        add (G5, "Night DX with Heterodyne (1000 km)", [&] (UserParams& u) { am (u, 4, 1000, 50, EnvRural); u.amFreqKHz = 1030; u.timeOfDay = 1.0; u.coDb = -8; u.adjDb = 4; });
        add (G5, "Transcontinental DX (1700 km)",    [&] (UserParams& u) { am (u, 3, 1700, 50, EnvOpen); u.amFreqKHz = 1190; u.timeOfDay = 1.0; u.trimDb = 12; u.adjDb = 6; u.adj2Db = 2; });

        //=========================================================== AM interference
        const char* G6 = "AM / Interference";
        add (G6, "Thunderstorm Crackle (30 km)",     [&] (UserParams& u) { am (u, 4, 30, 10, EnvRural); u.storm = 0.9; u.impulse = 0.45; u.timeOfDay = 0.3; });
        add (G6, "Power-line Buzz (kitchen)",        [&] (UserParams& u) { am (u, 5, 15, 10, EnvUrban); u.buzz = 0.75; });
        add (G6, "Adjacent-channel Whistle (10 kHz)",[&] (UserParams& u) { am (u, 4, 30, 10, EnvSuburban); u.adjDb = 10; u.adj2Db = 3; });
        add (G6, "Mistuned Pocket AM",               [&] (UserParams& u) { am (u, 3, 30, 10, EnvSuburban); u.detune = 0.55; });
        add (G6, "Fringe Noise + Static (storm)",    [&] (UserParams& u) { am (u, 3, 170, 10, EnvRural); u.storm = 0.6; u.impulse = 0.3; });
        return v;
    }();
    return list;
}

} // namespace radiosim
