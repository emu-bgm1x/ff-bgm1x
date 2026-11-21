# 소리 끊김 검사: 100ms 마다 원본(ref)과 생성(gen)을 견주어
#   - 멈춤: 원본은 소리가 나는데(rms > 300) 생성은 거의 무음(원본의 10% 미만)
#   - 고정: 생성 표본의 절반 넘게가 최댓값(|값| >= 32767) 에 붙어 있음
# 인 구간을 찾아 이어진 구간으로 묶어 찍는다. 없으면 "끊김 없음".
# 사용: python gapcheck.py ref.wav gen.wav   (gen 만 볼 때: python gapcheck.py - gen.wav, 고정만 검사)
import sys
import numpy as np
from scipy.io import wavfile


def load(p):
    r, d = wavfile.read(p)
    d = d.astype(np.float64)
    return r, (d if d.ndim == 2 else d[:, None])


rg, g = load(sys.argv[2])
ref = None
if sys.argv[1] != '-':
    rr, ref = load(sys.argv[1])
    assert rr == rg, 'sample rate differs'
win = rg // 10
n = len(g) // win
if ref is not None:
    n = min(n, len(ref) // win)
bad = []
for i in range(n):
    a = g[i * win:(i + 1) * win]
    stuck = (np.abs(a) >= 32767).mean() > 0.5
    stop = False
    if ref is not None:
        rr_ = np.sqrt((ref[i * win:(i + 1) * win].mean(axis=1) ** 2).mean())
        gr_ = np.sqrt((a.mean(axis=1) ** 2).mean())
        stop = rr_ > 300 and gr_ < 0.1 * rr_
    if stuck or stop:
        bad.append((i, 'stuck' if stuck else 'stop'))
if not bad:
    print('끊김 없음 (%d개 구간)' % n)
else:
    runs = []
    for i, k in bad:
        if runs and runs[-1][1] == i - 1 and runs[-1][2] == k:
            runs[-1][1] = i
        else:
            runs.append([i, i, k])
    for a, b, k in runs:
        print('%s %.1f~%.1f초' % ('최댓값 고정' if k == 'stuck' else '멈춤', a / 10, (b + 1) / 10))
    print('문제 구간 %d개 / 100ms 창 %d개' % (len(bad), n))
