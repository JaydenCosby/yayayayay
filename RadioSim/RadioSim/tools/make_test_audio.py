#!/usr/bin/env python3
"""Synthesise a short stereo 'music' test signal (bass, chords, hi-hats, lead) for the RadioSim demos."""
import sys, numpy as np
from scipy.io import wavfile

def make(sr=44100, secs=14.0, seed=3):
    rng = np.random.default_rng(seed)
    n = int(sr * secs); t = np.arange(n) / sr
    L = np.zeros(n); R = np.zeros(n)
    bpm = 100; beat = 60.0 / bpm
    prog = [(57, [0, 3, 7, 10]), (53, [0, 4, 7, 11]), (48, [0, 4, 7, 11]), (55, [0, 4, 7, 10])]
    def mtof(m): return 440.0 * 2 ** ((m - 69) / 12)
    def env(tt, a, d): return np.where(tt < 0, 0, np.minimum(tt / a, 1.0) * np.exp(-np.maximum(tt, 0) / d))
    for bar in range(int(secs / (4 * beat)) + 1):
        root, ch = prog[bar % 4]
        t0 = bar * 4 * beat
        for k, off in enumerate(ch):                       # chord pad, spread in stereo
            f = mtof(root + 12 + off); pan = (k / 3.0) * 0.8 - 0.4
            tt = t - t0
            m = (tt >= 0) & (tt < 4 * beat)
            sig = np.zeros(n)
            for h, a in ((1, 1), (2, .4), (3, .25), (4, .12)):
                sig += a * np.sin(2 * np.pi * f * h * t)
            sig *= m * env(tt, 0.05, 2.0) * 0.07
            L += sig * (0.5 - pan / 2) * 1.4; R += sig * (0.5 + pan / 2) * 1.4
        for b in range(4):                                 # bass
            tt = t - (t0 + b * beat)
            m = (tt >= 0) & (tt < beat)
            sig = np.sin(2 * np.pi * mtof(root - 12) * t) + 0.3 * np.sin(2 * np.pi * mtof(root - 12) * 2 * t)
            sig *= m * env(tt, 0.005, 0.35) * 0.35
            L += sig; R += sig
        for q in range(8):                                 # hi-hat, noise burst, slightly right
            tt = t - (t0 + q * beat / 2)
            m = (tt >= 0) & (tt < 0.12)
            nz = rng.standard_normal(n)
            nz = np.diff(nz, prepend=0)                    # high-passed
            sig = nz * m * env(tt, 0.001, 0.025) * 0.12
            L += sig * 0.4; R += sig * 0.6
        for b in (0, 2):                                   # kick
            tt = t - (t0 + b * beat)
            m = (tt >= 0) & (tt < 0.3)
            sig = np.sin(2 * np.pi * (45 + 90 * np.exp(-np.maximum(tt, 0) * 25)) * np.maximum(tt, 0)) * m * env(tt, 0.002, 0.12) * 0.6
            L += sig; R += sig
    # lead: vocal-ish saw with moving formants, mono-ish centre
    mel = [69, 72, 76, 74, 72, 69, 67, 71]
    lead = np.zeros(n)
    for i in range(int(secs / (beat / 2)) + 1):
        f = mtof(mel[i % len(mel)]); tt = t - i * beat / 2
        m = (tt >= 0) & (tt < beat / 2)
        ph = (f * t) % 1.0
        saw = 2 * ph - 1
        lead += saw * m * env(tt, 0.01, 0.4) * 0.12
    from scipy.signal import butter, lfilter
    for fc, g in ((900, 1.0), (2400, 0.6)):
        b_, a_ = butter(2, [fc * 0.8 / (sr / 2), fc * 1.25 / (sr / 2)], btype='band')
        lead_f = lfilter(b_, a_, lead) * g
        L += lead_f * 3; R += lead_f * 3
    L += 0.003 * rng.standard_normal(n); R += 0.003 * rng.standard_normal(n)
    pk = max(np.abs(L).max(), np.abs(R).max())
    return sr, (L / pk * 0.9).astype(np.float32), (R / pk * 0.9).astype(np.float32)

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'test_music.wav'
    sr, L, R = make()
    wavfile.write(out, sr, np.stack([L, R], axis=1))
    print('wrote', out)
