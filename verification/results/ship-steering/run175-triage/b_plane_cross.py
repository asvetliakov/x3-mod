# Run175 B Q3/Q4: plane crossings of a lane gate's plane from ship_gate_ctl_member rows (p_m = R,U,F in the gate frame; ~1 row per game s):
# per ship and gate, sign changes of F with sqrt(R^2+U^2) < LAT m on either row, by role, with the game times; a crossing followed by the
# ship's launch at that gate within 30 s is listed as such. Usage: python3 -I b_plane_cross.py ROWS [LAT_M=3314]
import sys, re, math, collections as C
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
LAT = float(sys.argv[2]) if len(sys.argv) > 2 else 3314.0
last = {}; cr = C.defaultdict(list); launch = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl_member '):
        d = kv(l); p = [float(x) for x in d['p_m'].split(',')]; k = (d['ship'], d['gate'][-4:]); t = int(d['t_ms']) / 1000
        if k in last:
            t0, p0, r0 = last[k]
            if (p0[2] > 0) != (p[2] > 0) and min(math.hypot(p0[0], p0[1]), math.hypot(p[0], p[1])) < LAT and t - t0 < 10:
                cr[k].append((t, d['role'], d['order'], '%+.0f>%+.0f' % (p0[2], p[2]), '%.0f' % math.hypot(p[0], p[1])))
        last[k] = (t, p, d['role'])
    elif l.startswith('ship_gate_ctl ') and ' to=none ' in l and (' why=gone' in l or ' why=order' in l):
        d = kv(l); launch[(d['ship'], d['gate'][-4:])].append(int(d['t_ms']) / 1000)
n = 0
for k, v in sorted(cr.items()):
    for t, role, o, f, lat in v:
        jl = any(0 <= x - t <= 30 for x in launch[k]); n += not jl
        print(k, '%.1f' % t, role, o, 'F', f, 'lat', lat, 'launch<=30s' if jl else 'NO launch')
print('crossings without a launch within 30 s:', n)
