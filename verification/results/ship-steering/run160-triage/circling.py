"""Run159 triage, questions 3 and 6: heading rotation per traced ship from ship_trace rows.
Usage: python3 -I circling.py <extracted rows> [--small]   (default: capitals M1/M2/TL/M7/M0; --small: every other class)
Per ship: the summed heading rotation (angle between consecutive fwd rows) split by order (order 4 also by the join mode of
ship_gate_capital_join for capitals), and the worst 60 s game window: the most heading turned inside 60 s, with the
ship's largest displacement from the window's first position, order mix, targets, mean v and the summed yaw-rate sign
(net turn direction: |sum signed| close to the total means one-way circling, small means oscillation)."""
import math, os, re, sys
from collections import Counter, defaultdict
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'probe'))
import ship_trace_rows as T
CAP = {'M1', 'M2', 'TL', 'M7', 'M0'}; small = '--small' in sys.argv
def ang(u, v):  # normalised: the printed 5-decimal rows are not unit length (an unnormalised acos reads 0.38 deg per row of noise)
    d = sum(a * b for a, b in zip(u, v)) / math.sqrt(sum(a * a for a in u) * sum(b * b for b in v))
    return math.degrees(math.acos(max(-1.0, min(1.0, d))))
rows = defaultdict(list); joins = defaultdict(list)
with open(sys.argv[1], errors='replace') as f:
    for l in f:
        if l.startswith('ship_trace frame='):
            r = T.parse_row(l)
            if r and ((r['class'] in CAP) != small): rows[r['obj']].append(r)
        elif l.startswith('ship_gate_capital_join '):
            d = dict(re.findall(r'(\w+)=(\S+)', l)); joins[int(d['obj'], 16)].append((int(d['frame']), d['mode']))
out = []
for obj, rs in rows.items():
    js = joins.get(obj, []); ji = 0; mode = 'out'; by = Counter(); t = []; cum = 0.0; sgn = 0.0
    for a, b in zip(rs, rs[1:]):
        while ji < len(js) and js[ji][0] <= b['frame']: mode = js[ji][1]; ji += 1
        if b['frame'] - a['frame'] > 5 or b['t_ms'] < a['t_ms']: t.append((b, cum, sgn)); continue  # a trace gap: no turn credited
        d = ang(a['fwd'], b['fwd']); cum += d
        # signed turn about the ship's up axis: sign of (fwd_a x fwd_b) . up
        c = [a['fwd'][1]*b['fwd'][2]-a['fwd'][2]*b['fwd'][1], a['fwd'][2]*b['fwd'][0]-a['fwd'][0]*b['fwd'][2], a['fwd'][0]*b['fwd'][1]-a['fwd'][1]*b['fwd'][0]]
        sgn += math.copysign(d, sum(x * y for x, y in zip(c, b['up'])))
        lab = 'o%x' % b['order'] + (':' + mode if b['order'] == 4 and rs[0]['class'] in CAP else '')
        by[lab] += d; t.append((b, cum, sgn))
    best = None; j = 0
    for i in range(len(t)):
        while t[i][0]['t_ms'] - t[j][0]['t_ms'] > 60000: j += 1
        turned = t[i][1] - t[j][1]
        if best is None or turned > best[0]: best = (turned, j, i)
    if not best: continue
    turned, j, i = best; w = [x[0] for x in t[j:i + 1]]; p0 = w[0]['pos']
    disp = max(math.dist(p0, x['pos']) for x in w) / 500
    out.append((turned, obj, rs[0]['class'], cum, by, w, disp, t[i][2] - t[j][2]))
for turned, obj, cls, cum, by, w, disp, net in sorted(out, key=lambda x: -x[0]):
    if small and turned < 360: continue
    o = Counter('0x%x' % x['order'] for x in w); tg = Counter('0x%08x' % x['tgt'] for x in w)
    print('0x%08x %s total_turn %.0f deg %s | worst 60 s: frames %d-%d t %.1f-%.1f turn %.0f deg net_signed %+.0f max_disp %.0f m v_mean %.0f m/s orders %s tgts %s obst %s' % (
        obj, cls, cum, {k: round(v) for k, v in by.most_common(6)}, w[0]['frame'], w[-1]['frame'], w[0]['t_ms'] / 1e3, w[-1]['t_ms'] / 1e3, turned, net, disp,
        sum(x['v'] for x in w) / len(w) / 500, dict(o), dict(tg.most_common(2)), dict(Counter('0x%08x' % x['obst'] for x in w if x['obst']).most_common(2))))
