#!/bin/sh
# Run154 triage one-liners. Usage: sh rows.sh [/mnt/c/X3/x3m.log] [/mnt/c/X3/x3m.prev.log] > rows_out.txt
# Never reads the log whole into memory: one grep extracts the ship_*/frame_end rows to a temp file.
L=${1:-/mnt/c/X3/x3m.log}; PREV=${2:-/mnt/c/X3/x3m.prev.log}
T=$(mktemp); trap 'rm -f "$T"' EXIT
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end )' "$L" > "$T"
echo "== 1 identity"
grep -a '^proxy_identity' "$T" | grep -oE 'sha256=[0-9a-f]+ bytes=[0-9]+|source_commit=[0-9a-f]+'
grep -a '^log_open' "$T"; grep -a '^session_end' "$T"
echo "crash files newer than the log start (none listed = none):"; find /mnt/c/X3 -maxdepth 2 -iname '*crash*' -newer "$PREV" 2>/dev/null
ls -la --time-style=full-iso /mnt/c/X3/X3Crash.* 2>/dev/null
echo "== 2 module and gate site rows"
grep -aE '^(ship_steering |ship_steering_site|ship_avoidance |ship_steering_gates|ship_steering_gate_site|ship_steering_refused|ship_steering_gates_refused)' "$T" | cut -c1-170
python3 - "$T" <<'P'
import re,sys,math,bisect,statistics as St
from collections import Counter,defaultdict
f=sys.argv[1]
summ=[];tr=[];ap=defaultdict(list);dec=[];det=[];stuck=[];T={}
for l in open(f,errors='replace'):
    if l.startswith('frame_end '):
        m=re.search(r' frame=(\d+) .*elapsed_ms=(\d+)',l)
        if m: T[int(m.group(1))]=int(m.group(2))
        continue
    d=dict(re.findall(r'(\w+)=(\S+)',l))
    if l.startswith('ship_steering_summary'): summ.append(d)
    elif l.startswith('ship_gate_transition'): tr.append(d)
    elif l.startswith('ship_gate_approach'): ap[d['obj']].append(d)
    elif l.startswith('ship_steering_decision'): dec.append(d)
    elif l.startswith('ship_avoid_detour'): det.append(d)
    elif l.startswith('ship_steering_stuck'): stuck.append(d)
def q(v,p=1):
    v=sorted(v); f='n=%d min=%.{0}f p25=%.{0}f med=%.{0}f p75=%.{0}f max=%.{0}f'.format(p)
    return f%(len(v),v[0],v[len(v)//4],St.median(v),v[3*len(v)//4],v[-1]) if v else 'n=0'
print('== 3 summary counters summed over %d intervals (cumulative *_total: last value)'%len(summ))
s=Counter()
for d in summ:
    for k,v in d.items():
        if k.startswith(('gate_','unresolved','pass_','conflict','emergency','detours','clamp_','snapshot_overflow','e_','part_','redirected','avoid_calls')):
            try: s[k]+=float(v)
            except ValueError: pass
for k in sorted(s): print(k,int(s[k]))
print('stuck_total(last)',summ[-1]['stuck_total'],'handled_by_order_total(last)',summ[-1]['handled_by_order_total'])
print('gate_dock_writes by interval (nonzero):',[(d['frame'],d['gate_dock_writes']) for d in summ if d['gate_dock_writes']!='0'])
p99=[float(d['handler_us_p99']) for d in summ];mx=[float(d['handler_us_max']) for d in summ];cps=[float(d['calls_per_ship_s']) for d in summ]
print('handler_us_p50 median',St.median(float(d['handler_us_p50']) for d in summ),'p99 median',St.median(p99),'p99 max',max(p99),'p99>5',sum(x>5 for x in p99),'max_us',max(mx),'max>50',sum(x>50 for x in mx),'calls_per_ship_s median',St.median(cps),'max',max(cps))
print('== 4 ship_gate_transition',len(tr),'class',dict(Counter(t['class'] for t in tr)),'capital',dict(Counter(t['capital'] for t in tr)),'validated',dict(Counter(t['validated'] for t in tr)),'crossed',dict(Counter(t['crossed'] for t in tr)),'dock_blocked',dict(Counter(t['dock_blocked'] for t in tr)),'desired_ms',dict(Counter(t['desired_ms'] for t in tr)))
print('err_B_deg',q([float(t['err_B_deg']) for t in tr]),'| >18.2:',sum(float(t['err_B_deg'])>18.2 for t in tr),'>45.6:',sum(float(t['err_B_deg'])>45.6 for t in tr))
print('v_ms',q([float(t['v_ms']) for t in tr]),'| <20:',sum(float(t['v_ms'])<20 for t in tr))
print('last_dA_r',q([float(t['last_dA_r']) for t in tr],2))
for c in sorted(set(t['class'] for t in tr)):
    x=[t for t in tr if t['class']==c]; print('  %s err_B %s | v %s'%(c,q([float(t['err_B_deg']) for t in x]),q([float(t['v_ms']) for t in x])))
B=[list(map(float,t['B_gate_r'].split(','))) for t in tr]
print('A_gate_r distinct',dict(Counter(t['A_gate_r'] for t in tr)))
print('B_gate_r: |x|',q([abs(b[0]) for b in B],2),'| z values',dict(Counter(b[2] for b in B)))
print('B_gate_r: hypot(x,y)',q([math.hypot(b[0],b[1]) for b in B],3),'| hypot(y,z) (pinned rho)',q([math.hypot(b[1],b[2]) for b in B],3))
print('B passes the pinned check (|x|<=0.03 and |hypot(y,z)-0.55|<=0.03):',sum(abs(b[0])<=0.03 and abs(math.hypot(b[1],b[2])-0.55)<=0.03 for b in B),'of',len(B))
print('implied theta=atan2(|x|,-y) deg',q([math.degrees(math.atan2(abs(b[0]),-b[1])) for b in B]),'x>0',sum(b[0]>0 for b in B),'x<0',sum(b[0]<0 for b in B))
# geometric err_B: ship at the spread crossing point X + offset heading h, against the measured B
a=math.radians(23.6);h=(0,math.sin(a),math.cos(a));A=(0,-0.8,-1.0)
def ang(u,v):
    c=sum(x*y for x,y in zip(u,v))/math.hypot(*u)/math.hypot(*v); return math.degrees(math.acos(max(-1,min(1,c))))
ge=[];pairs=[];rs=set()
for t in tr:
    aa=[x for x in ap.get(t['obj'],[]) if int(x['frame'])>=int(t['frame'])]
    if not aa: continue
    ox,oy=map(float,aa[0]['offset_m'].split(','));r=math.hypot(ox,oy)/0.25;rs.add(round(r))
    X=[A[i]-0.75*h[i] for i in range(3)];X[0]+=ox/r;X[1]+=oy/r
    b=list(map(float,t['B_gate_r'].split(',')))
    ge.append(ang(h,[b[i]-X[i] for i in range(3)]));pairs.append((float(aa[0]['cross_heading_err_deg']),float(t['err_B_deg'])))
print('geometric err_B at the crossing point heading h (r_gate from offset_m: %s m):'%sorted(rs),q(ge))
al=[p for p in pairs if p[0]<=13.9]
print('transitions whose crossing heading was <=13.9 deg off h:',len(al),'their measured err_B',q([p[1] for p in al]))
A_=[x for v in ap.values() for x in v]
print('== ship_gate_approach',len(A_),'class',dict(Counter(x['class'] for x in A_)),'capital',dict(Counter(x['capital'] for x in A_)))
print('cross_heading_err_deg',q([float(x['cross_heading_err_deg']) for x in A_]),'| >13.9:',sum(float(x['cross_heading_err_deg'])>13.9 for x in A_))
print('cross_v_ms',q([float(x['cross_v_ms']) for x in A_])); print('v0_ms',q([float(x['v0_ms']) for x in A_]),'| <20:',sum(float(x['v0_ms'])<20 for x in A_))
for c in sorted(set(x['class'] for x in A_)):
    y=[x for x in A_ if x['class']==c]; print('  %s cross_to_launch_s %s | v0 %s'%(c,q([float(x['cross_to_launch_ms'])/1000 for x in y]),q([float(x['v0_ms']) for x in y])))
print('== 4 decision samples: orders',dict(Counter(d['order'] for d in dec)),'classes',dict(Counter(d['class'] for d in dec)))
for d in dec:
    if d['order'] in ('0x5','0x4'):
        print(' ',d['frame'],'t=%.1fs'%(T.get(int(d['frame']),0)/1000),d['obj'],d['order'],d['class'],'theta',d['theta_deg'],'v',d['v_ms'],'v_corner',d['v_corner_ms'],'d_m',d['d_m'],'d_arr',d['d_arr_m'],'omega',d['omega_dps'],'omega_cmd',d['omega_cmd_dps'],'W',d['W_dps'],'a',d['a_dps2'],'f2b0',d['f2b0'],'rot_pct',d['rot_pct'],'P',d['P_m'])
print('== 5 capitals')
small={'TS','TP','M3','M4','M5','M6','M8'};known={}
for d in dec+tr: known[d['obj']]=d['class']
ob=defaultdict(list)
for d in det:
    if d['kind']=='0' and known.get(d['obj']) in small: ob[d['obstacle']].append(float(d['C_m']))
print('ship obstacles with C_m>800 against known small ships (C = r_own + r_obs + max(25, r_own/2)): r_obs ~ C - r_small - 25..50')
for k,v in sorted(ob.items(),key=lambda kv:-min(kv[1])):
    if min(v)>800: print('  ',k,'minC %.0f n %d'%(min(v),len(v)))
k3=Counter((d['kind'],d['order']) for d in det); print('detour rows by (kind,order):',sorted(k3.items()))
G2=(-7200,10200,-57600)  # gate 0x8c8d7890 estimated from the order-5 B samples / kind-3 detours (+-1 km)
ends=[int(d['frame']) for d in summ]
for o in ('0x8c4d3ee0','0x8c8d1ab8','0x8c4db038'):
    v=[d for d in det if d['obj']==o]
    xs=[tuple(map(float,d['x_m'].split(','))) for d in v]
    late=[(int(d['frame']),math.dist(x,G2)) for d,x in zip(v,xs) if int(d['frame'])>=22740]
    idx=sorted(set(bisect.bisect_left(ends,int(d['frame'])) for d in v)); lc=[int(summ[i]['gate_lead_calls']) for i in idx if i<len(summ)]
    print(o,'rows',len(v),'frames %s-%s'%(v[0]['frame'],v[-1]['frame']),'orders',dict(Counter(d['order'] for d in v)),'kinds',dict(Counter(d['kind'] for d in v)),
          'top obstacles',Counter(d['obstacle'] for d in v).most_common(4),'| dist to G2 from frame 22740: %.0f-%.0f m'%(min(x[1] for x in late),max(x[1] for x in late)) if late else '',
          '| gate_lead_calls in its intervals min %d med %d'%(min(lc),sorted(lc)[len(lc)//2]))
    bk=defaultdict(list)
    for d,x in zip(v,xs): bk[int(d['frame'])//3000].append((int(d['frame']),x))
    tr_=[]
    for b,w in sorted(bk.items()):
        (fa,xa),(fz,xz)=w[0],w[-1];dt=(T.get(fz,0)-T.get(fa,0))/1000
        tr_.append('%d:%.0fkm/%s'%(fa,math.dist(xa,G2)/1000,'%.0f'%(math.dist(xa,xz)/dt) if dt>0.5 else '-'))
    print('   frame:dist_G2/apparent_m_per_real_s',' '.join(tr_))
esc=Counter(d['obj'] for d in det if d['order']=='0x14' and math.dist(tuple(map(float,d['x_m'].split(','))),G2)<12000)
print('order-0x14 ships within 12 km of G2:',len(esc))
print('== SETA check: known TS 0x8c901498 detour displacement / real time (its [obj+0x10] at its transition: 158.6 m/s)')
v=[d for d in det if d['obj']=='0x8c901498' and int(d['frame'])>=33007]
if v:
    fa,fz=int(v[0]['frame']),int(v[-1]['frame']);xa=tuple(map(float,v[0]['x_m'].split(',')));xz=tuple(map(float,v[-1]['x_m'].split(',')))
    print('  frames %d-%d real %.2f s moved %.0f m -> %.0f m/s'%(fa,fz,(T[fz]-T[fa])/1000,math.dist(xa,xz),math.dist(xa,xz)/((T[fz]-T[fa])/1000)))
print('== 6 avoidance by kind',dict(Counter(d['kind'] for d in det)),'gate-kind by order',dict(Counter(d['order'] for d in det if d['kind']=='3')))
print('stuck rows',len(stuck)); [print('  ',d['frame'],d['obj'],d['order'],d['d_m'],d['best_d_m']) for d in stuck]
P
echo "== 6 Run152 (previous log) handler p99"
LC_ALL=C grep -a '^log_open' "$PREV" | head -1
LC_ALL=C grep -a '^ship_steering_summary' "$PREV" | python3 -c "
import re,sys,statistics as S
p99=[];mx=[]
for l in sys.stdin:
    d=dict(re.findall(r'(\w+)=(\S+)',l)); p99.append(float(d['handler_us_p99']));mx.append(float(d['handler_us_max']))
print('summaries',len(p99),'p99 median',S.median(p99),'p99>5',sum(x>5 for x in p99),'max>50',sum(x>50 for x in mx))"
