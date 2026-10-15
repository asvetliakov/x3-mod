# Run176 cost triage Q1: every ship_gate_launch against the ship_steering_handler_slow rows (same ship, game ms after the launch) and
# the per-gate / per-kind tally of launches followed by a slow call. Usage: python3 -I cost_launch176.py ROWS
import sys, re, collections as C
la = []; slow = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_launch '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); la.append(d)
    elif l.startswith('ship_steering_handler_slow '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); slow.setdefault(d['obj'], []).append(d)
tally = C.Counter(); hit = set()
for d in la:
    s = [x for x in slow.get(d['ship'], []) if 0 <= int(x['t_ms']) - int(d['t_ms']) <= 60000]
    key = (d['gate'], d['class'], d['kind']); tally[key, bool(s)] += 1
    if s:
        x = s[0]; hit.add(x['frame'])
        print('launch frame=%s t_ms=%s ship=%s class=%s gate=%s kind=%s -> slow frame=%s +%d ms (+%d frames) us=%s cycles=%s order=%s'
              % (d['frame'], d['t_ms'], d['ship'], d['class'], d['gate'], d['kind'], x['frame'], int(x['t_ms']) - int(d['t_ms']),
                 int(x['frame']) - int(d['frame']), x['us'], x['cycles'], x['order']))
for o, ss in slow.items():
    for x in ss:
        if x['frame'] not in hit: print('slow without a launch of the ship within 60 s before: frame=%s obj=%s us=%s cycles=%s order=%s' % (x['frame'], o, x['us'], x['cycles'], x['order']))
print('launches', len(la))
for (k, h), n in sorted(tally.items()): print('gate=%s class=%s kind=%s slow_after=%d launches=%d' % (k + (h, n)))
# launch frames: first and last per gate
g = C.defaultdict(list)
for d in la: g[d['gate']].append(int(d['frame']))
for k, v in g.items(): print('gate', k, 'launch frames', min(v), '..', max(v), 'n', len(v))
