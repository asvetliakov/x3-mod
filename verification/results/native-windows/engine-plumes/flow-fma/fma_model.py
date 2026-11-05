"""Octave-1 lattice index of the production plume program (disassembly lines 97-106): p = mad(local.x, 1/n, -phase);
P1 = mad(p, 2.2, 5); m = mul(p, 2.2); i = P1 - frc(m). Fused (FMA) vs unfused mad: is i an integer per column?
Compared with the dips (ratio < 0.97 x neighbour mean) in the native FLOWDUMP rows."""
import math, sys, numpy as np
f = np.float32
def mad(a, b, c, fused): return f(math.fma(float(a), float(b), float(c))) if fused else f(f(f(a) * f(b)) + f(c))
def lattice_ok(local_x, n, phase, fused):
    q = f(f(1.0) / f(n))
    p = mad(f(local_x), q, -f(phase), fused)
    P1 = mad(p, f(2.2), f(5.0), fused)
    m = f(p * f(2.2))
    fr = f(m - f(math.floor(m)))
    i = f(P1 - fr)
    return float(i) == math.floor(float(i))
rows = {}
for l in open(sys.argv[1]):
    if l.startswith('FLOWDUMP'):
        d = dict(kv.split('=') for kv in l.split()[1:])
        rows.setdefault((int(d['w']), int(d['V'])), []).append((float(d['env']), float(d['p1']), float(d['p2'])))
phase = {100: (26.2499981, 26.7749977), 600: (21.8749981, 22.3124981), 1500: (8.74999905, 8.92499924), 10000: (7.87499952, 8.03249931)}
cx = {1920: 1200.0}
for (w, V), r in sorted(rows.items()):
    if w != 1920: continue
    n = 0.5 * V
    for k, ph in ((1, phase[V][0]), (2, phase[V][1])):
        a = [x[k] / x[0] for x in r]
        dips = {i for i in range(2, len(a) - 2) if a[i] < 0.97 * (a[i - 2] + a[i + 2]) / 2 and a[i] < 0.97 * max(a[i-1], a[i+1])}
        # pixel centre x = x_near - i + 0.5, x_near = 1152; local.x = (cx - x) / ppu, ppu = 60 / n
        bad = {}
        for fused in (True, False):
            bad[fused] = {i for i in range(len(a)) if not lattice_ok((cx[w] - (1152 - i + .5)) * n / 60.0, n, ph, fused)}
        inwin = lambda s: sorted(x for x in s if 70 <= x < 293)
        hitF = len(dips & bad[True]); hitU = len(dips & bad[False])
        print(f'V={V} p{k}: dips={len(dips)} in_window={len(inwin(dips))} fused_nonint={len(bad[True])} (window {len(inwin(bad[True]))}) unfused_nonint={len(bad[False])} '
              f'dips_on_fused={hitF} dips_on_unfused={hitU}')
        print('   dips', sorted(dips)[:20])
        print('   fused non-integer columns range', (min(bad[True]), max(bad[True])) if bad[True] else None, 'first', sorted(bad[True])[:12])
