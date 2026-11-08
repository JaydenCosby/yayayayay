# How the simulation maps to real radio

**Wavelength** λ = c/f. FM 87.5–108 MHz → 3.4–2.8 m (a quarter-wave whip ≈ 75 cm, which is why telescopic antennas are that long).
AM 530–1710 kHz → 566–175 m (far too long for an antenna, so AM sets use ferrite loops).

## FM chain (FmEngine.h)
Pre-emphasis 75 µs (50 µs Europe) → station processor/limiter → 15 kHz low-pass → stereo multiplex (ITU-R BS.450): mono L+R,
19 kHz pilot at 9 %, L−R on a suppressed 38 kHz subcarrier, optional 57 kHz RDS → FM at ±75 kHz.
Channel: Rician/Rayleigh multipath with Doppler (speed × f/c), log-normal shadowing, adjacent (±200 kHz, −400 kHz) and co-channel FM
stations, impulse noise, tuning error + drift, thermal/galactic/man-made noise (ITU-R P.372).
Receiver: IF filter (era bandwidth/order) → hard limiter → quadrature discriminator → 19 kHz PLL stereo decoder → pilot trap →
soft-mute / stereo blend / high-cut driven by a 70–90 kHz composite-noise detector (as real tuner chips do) → de-emphasis.
Verified: audio SNR rises 1 dB per dB of CNR above threshold; threshold ≈ 9 dB; stereo noise penalty measured 22 dB (theory 20–23).

## AM chain (AmEngine.h)
Compressor → asymmetric clipper (+125 % / −100 %) → audio band-limit → carrier × (1 + m·x).
Channel: groundwave (Norton surface-wave attenuation, ground conductivity, frequency dependent) + two-ray ionospheric skywave
(E-layer, D-layer daytime absorption, Rayleigh fading → selective fading), adjacent carriers at ±9/10 kHz (heterodyne whistle),
co-channel beat, atmospheric crackle, power-line buzz, noise (P.372 atmospheric/man-made).
Receiver: noise blanker → IF filter → AGC → diode envelope detector (diagonal clipping) or synchronous detector → whistle notch → audio filters.

## Link budget
FM: E = 106.9 + 10log(ERP kW) − 20log(d) − clutter − beyond-horizon diffraction; P = E + G − 20log f − 77.2 dBm; CNR = P − (kTB + F).
AM: E₀ = 109.7 + 10log(P kW) dBµV/m at 1 km; groundwave × Norton A(p); skywave via slant range; noise from P.372 converted with En = Fa + 20log f + 10log b − 95.5.
These are plausible engineering approximations, not a coverage-prediction tool. All era numbers (IF widths, noise figures, speaker voicings)
are modelling choices in `Source/dsp/Eras.h`.
