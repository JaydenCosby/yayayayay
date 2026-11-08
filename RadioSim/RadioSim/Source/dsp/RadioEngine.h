// RadioEngine.h - top level: owns the FM and AM engines, the speaker voicing, dry/wet mix and
// latency compensation. This is the only class the plugin wrapper talks to.
#pragma once
#include <array>
#include <map>
#include <memory>
#include <mutex>
#include "Scenario.h"
#include "Voicer.h"

namespace radiosim
{

class RadioEngine
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        blockCap = std::max (16, std::min (maxBlock, 4096));
        fm.prepare (sr);
        am.prepare (sr);
        voicer.prepare (sr);
        measureLatencies();
        for (int c = 0; c < 2; ++c) { dryDl[c].init (maxLatency + 8); wetPad[c].init (maxLatency + 8); }
        dryBuf[0].assign ((size_t) blockCap, 0.0f);
        dryBuf[1].assign ((size_t) blockCap, 0.0f);
        havePrev = false;
        setParams (applied);
        reset();
    }

    void reset()
    {
        fm.reset(); am.reset(); voicer.reset();
        for (int c = 0; c < 2; ++c) { dryDl[c].reset(); wetPad[c].reset(); }
        gainSmooth = gainTarget;
        mixSmooth = mixTarget;
    }

    // Latency the host should be told about: constant, independent of era/band.
    int latencySamples() const { return maxLatency; }

    void setParams (const UserParams& u)
    {
        applied = u;
        havePrev = true;
        if (u.band == BandFM) { buildFm (u, fmP, link); fm.setParams (fmP); }
        else                  { buildAm (u, amP, link); am.setParams (amP); }
        voicer.setEra (u.era);
        curLatency = latencyTable[u.band][std::max (0, std::min (kNumEras - 1, u.era))];
        gainTarget = dbToAmp (u.outDb);
        mixTarget = u.mix;
    }

    const LinkInfo& linkInfo() const { return link; }
    double meterCnr() const { return applied.band == BandFM ? fm.meterCnrDb() : am.meterCnrDb(); }
    double meterStereo() const { return applied.band == BandFM ? fm.meterStereo() : 0.0; }
    double meterMute() const { return applied.band == BandFM ? fm.meterMute() : 1.0; }

    void process (float* l, float* r, int n)
    {
        int done = 0;
        while (done < n)
        {
            const int c = std::min (blockCap, n - done);
            processChunk (l + done, r + done, c);
            done += c;
        }
    }

private:
    void processChunk (float* l, float* r, int n)
    {
        const int pad = std::max (0, maxLatency - curLatency);
        for (int i = 0; i < n; ++i)
        {
            dryBuf[0][(size_t) i] = dryDl[0].process (l[i], maxLatency);
            dryBuf[1][(size_t) i] = dryDl[1].process (r[i], maxLatency);
        }
        if (applied.band == BandFM) fm.process (l, r, n); else am.process (l, r, n);
        if (applied.speaker) voicer.process (l, r, n);
        for (int i = 0; i < n; ++i)
        {
            // safety: threshold clicks / noise bursts can be many times full scale; a real radio's
            // output stage clips them, so we soft-limit the wet path instead of blasting speakers
            const float wl = (float) softLimit (wetPad[0].process (l[i], pad), 0.85);
            const float wr = (float) softLimit (wetPad[1].process (r[i], pad), 0.85);
            gainSmooth += 0.002 * (gainTarget - gainSmooth);
            mixSmooth += 0.002 * (mixTarget - mixSmooth);
            l[i] = (float) (gainSmooth * (mixSmooth * wl + (1.0 - mixSmooth) * dryBuf[0][(size_t) i]));
            r[i] = (float) (gainSmooth * (mixSmooth * wr + (1.0 - mixSmooth) * dryBuf[1][(size_t) i]));
        }
    }

    // Group delay of each (band, era) chain, measured with a tone burst through a noise-free copy.
    void measureLatencies()
    {
        // The result depends only on the sample rate, so share it between all plugin instances.
        static std::mutex cacheMutex;
        static std::map<long, std::array<int, 2 * kNumEras>> cache;
        const long key = std::lround (sr);
        {
            std::lock_guard<std::mutex> lock (cacheMutex);
            auto it = cache.find (key);
            if (it != cache.end())
            {
                maxLatency = 0;
                for (int i = 0; i < 2 * kNumEras; ++i)
                {
                    latencyTable[i / kNumEras][i % kNumEras] = it->second[(size_t) i];
                    maxLatency = std::max (maxLatency, it->second[(size_t) i]);
                }
                return;
            }
        }
        maxLatency = 0;
        auto fmE = std::make_unique<FmEngine>();
        auto amE = std::make_unique<AmEngine>();
        const int N = (int) (0.16 * sr), burst = (int) (0.045 * sr), start = (int) (0.02 * sr);
        std::vector<float> in ((size_t) N, 0.0f), a ((size_t) N), b ((size_t) N);
        for (int i = 0; i < burst; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (kTwoPi * i / (burst - 1));
            in[(size_t) (start + i)] = (float) (0.25 * w * std::sin (kTwoPi * 700.0 * i / sr));
        }
        fmE->prepare (sr);
        amE->prepare (sr);
        for (int band = 0; band < 2; ++band)
        {
            for (int e = 0; e < kNumEras; ++e)
            {
                std::copy (in.begin(), in.end(), a.begin());
                std::copy (in.begin(), in.end(), b.begin());
                if (band == 0)
                {
                    FmParams p; p.era = std::max (1, e); p.noiseScale = 0.0; p.multipath = 0; p.rds = false;
                    p.floorScale = 0.0; p.smartFeatures = false; p.modDepth = 0.5; p.txProc = 0.0;
                    fmE->setParams (p); fmE->reset();
                    fmE->process (a.data(), b.data(), N);
                }
                else
                {
                    AmParams p; p.era = e; p.noiseScale = 0.0; p.floorScale = 0.0; p.smartFeatures = false;
                    p.modDepth = 0.5; p.txProc = 0.0;
                    amE->setParams (p); amE->reset();
                    amE->process (a.data(), b.data(), N);
                }
                const int maxLag = (int) (0.020 * sr);
                double best = -1e30; int bestLag = 0;
                for (int lag = 0; lag < maxLag; ++lag)
                {
                    double acc = 0.0;
                    for (int i = start; i < start + burst && i + lag < N; i += 2)
                        acc += (double) in[(size_t) i] * (double) a[(size_t) (i + lag)];
                    if (acc > best) { best = acc; bestLag = lag; }
                }
                latencyTable[band][e] = bestLag;
                maxLatency = std::max (maxLatency, bestLag);
            }
        }
        std::array<int, 2 * kNumEras> row {};
        for (int i = 0; i < 2 * kNumEras; ++i) row[(size_t) i] = latencyTable[i / kNumEras][i % kNumEras];
        std::lock_guard<std::mutex> lock (cacheMutex);
        cache[key] = row;
    }

    double sr = 44100;
    int blockCap = 1024;
    FmEngine fm;
    AmEngine am;
    Voicer voicer;
    UserParams applied;
    bool havePrev = false;
    FmParams fmP; AmParams amP; LinkInfo link;
    int latencyTable[2][kNumEras] {};
    int maxLatency = 0, curLatency = 0;
    DelayLine dryDl[2], wetPad[2];
    std::vector<float> dryBuf[2];
    double gainSmooth = 1.0, gainTarget = 1.0, mixSmooth = 1.0, mixTarget = 1.0;
};

} // namespace radiosim
