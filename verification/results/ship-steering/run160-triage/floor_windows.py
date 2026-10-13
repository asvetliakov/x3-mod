"""Run159 triage, question 2: where lane_seq_floor_ms (cumulative, game ms) grew, per summary interval, divided by the
interval's game time (ship_trace t_ms) = the mean number of capitals at the sequencing-cap floor; grouped into windows
separated by intervals without growth. Usage: python3 -I floor_windows.py <extracted rows>"""
import re, sys, bisect
S = []; tms = {}
with open(sys.argv[1], errors='replace') as f:
    for l in f:
        if l.startswith('ship_steering_summary '):
            d = dict(re.findall(r'(\w+)=(\S+)', l)); S.append((int(d['frame']), int(d.get('lane_seq_floor_ms', 0)), int(d.get('lane_seq_capped', 0))))
        elif l.startswith('ship_trace frame='):
            m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+)', l); tms.setdefault(int(m.group(1)), int(m.group(2)))
K = sorted(tms)
def t(fr): i = min(bisect.bisect_left(K, fr), len(K) - 1); return tms[K[i]]
win = []; cur = None
for (f0, v0, c0), (f1, v1, c1) in zip(S, S[1:]):
    if v1 > v0:
        g = t(f1) - t(f0)
        if cur is None: cur = [f0, f1, 0, 0, 0, 0.0]
        cur[1] = f1; cur[2] += v1 - v0; cur[3] += g; cur[4] += c1 - c0; cur[5] = max(cur[5], (v1 - v0) / g if g else 0)
    elif cur: win.append(cur); cur = None
if cur: win.append(cur)
print('frames | game_s of the window | floor_s summed | mean capitals at floor | peak interval | seq-capped decisions')
for f0, f1, fl, g, c, pk in win:
    print('%d-%d | %.1f | %.1f | %.2f | %.2f | %d' % (f0, f1, g / 1e3, fl / 1e3, fl / g if g else 0, pk, c))
print('total floor_s %.1f (last counter %d ms)' % (sum(w[2] for w in win) / 1e3, S[-1][1]))
