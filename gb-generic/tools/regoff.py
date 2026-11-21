# 레지스터 쓰기 시차: 순서가 맞은 쓰기마다 (생성 프레임 - 원본 프레임) 을 세고, 10초 구간별 많은 값을 찍는다. 0 이 정상.
# 사용: python regoff.py ref_regs.txt gen_regs.txt
import sys, difflib, collections
def seq(p):
    s=[]
    for l in open(p):
        q=l.split()
        if len(q)!=3 or q[1]=='26': continue
        s.append((int(q[0]),q[1],q[2]))
    return s
a=seq(sys.argv[1]); b=seq(sys.argv[2])
sm=difflib.SequenceMatcher(None,[x[1:] for x in a],[x[1:] for x in b],autojunk=False)
off=collections.Counter(); byt=collections.defaultdict(collections.Counter)
for blk in sm.get_matching_blocks():
    for k in range(blk.size):
        fa=a[blk.a+k][0]; fb=b[blk.b+k][0]
        off[fb-fa]+=1; byt[fa//600][fb-fa]+=1
print("offset(gen-ref frames):", off.most_common(6))
for t in sorted(byt): print(" %3ds"%(t*10), byt[t].most_common(3))
