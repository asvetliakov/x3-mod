# Run160b: enumerate capitals from every row kind (trace class, join rows, gate_approach capital=1, E rows B_code, warn rows code,
# lane rows A/B). Usage: python3 -I caps.py rows.txt
import sys,re,collections as C
CAP={'M1','M2','TL','M7','M0'}
src=C.defaultdict(C.Counter); cls={}; fr=C.defaultdict(lambda:[10**9,0])
def see(o,k,c,f):
    src[o][k]+=1
    if c in CAP: cls[o]=c
    a=fr[o]; a[0]=min(a[0],f); a[1]=max(a[1],f)
for l in open(sys.argv[1],errors='replace'):
    if l.startswith('ship_trace frame='):
        m=re.match(r'ship_trace frame=(\d+) t_ms=\d+ obj=(\S+) class=(\S+)',l)
        if m and m[3] in CAP: see(m[2],'trace',m[3],int(m[1]))
    elif l.startswith(('ship_gate_capital_join ','ship_gate_capital_latch ')):
        d=dict(re.findall(r'(\w+)=(\S+)',l)); see(d['obj'],'join',d['class'],int(d['frame']))
    elif l.startswith('ship_gate_approach ') and ' capital=1 ' in l:
        d=dict(re.findall(r'(\w+)=(\S+)',l)); see(d['obj'],'approach',d['class'],int(d['frame']))
    elif l.startswith('ship_gate_transition ') and ' capital=1 ' in l:
        d=dict(re.findall(r'(\w+)=(\S+)',l)); see(d['obj'],'transition',d['class'],int(d['frame']))
    elif l.startswith('ship_avoid_e_verdict '):
        d=dict(re.findall(r'(\w+)=(\S+)',l))
        if d['B_code'] in CAP: see(d['B'],'E_B',d['B_code'],int(d['frame']))
    elif l.startswith('ship_capital_lane '):
        d=dict(re.findall(r'(\w+)=(\S+)',l)); see(d['A'],'lane_A','',int(d['frame'])); see(d['B'],'lane_B','',int(d['frame']))
    elif l.startswith('ship_avoid_engine_warn '):
        d=dict(re.findall(r'(\w+)=(\S+)',l))
        if d.get('code') in CAP: see(d['other'],'warn_other',d['code'],int(d['frame']))
for o in sorted(src,key=lambda o:fr[o][0]): print(o,cls.get(o,'?'),'frames %d-%d'%tuple(fr[o]),dict(src[o]))
