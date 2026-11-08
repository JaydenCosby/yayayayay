# RadioSim — realistic FM / AM radio simulator (VST3 for FL Studio)

Not an EQ-and-noise "radio filter": your audio is actually **modulated** (FM with a real stereo multiplex, or AM),
sent through a simulated radio channel, and **demodulated** by a simulated receiver. Hiss, stereo collapse,
threshold clicks, capture effect, heterodyne whistles and night-time skywave fading come out of that chain.

## Build (pick one)
**A. No compiler needed:** push this folder to a GitHub repo → *Actions* tab → "Build RadioSim VST3" → download the artifact.
**B. Locally** (Visual Studio 2022 + CMake): `cmake -B build -G "Visual Studio 17 2022" -A x64` then `cmake --build build --config Release`.
Result: `build/RadioSim_artefacts/Release/VST3/RadioSim.vst3`

## Install in FL Studio 25
Copy `RadioSim.vst3` to `C:\Program Files\Common Files\VST3\`, then *Options → Manage plugins → Find installed plugins*.
Add it as a mixer effect. Pick a preset from the top menu, then tweak.

## Controls
Band (FM/AM) · Era (1920s–2020s) · tuning dial (shows wavelength and the quarter-wave antenna length) · Distance · Station power ·
Surroundings · Antenna · Ground (AM) · Time of day (AM skywave) · Storm · Speed (Doppler fading) · Multipath · Detune ·
Adjacent / 2nd-adjacent / Co-channel stations · Impulse noise · Line buzz · Modulation · Station processing · Stereo / RDS ·
Speaker · Smart receiver (soft-mute, blend, high-cut) · Signal trim · Mix · Output.
49 factory presets (distance ladders for both bands, driving, interference, one per era).

## Notes
* Reports a constant latency (~0.8 ms) so Mix stays phase-aligned. CPU: roughly 5–10 % of one core per FM instance, less for AM.
* `tools/` has an offline renderer (`radiosim_cli`) and the test/analysis scripts. `docs/RADIO_NOTES.md` explains the physics.
* Tested: DSP engine (SNR curves, threshold, stereo penalty, interference, all eras, 6 sample rates). The JUCE UI is
  compile-checked against JUCE 8.0.4 headers but **not visually tested** — layout tweaks may be needed.
