# Run155: per traced gate transition, the gap between the last order-4 (gate) ship_trace row and the first order-5 row.
# Usage: python3 trace_gap.py <ship_rows.txt>   (the grep of ^ship_ rows from x3m.log)
import re,sys,math,statistics as S
from collections import defaultdict
rows=defaultdict(list)
for l in open(sys.argv[1],errors='replace'):
    if l.startswith('ship_trace frame'):
        d=dict(re.findall(r'(\w+)=(\S+)',l)); rows[d['obj']].append(d)
res=[]
for o,v in rows.items():
    for i in range(1,len(v)):
        b=v[i]
        if b['order']=='0x5' and b['tgt_class']=='18' and v[i-1]['order']!='0x5':
            j=i-1
            while j>=0 and not (v[j]['order']=='0x4' and v[j]['tgt_class']=='18'): j-=1
            if j<0: continue
            a=v[j];pa=[int(x) for x in a['pos'].split(',')];pb=[int(x) for x in b['pos'].split(',')];A=[int(x) for x in a['given'].split(',')]
            res.append((int(b['t_ms'])-int(a['t_ms']),math.dist(pa,A)/552500,math.dist(pb,A)/552500,int(b['frame'])-int(a['frame'])))
g=sorted(r[0] for r in res)
print('traced transitions',len(res),'| game-ms gap last o4 row -> first o5 row: min %d med %d max %d, >1000 ms: %d, >5000 ms: %d'%(g[0],S.median(g),g[-1],sum(x>1000 for x in g),sum(x>5000 for x in g)))
print('|pos-A|/r at the last o4 row: med %.2f min %.2f max %.2f | at the first o5 row: med %.2f'%(S.median(r[1] for r in res),min(r[1] for r in res),max(r[1] for r in res),S.median(r[2] for r in res)))
