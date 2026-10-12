# Run155: what happens in the trace gap between the last order-4 row and the first order-5 row of each traced transition.
import re,sys,math,statistics as S
from collections import defaultdict,Counter
rows=defaultdict(list); det=defaultdict(list)
pat=re.compile(r'(\w+)=(\S+)')
for l in open(sys.argv[1],errors='replace'):
    if l.startswith('ship_trace frame'):
        d=dict(pat.findall(l)); rows[d['obj']].append((int(d['frame']),int(d['t_ms']),d['order'],d['tgt_class'],d['pos'],d['given']))
    elif l.startswith('ship_avoid_detour frame'):
        d=dict(pat.findall(l)); det[d['obj']].append((int(d['frame']),d['order'],d['obstacle'],d['kind'],float(d['t_cpa_s']),float(d['miss_m'])))
res=[]
for o,v in rows.items():
    for i in range(1,len(v)):
        b=v[i]
        if b[2]=='0x5' and b[3]=='18' and v[i-1][2]!='0x5':
            j=i-1
            while j>=0 and not (v[j][2]=='0x4' and v[j][3]=='18'): j-=1
            if j<0: continue
            a=v[j]
            between=[r[2] for r in v[j+1:i]]
            dets=[x for x in det[o] if a[0]<=x[0]<=b[0]]
            res.append((b[1]-a[1],o,a[0],b[0],b[0]-a[0],Counter(between),Counter(x[1] for x in dets),len(dets)))
res.sort(reverse=True)
print('transitions',len(res))
for r in res[:31]:
    print('gap_ms=%d obj=%s frames %d..%d (%d) rows_between=%s detours_in_gap=%d orders=%s'%(r[0],r[1],r[2],r[3],r[4],dict(r[5]),r[7],dict(r[6])))
