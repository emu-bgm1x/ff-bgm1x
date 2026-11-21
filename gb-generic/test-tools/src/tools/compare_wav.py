# 두 WAV 의 소리 대조: 짧은 구간(기본 100ms)마다 크기 스펙트럼의 코사인 유사도와 음량을 비교한다.
# 파형 위상(사각파 시작점 등)이 달라도 같은 음이면 유사도가 1 에 가깝다.
# 사용: python compare_wav.py a.wav b.wav [구간ms] [초당 요약만=1]
import sys
import numpy as np
from scipy.io import wavfile


def load(p):
    rate, d = wavfile.read(p)
    d = d.astype(np.float64)
    if d.ndim == 2:
        d = d.mean(axis=1)
    return rate, d


def spectra(x, n, hop):
    win = np.hanning(n)
    frames = []
    for i in range(0, len(x) - n, hop):
        frames.append(np.abs(np.fft.rfft(x[i:i + n] * win)))
    return np.array(frames)


def main():
    ra, a = load(sys.argv[1])
    rb, b = load(sys.argv[2])
    ms = int(sys.argv[3]) if len(sys.argv) > 3 else 100
    n = ra * ms // 1000
    m = min(len(a), len(b))
    a, b = a[:m], b[:m]
    sa, sb = spectra(a, n, n), spectra(b, n, n)
    per_sec = 1000 // ms
    print("sec  rmsA   rmsB   spec_sim(min/mean)")
    sims = []
    for s in range(len(sa) // per_sec):
        rows = []
        for k in range(s * per_sec, (s + 1) * per_sec):
            x, y = sa[k], sb[k]
            if x.sum() < 1e-6 and y.sum() < 1e-6:
                continue
            rows.append(float(np.dot(x, y) / (np.linalg.norm(x) * np.linalg.norm(y) + 1e-9)))
        seg = slice(s * ra, (s + 1) * ra)
        ra_ = np.sqrt(np.mean(a[seg] ** 2)) if m > s * ra else 0
        rb_ = np.sqrt(np.mean(b[seg] ** 2)) if m > s * ra else 0
        if rows:
            sims += rows
            print("%3d %6.0f %6.0f   %.3f / %.3f" % (s, ra_, rb_, min(rows), sum(rows) / len(rows)))
        else:
            print("%3d %6.0f %6.0f   (silent)" % (s, ra_, rb_))
    if sims:
        print("overall mean %.3f, windows below 0.9: %d of %d" % (sum(sims) / len(sims), sum(1 for v in sims if v < 0.9), len(sims)))


main()
