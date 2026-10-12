# Run155: the last 40 order-4 ship_trace rows before each traced ship's order-5 start at a gate (class 18):
# angle(fwd, point-pos), whether the module redirected (point != given), |pos-A| / r. Usage: python3 trace_crossing.py <ship_rows.txt>
import re,sys,math,statistics as S
from collections import defaultdict
rows=defaultdict(list)
for l in open(sys.argv[1],errors='replace'):
    if not l.startswith('ship_trace frame') or 'tgt_class=18' not in l: continue
    m=re.search(r'frame=(\d+) t_ms=(\d+) obj=(\S+) class=(\S+) order=(\S+) .*pos=(\S+) fwd=(\S+) .*point=(\S+) given=(\S+) e=(\S+)',l)
    rows[m.group(3)].append(m.groups())
V=lambda s:[float(x) for x in s.split(',')]
def ang(u,v):
    n=math.sqrt(sum(a*a for a in u)*sum(b*b for b in v)); return math.degrees(math.acos(max(-1,min(1,sum(a*b for a,b in zip(u,v))/n)))) if n else float('nan')
out=[];red=[];last=[]
for o,v in rows.items():
    for i in range(1,len(v)):
        if v[i][4]=='0x5' and v[i-1][4]=='0x4':
            seg=[r for r in v[max(0,i-40):i] if r[4]=='0x4']
            A=V(seg[-1][8])
            for r in seg:
                p,f,pt,g=V(r[5]),V(r[6]),V(r[7]),V(r[8])
                out.append(ang(f,[a-b for a,b in zip(pt,p)])); red.append(pt!=g)
            r=seg[-1]; p,f,pt,g=V(r[5]),V(r[6]),V(r[7]),V(r[8])
            last.append((o,r[3],r[0],round(ang(f,[a-b for a,b in zip(pt,p)]),1),pt!=g,round(math.dist(p,A)/500/1105,2),round(ang(f,[a-b for a,b in zip(A,p)]),1)))
print('transitions in trace',len(last),'| last-40 order-4 rows',len(out),'redirected',sum(red),'angle(fwd,point-pos) med %.1f p75 %.1f max %.1f'%(S.median(out),sorted(out)[3*len(out)//4],max(out)))
print('last order-4 row per transition: obj class frame ang(fwd,point) redirected |pos-A|/r ang(fwd,A)')
for x in last: print(' ',*x)
