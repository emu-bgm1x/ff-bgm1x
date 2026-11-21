# 곡 불일치 검사: 1초마다 원본(ref)과 생성(gen)의 스펙트럼 유사도를 재어, 둘 다 소리가 나는데(rms > 500)
# 유사도가 0.6 미만인 초가 3초 넘게 이어지는 구간(그림자가 다른 곡·다른 위치를 내는 것으로 의심)을 찍는다.
# 사용: python songcheck.py ref.wav gen.wav
import sys
import numpy as np
from scipy.io import wavfile


def load(p):
    r, d = wavfile.read(p)
    d = d.astype(np.float64)
    return r, (d.mean(axis=1) if d.ndim == 2 else d)


ra, a = load(sys.argv[1])
rb, b = load(sys.argv[2])
assert ra == rb
n = ra // 10
w = np.hanning(n)
secs = min(len(a), len(b)) // ra
bad = []
for s in range(secs):
    sims = []
    for i in range(10):
        x = a[s * ra + i * n:s * ra + (i + 1) * n]
        y = b[s * ra + i * n:s * ra + (i + 1) * n]
        if len(x) < n or len(y) < n:
            continue
        X = np.abs(np.fft.rfft(x * w)); Y = np.abs(np.fft.rfft(y * w))
        if X.sum() < 1e-6 and Y.sum() < 1e-6:
            continue  # 둘 다 무음인 창은 뺀다 (compare_wav.py 와 같음)
        sims.append((X * Y).sum() / (np.linalg.norm(X) * np.linalg.norm(Y) + 1e-9))
    ra_ = np.sqrt((a[s * ra:(s + 1) * ra] ** 2).mean()); rb_ = np.sqrt((b[s * ra:(s + 1) * ra] ** 2).mean())
    if ra_ > 500 and rb_ > 500 and sims and np.mean(sims) < 0.6:
        bad.append(s)
runs = []
for s in bad:
    if runs and runs[-1][1] == s - 1:
        runs[-1][1] = s
    else:
        runs.append([s, s])
runs = [r for r in runs if r[1] - r[0] + 1 >= 3]
if not runs:
    print('곡 불일치 없음')
else:
    print('곡 불일치 의심: ' + ', '.join('%d~%d초' % (x, y + 1) for x, y in runs))
