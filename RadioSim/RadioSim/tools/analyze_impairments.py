import sys, subprocess, numpy as np
from scipy.io import wavfile
from scipy.signal import welch
S = sys.argv[1]
def run(args, src='test_music.wav', out='tmp.wav', band=0):
    subprocess.run([f'{S}/radiosim_cli', f'{S}/{src}', f'{S}/{out}', f'band={band}', 'speaker=0'] + args, check=True)
    sr, o = wavfile.read(f'{S}/{out}'); return sr, o.astype(float) / 32768
sr, ref = run(['era=8', 'noiseless=1', 'mp=0', 'cnr=90'], out='fm_clean.wav')
sk = int(sr * .5)
def snr(o, ch=None):
    a = ref[sk:]; b = o[sk:]
    return 10 * np.log10(np.sum(a ** 2) / np.sum((b - a) ** 2))
print('FM @ CNR 45 dB, era 8 (modern), one impairment at a time')
base = ['era=8', 'cnr=45', 'mp=0']
print(f'  clean channel                     SNR {snr(run(base)[1]):6.1f} dB')
for mp in (0.3, 0.6, 1.0):
    print(f'  multipath {mp:.1f}, static rx           SNR {snr(run(base[:2] + [f"mp={mp}", "speed=0"])[1]):6.1f} dB')
for sp in (30, 100):
    print(f'  multipath 0.6, {sp:3d} km/h (Doppler) SNR {snr(run(base[:2] + ["mp=0.6", f"speed={sp}"])[1]):6.1f} dB')
for adj in (0, 20, 35):
    print(f'  adjacent channel {adj:+3d} dB          SNR {snr(run(base + [f"adj={adj}"])[1]):6.1f} dB')
for co in (-20, -10, -5, 0):
    print(f'  co-channel {co:+3d} dB (D/U {-co:+d})    SNR {snr(run(base + [f"co={co}"])[1]):6.1f} dB')
for im in (0.2, 0.6):
    print(f'  impulse noise {im:.1f}                 SNR {snr(run(base + [f"impulse={im}"])[1]):6.1f} dB')
for dt in (0.3, 0.6, 0.9):
    print(f'  mistuned {dt*120:4.0f} kHz               SNR {snr(run(base + [f"detune={dt}"])[1]):6.1f} dB')
