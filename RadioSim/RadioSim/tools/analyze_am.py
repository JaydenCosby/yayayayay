import sys, subprocess, numpy as np
from scipy.io import wavfile
from scipy.signal import welch
S = sys.argv[1]
def run(args, out='tmp.wav'):
    subprocess.run([f'{S}/radiosim_cli', f'{S}/test_music.wav', f'{S}/{out}', 'band=1', 'speaker=0'] + args, check=True)
    sr, o = wavfile.read(f'{S}/{out}'); return sr, o.astype(float)[:, 0] / 32768
for era in ('8', '4'):
    sr, ref = run([f'era={era}', 'noiseless=1', 'cnr=90'], 'am_clean.wav')
    print(f'--- era {era}: AM audio SNR vs CNR (no interferers)')
    for cnr in (0, 5, 10, 15, 20, 25, 30, 40, 50):
        sr, o = run([f'era={era}', f'cnr={cnr}'])
        sk = int(sr * .5)
        d = o[sk:] - ref[sk:]
        print(f'  CNR {cnr:3d} dB -> SNR {10*np.log10(np.sum(ref[sk:]**2)/np.sum(d**2)):6.1f} dB')
# whistle: adjacent channel carrier on a silent input
sil = np.zeros((44100 * 4, 2), dtype=np.float32); wavfile.write(f'{S}/silence.wav', 44100, sil)
for era in ('4', '5'):
    subprocess.run([f'{S}/radiosim_cli', f'{S}/silence.wav', f'{S}/whistle.wav', 'band=1', 'speaker=0', f'era={era}', 'cnr=45', 'adj=-6', 'impulse=0'], check=True)
    sr, o = wavfile.read(f'{S}/whistle.wav'); o = o.astype(float)[:, 0] / 32768
    f, P = welch(o[sr:], sr, nperseg=8192)
    pk = f[np.argmax(P)]
    print(f'era {era} adjacent +10 kHz carrier at -6 dB: strongest tone {pk:.0f} Hz, level {10*np.log10(P.max()*(f[1]-f[0])):.1f} dBFS(ish); IF/LP passband limit = {"n/a"}')
