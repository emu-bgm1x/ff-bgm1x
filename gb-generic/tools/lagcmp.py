# 소리 시차: 1초마다 생성 소리를 -600~+600ms 밀어 보며 원본과 가장 닮은 시차를 찾는다. 음수 = 생성이 빠름.
# 사용: python lagcmp.py ref.wav gen.wav
import sys, numpy as np
from scipy.io import wavfile
def load(p):
    r,d=wavfile.read(p); d=d.astype(float); return r,(d.mean(axis=1) if d.ndim==2 else d)
r,a=load(sys.argv[1]); _,b=load(sys.argv[2])
n=r//20; hop=r//100
def spec(x):
    w=np.hanning(n); return np.array([np.abs(np.fft.rfft(x[i:i+n]*w)) for i in range(0,len(x)-n,hop)])
A=spec(a); B=spec(b); m=min(len(A),len(B))
def sim(x,y): return (x*y).sum(1)/(np.linalg.norm(x,axis=1)*np.linalg.norm(y,axis=1)+1e-9)
for s0 in range(0, m//100):
    seg=slice(s0*100,(s0+1)*100)
    best=(-1,0)
    for lag in range(-60,61,1):
        i0=s0*100+lag
        if i0<0 or i0+100>m: continue
        v=sim(A[seg],B[i0:i0+100]).mean()
        if v>best[0]: best=(v,lag)
    print("%3d sim0=%.3f best=%.3f lag=%+dms"%(s0, sim(A[seg],B[seg]).mean(), best[0], best[1]*10))
