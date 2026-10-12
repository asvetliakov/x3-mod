#!/bin/sh
# Run155 triage one-liners. Usage: sh rows.sh [/mnt/c/X3/x3m.log] [/mnt/c/X3/x3m.prev.log] > rows_out.txt
# Never reads a log whole into memory: one grep per log extracts the ship_*/frame_end rows to temp files.
L=${1:-/mnt/c/X3/x3m.log}; PREV=${2:-/mnt/c/X3/x3m.prev.log}
T=$(mktemp); P=$(mktemp); trap 'rm -f "$T" "$P"' EXIT
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end )' "$L" > "$T"
LC_ALL=C grep -aE '^(ship_|log_open|session_end|frame_end )' "$PREV" > "$P"
echo "== 1 identity"
grep -a '^proxy_identity' "$T" | grep -oE 'sha256=[0-9a-f]+ bytes=[0-9]+|source_commit=[0-9a-f]+'
grep -a '^log_open' "$T"; grep -a '^session_end' "$T"
echo "crash files newer than the previous log (none listed = none):"; find /mnt/c/X3 -maxdepth 2 -iname '*crash*' -newer "$PREV" 2>/dev/null
grep -aE '^(ship_steering |ship_steering_site|ship_avoidance |ship_steering_gates|ship_steering_gate_site|ship_steering_refused|ship_steering_gates_refused|ship_trace installed|ship_trace_refused)' "$T" | cut -c1-120
python3 - "$T" "$P" <<'PY'
import re,sys,math,bisect,statistics as St
from collections import Counter,defaultdict
def load(f,trace=True):
    D=defaultdict(list);FE={};FT={}
    for l in open(f,errors='replace'):
        if l.startswith('frame_end '):
            m=re.search(r' frame=(\d+) .*elapsed_ms=(\d+)',l)
            if m: FE[int(m.group(1))]=int(m.group(2))
            continue
        if l.startswith('ship_trace frame='):
            m=re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) class=(\S+) order=(\S+) tgt=(\S+) tgt_class=(\d+) .*given=(\S+)',l)
            FT.setdefault(int(m.group(1)),int(m.group(2)))
            if m.group(7)=='18': D['trgate'].append(m.groups())
            continue
        name=l.split(' ',1)[0]
        if name in('ship_steering_summary','ship_gate_transition','ship_gate_approach','ship_steering_decision','ship_avoid_detour','ship_steering_stuck'):
            D[name].append(dict(re.findall(r'(\w+)=(\S+)',l)))
    return D,FE,FT
def q(v,p=1):
    v=sorted(v); f='n=%d min=%.{0}f p25=%.{0}f med=%.{0}f p75=%.{0}f max=%.{0}f'.format(p)
    return f%(len(v),v[0],v[len(v)//4],St.median(v),v[3*len(v)//4],v[-1]) if v else 'n=0'
V=lambda s:[float(x) for x in s.split(',')]
def sub(a,b): return [x-y for x,y in zip(a,b)]
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def nrm(a): return math.sqrt(dot(a,a))
def ang(u,v): return math.degrees(math.acos(max(-1,min(1,dot(u,v)/nrm(u)/nrm(v)))))
D,FE,FT=load(sys.argv[1]); D4,FE4,_=load(sys.argv[2])
summ=D['ship_steering_summary']
print('== 1 summary health over %d intervals'%len(summ))
s=Counter()
for d in summ:
    for k,v in d.items():
        if k.startswith(('gate_','unresolved','pass_','conflict','emergency','detours','clamp_','snapshot_overflow','e_','redirected','avoid_','ship_trace')):
            try: s[k]+=float(v)
            except ValueError: pass
print(' '.join('%s=%d'%(k,s[k]) for k in sorted(s) if s[k]))
print('stuck_total(last)',summ[-1]['stuck_total'],'stuck rows',len(D['ship_steering_stuck']),'| handled_by_order_total(last)',summ[-1]['handled_by_order_total'])
p99=[float(d['handler_us_p99']) for d in summ];mx=[float(d['handler_us_max']) for d in summ]
print('handler_us_p50 med',St.median(float(d['handler_us_p50']) for d in summ),'p99 med',St.median(p99),'p99 max',max(p99),'p99>5:',sum(x>5 for x in p99),'max_us',max(mx),'max>50:',sum(x>50 for x in mx),'calls_per_ship_s med',St.median(float(d['calls_per_ship_s']) for d in summ))
print('ship_trace rows printed',sum(1 for _ in FT) and len(FT),'frames; ship_trace_rows sum',int(s['ship_trace_rows']),'dropped',int(s['ship_trace_dropped']))
fs=sorted(FT)
def gt(f): i=min(bisect.bisect_left(fs,f),len(fs)-1); return FT[fs[i]]/1000.0
print('game/real time ratio (SETA): game %.0f s over real %.0f s'%((FT[fs[-1]]-FT[fs[0]])/1000,(FE[fs[-1]]-FE[fs[0]])/1000))
# gates: pass-point circle fit per gate target (order-5 'given' = B), A = order-4 given
Bs=defaultdict(set);As=defaultdict(Counter)
for fr,t,o,c,od,tg,tc,gv in D['trgate']:
    p=tuple(int(x)/500.0 for x in gv.split(','))
    if od=='0x5': Bs[tg].add(p)
    elif od=='0x4': As[tg][p]+=1
import numpy as np
GATES={}
for g,S in Bs.items():
    Pm=np.array(sorted(S)); c=Pm.mean(0); _,sv,vt=np.linalg.svd(Pm-c); e1,e2=vt[0],vt[1]; qq=np.c_[(Pm-c)@e1,(Pm-c)@e2]
    sol=np.linalg.lstsq(np.c_[2*qq,np.ones(len(qq))],(qq**2).sum(1),rcond=None)[0]; cc=c+sol[0]*e1+sol[1]*e2; rad=math.sqrt(sol[2]+sol[0]**2+sol[1]**2)
    A=np.array(As[g].most_common(1)[0][0]); GATES[g]=cc
    print('gate %s centre_m %s arc %.0f m (r_gate %.0f) A_m %s front=%s'%(g,np.round(cc),rad,rad/0.55,np.round(A),np.round((A-cc)/np.linalg.norm(A-cc),2)))
print('== 2 ship_gate_transition',len(D['ship_gate_transition']))
for tag,DD in (('Run155',D),('Run154',D4)):
    tr=DD['ship_gate_transition'];ap=DD['ship_gate_approach']
    print(tag,'transitions',len(tr),'class',dict(Counter(t['class'] for t in tr)),'capital',dict(Counter(t['capital'] for t in tr)),'validated',dict(Counter(t['validated'] for t in tr)),'dock_blocked',dict(Counter(t['dock_blocked'] for t in tr)))
    print('  err_B_deg',q([float(t['err_B_deg']) for t in tr]),'>18.2:',sum(float(t['err_B_deg'])>18.2 for t in tr),'>45.6:',sum(float(t['err_B_deg'])>45.6 for t in tr))
    print('  v_ms at o5 start',q([float(t['v_ms']) for t in tr]))
    print('  approach cross_heading_err_deg',q([float(x['cross_heading_err_deg']) for x in ap]),'| cross_v_ms',q([float(x['cross_v_ms']) for x in ap]),'| v0_ms',q([float(x['v0_ms']) for x in ap]))
    for c in sorted(set(x['class'] for x in ap)):
        y=[x for x in ap if x['class']==c]; print('    %s n=%d cross_to_launch_s %s'%(c,len(y),q([float(x['cross_to_launch_ms'])/1000 for x in y])))
tr=D['ship_gate_transition']
h=[0,0.2448,0.9696]
rows=[]
for t in tr:
    pp,pf,pc,op,of=V(t['p2_pos_r']),V(t['p2_fwd']),V(t['p2_carrot_r']),V(t['o5_pos_r']),V(t['o5_fwd']);B=V(t['B_gate_r'])
    rows.append(dict(fh=ang(pf,h),fc=ang(pf,sub(pc,pp)),ch=ang(sub(pc,pp),h),fB=ang(of,sub(B,op)),lat=math.hypot(pp[0],pp[1]-(-0.8+0.2448/0.9696*(pp[2]+1))),z=pp[2],dA=nrm(sub(pp,[0,-0.8,-1])),carz=pc[2]-pp[2],ms=float(t['ms_since_p2'])))
for k,lab in (('fh','angle(p2_fwd, h)'),('fc','angle(p2_fwd, carrot-pos): servo lag'),('ch','angle(carrot-pos, h): carrot geometry'),('fB','angle(o5_fwd, B-o5_pos) (= err_B check)'),('dA','|p2_pos - A| / r'),('z','p2_pos z / r (A at -1.0)'),('lat','p2 off the lead line in y (r, x ignored)'),('carz','carrot ahead in z (r)'),('ms','ms_since_p2')):
    print('  %-42s %s'%(lab,q([r[k] for r in rows],2)))
print('  gate_dock_writes %d gate_dock_invalid %d gate_dock_blocked %d'%(s['gate_dock_writes'],s['gate_dock_invalid'],s['gate_dock_blocked']))
print('== 3 capitals')
def capitals(DD,FEx,tag,f0):
    det=DD['ship_avoid_detour'];pc={}
    for d in det: pc.setdefault((d['obj'],d['obstacle']),set()).add(float(d['C_m']))
    mc=defaultdict(list)
    for d in det: mc[d['obj']].append(float(d['C_m']))
    big=[o for o,v in mc.items() if min(v)>1400 and Counter(x['kind'] for x in det if x['obj']==o)['0']>100]
    print(tag,'subjects with minC>1400 m and >100 ship detours:',big)
    for a in big:
        for b in big:
            if (a,b) in pc: print('   C(%s vs %s)=%.1f'%(a,b,min(pc[(a,b)])))
    return big,det
def solve(C_ab,C_ba,C_ac):
    pass
G2=GATES['0x902e2c90']
for tag,DD,FEx,f0 in (('Run155',D,FE,16000),('Run154',D4,FE4,0)):
    big,det=capitals(DD,FEx,tag,f0)
    # radii (C = rA + rB + max(25, rA/2)), leader L = the order-4 one, wingmen W1, W2
    L_=[o for o in big if all(x['order']=='0x4' for x in det if x['obj']==o)][0]; W=[o for o in big if o!=L_]
    cLW1=min(next(pc for pc in [[float(x['C_m']) for x in det if x['obj']==L_ and x['obstacle']==W[0]]] ) or [0])
    cW1W2=min([float(x['C_m']) for x in det if x['obj']==W[0] and x['obstacle']==W[1]]); cW2W1=min([float(x['C_m']) for x in det if x['obj']==W[1] and x['obstacle']==W[0]])
    # shared small obstacle of L and W1 with equal C -> rL == rW1
    rL=cLW1/2.5; rW1=rL; rW2=rW1-2*(cW1W2-cW2W1)
    R={L_:rL,W[0]:rW1,W[1]:rW2}
    print('  radii (inferred from C pairs) m:',{k:round(v) for k,v in R.items()},'leader',L_)
    for o in [L_]+W:
        v=[x for x in det if x['obj']==o]
        rel=[np.array(V(x['x_m']))-G2 for x in v]; dist=[np.linalg.norm(p) for p in rel]
        near=[(x,p) for x,p in zip(v,rel) if np.linalg.norm(p)<12000]
        nr=[x for x,p in near]
        robs=[float(x['C_m'])-1.5*R[o] for x in nr]
        print('  %s r=%.0f rows %d orders %s | within 12 km of gate 0x902e2c90: rows %d frames %s-%s real %.0f-%.0f s'%(o,R[o],len(v),dict(Counter(x['order'] for x in v)),len(nr),nr[0]['frame'] if nr else '-',nr[-1]['frame'] if nr else '-',FEx[int(nr[0]['frame'])]/1e3 if nr else 0,FEx[int(nr[-1]['frame'])]/1e3 if nr else 0))
        if not nr: continue
        print('     dist to gate m',q([np.linalg.norm(p) for x,p in near],0),'| depth (+front) m',q([p[2] for x,p in near],0),'behind plane:',sum(p[2]<0 for x,p in near))
        depth=2*(1105+R[o]); grid=[np.array([sx*666*a,-666*b,depth]) for a in (0,1,-1,2,-2) for b in range(4) for sx in (1,-1)]
        gd=[min(np.linalg.norm(p-g) for g in grid) for x,p in near]
        print('     dist to nearest candidate grid point (depth %.0f m, both x signs) m'%depth,q(gd,0),'<222:',sum(d<222 for d in gd),'<666:',sum(d<666 for d in gd))
        print('     kinds',dict(Counter(x['kind'] for x in nr)),'vertical',dict(Counter(x['vertical'] for x in nr)),'vs formation capitals',sum(x['obstacle'] in R for x in nr),'vs r_obs<200 m (r_obs=C-1.5 r_own)',sum(1 for x,ro in zip(nr,robs) if ro<200 and x['obstacle'] not in R),'distinct',len({x['obstacle'] for x,ro in zip(nr,robs) if ro<200 and x['obstacle'] not in R}),'r_obs',q([ro for x,ro in zip(nr,robs) if x['obstacle'] not in R],0))
        print('     miss_m',q([float(x['miss_m']) for x in nr],0),'miss>C:',sum(float(x['miss_m'])>float(x['C_m']) for x in nr),'| t_cpa_s',q([float(x['t_cpa_s']) for x in nr]),'| |P-x| m',q([np.linalg.norm(np.array(V(x['P_m']))-np.array(V(x['x_m']))) for x in nr],0))
        sec={FEx[int(x['frame'])]//1000 for x in nr}; a0,a1=FEx[int(nr[0]['frame'])]//1000,FEx[int(nr[-1]['frame'])]//1000
        print('     real seconds with a detour row %d of %d in its near-gate window'%(len(sec),a1-a0+1))
for o in ('0x902bc4f0',):
    v=[x for x in D['ship_avoid_detour'] if x['obj']==o and int(x['frame'])>=16000]
    print('  leader 0x902bc4f0, 20-game-s bins near gate 0x902e2c90: game_s frames rows | mean rel x,y,depth m | apparent m/s (game) | top obstacle')
    b={}
    for x in v: b.setdefault(int(gt(int(x['frame']))//20),[]).append(x)
    for k in sorted(b):
        w=b[k]; ps=[np.array(V(x['x_m']))-G2 for x in w]; m=np.mean(ps,0); t0,t1=gt(int(w[0]['frame'])),gt(int(w[-1]['frame']))
        sp='%.0f'%(np.linalg.norm(ps[-1]-ps[0])/(t1-t0)) if t1-t0>1 else '-'
        print('   %d %s-%s %d | %.0f,%.0f,%.0f | %s | %s'%(k*20,w[0]['frame'],w[-1]['frame'],len(w),m[0],m[1],m[2],sp,Counter(x['obstacle'] for x in w).most_common(1)))
    st=[x for x in v if 16571<=int(x['frame'])<=17541]
    print('  stall window frames 16571-17541: game %.1f-%.1f s, rows %d, obstacle %s, C %s, miss %s, t_cpa %s, P-x %s'%(gt(16571),gt(17541),len(st),dict(Counter(x['obstacle'] for x in st)),sorted({x['C_m'] for x in st}),q([float(x['miss_m']) for x in st],0),q([float(x['t_cpa_s']) for x in st]),
          q([np.linalg.norm(np.array(V(x['P_m']))-np.array(V(x['x_m']))) for x in st],0)))
    ob=[x for x in D['ship_avoid_detour'] if x['obstacle']=='0x9030de88']
    print('  obstacle 0x9030de88: rows %d, C values %s (r_obs ~ %.0f m), as subject rows %d'%(len(ob),sorted({x['C_m'] for x in ob}),1645.3-1.5*1091.5,sum(x['obj']=='0x9030de88' for x in D['ship_avoid_detour'])))
print('  decision rows by class',dict(Counter(d['class'] for d in D['ship_steering_decision'])),'| ship_trace classes',dict(Counter(c for _,_,_,c,*r in D['trgate'])))
pn=[(d['frame'],d['pass_order_not_steered']) for d in summ if d['pass_order_not_steered']!='0']
print('  pass_order_not_steered intervals',len(pn),'sum',sum(int(x) for _,x in pn),'max per interval',max(int(x) for _,x in pn) if pn else 0)
print('  emergency per interval: nonzero',sum(d['emergency']!='0' for d in summ),'max',max(int(d['emergency']) for d in summ))
PY
D=$(dirname "$0")
echo "== 2b trace: lead-in carrot liveness in the last 40 order-4 rows (trace_crossing.py)"; python3 "$D/trace_crossing.py" "$T" | head -2
echo "== 2c trace: gap before order 5 (trace_gap.py)"; python3 "$D/trace_gap.py" "$T"
