"""Project run12 engine_draw nozzle origins and plume axes into the capture frames' camera (camera_state rows).
Prints per record: NDC x,y, view z, cos(view dir, plume axis) (1 = looking straight down the plume into the nozzle),
and the distance between the ship's nozzles in record units against the light reach 3 x value_eff."""
import sys, math
log = sys.argv[1]
frames = {int(a) for a in sys.argv[2].split(',')}
cams, draws = {}, {}
def kv(line):
    return dict(t.split('=', 1) for t in line.split()[1:] if '=' in t)
with open(log, errors='replace') as f:
    for line in f:
        k = line.split(' ', 1)[0]
        if k not in ('camera_state', 'engine_draw'): continue
        d = kv(line)
        if 'frame' not in d: continue
        fr = int(d['frame'])
        if fr not in frames: continue
        if k == 'camera_state': cams[fr] = d
        else: draws.setdefault(fr, []).append(d)
for fr in sorted(frames):
    c = cams.get(fr)
    if not c: continue
    R = [[float(c[f'r{i}{j}']) for j in range(3)] for i in range(3)]
    t = [float(x) for x in c['t'].split(',')]
    p00, p11 = float(c['p00']), float(c['p11'])
    # camera position: row-vector view p*R + t = 0 -> cam = -t R^T
    cam = [-sum(t[j] * R[i][j] for j in range(3)) for i in range(3)]
    fwd = [R[i][2] for i in range(3)]
    rows = []
    for d in draws.get(fr, []):
        if float(d['value_eff']) <= 0: continue
        for scale, tag in ((1.0, 'origin'),):
            o = [float(x) * scale for x in d['origin'].split(',')]
            v = [sum(o[i] * R[i][j] for i in range(3)) + t[j] for j in range(3)]
            ax = [float(x) for x in d['axis'].split(',')]
            to_cam = [cam[i] - o[i] for i in range(3)]
            n = math.sqrt(sum(x * x for x in to_cam)) or 1
            cosv = sum(ax[i] * to_cam[i] for i in range(3)) / n
            ndc = (v[0] * p00 / v[2], v[1] * p11 / v[2]) if v[2] else (float('nan'),) * 2
            rows.append((d, o, ax))
            print(f"frame={fr} {d['name'].split(chr(92))[-1]} handle={d['handle']} value_eff={d['value_eff']} view_z={v[2]:.0f} "
                  f"ndc=({ndc[0]:+.3f},{ndc[1]:+.3f}) cos_axis_to_cam={cosv:+.3f}")
    big = [(d, o, ax) for d, o, ax in rows if float(d['radius']) > 1000]
    for i in range(len(big)):
        for j in range(i + 1, len(big)):
            (d1, o1, a1), (d2, o2, a2) = big[i], big[j]
            v = float(d1['value_eff'])
            L = [o1[k] + a1[k] * 0.5 * v for k in range(3)]
            dist = math.dist(L, o2)
            print(f"frame={fr} light_at={d1['handle']} other={d2['handle']} |L-nozzle_other|={dist:.0f} reach={3 * v:.0f} "
                  f"plate_reach={1.0 * float(d2['value_eff']):.0f} nozzle_sep={math.dist(o1, o2):.0f}")
