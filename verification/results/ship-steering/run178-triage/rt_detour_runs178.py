# Run178 Q1: runs of traced frames where our avoidance moved the leaf point (point != given, obst != 0) for the four return-trip
# ships, with the obstacle, the order, the game-time span, and the next 0x12 of that ship (first engine warn row of an episode) within 35 s.
# Usage: python3 -I rt_detour_runs178.py ROWS
import sys, re
S = ['0x8fe612b8', '0x8fe58420', '0x39cfa520', '0x39cf2180']
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
runs = {s: [] for s in S}; cur = {}; warn = {s: [] for s in S}; f2t = {}; lastw = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        d = kv(l); f = int(d['frame']); t = int(d['t_ms']) / 1e3; f2t.setdefault(f, t); s = d['obj']
        if s not in runs: continue
        on = d['point'] != d['given'] and d['obst'] != '0x00000000'
        c = cur.get(s)
        if on and c and c['obst'] == d['obst'] and c['order'] == d['order'] and f - c['f1'] <= 30: c['f1'] = f; c['t1'] = t; c['n'] += 1
        else:
            if c: runs[s].append(c); cur[s] = None
            if on: cur[s] = dict(obst=d['obst'], order=d['order'], f0=f, f1=f, t0=t, t1=t, n=1)
    elif l.startswith('ship_avoid_engine_warn '):
        d = kv(l); s = d['obj']; f = int(d['frame'])
        if s in warn:
            if lastw.get(s, -10) < f - 3: warn[s].append((f, d['other']))
            lastw[s] = f
for s in S:
    if cur.get(s): runs[s].append(cur[s])
    for r in runs[s]:
        nx = [(f2t.get(f, 0), o) for f, o in warn[s] if r['f1'] <= f]
        nx = [x for x in nx if x[0] - r['t1'] <= 35]
        print('%s order=%s obst=%s g=%.1f-%.1f (%d rows) | next 0x12 of this ship within 35 s: %s' % (
            s, r['order'], r['obst'], r['t0'], r['t1'], r['n'], ('%.1f vs %s (+%.1f s)' % (nx[0][0], nx[0][1], nx[0][0] - r['t1'])) if nx else 'none'))
