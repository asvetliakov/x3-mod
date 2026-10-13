"""Run160b Q2: every capital 0x12 episode (from cap_orders.py segmentation of all rows that carry the capital's order) with the
engine warn row that started it (same obj, frame within [start-15, start+5]): other id/class/code, the other's order at the
warn (warn_pre entry), d_m; the capital's order before the episode; phase = arrival (<= 60 s after its first row, at the arrival
gate), exit-approach (previous order 0x4), escort leg (previous order 0x14/0x15/0x0 inside an escort leg). Gate distance for
traced capitals. Usage: python3 -I episodes12.py rows.txt"""
import sys, re, bisect, math, collections as C
caps = {l.split()[0]: l.split()[1] for l in open('caps.txt')}
tm = {}; S = C.defaultdict(dict); W = C.defaultdict(list); PRE = {}; pos = C.defaultdict(dict)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) class=\S+ order=(\S+) tgt=\S+ tgt_class=\d+ pos=(\S+)', l); f = int(m[1])
        tm.setdefault(f, int(m[2]))
        if m[3] in caps: S[m[3]][f] = m[4]; pos[m[3]][f] = [int(x) / 500 for x in m[5].split(',')]
    elif l.startswith('ship_avoid_e_verdict '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame'])
        if d['A'] in caps: S[d['A']].setdefault(f, d['A_order'])
        if d['B'] in caps: S[d['B']].setdefault(f, d['B_order'])
    elif l.startswith('ship_avoid_engine_warn_pre '):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['obj'] in caps: S[d['obj']].setdefault(int(d['frame']), d['order']); PRE[(d['obj'], int(d['frame']))] = l.split('e_verdict=')[1].strip()
    elif l.startswith('ship_avoid_engine_warn '):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['obj'] in caps: S[d['obj']].setdefault(int(d['frame']), d['order']); W[d['obj']].append(d)
fs = sorted(tm)
def g(f): i = min(bisect.bisect_left(fs, f), len(fs) - 1); return tm[fs[i]] / 1e3
G = {'0x8fa9eee8': [4259, -18037, 31134], '0x8fa9d688': [-7498, 10774, -57569]}  # Run161 gates (gates.txt)
tot = C.Counter(); n = C.Counter()
print('obj class | start-end frame game_s dur_s | prev order | phase | warn other class/code d_m | other entry at warn_pre (other:class:code:order:leader:member:d:r:E)')
for o in sorted(S, key=lambda o: min(S[o])):
    seq = sorted(S[o].items()); segs = []
    for f, od in seq:
        if segs and segs[-1][0] == od: segs[-1][1].append(f)
        else: segs.append([od, [f]])
    t0 = g(seq[0][0])
    for i, (od, fl) in enumerate(segs):
        if od != '0x12': continue
        end = segs[i + 1][1][0] if i + 1 < len(segs) else fl[-1]; dur = g(end) - g(fl[0])
        prev = next((segs[j][0] for j in range(i - 1, -1, -1) if segs[j][0] != '0x0'), '-')
        ph = 'arrival' if g(fl[0]) - t0 <= 60 else ('exit-approach' if prev in ('0x4', '0x1f', '0x5') else 'escort/transit leg (prev %s)' % prev)
        w = [d for d in W[o] if fl[0] - 15 <= int(d['frame']) <= fl[0] + 5]
        wd = w[0] if w else None
        pre = [v for (ob, f), v in PRE.items() if ob == o and fl[0] - 20 <= f <= fl[0] + 5]
        gd = ''
        if fl[0] in pos[o] or any(f in pos[o] for f in fl):
            p = pos[o].get(fl[0]) or pos[o][next(f for f in fl if f in pos[o])]
            gd = ' |x-G| ' + ', '.join('%s %.0f' % (k[-4:], math.dist(p, v)) for k, v in G.items())
        tot[ph.split()[0]] += dur; n[ph.split()[0]] += 1
        print('%s %s | %d-%d %.1f %.1f | %s | %s%s | %s | %s' % (o, caps[o], fl[0], end, g(fl[0]), dur, prev, ph, gd,
              ('%s %s/%s %s' % (wd['other'], wd['class'], wd['code'], wd['d_m'])) if wd else 'no warn row (gate_rows_dropped?)', pre[0][:160] if pre else '-'))
print('totals by phase (game s):', {k: round(v, 1) for k, v in tot.items()}, 'episodes', dict(n), 'all %.1f s %d' % (sum(tot.values()), sum(n.values())))
