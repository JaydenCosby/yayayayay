// Robustness + latency regression test for radiosim::RadioEngine (no JUCE needed).
//   g++ -std=c++17 -O2 tools/test_engine.cpp -o test_engine && ./test_engine
#include <cstdio>
#include <random>
#include "../Source/dsp/RadioEngine.h"
using namespace radiosim;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf ("  FAIL: "); printf (__VA_ARGS__); printf ("\n"); } } while (0)

static void makeSignal (std::vector<float>& l, std::vector<float>& r, double sr, int n, unsigned seed)
{
    std::mt19937 g (seed); std::normal_distribution<double> nd (0, 1);
    l.resize ((size_t) n); r.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double s = 0.35 * std::sin (kTwoPi * (200.0 + 3000.0 * std::fmod (t * 2.0, 1.0)) * t) + 0.15 * nd (g) * std::fabs (std::sin (kTwoPi * 3.0 * t));
        l[(size_t) i] = (float) (s + 0.2 * std::sin (kTwoPi * 110.0 * t));
        r[(size_t) i] = (float) (0.8 * s - 0.2 * std::sin (kTwoPi * 110.0 * t));
    }
}

int main()
{
    const double rates[] = { 22050, 44100, 48000, 88200, 96000, 192000 };
    std::mt19937 rng (42);
    std::uniform_real_distribution<double> U (0.0, 1.0);

    for (double sr : rates)
    {
        printf ("sample rate %.0f\n", sr);
        RadioEngine eng;
        eng.prepare (sr, 512);
        printf ("  reported latency: %d samples (%.2f ms)\n", eng.latencySamples(), 1000.0 * eng.latencySamples() / sr);
        const int n = (int) sr;       // one second
        std::vector<float> l0, r0; makeSignal (l0, r0, sr, n, 5);

        double worstPeak = 0.0;
        for (int band = 0; band < 2; ++band)
            for (int era = 0; era < kNumEras; ++era)
                for (int trial = 0; trial < 3; ++trial)
                {
                    UserParams u;
                    u.band = band; u.era = era;
                    if (trial == 0) { /* defaults */ }
                    else
                    {
                        u.region = (int) (U (rng) * 2); u.distanceKm = std::pow (10.0, -1.0 + 3.5 * U (rng));
                        u.powerKw = std::pow (10.0, -1.0 + 3.0 * U (rng)); u.env = (int) (U (rng) * 4);
                        u.antenna = (int) (U (rng) * 6); u.ground = (int) (U (rng) * 4); u.timeOfDay = U (rng);
                        u.storm = U (rng); u.speedKmh = 130.0 * U (rng); u.multipath = U (rng);
                        u.adjDb = -60.0 + 90.0 * U (rng); u.adj2Db = -60.0 + 90.0 * U (rng); u.coDb = -60.0 + 70.0 * U (rng);
                        u.impulse = U (rng); u.buzz = U (rng); u.detune = 2.0 * U (rng) - 1.0;
                        u.stereo = U (rng) > 0.3; u.rds = U (rng) > 0.5; u.drive = 0.2 + 1.1 * U (rng);
                        u.proc = (int) (U (rng) * 5); u.speaker = U (rng) > 0.3; u.smart = U (rng) > 0.3;
                        u.trimDb = -20.0 + 40.0 * U (rng); u.mix = U (rng); u.outDb = -6.0 + 12.0 * U (rng);
                        u.fmFreqMHz = 87.5 + 20.5 * U (rng); u.amFreqKHz = 530.0 + 1170.0 * U (rng);
                        if (trial == 2) { u.distanceKm = 0.1; u.powerKw = 250.0; u.trimDb = 20.0; }  // overload / absurdly strong
                    }
                    eng.setParams (u); eng.reset();
                    std::vector<float> l = l0, r = r0;
                    // process in awkward block sizes, changing parameters mid-stream
                    int pos = 0, blk = 37;
                    while (pos < n)
                    {
                        const int c = std::min (blk, n - pos);
                        eng.process (l.data() + pos, r.data() + pos, c);
                        pos += c; blk = (blk * 7 + 13) % 701 + 1;
                        if ((pos / 5000) % 2 == 1) { u.distanceKm *= 1.0005; eng.setParams (u); }
                    }
                    double pk = 0.0; bool finite = true;
                    for (int i = 0; i < n; ++i)
                    {
                        if (! std::isfinite (l[(size_t) i]) || ! std::isfinite (r[(size_t) i])) finite = false;
                        pk = std::max (pk, (double) std::max (std::fabs (l[(size_t) i]), std::fabs (r[(size_t) i])));
                    }
                    CHECK (finite, "non-finite output band %d era %d trial %d", band, era, trial);
                    CHECK (pk < 40.0, "absurd peak %.1f band %d era %d trial %d", pk, band, era, trial);
                    worstPeak = std::max (worstPeak, pk);
                }
        printf ("  3 x 18 configs finite, worst peak %.2f\n", worstPeak);

        // dry/wet alignment: mix = 0 must reproduce the input exactly, delayed by the reported latency
        {
            UserParams u; u.mix = 0.0; u.outDb = 0.0; eng.setParams (u); eng.reset();
            std::vector<float> l = l0, r = r0; eng.process (l.data(), r.data(), n);
            const int L = eng.latencySamples(); double err = 0;
            for (int i = L + 2000; i < n; ++i) err = std::max (err, (double) std::fabs (l[(size_t) i] - l0[(size_t) (i - L)]));
            CHECK (err < 1e-5, "mix=0 not a pure delay (err %.2e)", err);
        }
        // wet path alignment across eras: cross-correlate wet output with latency-shifted input
        {
            int worst = 0;
            for (int band = 0; band < 2; ++band)
                for (int era : { 1, 3, 4, 6, 8 })
                {
                    UserParams u; u.band = band; u.era = era; u.mix = 1.0; u.speaker = false; u.distanceKm = 1.0; u.trimDb = 30;
                    u.smart = false; u.multipath = 0; u.env = EnvOpen;
                    eng.setParams (u); eng.reset();
                    std::vector<float> l = l0, r = r0; eng.process (l.data(), r.data(), n);
                    int best = 0; double bv = -1e30; const int L = eng.latencySamples();
                    for (int lag = L - 40; lag <= L + 40; ++lag)
                    {
                        double a = 0;
                        for (int i = L + 4000; i < n - 100; i += 3) a += (double) l[(size_t) i] * (double) l0[(size_t) (i - lag)];
                        if (a > bv) { bv = a; best = lag; }
                    }
                    worst = std::max (worst, std::abs (best - L));
                }
            printf ("  wet/dry alignment: worst residual offset %d samples (%.2f ms)\n", worst, 1000.0 * worst / sr);
            CHECK (worst <= 24, "wet path misaligned by %d samples", worst);
        }
    }
    printf (failures ? "\n%d FAILURES\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
