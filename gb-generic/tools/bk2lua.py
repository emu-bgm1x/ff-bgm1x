# BizHawk 무비 입력(Input Log.txt)을 mGBA 스크립트(Lua)로 바꾼다. mGBA 에서 롬을 켠 뒤
# 도구 > 스크립트 에서 이 파일을 불러오면 게임을 처음부터 다시 시작해 프레임마다 버튼을 넣는다.
# 사용: python bk2lua.py "Input Log.txt" 빼는프레임 출력.lua
import sys

KEYS = [64, 128, 32, 16, 8, 4, 2, 1]  # BizHawk GB 열: Up Down Left Right Start Select B A (Power 는 버림)
src, skip, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
frames = []
for line in open(src, encoding='utf-8', errors='replace'):
    if not line.startswith('|') or line.startswith('|#'):
        continue
    cells = line.strip().strip('|').replace('|', '')
    k = 0
    for i, m in enumerate(KEYS):
        if i < len(cells) and cells[i] not in '. ':
            k |= m
    frames.append(k)
frames = frames[skip:]
runs = []
prev, start = None, 0
for f, k in enumerate(frames + [-1]):
    if k != prev:
        if prev is not None:
            runs.append((start, prev))
        start, prev = f, k
with open(out, 'w', encoding='utf-8') as w:
    w.write('-- ff-bgm1x 시험용: 무비 입력 재생 (프레임 %d 개)\n' % len(frames))
    w.write('local runs = {\n')
    for s, k in runs:
        w.write('{%d,%d},\n' % (s, k))
    w.write('}\n')
    w.write('''local base = nil
local idx = 1
callbacks:add("reset", function() base = nil; idx = 1 end)
callbacks:add("frame", function()
  local now = emu:currentFrame()
  if base == nil then base = now end
  local f = now - base
  while idx < #runs and runs[idx + 1][1] <= f do idx = idx + 1 end
  if f >= %d then
    emu:setKeys(0)
  else
    emu:setKeys(runs[idx][2])
  end
end)
console:log("ff-bgm1x: movie input loaded, restarting the game")
emu:reset()
''' % len(frames))
print('ok runs', len(runs))
