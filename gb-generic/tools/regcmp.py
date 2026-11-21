# 레지스터 쓰기 순서 일치율: 원본(ref_regs.txt)과 생성(gen_regs.txt)의 쓰기 (레지스터, 값) 순서를 맞춰 보고, 생성 쪽 쓰기 중 같은 순서로 맞은 비율을 찍는다. NR52(26) 는 뺀다.
# 사용: python regcmp.py ref_regs.txt gen_regs.txt
import sys, difflib
def seq(p):
    s=[]
    for l in open(p):
        p=l.split()
        if len(p)!=3: continue
        f,r,v=p
        if r!='26': s.append((r,v))
    return s
a=seq(sys.argv[1]); b=seq(sys.argv[2])
sm=difflib.SequenceMatcher(None,a,b,autojunk=False)
m=sum(x.size for x in sm.get_matching_blocks())
print('%.1f%%' % (100*m/max(1,len(b))))
