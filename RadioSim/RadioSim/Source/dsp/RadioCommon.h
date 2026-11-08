// RadioCommon.h - small DSP toolbox shared by the FM and AM engines.
// Header-only, no external dependencies (plain C++17).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace radiosim
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kSpeedOfLight = 299792458.0;

inline double clampd (double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline double dbToAmp (double db) { return std::pow (10.0, db * 0.05); }
inline double dbToPow (double db) { return std::pow (10.0, db * 0.1); }
inline double ampToDb (double a) { return 20.0 * std::log10 (std::max (a, 1e-30)); }
inline double powToDb (double p) { return 10.0 * std::log10 (std::max (p, 1e-30)); }
inline double lerpd (double a, double b, double t) { return a + (b - a) * t; }
inline double smoothstep (double e0, double e1, double x)
{
    const double t = clampd ((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
// one-pole smoothing coefficient for a time constant (seconds) at sample rate fs
inline double onePoleCoef (double tauSeconds, double fs)
{
    return tauSeconds <= 0.0 ? 1.0 : 1.0 - std::exp (-1.0 / (tauSeconds * fs));
}

//------------------------------------------------------------------ complex
struct Cx
{
    double re = 0.0, im = 0.0;
};
inline Cx operator+ (Cx a, Cx b) { return { a.re + b.re, a.im + b.im }; }
inline Cx operator- (Cx a, Cx b) { return { a.re - b.re, a.im - b.im }; }
inline Cx operator* (Cx a, Cx b) { return { a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re }; }
inline Cx operator* (Cx a, double s) { return { a.re * s, a.im * s }; }
inline Cx cconj (Cx a) { return { a.re, -a.im }; }
inline double cnorm2 (Cx a) { return a.re * a.re + a.im * a.im; }
inline Cx expj (double ph) { return { std::cos (ph), std::sin (ph) }; }

// Polynomial sin/cos for |x| <= ~1.6 rad (error < 1e-6). Used for the small
// per-sample phase increments of the FM modulators (no libm call per sample).
inline void fastSinCosSmall (double x, double& s, double& c)
{
    const double x2 = x * x;
    s = x * (1.0 + x2 * (-1.0 / 6.0 + x2 * (1.0 / 120.0 + x2 * (-1.0 / 5040.0 + x2 * (1.0 / 362880.0 + x2 * (-1.0 / 39916800.0))))));
    c = 1.0 + x2 * (-0.5 + x2 * (1.0 / 24.0 + x2 * (-1.0 / 720.0 + x2 * (1.0 / 40320.0 + x2 * (-1.0 / 3628800.0 + x2 * (1.0 / 479001600.0))))));
}

// atan2 with ~1e-5 rad worst-case error (Abramowitz & Stegun 4.4.49 minimax polynomial).
// The discriminator calls this once per RF sample, so it matters for CPU.
inline double fastAtan2 (double y, double x)
{
    const double ax = std::fabs (x), ay = std::fabs (y);
    const double mx = std::max (ax, ay), mn = std::min (ax, ay);
    if (mx < 1e-300) return 0.0;
    const double a = mn / mx, s = a * a;
    double r = ((((0.0208351 * s - 0.0851330) * s + 0.1801410) * s - 0.3302995) * s + 0.9998660) * a;
    if (ay > ax) r = 1.57079632679489662 - r;
    if (x < 0.0) r = 3.14159265358979324 - r;
    return y < 0.0 ? -r : r;
}

// Complex oscillator that is advanced by a (small) phase increment each sample.
struct Oscillator
{
    Cx z { 1.0, 0.0 };
    int count = 0;
    void reset (double phase = 0.0) { z = expj (phase); count = 0; }
    inline void advance (double dphi) // |dphi| <= ~1.6 rad
    {
        double s, c;
        fastSinCosSmall (dphi, s, c);
        const Cx n { z.re * c - z.im * s, z.re * s + z.im * c };
        z = n;
        if (++count >= 1024)
        {
            count = 0;
            const double inv = 1.0 / std::sqrt (cnorm2 (z));
            z.re *= inv;
            z.im *= inv;
        }
    }
    // advance by a precomputed exact rotation (cosd, sind)
    inline void rotate (double c, double s)
    {
        const Cx n { z.re * c - z.im * s, z.re * s + z.im * c };
        z = n;
        if (++count >= 1024)
        {
            count = 0;
            const double inv = 1.0 / std::sqrt (cnorm2 (z));
            z.re *= inv;
            z.im *= inv;
        }
    }
};

//------------------------------------------------------------------ random
class Rng
{
public:
    explicit Rng (uint64_t seed = 1) { reseed (seed); }
    void reseed (uint64_t seed)
    {
        s_ = seed * 0x9E3779B97F4A7C15ULL + 0x1234567ULL;
        if (s_ == 0) s_ = 1;
        haveSpare_ = false;
        for (int i = 0; i < 8; ++i) nextU64();
    }
    inline uint64_t nextU64()
    {
        s_ ^= s_ >> 12;
        s_ ^= s_ << 25;
        s_ ^= s_ >> 27;
        return s_ * 0x2545F4914F6CDD1DULL;
    }
    inline double uniform() { return (double) (nextU64() >> 11) * (1.0 / 9007199254740992.0); } // [0,1)
    inline double uniformOpen()
    {
        double u;
        do { u = uniform(); } while (u <= 1e-300);
        return u;
    }
    inline double gauss()
    {
        if (haveSpare_) { haveSpare_ = false; return spare_; }
        double u, v, s;
        do
        {
            u = 2.0 * uniform() - 1.0;
            v = 2.0 * uniform() - 1.0;
            s = u * u + v * v;
        } while (s >= 1.0 || s == 0.0);
        const double m = std::sqrt (-2.0 * std::log (s) / s);
        spare_ = v * m;
        haveSpare_ = true;
        return u * m;
    }
    inline double exponential() { return -std::log (uniformOpen()); }

private:
    uint64_t s_ = 1;
    double spare_ = 0.0;
    bool haveSpare_ = false;
};

//------------------------------------------------------------------ filters
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.0; }

    void set (double B0, double B1, double B2, double A0, double A1, double A2)
    {
        const double inv = 1.0 / A0;
        b0 = B0 * inv; b1 = B1 * inv; b2 = B2 * inv; a1 = A1 * inv; a2 = A2 * inv;
    }
    // RBJ cookbook designs. f is clamped just below Nyquist.
    void setLowpass (double fs, double f, double q)
    {
        f = clampd (f, 1.0, fs * 0.49);
        const double w = kTwoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set ((1 - c) * 0.5, 1 - c, (1 - c) * 0.5, 1 + al, -2 * c, 1 - al);
    }
    void setHighpass (double fs, double f, double q)
    {
        f = clampd (f, 1.0, fs * 0.49);
        const double w = kTwoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set ((1 + c) * 0.5, -(1 + c), (1 + c) * 0.5, 1 + al, -2 * c, 1 - al);
    }
    void setBandpass (double fs, double f, double q) // 0 dB peak gain
    {
        f = clampd (f, 1.0, fs * 0.49);
        const double w = kTwoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void setNotch (double fs, double f, double q)
    {
        f = clampd (f, 1.0, fs * 0.49);
        const double w = kTwoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set (1, -2 * c, 1, 1 + al, -2 * c, 1 - al);
    }
    void setPeaking (double fs, double f, double q, double gainDb)
    {
        f = clampd (f, 1.0, fs * 0.49);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = kTwoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void setIdentity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
};

// First-order section from an analogue prototype H(s) = (1 + s/wz) / (1 + s/wp)
// (wz <= 0 means "no zero": a plain one-pole low-pass). Bilinear transform.
struct FirstOrder
{
    double b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;
    inline double process (double x)
    {
        const double y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x; y1 = y;
        return y;
    }
    void reset() { x1 = y1 = 0.0; }
    void setPoleZero (double fs, double wz, double wp)
    {
        const double c = 2.0 * fs;
        double nb0 = 1.0, nb1 = 1.0;
        if (wz > 0.0) { nb0 = 1.0 + c / wz; nb1 = 1.0 - c / wz; }
        const double d0 = 1.0 + c / wp, d1 = 1.0 - c / wp;
        b0 = nb0 / d0; b1 = nb1 / d0; a1 = d1 / d0;
    }
    void setLowpassHz (double fs, double f) { setPoleZero (fs, 0.0, kTwoPi * f); }
};

// Cascade of biquads forming an even-order Butterworth low/high-pass.
struct ButterChain
{
    std::vector<Biquad> sec;

    void designLowpass (double fs, double fc, int order)
    {
        const int n = std::max (1, order / 2);
        if ((int) sec.size() != n) sec.assign ((size_t) n, Biquad());
        for (int k = 1; k <= n; ++k)
        {
            const double q = 1.0 / (2.0 * std::cos (kPi * (2.0 * k - 1.0) / (2.0 * (2 * n))));
            sec[(size_t) k - 1].setLowpass (fs, fc, q);
        }
    }
    void designHighpass (double fs, double fc, int order)
    {
        const int n = std::max (1, order / 2);
        if ((int) sec.size() != n) sec.assign ((size_t) n, Biquad());
        for (int k = 1; k <= n; ++k)
        {
            const double q = 1.0 / (2.0 * std::cos (kPi * (2.0 * k - 1.0) / (2.0 * (2 * n))));
            sec[(size_t) k - 1].setHighpass (fs, fc, q);
        }
    }
    inline double process (double x)
    {
        for (auto& s : sec) x = s.process (x);
        return x;
    }
    void reset() { for (auto& s : sec) s.reset(); }
};

// Same real filter applied to I and Q = a symmetric complex baseband filter.
struct ComplexLowpass
{
    ButterChain ci, cq;
    void design (double fs, double fc, int order)
    {
        ci.designLowpass (fs, fc, order);
        cq.designLowpass (fs, fc, order);
    }
    inline Cx process (Cx x) { return { ci.process (x.re), cq.process (x.im) }; }
    void reset() { ci.reset(); cq.reset(); }
};

//------------------------------------------------------------------ resampling
inline double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    const double q = x * 0.5;
    for (int k = 1; k < 60; ++k)
    {
        term *= (q / k) * (q / k);
        sum += term;
        if (term < 1e-14 * sum) break;
    }
    return sum;
}

// Kaiser-windowed sinc low-pass; fc in cycles/sample at the design rate.
inline std::vector<double> designKaiserLowpass (int N, double fc, double beta)
{
    std::vector<double> h ((size_t) N);
    const double M = 0.5 * (N - 1), i0b = besselI0 (beta);
    for (int n = 0; n < N; ++n)
    {
        const double x = n - M;
        const double s = std::fabs (x) < 1e-9 ? 2.0 * fc : std::sin (kTwoPi * fc * x) / (kPi * x);
        const double r = M > 0 ? x / M : 0.0;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
        h[(size_t) n] = s * w;
    }
    return h;
}

// Polyphase interpolator: 1 input sample -> L output samples.
class PolyInterp
{
public:
    void init (int L_, int K_, double cutoffFrac, double beta = 8.0)
    {
        L = L_; K = K_;
        const int N = L * K;
        auto h = designKaiserLowpass (N, cutoffFrac * 0.5 / L, beta);
        double sum = 0; for (double v : h) sum += v;
        const double g = (double) L / sum;
        poly.assign ((size_t) N, 0.0);
        for (int p = 0; p < L; ++p)
            for (int k = 0; k < K; ++k)
                poly[(size_t) (p * K + k)] = h[(size_t) (p + k * L)] * g;
        buf.assign ((size_t) (2 * K), 0.0);
        pos = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0); pos = 0; }
    inline void process (double x, double* out)
    {
        if (--pos < 0) pos = K - 1;
        buf[(size_t) pos] = buf[(size_t) (pos + K)] = x;
        const double* b = &buf[(size_t) pos];
        for (int p = 0; p < L; ++p)
        {
            const double* c = &poly[(size_t) (p * K)];
            double a0 = 0, a1 = 0, a2 = 0, a3 = 0;
            int k = 0;
            for (; k + 3 < K; k += 4)
            {
                a0 += c[k] * b[k]; a1 += c[k + 1] * b[k + 1];
                a2 += c[k + 2] * b[k + 2]; a3 += c[k + 3] * b[k + 3];
            }
            for (; k < K; ++k) a0 += c[k] * b[k];
            out[p] = (a0 + a1) + (a2 + a3);
        }
    }
    double delayInInputSamples() const { return (double) (L * K - 1) / (2.0 * L); }
    int L = 1, K = 1;

private:
    std::vector<double> poly, buf;
    int pos = 0;
};

// Decimator: M input samples -> 1 output sample.
class PolyDecim
{
public:
    void init (int M_, int K_, double cutoffFrac, double beta = 8.0)
    {
        M = M_; N = M * K_;
        h = designKaiserLowpass (N, cutoffFrac * 0.5 / M, beta);
        double sum = 0; for (double v : h) sum += v;
        for (double& v : h) v /= sum;
        buf.assign ((size_t) (2 * N), 0.0);
        pos = 0; phase = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0); pos = 0; phase = 0; }
    // returns true when an output sample is produced
    inline bool process (double x, double& y)
    {
        if (--pos < 0) pos = N - 1;
        buf[(size_t) pos] = buf[(size_t) (pos + N)] = x;
        if (++phase < M) return false;
        phase = 0;
        const double* b = &buf[(size_t) pos];
        const double* c = h.data();
        double a0 = 0, a1 = 0, a2 = 0, a3 = 0;
        int j = 0;
        for (; j + 3 < N; j += 4)
        {
            a0 += c[j] * b[j]; a1 += c[j + 1] * b[j + 1];
            a2 += c[j + 2] * b[j + 2]; a3 += c[j + 3] * b[j + 3];
        }
        for (; j < N; ++j) a0 += c[j] * b[j];
        y = (a0 + a1) + (a2 + a3);
        return true;
    }
    double delayInOutputSamples() const { return (double) (N - 1) / (2.0 * M); }
    int M = 1, N = 1;

private:
    std::vector<double> h, buf;
    int pos = 0, phase = 0;
};

//------------------------------------------------------------------ misc
// Integer delay line (used for the dry path of the Mix control).
class DelayLine
{
public:
    void init (int maxDelay)
    {
        size = 1;
        while (size < maxDelay + 2) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline float process (float x, int delay)
    {
        buf[(size_t) w] = x;
        const int r = (w - delay) & (size - 1);
        w = (w + 1) & (size - 1);
        return buf[(size_t) r];
    }

private:
    std::vector<float> buf;
    int size = 1, w = 0;
};

// Complex delay line with 4-point cubic-Lagrange fractional taps.
class FracDelayC
{
public:
    static constexpr int kSize = 128; // power of two
    void reset() { std::fill (re, re + kSize, 0.0); std::fill (im, im + kSize, 0.0); w = 0; }
    inline void push (Cx x)
    {
        w = (w + 1) & (kSize - 1);
        re[w] = x.re; im[w] = x.im;
    }
    // delay in samples, must be >= 1 and <= kSize - 3
    inline Cx tap (double d) const
    {
        const int i = (int) d;
        const double mu = d - i;
        const double c0 = -mu * (mu - 1.0) * (mu - 2.0) * (1.0 / 6.0);
        const double c1 = (mu + 1.0) * (mu - 1.0) * (mu - 2.0) * 0.5;
        const double c2 = -(mu + 1.0) * mu * (mu - 2.0) * 0.5;
        const double c3 = (mu + 1.0) * mu * (mu - 1.0) * (1.0 / 6.0);
        const int i0 = (w - (i - 1)) & (kSize - 1);
        const int i1 = (w - i) & (kSize - 1);
        const int i2 = (w - i - 1) & (kSize - 1);
        const int i3 = (w - i - 2) & (kSize - 1);
        return { c0 * re[i0] + c1 * re[i1] + c2 * re[i2] + c3 * re[i3],
                 c0 * im[i0] + c1 * im[i1] + c2 * im[i2] + c3 * im[i3] };
    }

private:
    double re[kSize] {}, im[kSize] {};
    int w = 0;
};

// Sum-of-sinusoids Rayleigh fading generator (unit mean power).
class FadingGen
{
public:
    static constexpr int N = 12;
    void init (Rng& rng)
    {
        for (int k = 0; k < N; ++k)
        {
            cosAlpha[k] = std::cos (kTwoPi * (k + rng.uniform()) / N);
            ph[k] = kTwoPi * rng.uniform();
        }
    }
    // advance by dt seconds with maximum Doppler fd (Hz)
    Cx step (double fd, double dt)
    {
        double re = 0.0, im = 0.0;
        for (int k = 0; k < N; ++k)
        {
            ph[k] += kTwoPi * fd * cosAlpha[k] * dt;
            if (ph[k] > 1.0e4) ph[k] = std::fmod (ph[k], kTwoPi);
            if (ph[k] < -1.0e4) ph[k] = std::fmod (ph[k], kTwoPi);
            re += std::cos (ph[k]);
            im += std::sin (ph[k]);
        }
        const double s = 1.0 / std::sqrt ((double) N);
        return { re * s, im * s };
    }

private:
    double cosAlpha[N] {}, ph[N] {};
};

// Soft limiter: linear below knee, tanh-shaped approach to +/-1 above it.
inline double softLimit (double x, double knee)
{
    const double a = std::fabs (x);
    if (a <= knee) return x;
    const double r = 1.0 - knee;
    const double y = knee + r * std::tanh ((a - knee) / std::max (r, 1e-6));
    return x < 0 ? -y : y;
}

} // namespace radiosim
