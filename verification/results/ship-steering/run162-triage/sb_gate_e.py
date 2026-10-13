# Run162 Q4/Q8: site E's verdicts for a capital member against its own gate 0x8e4fbdb0 (B_class 18) during its stage-B funnel windows
# (release -> eviction / order 5, from the ship_gate_ctl rows), by (A_order, verdict), with the d_m range; and the engine 0x12 warn
# that ended each window. Usage: python3 -I sb_gate_e.py rows.txt
import sys, re, collections as C
ev = C.defaultdict(list); E = []; W = []
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ev[d['ship']].append((int(d['frame']), int(d['t_ms']), d['why'], d['stage']))
    elif l.startswith('ship_avoid_e_verdict ') and ' B=0x8e4fbdb0 ' in l:
        d = dict(re.findall(r'(\w+)=(\S+)', l)); E.append((int(d['frame']), d['A'], d['A_order'], d['verdict'], int(d['d_m'])))
    elif l.startswith('ship_avoid_engine_warn ') and ' other=0x8e4fbdb0 ' in l:
        d = dict(re.findall(r'(\w+)=(\S+)', l)); W.append((int(d['frame']), d['obj'], d['d_m']))
for s, rows in ev.items():
    for i, (f, t, why, stg) in enumerate(rows):
        if why != 'release': continue
        end = next((r for r in rows[i + 1:] if r[2] in ('evict', 'dock', 'launch')), None)
        f1 = end[0] if end else 10**9
        c = C.Counter((o, v) for (fe, a, o, v, dm) in E if a == s and f <= fe <= f1); dms = [dm for (fe, a, o, v, dm) in E if a == s and f <= fe <= f1]
        w = [x for x in W if x[1] == s and f <= x[0] <= f1]
        print('%s release f %d game %.1f -> %s f %s game %.1f (%.1f s) | E vs gate %s d_m %s..%s | engine warn on gate %s' % (s, f, t / 1000,
              end[2] if end else '-', f1, end[1] / 1000 if end else 0, (end[1] - t) / 1000 if end else 0, dict(c), min(dms) if dms else '-',
              max(dms) if dms else '-', w))
print('all E rows vs 0x8e4fbdb0 by verdict', dict(C.Counter(v for *_, v, dm in E)))
