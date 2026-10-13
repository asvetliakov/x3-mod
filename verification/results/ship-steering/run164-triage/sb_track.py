# Run162 Q1/Q4: a traced capital's track in the frame of its target gate, per window of --step game s: order, target, speed (m/s),
# distance to G, along-F z (m, < 0 in front), lateral, nose.F, the turn accumulated in the window (deg, from the nose) and the
# window's game/real time. Gates: fbdb0 from gates.txt (G, F); c158 from its arrivals' launch points (G only, F unknown: z/lat blank).
# Usage: python3 -I sb_track.py rows.txt obj [--step 10] [--from F] [--to F] [--gate 0x...]
import sys, os, re, math; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
a = sys.argv; obj = a[2]; step = float(a[a.index('--step') + 1]) if '--step' in a else 10.0
f0 = int(a[a.index('--from') + 1]) if '--from' in a else 0; f1 = int(a[a.index('--to') + 1]) if '--to' in a else 10**9
tm = sb_time.load(a[1])
GATES = {'0x8e04ff20': ((4259.0, -18037.0, 31134.0), (0.0, 0.0, 1.0)), '0x8e051030': ((-7498.0, 10774.0, -57569.0), (0.0, 0.0, -1.0)), '0x8e7ff4f0': ((4259.0, -18037.0, 31134.0), (0.0, 0.0, 1.0)), '0x8e7ff9d0': ((-7498.0, 10774.0, -57569.0), (0.0, 0.0, -1.0)),  # Run163 (gates.txt)
         '0x8e4fbdb0': ((-7498.0, 10774.0, -57569.0), (0.0, 0.0, -1.0)), '0x8e4fc158': ((4400.0, -17450.0, 31080.0), None)}
dot = lambda x, y: sum(p * q for p, q in zip(x, y))
w = None; out = []
def flush(w):
    if not w: return
    f, t, g, o, tg, v, d, z, lat, nf = w['first']
    print('f %6d-%6d game %8.1f-%8.1f real %5.1f-%5.1f | %s %s v %3.0f-%3.0f d %6.0f->%6.0f z %s lat %s noseF %s turn %4.0f' % (
        f, w['lf'], t, w['lt'], tm.real(f) / 1000, tm.real(w['lf']) / 1000, o, tg[-4:], min(w['v']), max(w['v']), d, w['ld'],
        '%.0f' % z if z is not None else '-', '%.0f' % lat if lat is not None else '-', '%.2f' % nf if nf is not None else '-', w['turn']))
pf = None
for l in open(a[1], errors='replace'):
    if not l.startswith('ship_trace frame=') or (' obj=' + obj + ' ') not in l: continue
    d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame'])
    if f < f0 or f > f1: continue
    t = int(d['t_ms']) / 1000; p = [float(x) / 500 for x in d['pos'].split(',')]; fw = [float(x) for x in d['fwd'].split(',')]; _n = math.sqrt(sum(x * x for x in fw)) or 1.0; fw = [x / _n for x in fw]  # Run164: normalized (5-decimal rows made acos(dot) accumulate ~0.36 deg/frame of noise)
    v = float(d['v']) / 500; tg = d['tgt']; G = GATES.get(a[a.index('--gate') + 1] if '--gate' in a else tg)  # Run164: --gate frames every row in that gate
    if G:
        dd = [p[i] - G[0][i] for i in range(3)]; dist = math.sqrt(dot(dd, dd))
        if G[1]: z = dot(dd, G[1]); lat = math.sqrt(max(0, dist * dist - z * z)); nf = dot(fw, G[1])
        else: z = lat = nf = None
    else: dist = 0; z = lat = nf = None
    if w is None or t - w['t0'] >= step or d['order'] != w['first'][3] or tg != w['first'][4]:
        flush(w); w = {'t0': t, 'first': (f, t, G, d['order'], tg, v, dist, z, lat, nf), 'v': [], 'turn': 0.0}; pf = None
    if pf: w['turn'] += math.degrees(math.acos(max(-1, min(1, dot(pf, fw)))))
    pf = fw; w['v'].append(v); w['lf'] = f; w['lt'] = t; w['ld'] = dist
flush(w)
