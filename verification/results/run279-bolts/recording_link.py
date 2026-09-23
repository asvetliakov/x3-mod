# Link transient blobs (transient.py output) across consecutive distinct frames into bolt tracks.
import re, sys, statistics as st
fn, R, amin = sys.argv[1], float(sys.argv[2]), int(sys.argv[3]); XLO, XHI = int(sys.argv[4]), int(sys.argv[5])
frames=[]
for line in open(fn):
    p=line.split(); k=int(p[0])
    bl=[tuple(map(int,m.groups())) for m in re.finditer(r'\((\d+),(\d+) (\d+)x(\d+) a(\d+) pk(\d+)\)',line)]
    bl=[b for b in bl if b[4]>=amin and XLO<=b[0]<=XHI]
    frames.append((k,p[1],bl))
tracks=[]; open_=[]
for k,name,bl in frames:
    used=set(); nxt=[]
    for t in open_:
        lk,(x,y,*_)=t[-1][0],t[-1][2]
        best=None
        for i,b in enumerate(bl):
            if i in used: continue
            d=((b[0]-x)**2+(b[1]-y)**2)**.5
            if d<=R and b[4]<=1.5*t[-1][2][4]+3 and (best is None or d<best[0]): best=(d,i)
        if best: used.add(best[1]); t.append((k,name,bl[best[1]])); nxt.append(t)
        elif k-lk<=2: nxt.append(t)
        else: tracks.append(t)
    for i,b in enumerate(bl):
        if i not in used: nxt.append([(k,name,b)])
    open_=nxt
tracks+=open_
tr=[t for t in tracks if len(t)>=2]
L=[t[-1][0]-t[0][0]+1 for t in tr]
import collections; print('span histogram', sorted(collections.Counter(L).items()))
print(f'{fn}: tracks>=2 {len(tr)} span(distinct frames) median {st.median(L) if L else 0} min {min(L) if L else 0} max {max(L) if L else 0}')
for t in sorted(tr,key=lambda t:-(t[-1][0]-t[0][0]))[:6]+sorted(tr,key=lambda t:t[0][0])[:6]:
    a,b=t[0][2],t[-1][2]
    print(f'  frames {t[0][1]}..{t[-1][1]} n{len(t)} span{t[-1][0]-t[0][0]+1} start ({a[0]},{a[1]}) {a[2]}x{a[3]} a{a[4]} pk{a[5]} -> end ({b[0]},{b[1]}) {b[2]}x{b[3]} a{b[4]} pk{b[5]}')
