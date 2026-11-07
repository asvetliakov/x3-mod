# Mirror of weapon_effects_bolt_stage.h bolt_prev_index / bolt_prev_find with the production stamp frame|1.
M=0xffffffff; SIZE=1024
def slot(o,c): return ((((o ^ ((c*0x85ebca6b)&M)) * 0x9e3779b1) & M) >> 22) & (SIZE-1)
class X:
    def __init__(s): s.gen=0; s.obj=[0]*SIZE; s.cr=[0]*SIZE; s.st=[0]*SIZE; s.ix=[0]*SIZE
def build(x, recs, stamp):
    x.gen=stamp
    for i,(o,c) in enumerate(recs):
        h=slot(o,c)
        for _ in range(SIZE):
            if x.st[h]!=stamp: x.st[h]=stamp; x.obj[h]=o; x.cr[h]=c; x.ix[h]=i; break
            h=(h+1)&(SIZE-1)
def find(x,o,c):
    h=slot(o,c); probes=0
    for _ in range(SIZE):
        probes+=1
        if x.st[h]!=x.gen: return -1,probes
        if x.obj[h]==o and x.cr[h]==c: return x.ix[h],probes
        h=(h+1)&(SIZE-1)
    return -1,probes
import random
random.seed(1)
bolts=[(0x1000000+16*i, 1000+i) for i in range(400)]
x=X()
for frame in range(4,8):
    prev=bolts[:]                      # previous frame's list (tap order)
    random.shuffle(bolts[:0])
    bolts=[(0x9000000+16*frame+k, 5000+frame) for k in range(8)]+bolts[8:]  # 8 new bullets at the front, 8 expired
    build(x, prev, frame|1)
    wrong=0; maxp=0
    for o,c in bolts:
        i,p=find(x,o,c); maxp=max(maxp,p)
        if i>=0 and prev[i]!=(o,c): wrong+=1
    print('frame',frame,'stamp',frame|1,'wrong prev index',wrong,'of',len(bolts),'max probes',maxp)
