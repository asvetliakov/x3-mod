"""Run159 triage, question 2: the sequencing wait chain seen from one traced capital's ship_capital_lane rows.
Usage: python3 -I chain.py <extracted rows> <A obj> <frame0> <frame1> [step_frames]
While A holds still (A's own along-F motion from its ship_trace rows printed per sample), ds_m of A's rows against each
neighbour B is B's along-F position relative to A, so B's along-F speed is d(ds)/dt. The ships are ordered by s; for each
adjacent pair (T behind L) the gap s_L - s_T is printed against D_x = h_T + h_L + 100 m (h from the rows' h_T/h_L), '*' when
gap < D_x (T inside the leader's D_x: the 16b cap holds T at the floor when the pair is obliged). Game time from t_ms."""
import re, sys
from collections import defaultdict
KV = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
path, A, f0, f1 = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]); step = int(sys.argv[5]) if len(sys.argv) > 5 else 100
lane = []; h = {}; tms = {}; apos = {}
with open(path, errors='replace') as f:
    for l in f:
        if l.startswith('ship_capital_lane ') and ' A=%s ' % A in l:
            d = KV(l); fr = int(d['frame'])
            if f0 <= fr <= f1: lane.append(d)
            if d['trail'] == d['A']: h[d['A']] = float(d['h_T']); h[d['B']] = float(d['h_L'])
            elif d['trail'] == d['B']: h[d['B']] = float(d['h_T']); h[d['A']] = float(d['h_L'])
        elif l.startswith('ship_trace frame='):
            m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) .*? pos=(-?\d+),(-?\d+),(-?\d+) ', l)
            fr = int(m.group(1)); tms.setdefault(fr, int(m.group(2)))
            if m.group(3) == A and f0 <= fr <= f1: apos[fr] = [int(m.group(i)) / 500 for i in (4, 5, 6)]
last = {}; prev = {}; nxt = f0
t = lambda fr: tms.get(fr, tms.get(fr - 1, 0)) / 1e3
a0 = None
print('frame t_game_s | A moved_m since f0 | per B: ds(B-A) vF_B m/s verdict foot_B | chain by s: T>L gap/Dx')
for d in lane:
    fr = int(d['frame']); last[d['B']] = (fr, float(d['ds_m']), d['verdict'] + '/' + d['route'], d['foot'].split(',')[1])
    if fr < nxt: continue
    nxt = fr + step
    ap = apos.get(fr) or apos.get(min(apos, key=lambda k: abs(k - fr))) if apos else None
    if a0 is None and ap: a0 = ap
    moved = sum((x - y) ** 2 for x, y in zip(ap, a0)) ** 0.5 if ap and a0 else float('nan')
    cur = {b: v for b, v in last.items() if fr - v[0] <= 150}
    parts = []
    for b, (bf, ds, vr, foot) in sorted(cur.items(), key=lambda kv: kv[1][1]):
        vf = float('nan')
        if b in prev and t(bf) > t(prev[b][0]): vf = (ds - prev[b][1]) / (t(bf) - t(prev[b][0]))
        parts.append('%s:%.0f %+.1f %s %s' % (b[-4:], ds, vf, vr, foot))
        prev[b] = (bf, ds)
    ships = sorted([(0.0, A)] + [(v[1], b) for b, v in cur.items()])
    ch = ['%s>%s %.0f/%.0f%s' % (T[-4:], L[-4:], sL - sT, h.get(T, 0) + h.get(L, 0) + 100, '*' if sL - sT < h.get(T, 0) + h.get(L, 0) + 100 else '')
          for (sT, T), (sL, L) in zip(ships, ships[1:])]
    print('%d %.1f | %.0f | %s | %s' % (fr, t(fr), moved, '; '.join(parts), ' '.join(ch)))
