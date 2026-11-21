# 레지스터 쓰기 시차가 바뀌는 곳: 원본 프레임 구간마다 시차를 찍는다 (늦어짐이 어디서 생기는지 찾을 때).
# 사용: python segoff.py ref_regs.txt gen_regs.txt
import sys, difflib
def seq(p):
    s=[]
    for l in open(p):
        q=l.split()
        if len(q)!=3 or q[1]=='26': continue
        s.append((int(q[0]),q[1],q[2]))
    return s
a=seq(sys.argv[1]); b=seq(sys.argv[2])
sm=difflib.SequenceMatcher(None,[x[1:] for x in a],[x[1:] for x in b],autojunk=False)
pairs=[]
for blk in sm.get_matching_blocks():
    for k in range(blk.size): pairs.append((a[blk.a+k][0], b[blk.b+k][0]-a[blk.a+k][0]))
prev=None
for fa,o in pairs:
    if o!=prev:
        if prev is not None: print("ref f%d-f%d off %d"%(start,last,prev))
        prev=o; start=fa
    last=fa
print("ref f%d-f%d off %d"%(start,last,prev))
