// radiosim_cli - offline renderer for the RadioSim DSP engine (no JUCE needed).
//   radiosim_cli in.wav out.wav key=value ...
// Keys mirror the plugin parameters (see parseArg below). Extra test keys:
//   cnr=<dB>      force the carrier-to-noise ratio, bypassing the link budget
//   fm_noise_only, noiseless=1, info=1
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include "../Source/dsp/RadioEngine.h"
#include "../Source/dsp/Presets.h"

using namespace radiosim;

struct Wav { int sr = 44100; int ch = 2; std::vector<float> l, r; };

static bool readWav (const char* path, Wav& w)
{
    FILE* f = fopen (path, "rb");
    if (! f) return false;
    char id[4]; uint32_t sz;
    if (fread (id, 1, 4, f) != 4 || memcmp (id, "RIFF", 4)) { fclose (f); return false; }
    fread (&sz, 4, 1, f); fread (id, 1, 4, f);
    uint16_t fmt = 1, ch = 2, bits = 16; uint32_t sr = 44100;
    std::vector<unsigned char> data;
    while (fread (id, 1, 4, f) == 4 && fread (&sz, 4, 1, f) == 1)
    {
        if (! memcmp (id, "fmt ", 4))
        {
            std::vector<unsigned char> b (sz); fread (b.data(), 1, sz, f);
            fmt = (uint16_t) (b[0] | (b[1] << 8)); ch = (uint16_t) (b[2] | (b[3] << 8));
            sr = b[4] | (b[5] << 8) | (b[6] << 16) | ((uint32_t) b[7] << 24); bits = (uint16_t) (b[14] | (b[15] << 8));
        }
        else if (! memcmp (id, "data", 4)) { data.resize (sz); fread (data.data(), 1, sz, f); break; }
        else fseek (f, (long) sz, SEEK_CUR);
    }
    fclose (f);
    w.sr = (int) sr; w.ch = ch;
    const size_t bytes = bits / 8, frames = data.size() / (bytes * ch);
    w.l.resize (frames); w.r.resize (frames);
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < 2; ++c)
        {
            const unsigned char* p = &data[(i * ch + std::min<int> (c, ch - 1)) * bytes];
            float v;
            if (fmt == 3 && bits == 32) memcpy (&v, p, 4);
            else if (bits == 16) v = (float) (int16_t) (p[0] | (p[1] << 8)) / 32768.0f;
            else if (bits == 24) v = (float) ((int32_t) ((p[0] << 8) | (p[1] << 16) | ((uint32_t) p[2] << 24)) >> 8) / 8388608.0f;
            else v = 0.0f;
            (c == 0 ? w.l : w.r)[i] = v;
        }
    return true;
}

static void writeWav (const char* path, const Wav& w)
{
    FILE* f = fopen (path, "wb");
    const uint32_t frames = (uint32_t) w.l.size(), dataBytes = frames * 2 * 2, sr = (uint32_t) w.sr;
    uint32_t v32; uint16_t v16;
    fwrite ("RIFF", 1, 4, f); v32 = 36 + dataBytes; fwrite (&v32, 4, 1, f); fwrite ("WAVEfmt ", 1, 8, f);
    v32 = 16; fwrite (&v32, 4, 1, f); v16 = 1; fwrite (&v16, 2, 1, f); v16 = 2; fwrite (&v16, 2, 1, f);
    fwrite (&sr, 4, 1, f); v32 = sr * 4; fwrite (&v32, 4, 1, f); v16 = 4; fwrite (&v16, 2, 1, f); v16 = 16; fwrite (&v16, 2, 1, f);
    fwrite ("data", 1, 4, f); fwrite (&dataBytes, 4, 1, f);
    for (uint32_t i = 0; i < frames; ++i)
        for (int c = 0; c < 2; ++c)
        {
            float x = (c == 0 ? w.l : w.r)[i];
            x = std::max (-1.0f, std::min (1.0f, x));
            const int16_t s = (int16_t) std::lrint (x * 32767.0f);
            fwrite (&s, 2, 1, f);
        }
    fclose (f);
}

int main (int argc, char** argv)
{
    if (argc < 3) { fprintf (stderr, "usage: radiosim_cli in.wav out.wav [key=value ...]\n"); return 1; }
    Wav w;
    if (! readWav (argv[1], w)) { fprintf (stderr, "cannot read %s\n", argv[1]); return 1; }

    UserParams u;
    for (int i = 3; i < argc; ++i)                       // preset=<index> first, so other keys can override it
        if (! strncmp (argv[i], "preset=", 7))
        {
            const int idx = atoi (argv[i] + 7);
            if (idx >= 0 && idx < (int) factoryPresets().size()) u = factoryPresets()[(size_t) idx].p;
        }
    double forceCnr = -999.0;
    bool noiseless = false, info = false;
    double gainDb = 0.0;
    for (int i = 3; i < argc; ++i)
    {
        std::string a = argv[i];
        const size_t eq = a.find ('=');
        if (eq == std::string::npos) continue;
        const std::string k = a.substr (0, eq);
        const double v = atof (a.c_str() + eq + 1);
        if (k == "band") u.band = (int) v;
        else if (k == "era") u.era = (int) v;
        else if (k == "region") u.region = (int) v;
        else if (k == "fm") u.fmFreqMHz = v;
        else if (k == "am") u.amFreqKHz = v;
        else if (k == "dist") u.distanceKm = v;
        else if (k == "power") u.powerKw = v;
        else if (k == "env") u.env = (int) v;
        else if (k == "ant") u.antenna = (int) v;
        else if (k == "ground") u.ground = (int) v;
        else if (k == "night") u.timeOfDay = v;
        else if (k == "storm") u.storm = v;
        else if (k == "speed") u.speedKmh = v;
        else if (k == "mp") u.multipath = v;
        else if (k == "adj") u.adjDb = v;
        else if (k == "adj2") u.adj2Db = v;
        else if (k == "co") u.coDb = v;
        else if (k == "impulse") u.impulse = v;
        else if (k == "buzz") u.buzz = v;
        else if (k == "detune") u.detune = v;
        else if (k == "stereo") u.stereo = v != 0.0;
        else if (k == "rds") u.rds = v != 0.0;
        else if (k == "drive") u.drive = v;
        else if (k == "proc") u.proc = (int) v;
        else if (k == "speaker") u.speaker = v != 0.0;
        else if (k == "smart") u.smart = v != 0.0;
        else if (k == "trim") u.trimDb = v;
        else if (k == "mix") u.mix = v;
        else if (k == "out") u.outDb = v;
        else if (k == "cnr") forceCnr = v;
        else if (k == "noiseless") noiseless = v != 0.0;
        else if (k == "info") info = v != 0.0;
        else if (k == "gain") gainDb = v;
    }

    const double sr = (double) w.sr;
    FmParams fmP; AmParams amP; LinkInfo li;
    if (u.band == BandFM) buildFm (u, fmP, li); else buildAm (u, amP, li);
    if (forceCnr > -900.0) { fmP.cnrDb = amP.cnrDb = forceCnr; li.cnrDb = forceCnr; }
    if (noiseless) { fmP.noiseScale = amP.noiseScale = 0.0; fmP.floorScale = amP.floorScale = 0.0; }
    if (info)
        fprintf (stderr, "%s  f=%.3f %s  lambda=%.2f m  field=%.1f dBuV/m  CNR=%.1f dB  horizon=%.0f km  skyRatio=%.2f\n",
                 li.am ? "AM" : "FM", li.freq, li.am ? "kHz" : "MHz", li.wavelengthM, li.fieldDbuv, li.cnrDb, li.horizonKm, li.skyRatio);

    // run the engines directly (we may have overridden CNR)
    FmEngine fm; AmEngine am; Voicer vo;
    fm.prepare (sr); am.prepare (sr); vo.prepare (sr);
    if (u.band == BandFM) fm.setParams (fmP); else am.setParams (amP);
    vo.setEra (u.era);
    const float g = (float) dbToAmp (gainDb);
    for (auto& x : w.l) x *= g;
    for (auto& x : w.r) x *= g;

    const auto t0 = std::chrono::steady_clock::now();
    const int n = (int) w.l.size();
    for (int off = 0; off < n; off += 512)
    {
        const int c = std::min (512, n - off);
        if (u.band == BandFM) fm.process (&w.l[(size_t) off], &w.r[(size_t) off], c);
        else am.process (&w.l[(size_t) off], &w.r[(size_t) off], c);
        if (u.speaker) vo.process (&w.l[(size_t) off], &w.r[(size_t) off], c);
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    if (info)
    {
        fprintf (stderr, "rendered %.2f s of audio in %.3f s  (%.1f%% of one core, internal rate %.0f Hz)\n",
                 n / sr, secs, 100.0 * secs / (n / sr), u.band == BandFM ? fm.internalRate() : am.internalRate());
        if (u.band == BandFM) fprintf (stderr, "meter: CNR est %.1f dB, stereo %.2f, mute %.2f\n", fm.meterCnrDb(), fm.meterStereo(), fm.meterMute());
    }
    writeWav (argv[2], w);
    return 0;
}
