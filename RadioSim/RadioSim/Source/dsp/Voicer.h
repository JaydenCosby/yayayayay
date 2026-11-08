// Voicer.h - the "speaker and cabinet" of each era: band-limits, resonances, saturation, stereo width.
#pragma once
#include "RadioCommon.h"
#include "Eras.h"

namespace radiosim
{

class Voicer
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        design (era);
        reset();
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            hp[c].reset(); lp[c].reset();
            for (auto& b : eq[c]) b.reset();
        }
    }
    void setEra (int e)
    {
        e = std::max (0, std::min (kNumEras - 1, e));
        if (e != era || ! designedOnce) { era = e; design (e); }
        designedOnce = true;
    }
    void process (float* l, float* r, int n)
    {
        const Voicing& v = kVoicings[era];
        const double drive = 1.0 + 7.0 * v.satDrive;
        const double norm = v.satDrive > 0.0 ? 1.0 / std::tanh (drive * 0.7) * 0.7 : 1.0;
        for (int i = 0; i < n; ++i)
        {
            double s[2] = { (double) l[i], (double) r[i] };
            // width: collapse towards mono for small speakers
            const double mid = 0.5 * (s[0] + s[1]), side = 0.5 * (s[0] - s[1]) * v.width;
            s[0] = mid + side; s[1] = mid - side;
            for (int c = 0; c < 2; ++c)
            {
                double x = s[c];
                x = hp[c].process (x);
                for (auto& b : eq[c]) x = b.process (x);
                if (v.satDrive > 0.0)
                {
                    const double bias = v.satAsym * 0.25;
                    x = (std::tanh (drive * (x + bias) * 0.7) - std::tanh (drive * bias * 0.7)) * norm;
                }
                if (v.crossover > 0.0) x -= v.crossover * std::tanh (x / v.crossover) * 0.9; // class-B dead zone
                x = lp[c].process (x);
                s[c] = x;
            }
            l[i] = (float) s[0];
            r[i] = (float) s[1];
        }
    }

private:
    void design (int e)
    {
        const Voicing& v = kVoicings[e];
        for (int c = 0; c < 2; ++c)
        {
            hp[c].designHighpass (sr, v.hpHz, v.hpOrder);
            lp[c].designLowpass (sr, std::min (v.lpHz, 0.46 * sr), v.lpOrder);
            for (int b = 0; b < 3; ++b)
            {
                if (std::fabs (v.eq[b].gDb) < 0.01) eq[c][b].setIdentity();
                else eq[c][b].setPeaking (sr, v.eq[b].f, v.eq[b].q, v.eq[b].gDb);
            }
        }
    }
    double sr = 44100;
    int era = 6;
    bool designedOnce = false;
    ButterChain hp[2], lp[2];
    Biquad eq[2][3];
};

} // namespace radiosim
