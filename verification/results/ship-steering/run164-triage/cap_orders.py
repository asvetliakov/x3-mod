"""Run160b: per capital, the order timeline from every row that carries its order (traced or not): ship_trace (order),
E rows as A (A_order) and as B (B_order), warn / warn_pre (order). Consecutive equal orders form a segment; per segment:
frames, game start s, game duration s (first sample to next segment's first sample), samples, max sampling gap s.
Game time per frame from any ship_trace row's t_ms. Usage: python3 -I cap_orders.py rows.txt [--min-s 1]"""
import sys, re, collections as C
caps = {l.split()[0]: l.split()[1] for l in open(__file__.rsplit('/', 1)[0] + '/caps.txt' if '/' in __file__ else 'caps.txt')}
MIN = float(sys.argv[sys.argv.index('--min-s') + 1]) if '--min-s' in sys.argv else 1.0
tm = {}; S = C.defaultdict(dict)
def put(o, f, od):
    if o in caps: S[o].setdefault(f, od)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) class=\S+ order=(\S+)', l); f = int(m[1])
        tm.setdefault(f, int(m[2])); 
        if m[3] in caps: S[m[3]][f] = m[4]
    elif l.startswith('ship_avoid_e_verdict '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame']); put(d['A'], f, d['A_order']); put(d['B'], f, d['B_order'])
    elif l.startswith(('ship_avoid_engine_warn ', 'ship_avoid_engine_warn_pre ')):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); put(d['obj'], int(d['frame']), d['order'])
fs = sorted(tm)
import bisect
def g(f):
    i = bisect.bisect_left(fs, f); i = min(i, len(fs) - 1); return tm[fs[i]] / 1e3
tot12 = 0; ep12 = 0
for o in sorted(S, key=lambda o: min(S[o])):
    seq = sorted(S[o].items()); segs = []
    for f, od in seq:
        if segs and segs[-1][0] == od: segs[-1][1].append(f)
        else: segs.append([od, [f]])
    print('== %s %s samples %d frames %d-%d game %.1f-%.1f' % (o, caps[o], len(seq), seq[0][0], seq[-1][0], g(seq[0][0]), g(seq[-1][0])))
    t = C.Counter(); n12 = 0
    for i, (od, fl) in enumerate(segs):
        end = segs[i + 1][1][0] if i + 1 < len(segs) else fl[-1]
        dur = g(end) - g(fl[0]); t[od] += dur
        gaps = [g(b) - g(a) for a, b in zip(fl, fl[1:])] or [0]
        if od == '0x12': n12 += 1
        if dur >= MIN or od in ('0x12', '0x1f', '0x5', '0x1e', '0x21'):
            print('   %-5s %d-%d game %.1f dur %.1f n %d maxgap %.1f' % (od, fl[0], end, g(fl[0]), dur, len(fl), max(gaps)))
    print('   totals', {k: round(v, 1) for k, v in t.items()}, '0x12 episodes', n12); tot12 += t['0x12']; ep12 += n12
print('ALL capitals 0x12 total game s %.1f episodes %d' % (tot12, ep12))
