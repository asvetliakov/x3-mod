"""Run161 Q3(e)/Q6: the controller counters per window from the summary rows (cumulative -> deltas): ctl_yields, ctl_capped,
ctl_conflicts, ctl_unresolved, ctl_released, e_pass_ctl, e_suppressed_ctl, and the summary intervals (game s from t_ms when
present, else count) in which ctl_capped rose. Usage: python3 -I ctl_counters.py rows.txt"""
import sys, re, collections as C
K = ['ctl_yields', 'ctl_capped', 'ctl_conflicts', 'ctl_unresolved', 'ctl_released', 'e_pass_ctl', 'e_suppressed_ctl', 'e_suppressed_arrival_gate', 'avoid_ctl_excluded']
W = [(0, 40700, 'before arrivals'), (40700, 52000, 'arrivals 0x8fa9d688'), (52000, 63300, 'convoys to 0x8fa9eee8'), (63300, 69100, 'exit 0x8fa9eee8'), (69100, 10**9, 'after')]
prev = {}; D = C.defaultdict(C.Counter); rise = C.Counter()
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('ship_steering_summary '): continue
    d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame'])
    w = next(x[2] for x in W if x[0] <= f < x[1])
    for k in K:
        if k not in d: continue
        v = int(d[k]); D[w][k] += v - prev.get(k, 0); prev[k] = v
    if D[w]['ctl_capped'] and 'ctl_capped' in d: pass
for w in W: print(w[2], dict(D[w[2]]))
