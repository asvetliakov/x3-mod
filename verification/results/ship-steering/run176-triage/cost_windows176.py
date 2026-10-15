# Run176 cost triage Q1: the ship_steering_summary window holding each slow handler row against its neighbours (one window before and
# after), the per-window counters that a fixed work item would move. Usage: python3 -I cost_windows176.py ROWS
import sys, re, bisect
F = ('calls', 'handled', 'ships', 'handler_us_p99', 'handler_us_max', 'avoid_calls', 'conflicts', 'detours', 'unresolved', 'unresolved_blocked',
     'unresolved_behind', 'unresolved_degenerate', 'snapshots', 'walked', 'snapshot_max', 'part_models', 'part_fallback', 'dock_models_built',
     'emergency', 'e_calls', 'gate_lead_calls', 'gate_crossings', 'join_evals', 'ctl_decisions', 'ctl_rollouts', 'ctl_entered', 'member_set',
     'avoid_excluded_followers', 'idle_steered')
summ = []; slow = []
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_steering_summary '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); summ.append((int(d['frame']), d))
    elif l.startswith('ship_steering_handler_slow '):
        slow.append(int(re.search(r' frame=(\d+)', l)[1]))
fr = [f for f, _ in summ]
print('field', *F)
for s in slow:
    i = bisect.bisect_left(fr, s)
    for j in (i - 1, i, i + 1):
        if 0 <= j < len(summ):
            f, d = summ[j]
            print(('SLOW %d ' % s) if j == i else '         ', 'summary', f, ' '.join('%s=%s' % (k, d.get(k)) for k in F))
