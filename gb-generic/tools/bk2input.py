# BizHawk 무비(.bk2 를 푼 "Input Log.txt")의 GB 버튼 입력을 시험 도구 입력("프레임:키:길이,...")으로 바꾼다.
# 같은 키 상태가 이어지면 한 덩어리로 묶는다. Power(리셋) 열은 버린다.
# 사용: python bk2input.py "Input Log.txt" [빼는 프레임 수 (부트 롬 길이 맞추기, 음수면 앞에 빈 프레임)] [최대 프레임] > input.txt
import sys

# BizHawk GB 열 순서: Up Down Left Right Start Select B A Power
KEYS = [64, 128, 32, 16, 8, 4, 2, 1]
path = sys.argv[1]
skip = int(sys.argv[2]) if len(sys.argv) > 2 else 0
limit = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 30
frames = []
for line in open(path, encoding='utf-8', errors='replace'):
    if not line.startswith('|') or line.startswith('|#'):
        continue
    cells = line.strip().strip('|').replace('|', '')
    k = 0
    for i, mask in enumerate(KEYS):
        if i < len(cells) and cells[i] not in '. ':
            k |= mask
    frames.append(k)
out = []
start = None
prev = 0
seq = ([0] * -skip + frames) if skip < 0 else frames[skip:]  # 음수면 앞에 빈 프레임을 넣는다
for f, k in enumerate(seq[:limit] + [0]):
    if k != prev:
        if prev:
            out.append('%d:%d:%d' % (start, prev, f - start))
        start = f
        prev = k
print(','.join(out))
