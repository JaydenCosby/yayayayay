import sys, subprocess, numpy as np
from scipy.io import wavfile
S = sys.argv[1]; era = sys.argv[2] if len(sys.argv) > 2 else '8'
sr, ref = wavfile.read(f'{S}/fm_clean.wav'); ref = ref.astype(np.float64) / 32768
def snr(a, b):
    skip = int(sr * 0.5)
    a = a[skip:]; b = b[skip:]
    return 10 * np.log10(np.sum(a ** 2) / max(np.sum((b - a) ** 2), 1e-20))
print(f'{"CNR":>5} {"SNR L+R":>8} {"SNR L-R":>8} {"SNR L":>7}  (era {era}, smart features off)')
for cnr in (0, 4, 6, 8, 10, 12, 15, 20, 25, 30, 40, 50):
    subprocess.run([f'{S}/radiosim_cli', f'{S}/test_music.wav', f'{S}/tmp.wav', 'band=0', f'era={era}', f'cnr={cnr}', 'speaker=0', 'mp=0', 'smart=0'], check=True)
    sr2, o = wavfile.read(f'{S}/tmp.wav'); o = o.astype(np.float64) / 32768
    m_ref, m_o = ref.sum(1) / 2, o.sum(1) / 2
    s_ref, s_o = (ref[:, 0] - ref[:, 1]) / 2, (o[:, 0] - o[:, 1]) / 2
    print(f'{cnr:5d} {snr(m_ref, m_o):8.1f} {snr(s_ref, s_o):8.1f} {snr(ref[:,0], o[:,0]):7.1f}')
