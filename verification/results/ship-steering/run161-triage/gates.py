"""Run160b: gate frame for every gate in the log, from the rows (no hard-coded coordinates).
F = normal of the plane through the small ships' order-5 pass points B (largest-area triple), signed so (G - A_small).F > 0.
G = the capitals' order-5 given when a capital launched there, else the centre of the circle fitted (Kasa) to the B points
projected on the plane (B - G = r(0.55 sin t R - 0.55 cos t U - 0.01 F)) moved by +0.01 r F; r_gate = the class-18 warn r_m
for that gate id when logged, else |A_small - G| / sqrt(1.64). Writes 'gate G_m F r_m src nB' lines.
Usage: python3 -I gates.py rows.txt > gates.txt"""
import math, os, re, sys
from collections import Counter, defaultdict
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'probe'))
import ship_trace_rows as T
CAP = {'M1', 'M2', 'TL', 'M7', 'M0'}
sub = lambda a, b: [x - y for x, y in zip(a, b)]; dot = lambda a, b: sum(x * y for x, y in zip(a, b)); nrm = lambda a: math.sqrt(dot(a, a))
unit = lambda a: [x / nrm(a) for x in a]
cross = lambda a, b: [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
M = lambda p: [x / 500 for x in p]
s4 = defaultdict(Counter); s5 = defaultdict(set); c5 = defaultdict(Counter); rw = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        r = T.parse_row(l)
        if not r or r['tgt_class'] != 18: continue
        if r['class'] in CAP:
            if r['order'] == 5: c5[r['tgt']][r['given']] += 1
        elif r['order'] == 4: s4[r['tgt']][r['given']] += 1
        elif r['order'] == 5: s5[r['tgt']].add(r['given'])
    elif l.startswith('ship_avoid_engine_warn ') and ' class=18 ' in l:
        d = dict(re.findall(r'(\w+)=(\S+)', l)); rw[int(d['other'], 16)] = float(d['r_m'])
for g in sorted(set(s5) | set(c5)):
    Bs = [M(b) for b in s5.get(g, ())]
    if len(Bs) < 3 or g not in s4: print('0x%08x insufficient' % g); continue
    best = None
    for i in range(len(Bs)):
        for j in range(i + 1, len(Bs)):
            for k in range(j + 1, len(Bs)):
                c = cross(sub(Bs[j], Bs[i]), sub(Bs[k], Bs[i]))
                if best is None or nrm(c) > nrm(best): best = c
    F = unit(best); As = M(s4[g].most_common(1)[0][0])
    R = unit(cross(F, [0, 1, 0]) if abs(F[1]) < 0.9 else cross(F, [1, 0, 0])); U = cross(F, R)
    # Kasa circle fit in (R,U)
    pts = [(dot(b, R), dot(b, U)) for b in Bs]; n = len(pts)
    import itertools
    Sx = sum(p[0] for p in pts); Sy = sum(p[1] for p in pts); Sxx = sum(p[0]**2 for p in pts); Syy = sum(p[1]**2 for p in pts); Sxy = sum(p[0]*p[1] for p in pts)
    Sz = [p[0]**2 + p[1]**2 for p in pts]; Sxz = sum(p[0]*z for p, z in zip(pts, Sz)); Syz = sum(p[1]*z for p, z in zip(pts, Sz)); Szs = sum(Sz)
    A = [[Sxx, Sxy, Sx], [Sxy, Syy, Sy], [Sx, Sy, n]]; b = [Sxz, Syz, Szs]
    def det(m): return m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1]) - m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0]) + m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0])
    D = det(A); sol = []
    for c in range(3):
        m = [row[:] for row in A]
        for rr in range(3): m[rr][c] = b[rr]
        sol.append(det(m) / D)
    cx, cy = sol[0] / 2, sol[1] / 2; rad = math.sqrt(sol[2] + cx * cx + cy * cy)
    fdep = sum(dot(bb, F) for bb in Bs) / n
    Gfit = [cx * R[i] + cy * U[i] + fdep * F[i] for i in range(3)]
    rg_fit = rad / 0.55; Gfit = [Gfit[i] + 0.01 * rg_fit * F[i] for i in range(3)]
    if c5.get(g): G = M(c5[g].most_common(1)[0][0]); src = 'capital_o5'
    else: G = Gfit; src = 'circle_fit'
    F = F if dot(sub(G, As), F) > 0 else [-x for x in F]
    rg = rw.get(g, nrm(sub(As, G)) / math.sqrt(1.64))
    print('0x%08x %s %s %.1f %s nB=%d fit_centre_err_m=%.0f fit_r_gate=%.0f' % (g, ','.join('%.1f' % x for x in G), ','.join('%.4f' % x for x in F), rg, src, n, nrm(sub(Gfit, G)), rg_fit))
