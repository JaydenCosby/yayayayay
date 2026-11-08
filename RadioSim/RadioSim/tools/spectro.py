import sys, numpy as np, matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt
from scipy.io import wavfile
from scipy.signal import spectrogram
S=sys.argv[1]; items=[('test_music.wav','Original')]+[(f'demo/p{i}.wav',n) for i,n in zip(sys.argv[3::2],sys.argv[4::2])]
fig,axs=plt.subplots(len(items),1,figsize=(11,2.1*len(items)),sharex=True)
for ax,(f,t) in zip(axs,items):
    sr,x=wavfile.read(f'{S}/{f}'); x=(x.astype(float)/32768 if x.dtype.kind=='i' else x.astype(float)); m=x.mean(1) if x.ndim>1 else x
    ff,tt,P=spectrogram(m[:sr*10],sr,nperseg=2048,noverlap=1536)
    ax.pcolormesh(tt,ff/1000,10*np.log10(P+1e-12),vmin=-110,vmax=-30,shading='auto',cmap='magma'); ax.set_ylim(0,20); ax.set_ylabel('kHz'); ax.set_title(t,fontsize=9,loc='left')
axs[-1].set_xlabel('seconds'); plt.tight_layout(); plt.savefig(sys.argv[2],dpi=70)
