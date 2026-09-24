"""Offline sun-shadow visibility of run305 burst pixels (approximation of the apply pass: first cascade whose box holds
the point, margin from sun_shadow_apply_params, hard compare against bias_max<k>; no PCF, no band blend). Reconstruction as
tools/analysis/fog_shader_twin.py: z from depth_1_<f>.rgba32f (.z view z when > 0, else m32/(d-m22)), camera-space point =
ray*z, rows<k> map it to sun NDC. Prints, for a screen rectangle, geometry px, visible share and cascade histogram, and for
red pixels (crop_hdr.py rule) the visible share; optional PNG (V grey, cascade tint) for the eye.
usage: shadow_vis.py CAPDIR FRAME x0 y0 x1 y1 [OUT.png]"""
import sys, re, glob, subprocess
import numpy as np
D, F = sys.argv[1].rstrip('/') + '/', int(sys.argv[2]); x0, y0, x1, y1 = map(int, sys.argv[3:7])
log = glob.glob(D + 'session-*.log')[0]
line = subprocess.run(['grep', '-m1', '-E', r'^sun_shadow_apply_params device=1 frame=%d ' % F, log], capture_output=True, text=True).stdout
kv = dict(re.findall(r'(\w+)=([-\w.+,]+)', line))
W, H = int(kv['width']), int(kv['height']); m00, m11, m20, m21, m22, m32 = (float(kv[k]) for k in ('m00', 'm11', 'm20', 'm21', 'm22', 'm32'))
margin = float(kv['margin'])
rows = [np.array(kv['rows%d' % i].split(','), np.float64).reshape(3, 4) for i in range(5)]
valid = [kv['valid%d' % i] == '1' for i in range(5)]; size = [int(kv['map%d' % i]) for i in range(5)]
bias = [float(kv['bias_max%d' % i]) for i in range(5)]
maps = [np.fromfile(D + 'shadow_map%d_1_%d.r32f' % (i, F), np.float32).reshape(size[i], size[i]) if valid[i] else None for i in range(5)]
ds = np.fromfile(D + 'depth_1_%d.rgba32f' % F, np.float32).reshape(H, W, 4)[y0:y1, x0:x1].astype(np.float64)
geo = (ds[..., 0] >= 0) & (ds[..., 0] < 1)
with np.errstate(all='ignore'):
    z = np.where(geo, np.where(ds[..., 2] > 0, ds[..., 2], m32 / (ds[..., 0] - m22)), 0)
ys, xs = np.mgrid[y0:y1, x0:x1]
ray = np.stack([((2 * (xs + .5) / W - 1) - m20) / m00, ((1 - 2 * (ys + .5) / H) - m21) / m11, np.ones(xs.shape)], -1)
P4 = np.concatenate([ray * z[..., None], np.ones(xs.shape + (1,))], -1)
V = np.ones(xs.shape); C = np.full(xs.shape, -1); todo = geo.copy()
for i in range(5):
    if not valid[i]: continue
    s = P4 @ rows[i].T
    ins = todo & (np.maximum(np.abs(s[..., 0]), np.abs(s[..., 1])) <= margin) & (s[..., 2] >= 0) & (s[..., 2] <= 1)
    u = np.clip(np.floor(((s[..., 0] * .5 + .5) + .5 / size[i]) * size[i]), 0, size[i] - 1).astype(np.int64)
    v = np.clip(np.floor(((-s[..., 1] * .5 + .5) + .5 / size[i]) * size[i]), 0, size[i] - 1).astype(np.int64)
    V = np.where(ins, (maps[i][v, u] >= s[..., 2] - bias[i]).astype(float), V); C = np.where(ins, i, C); todo &= ~ins
a = np.nan_to_num(np.fromfile(D + 'hdr_1_%d.rgba16f' % F, np.float16).reshape(H, W, 4)[y0:y1, x0:x1, :3].astype(np.float32))
r, g, b = a[..., 0], a[..., 1], a[..., 2]; red = geo & ((r + g + b) > 0.05) & (r > 1.5 * g) & (r > 1.5 * b)
print(f'frame {F} rect {x0},{y0}-{x1},{y1} geo_px={int(geo.sum())} visible_share={V[geo].mean():.4f}'
      f' cascades={dict(zip(*np.unique(C[geo], return_counts=True)))} z_p50={np.median(z[geo]):.0f}'
      f' red_px={int(red.sum())} red_visible_share={V[red].mean() if red.any() else float("nan"):.4f}'
      f' red_rgb_lit=({",".join("%.3f" % x for x in a[red & (V > 0)].mean(0)) if (red & (V > 0)).any() else "-"})'
      f' red_rgb_shadowed=({",".join("%.3f" % x for x in a[red & (V == 0)].mean(0)) if (red & (V == 0)).any() else "-"})')
if len(sys.argv) > 7:
    from PIL import Image
    t = (np.clip(a / (1 + a), 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)
    sh = np.where(geo[..., None], np.where((V > 0)[..., None], [0, 200, 0], [220, 0, 220]), [0, 0, 0]).astype(np.uint8)
    Image.fromarray(np.concatenate([t, sh], 0)).resize(((x1 - x0) * 2, (y1 - y0) * 4), Image.NEAREST).save(sys.argv[7])
