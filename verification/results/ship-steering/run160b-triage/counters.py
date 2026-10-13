# Run160b: cumulative summary counters (lane_*, gate_capital_*) -- the frames where each one rose, and per-interval
# counters (gate_rows_dropped, emergency) summed by frame window. Usage: python3 -I counters.py rows.txt
import sys,re
cum=['lane_apart','lane_seq_lat','lane_seq_cross','lane_seq_along','lane_capped','lane_cross','lane_follow','lane_exempt','gate_capital_join','gate_capital_fallback_pred','gate_capital_fallback_past_x','gate_capital_slow_join','gate_capital_unlatch','gate_capital_not_kept_mode','gate_capital_slowed','gate_capital_kept','join_slots_dropped','e_suppressed_lane']
per=['gate_rows_dropped','emergency','unresolved','conflict_episodes','gate_capital_detour_write']
prev={}; rises={k:[] for k in cum}; P={k:[] for k in per}
for l in open(sys.argv[1],errors='replace'):
    if not l.startswith('ship_steering_summary'): continue
    d=dict(re.findall(r'(\w+)=(\S+)',l)); f=int(d['frame'])
    for k in cum:
        if k in d:
            try: v=int(d[k])
            except ValueError: continue
            if v!=prev.get(k,0): rises[k].append((f,v-prev.get(k,0))); prev[k]=v
    for k in per:
        if k in d:
            try: P[k].append((f,float(d[k])))
            except ValueError: pass
for k in cum:
    r=rises[k]; print(k,'final',prev.get(k,'-'),'rises',len(r),'first..last frame',(r[0][0],r[-1][0]) if r else '-', r[:12] if k not in('lane_seq_lat','lane_exempt','e_suppressed_lane') else '')
for k in per:
    v=P[k]; tot=sum(x for _,x in v); nz=[(f,x) for f,x in v if x]
    print(k,'sum',tot,'nonzero intervals',len(nz),'frames',(nz[0][0],nz[-1][0]) if nz else '-')
    for lo,hi in ((0,38000),(38000,52900),(52900,61300),(61300,99999)):
        print('   window',lo,hi,sum(x for f,x in v if lo<=f<hi))
