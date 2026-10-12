#!/bin/sh
# Run152 triage one-liners. Usage: sh rows.sh [/mnt/c/X3/x3m.log] > rows_out.txt
L=${1:-/mnt/c/X3/x3m.log}
echo "== 1 identity"; grep -a '^proxy_identity' "$L" | grep -oE 'sha256=[0-9a-f]+ |source_commit=[0-9a-f]+'; grep -a '^log_open' "$L"; grep -a '^session_end' "$L"
ls -la --time-style=full-iso /mnt/c/X3/X3Crash.* 2>/dev/null
echo "== 2 gate module"; grep -aE '^(ship_steering_gates|ship_steering_gate_site|ship_steering_gates_refused)' "$L" | cut -c1-200
echo "== 3,5,6,7 summary sums (per-interval counters)"
python3 - "$L" <<'P'
import re,sys,statistics as S
from collections import Counter
sums=Counter();p50=[];p99=[];mx=[];cps=[];nz=set();ga=[];dec=[];kinds=Counter();gk=Counter()
for l in open(sys.argv[1],'rb'):
    if l.startswith(b'ship_steering_summary'):
        d=dict(re.findall(r'(\w+)=(\S+)',l.decode()))
        for k,v in d.items():
            if (k.startswith(('gate_','unresolved','pass_','conflict','emergency','detours','clamp_','stuck','e_'))) and v.replace('.','').isdigit(): sums[k]+=float(v)
        p50.append(float(d['handler_us_p50']));p99.append(float(d['handler_us_p99']));mx.append(float(d['handler_us_max']));cps.append(float(d['calls_per_ship_s']))
    elif l.startswith(b'ship_gate_approach'): ga.append(dict(re.findall(r'(\w+)=(\S+)',l.decode())))
    elif l.startswith(b'ship_steering_decision'): dec.append(dict(re.findall(r'(\w+)=(\S+)',l.decode())))
    elif l.startswith(b'ship_avoid_detour'):
        d=dict(re.findall(r'(\w+)=(\S+)',l.decode())); kinds[d['kind']]+=1
        if d['kind']=='3': gk[d['order']]+=1
print('summaries',len(p50)); [print(k,int(v)) for k,v in sorted(sums.items()) if v]
print('p50 median',S.median(p50),'max',max(p50),'p99 max',max(p99),'max_us',max(mx),'cps median',S.median(cps),'max',max(cps))
print('detour kinds (0 ship,1 station,2 asteroid,3 gate)',dict(kinds),'gate-kind by order',dict(gk))
print('== 4 ship_gate_approach n',len(ga),Counter(g['class'] for g in ga))
print('cross_v_ms values',Counter(g['cross_v_ms'] for g in ga),'cross_to_launch_ms',Counter(g['cross_to_launch_ms'] for g in ga))
v=sorted(float(g['v0_ms']) for g in ga); print('v0_ms min/median/max',v[0],S.median(v),v[-1],'below20',sum(x<20 for x in v))
print('== 5 decision orders',Counter(d['order'] for d in dec))
for d in dec:
    if d['order'] in('0x4','0x5'):
        t=float(d['theta_deg']);vv=float(d['v_ms'])
        if t>18.2 or vv<20: print('slow/misaligned',d['frame'],d['obj'],d['order'],d['class'],'theta',t,'d_m',d['d_m'],'v',vv,'v_cmd',d['v_cmd_ms'],d['flags'])
print('order4 TS-class with d_arr 0 (no ramp):',sum(1 for d in dec if d['order']=='0x4' and d['d_arr_m']=='0.0'),'of',sum(1 for d in dec if d['order']=='0x4'))
P
echo "== stuck rows"; grep -ac '^ship_steering_stuck' "$L"
