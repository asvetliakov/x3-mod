#!/bin/sh
# Run156 triage one-liners. Usage: sh rows.sh [/mnt/c/X3/x3m.log] > rows_out.txt
# One grep extracts the ship_*/frame_end/identity rows to a temp file; the log is never read whole into memory.
L=${1:-/mnt/c/X3/x3m.log}; D=$(cd "$(dirname "$0")" && pwd); R155="$D/../run155-triage/rows_out.txt"
T=$(mktemp); trap 'rm -f "$T"' EXIT
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end )' "$L" > "$T"
echo "== 1 identity"
grep -a '^proxy_identity' "$T" | grep -oE 'sha256=[0-9a-f]+ bytes=[0-9]+|source_commit=[0-9a-f]+'
grep -a '^log_open' "$T"; grep -a '^session_end' "$T"
echo "exception rows in the whole log: $(LC_ALL=C grep -acE 'exception=[1-9]|^exception|unhandled' "$L")"
echo "crash files newer than the log start (none listed = none):"; find /mnt/c/X3 -maxdepth 2 -iname '*crash*' -newer /mnt/c/X3/x3m.prev.log 2>/dev/null
grep -aE '^(ship_steering |ship_steering_site|ship_avoidance |ship_steering_gates |ship_steering_gate_site|ship_steering_refused|ship_steering_gates_refused|ship_trace installed|ship_trace_refused)' "$T" | cut -c1-110
echo "refused rows: $(grep -ac 'refused' "$T")"
python3 -I - "$T" "$R155" <<'PY'
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
    if n in('ship_steering_summary','ship_gate_transition','ship_gate_approach','ship_avoid_detour','ship_steering_stuck'):
        D[n].append(dict(re.findall(r'(\w+)=(\S+)',l)))
def q(v,p=1):
    v=sorted(v); f='n=%d min=%.{0}f p25=%.{0}f med=%.{0}f p75=%.{0}f max=%.{0}f'.format(p)
    return f%(len(v),v[0],v[len(v)//4],St.median(v),v[3*len(v)//4],v[-1]) if v else 'n=0'
summ=D['ship_steering_summary'];s=Counter()
for d in summ:
    for k,v in d.items():
        try:
            if k not in('frame','interval_ms','claims','ships','stuck_total') and not k.startswith(('handler_us','calls_per')): s[k]+=float(v)
        except ValueError: pass
fs=sorted(FT); g=(FT[fs[-1]]-FT[fs[0]])/1e3; r=(FE[fs[-1]]-FE[fs[0]])/1e3
print('SETA: trace game %.0f s over real %.0f s, ratio %.2f'%(g,r,g/r))
print('== 1 summary health over %d intervals (interval_ms sum %.0f)'%(len(summ),sum(float(d['interval_ms']) for d in summ)))
r155=dict(re.findall(r'(\w+)=(\d+)',open(sys.argv[2]).read().split('summary health')[1].split('\n')[1]))
keys=['conflict_episodes','unresolved_episodes','unresolved','unresolved_inside_stop','unresolved_inside_turn','unresolved_blocked','unresolved_behind','detours','emergency','avoid_calls','avoid_skipped_small','avoid_excluded_followers','gate_crossings','gate_p2','gate_p4','gate_p5','gate_p2_capital','gate_p4_capital','gate_capital_aligned','gate_lead_validated','gate_lead_fallback','gate_dock_writes','gate_dock_blocked','gate_dock_invalid','gate_warp_calls','gate_warp_skipped','gate_rows_dropped','snapshot_overflow','pass_order_not_steered','pass_table_full','pass_not_finite','ship_trace_rows','ship_trace_dropped']
print('%-28s %10s %10s'%('counter','Run156','Run155'))
for k in keys: print('%-28s %10d %10s'%(k,s[k],r155.get(k,'0')))
print('stuck_total(last)',summ[-1]['stuck_total'],'stuck rows',len(D['ship_steering_stuck']),'| handled_by_order_total(last)',summ[-1]['handled_by_order_total'])
p99=[float(d['handler_us_p99']) for d in summ];mx=[float(d['handler_us_max']) for d in summ]
print('handler_us_p50 med',St.median(float(d['handler_us_p50']) for d in summ),'p99 med',St.median(p99),'p99 max',max(p99),'p99>5:',sum(x>5 for x in p99),'max_us',max(mx),'max>50:',sum(x>50 for x in mx))
print('== 3 small ships: ship_gate_transition',len(D['ship_gate_transition']),'approach',len(D['ship_gate_approach']))
gt={}
for fr,t,o,c,od,tg in D['tr']:
    if od=='0x5': gt.setdefault(o,set()).add(tg)
G='0x8cb546d8'
tr=D['ship_gate_transition'];ap=D['ship_gate_approach']
print('class',dict(Counter(t['class'] for t in tr)),'capital',dict(Counter(t['capital'] for t in tr)),'validated',dict(Counter(t['validated'] for t in tr)),'dock_blocked',dict(Counter(t['dock_blocked'] for t in tr)))
for tag,sel in (('all',lambda o:True),('gate '+G+' (traced)',lambda o:G in gt.get(o,()))):
    t2=[t for t in tr if t['capital']=='0' and sel(t['obj'])];a2=[x for x in ap if x['capital']=='0' and sel(x['obj'])]
    print(' ',tag,'transitions',len(t2),'err_B_deg',q([float(t['err_B_deg']) for t in t2]),'>18.2:',sum(float(t['err_B_deg'])>18.2 for t in t2))
    print('   v_ms at o5 start',q([float(t['v_ms']) for t in t2]),'| cross_heading_err',q([float(x['cross_heading_err_deg']) for x in a2]),'| cross_v',q([float(x['cross_v_ms']) for x in a2]))
    for c in sorted(set(x['class'] for x in a2)):
        y=[x for x in a2 if x['class']==c]; print('    %s n=%d cross_to_launch_s %s'%(c,len(y),q([float(x['cross_to_launch_ms'])/1000 for x in y])))
print('capital approach rows:',[(x['obj'],x['class'],x['cross_v_ms'],x['cross_heading_err_deg'],x['cross_to_launch_ms']) for x in ap if x['capital']=='1'])
print('== 4 small-ship detours vs capital obstacles')
caps={'0x8c71c728','0x8c7228a8','0x8cb4e7c8'}
det=D['ship_avoid_detour']
v=[d for d in det if d['obstacle'] in caps and d['obj'] not in caps]
print('rows',len(v),'subjects',len({d['obj'] for d in v}),'miss<C',sum(float(d['miss_m'])<float(d['C_m']) for d in v),'miss',q([float(d['miss_m']) for d in v],0),'C',sorted({d['C_m'] for d in v}))
for o in sorted({d['obj'] for d in v}):
    w=[d for d in v if d['obj']==o]; print('  ',o,'vs',dict(Counter(d['obstacle'] for d in w)),'min miss %.0f'%min(float(d['miss_m']) for d in w),'min t_cpa %.1f'%min(float(d['t_cpa_s']) for d in w),'leader',set(d['leader'] for d in w))
c=[d for d in det if d['obj'] in caps]
print('capital-as-subject detours',len(c),'vs small (C<2000)',sum(float(d['C_m'])<2000 for d in c),'distinct small obstacles',len({d['obstacle'] for d in c if float(d['C_m'])<2000}))
print('per-interval nonzero: unresolved_inside_stop',sum(d.get('unresolved_inside_stop','0')!='0' for d in summ),'emergency',sum(d.get('emergency','0')!='0' for d in summ),'max emergency',max(int(d['emergency']) for d in summ))
PY
echo "== 2 capital timeline: capital_timeline.py > capital_timeline.txt"
python3 -I "$D/capital_timeline.py" "$T" > "$D/capital_timeline.txt"; tail -n +1 "$D/capital_timeline.txt" | grep -E '^==|first row|total|0x1f at'
echo "== replay --gate (capitals)"
for o in 0x8c71c728 0x8cb4e7c8 0x8c7228a8; do python3 -I "$D/../../../probe/ship_trace_replay.py" --gate --obj $o "$T" 2>&1 | tail -n 12; done
echo "== SETA (seta= field per frame) over the capital phase windows: seta=1 frames / frames"
LC_ALL=C grep -a 'seta=' "$L" | awk '{f="";s="";for(i=1;i<=NF;i++){if($i~/^frame=/)f=substr($i,7); if($i~/^seta=/)s=substr($i,6)}; if(f!="")print f,s}' |
 awk 'BEGIN{split("812 4372 13495 16902 16904 17315 17317 17508 17510 17522 17740 20513 20515 21048 21050 21170 21299 23527 23529 24496 24498 24832 24834 24853",r," ")} {s[$1]=$2; t+=$2; n++} END{print "session seta=1 frames",t,"of",n; for(i=1;i<=24;i+=2){m=0;k=0;for(f=r[i];f<=r[i+1];f++) if(f in s){m++;k+=s[f]} print r[i]"-"r[i+1], k"/"m}}'
echo "== classes of the small (C<2000 m) obstacles the capitals still detour around (from any row naming them as obj)"
for o in $(grep -aE '^ship_avoid_detour .*obj=0x(8c71c728|8c7228a8|8cb4e7c8) ' "$T" | awk '{for(i=1;i<=NF;i++){if($i~/^obstacle=/)o=substr($i,10); if($i~/^C_m=/)c=substr($i,5)}; if(c<2000)print o}' | sort -u); do
 echo "$o rows_as_obstacle=$(grep -acE "^ship_avoid_detour .*obj=0x(8c71c728|8c7228a8|8cb4e7c8) .*obstacle=$o " "$T") $(grep -a "obj=$o " "$T" | grep -oE 'class=[A-Z0-9_]+' | sort | uniq -c | tr '\n' ' ')"; done
