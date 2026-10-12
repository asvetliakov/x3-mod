#!/bin/sh
# Run157 triage one-liners. Usage: sh rows.sh [/mnt/c/X3/x3m.log] > rows_out.txt
# One grep extracts the ship_*/frame_end/identity rows to a temp file; the log is never read whole into memory.
# Compared with Run156 (../run156-triage/rows_out.txt). e_suppressed_small / e_small_pass* are totals since install
# (the summary prints them cumulative); every other counter is per interval and summed.
L=${1:-/mnt/c/X3/x3m.log}; D=$(cd "$(dirname "$0")" && pwd); R156="$D/../run156-triage/rows_out.txt"
T=$(mktemp); trap 'rm -f "$T"' EXIT
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end )' "$L" > "$T"
echo "== 1 identity"
grep -a '^proxy_identity' "$T" | grep -oE 'sha256=[0-9a-f]+ bytes=[0-9]+|source_commit=[0-9a-f]+'
grep -a '^log_open' "$T"; grep -a '^session_end' "$T"
echo "exception rows in the whole log: $(LC_ALL=C grep -acE 'exception=[1-9]|^exception|unhandled' "$L")"
echo "crash files newer than x3m.prev.log (none listed = none):"; find /mnt/c/X3 -maxdepth 2 -iname '*crash*' -newer /mnt/c/X3/x3m.prev.log 2>/dev/null
grep -aE '^(ship_steering |ship_steering_site|ship_avoidance |ship_steering_gates|ship_steering_gate_site|ship_trace installed|ship_avoid_census)' "$T" | cut -c1-140
echo "ship_* refused rows: $(grep -ac 'refused' "$T")"
echo "seta field: $(LC_ALL=C grep -a '^engine_stage .*seta=' "$L" | grep -oE 'seta=[01]' | sort | uniq -c | tr '\n' ' ')"
python3 -I - "$T" "$R156" <<'PY'
import re,sys,statistics as St
from collections import Counter,defaultdict
D=defaultdict(list);FE={};FT={}
for l in open(sys.argv[1],errors='replace'):
    if l.startswith('frame_end '):
        m=re.search(r' frame=(\d+) .*elapsed_ms=(\d+)',l)
        if m: FE[int(m.group(1))]=int(m.group(2))
        continue
    if l.startswith('ship_trace frame='):
        m=re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) class=(\S+) order=(\S+) tgt=(\S+)',l)
        FT.setdefault(int(m.group(1)),int(m.group(2))); D['tr'].append(m.groups()); continue
    n=l.split(' ',1)[0]
    if n in('ship_steering_summary','ship_gate_transition','ship_gate_approach','ship_avoid_detour','ship_steering_stuck','ship_avoid_engine_warn'):
        D[n].append(dict(re.findall(r'(\w+)=(\S+)',l)))
def q(v,p=1):
    v=sorted(v); f='n=%d min=%.{0}f p25=%.{0}f med=%.{0}f p75=%.{0}f max=%.{0}f'.format(p)
    return f%(len(v),v[0],v[len(v)//4],St.median(v),v[3*len(v)//4],v[-1]) if v else 'n=0'
summ=D['ship_steering_summary'];s=Counter()
CUM=('e_suppressed_small','e_small_pass','e_small_pass_no_autopilot','e_small_pass_order','e_small_pass_class','e_small_pass_not_steerable','e_small_pass_stale')
for d in summ:
    for k,v in d.items():
        try:
            if k not in('frame','interval_ms','claims','ships','stuck_total')+CUM and not k.startswith(('handler_us','calls_per')): s[k]+=float(v)
        except ValueError: pass
for k in CUM: s[k]=float(summ[-1].get(k,0))
fs=sorted(FT); g=(FT[fs[-1]]-FT[fs[0]])/1e3; r=(FE[fs[-1]]-FE[fs[0]])/1e3
print('SETA: trace game %.0f s over real %.0f s, ratio %.2f'%(g,r,g/r))
print('== 1 summary health over %d intervals (interval_ms sum %.0f)'%(len(summ),sum(float(d['interval_ms']) for d in summ)))
r156=dict(re.findall(r'^(\w+) +(\d+) +\S+$',open(sys.argv[2]).read().split('summary health')[1].split('stuck_total')[0],re.M))
keys=['conflict_episodes','unresolved_episodes','unresolved','unresolved_inside_stop','unresolved_inside_turn','unresolved_behind','detours','emergency','avoid_calls','avoid_skipped_small','avoid_excluded_followers','e_calls','e_suppressed','e_suppressed_small','e_small_pass','e_small_pass_no_autopilot','e_small_pass_order','e_small_pass_class','e_small_pass_not_steerable','e_small_pass_stale','gate_crossings','gate_p2','gate_p4','gate_p5','gate_p2_capital','gate_p4_capital','gate_capital_aligned','gate_capital_stop_kept','gate_capital_lead_validated','gate_capital_lead_fallback','gate_lead_validated','gate_lead_fallback','gate_lead_window_rejected','gate_speed_writes','gate_dock_writes','gate_dock_blocked','gate_dock_invalid','gate_rows_dropped','snapshot_overflow','pass_order_not_steered','pass_table_full','pass_not_finite','ship_trace_rows','ship_trace_dropped']
print('%-28s %10s %10s'%('counter','Run157','Run156'))
for k in keys: print('%-28s %10d %10s'%(k,s[k],r156.get(k,'-')))
print('stuck_total(last)',summ[-1]['stuck_total'],'stuck rows',len(D['ship_steering_stuck']),'| handled_by_order_total(last)',summ[-1]['handled_by_order_total'])
p99=[float(d['handler_us_p99']) for d in summ];mx=[float(d['handler_us_max']) for d in summ]
print('handler_us_p50 med',St.median(float(d['handler_us_p50']) for d in summ),'p99 med',St.median(p99),'p99 max',max(p99),'p99>5:',sum(x>5 for x in p99),'max_us',max(mx),'max>50:',sum(x>50 for x in mx))
# site E small-pair counters per interval, split by the traced capital's majority order in that interval
od={}
for fr,t,o,c,ordr,tg in D['tr']:
    if o=='0x8c30aad0': od[int(fr)]=ordr
prev=None;agg={}
for d in summ:
    f=int(d['frame']);cur=[int(d.get(k,0)) for k in ('e_suppressed_small','e_small_pass','e_small_pass_order','e_small_pass_stale')]
    if prev:
        fr=[od[x] for x in range(prev[0]+1,f+1) if x in od]
        k='o4' if fr and fr.count('0x4')>len(fr)/2 else 'o12' if fr and fr.count('0x12')>len(fr)/2 else 'other'
        ph='f<21000' if f<21000 else 'f>=21000'
        a=agg.setdefault((ph,k),[0,0,0,0,0]);a[0]+=1
        for i in range(4): a[i+1]+=cur[i]-prev[1][i]
    prev=(f,cur)
print('site E small pairs per interval by phase and the traced capital 0x8c30aad0 majority order: [intervals, d_suppressed_small, d_pass, d_pass_order, d_pass_stale]')
for k,v in sorted(agg.items()): print('  ',k,v)
print('== 3 ship_avoid_engine_warn rows',len(D['ship_avoid_engine_warn']),'subjects',len({w['obj'] for w in D['ship_avoid_engine_warn']}))
W=[w for w in D['ship_avoid_engine_warn'] if w['obj']=='0x8c30aad0']
print('  capital 0x8c30aad0: rows',len(W),'by obstacle code',dict(Counter(w['code'] for w in W)),'small (r < r_own/3 = 364 m)',sum(float(w['r_m'])<1091.5/3 for w in W))
for w in W: print('   frame %s other %s class %s code %s r_m %s d_m %s'%(w['frame'],w['other'],w['class'],w['code'],w['r_m'],w['d_m']))
det=D['ship_avoid_detour']
c=[d for d in det if d['obj']=='0x8c30aad0']
print('  capital 0x8c30aad0 as detour subject: rows',len(c),dict(Counter((d['obstacle'],d['kind'],d['leader'],d['obstacle_order']) for d in c)))
for o in ('0x8c3114d8','0x8c31d590','0x33645d50','0x33638ce8','0x3362c028','0x33642dc8'):
    print('  %s as obstacle (leader, obstacle_order): %s | as subject orders: %s'%(o,dict(Counter((d['leader'],d['obstacle_order']) for d in det if d['obstacle']==o)),dict(Counter(d['order'] for d in det if d['obj']==o))))
print('== 4 small ships: ship_gate_transition',len(D['ship_gate_transition']),'approach',len(D['ship_gate_approach']))
gt={}
for fr,t,o,c,ordr,tg in D['tr']:
    if ordr=='0x5': gt.setdefault(o,set()).add(tg)
G='0x8c322120'
tr=D['ship_gate_transition'];ap=D['ship_gate_approach']
print('class',dict(Counter(t['class'] for t in tr)),'capital',dict(Counter(t['capital'] for t in tr)),'validated',dict(Counter(t['validated'] for t in tr)),'dock_blocked',dict(Counter(t['dock_blocked'] for t in tr)))
for tag,sel in (('all',lambda o:True),('gate '+G+' (traced)',lambda o:G in gt.get(o,()))):
    t2=[t for t in tr if t['capital']=='0' and sel(t['obj'])];a2=[x for x in ap if x['capital']=='0' and sel(x['obj'])]
    print(' ',tag,'transitions',len(t2),'err_B_deg',q([float(t['err_B_deg']) for t in t2]),'>18.2:',sum(float(t['err_B_deg'])>18.2 for t in t2))
    print('   v_ms at o5 start',q([float(t['v_ms']) for t in t2]),'| cross_heading_err',q([float(x['cross_heading_err_deg']) for x in a2]),'| cross_v',q([float(x['cross_v_ms']) for x in a2]))
    for cl in sorted(set(x['class'] for x in a2)):
        y=[x for x in a2 if x['class']==cl]; print('    %s n=%d cross_to_launch_s %s'%(cl,len(y),q([float(x['cross_to_launch_ms'])/1000 for x in y])))
print('capital approach rows (obj class cross_v cross_err cross_to_launch_ms):',[(x['obj'],x['class'],x['cross_v_ms'],x['cross_heading_err_deg'],x['cross_to_launch_ms']) for x in ap if x['capital']=='1'])
caps={'0x8c30aad0','0x8c31d590','0x8c3114d8'}
v=[d for d in det if d['obstacle'] in caps and d['obj'] not in caps]
print('small-ship detours vs capital obstacles: rows',len(v),'subjects',len({d['obj'] for d in v}),'miss<C',sum(float(d['miss_m'])<float(d['C_m']) for d in v),'miss',q([float(d['miss_m']) for d in v],0),'C',sorted({d['C_m'] for d in v}))
for o in sorted({d['obj'] for d in v}):
    w=[d for d in v if d['obj']==o]; print('  ',o,'vs',dict(Counter(d['obstacle'] for d in w)),'miss<C %d min miss %.0f'%(sum(float(d['miss_m'])<float(d['C_m']) for d in w),min(float(d['miss_m']) for d in w)),'min t_cpa %.1f'%min(float(d['t_cpa_s']) for d in w),'orders',dict(Counter(d['order'] for d in w)))
cd=[d for d in det if d['obj'] in caps]
print('capital-as-subject detours',len(cd),'vs small (C<2000)',sum(float(d['C_m'])<2000 for d in cd))
print('per-interval nonzero: unresolved_inside_stop',sum(d.get('unresolved_inside_stop','0')!='0' for d in summ),'emergency',sum(d.get('emergency','0')!='0' for d in summ),'max emergency',max(int(d['emergency']) for d in summ))
PY
echo "== 2 capital timeline: capital_timeline.py --legs > capital_timeline.txt"
python3 -I "$D/capital_timeline.py" "$T" --legs > "$D/capital_timeline.txt"; grep -E '^==|grid point|first row|total|0x1f at' "$D/capital_timeline.txt" | cut -c1-260
