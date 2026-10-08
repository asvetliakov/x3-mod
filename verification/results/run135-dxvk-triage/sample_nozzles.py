"""Sample the F8 HDR capture (rgba16f, W x H) around each projected Ocelot nozzle origin (engine_draw + camera_state
of the same frame): box half-size = 0.5 x projected value_eff (px). Prints mean RGB, max luminance, and the share of
pixels with min(R,G,B) > 2 (white light-map gain 4 signature) per nozzle.
usage: sample_nozzles.py LOG CAPDIR frame[,frame...] [W H]"""
import sys, math, numpy as np
log, capdir = sys.argv[1], sys.argv[2]
frames = {int(a) for a in sys.argv[3].split(',')}
W, H = (int(sys.argv[4]), int(sys.argv[5])) if len(sys.argv) > 5 else (5120, 1440)
cams, draws = {}, {}
with open(log, errors='replace') as f:
    for line in f:
        k = line.split(' ', 1)[0]
        if k not in ('camera_state', 'engine_draw'): continue
        d = dict(t.split('=', 1) for t in line.split()[1:] if '=' in t)
        if 'frame' not in d or int(d['frame']) not in frames: continue
        fr = int(d['frame'])
        if k == 'camera_state': cams[fr] = d
        else: draws.setdefault(fr, []).append(d)
for fr in sorted(frames):
    c = cams[fr]
    R = [[float(c[f'r{i}{j}']) for j in range(3)] for i in range(3)]
    t = [float(x) for x in c['t'].split(',')]
    p00, p11 = float(c['p00']), float(c['p11'])
    img = np.fromfile(f'{capdir}/hdr_1_{fr}.rgba16f', dtype=np.float16).reshape(H, W, 4)[:, :, :3].astype(np.float32)
    for d in sorted(draws.get(fr, []), key=lambda d: d['handle']):
        if float(d['radius']) < 1000: continue
        o = [float(x) for x in d['origin'].split(',')]
        v = [sum(o[i] * R[i][j] for i in range(3)) + t[j] for j in range(3)]
        if v[2] <= 0: continue
        x = (v[0] * p00 / v[2] + 1) * 0.5 * W; y = (1 - v[1] * p11 / v[2]) * 0.5 * H
        half = max(2, int(0.5 * float(d['value_eff']) * p00 / v[2] * 0.5 * W))
        x0, x1, y0, y1 = int(x) - half, int(x) + half + 1, int(y) - half, int(y) + half + 1
        if x1 <= 0 or y1 <= 0 or x0 >= W or y0 >= H:
            print(f'frame={fr} handle={d["handle"]} px=({x:.0f},{y:.0f}) off_screen'); continue
        box = img[max(0, y0):min(H, y1), max(0, x0):min(W, x1)]
        lum = box @ np.array([.2126, .7152, .0722], np.float32)
        white = (box.min(axis=2) > 2).mean()
        m = box.reshape(-1, 3).mean(0)
        print(f'frame={fr} handle={d["handle"]} px=({x:.0f},{y:.0f}) half={half} mean_rgb={m[0]:.2f},{m[1]:.2f},{m[2]:.2f} '
              f'max_lum={lum.max():.2f} p90_lum={np.percentile(lum, 90):.2f} white_gt2={white:.3f}')
