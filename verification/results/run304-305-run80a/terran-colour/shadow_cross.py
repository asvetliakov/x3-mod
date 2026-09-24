"""Caster change vs view change for the ODS tower's shadowed pixels (run305). For the geometry pixels of a screen rectangle
in frame B, the sun-space position in cascade K of B (rows<K> of sun_shadow_apply_params) is carried to frame A's cascade K
map through the logged basis centres (shadow_map_diff.py: s_A = s_B + (c_B - c_A).axis / extent, depth + (c_B - c_A).f / R;
same axes both frames), and the point is tested against both maps with bias_max<K>. A point shadowed in one map and lit in
the other has an occluder present in only one frame's casters: the station is static (caster origins equal to 1e-3), so
the difference is the tower's record (0 in 12714, 1 in 13309), not the view. Red pixels (crop_hdr.py rule) reported apart.
usage: shadow_cross.py CAPDIR FRAME_B FRAME_A K x0 y0 x1 y1 [OUT.png]"""
import sys, re, glob, subprocess
import numpy as np
D = sys.argv[1].rstrip('/') + '/'; FB, FA, K = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]); x0, y0, x1, y1 = map(int, sys.argv[5:9])
log = glob.glob(D + 'session-*.log')[0]
def grep(pat): return subprocess.run(['grep', '-m8', '-E', pat, log], capture_output=True, text=True).stdout
def kvs(line): return dict(re.findall(r'(\w+)=([-\w.+,]+)', line))
pb = kvs(grep(r'^sun_shadow_apply_params device=1 frame=%d ' % FB))
bas = {}
for f in (FA, FB):
    b = kvs(grep(r'^shadow_replay_map_basis device=1 frame=%d cascade=%d ' % (f, K)))
    bas[f] = {k: np.array(b[k].split(','), float) for k in ('right', 'up', 'forward', 'center')}
    bas[f].update(E=float(b['extent']), R=float(b['depth_light']) + float(b['depth_behind']), size=int(b['size']))
W, H = int(pb['width']), int(pb['height']); m00, m11, m20, m21, m22, m32 = (float(pb[k]) for k in ('m00', 'm11', 'm20', 'm21', 'm22', 'm32'))
rows = np.array(pb['rows%d' % K].split(','), float).reshape(3, 4); bias = float(pb['bias_max%d' % K]); n = bas[FB]['size']
ds = np.fromfile(D + 'depth_1_%d.rgba32f' % FB, np.float32).reshape(H, W, 4)[y0:y1, x0:x1].astype(np.float64)
geo = (ds[..., 0] >= 0) & (ds[..., 0] < 1)
with np.errstate(all='ignore'):
    z = np.where(geo, np.where(ds[..., 2] > 0, ds[..., 2], m32 / (ds[..., 0] - m22)), 0)
ys, xs = np.mgrid[y0:y1, x0:x1]
ray = np.stack([((2 * (xs + .5) / W - 1) - m20) / m00, ((1 - 2 * (ys + .5) / H) - m21) / m11, np.ones(xs.shape)], -1)
s = np.concatenate([ray * z[..., None], np.ones(xs.shape + (1,))], -1) @ rows.T
d = bas[FB]['center'] - bas[FA]['center']; E, R = bas[FB]['E'], bas[FB]['R']
sA = s + np.array([d @ bas[FB]['right'] / E, d @ bas[FB]['up'] / E, d @ bas[FB]['forward'] / R])
def vis(f, t):
    m = np.fromfile(D + 'shadow_map%d_1_%d.r32f' % (K, f), np.float32).reshape(n, n)
    u = np.clip(np.floor(((t[..., 0] * .5 + .5) + .5 / n) * n), 0, n - 1).astype(int); v = np.clip(np.floor(((-t[..., 1] * .5 + .5) + .5 / n) * n), 0, n - 1).astype(int)
    return m[v, u] >= t[..., 2] - bias
ins = geo & (np.maximum(np.abs(s[..., 0]), np.abs(s[..., 1])) <= 1) & (np.maximum(np.abs(sA[..., 0]), np.abs(sA[..., 1])) <= 1)
VB, VA = vis(FB, s), vis(FA, sA)
a = np.nan_to_num(np.fromfile(D + 'hdr_1_%d.rgba16f' % FB, np.float16).reshape(H, W, 4)[y0:y1, x0:x1, :3].astype(np.float32))
r, g, b = a[..., 0], a[..., 1], a[..., 2]; red = ins & ((r + g + b) > 0.05) & (r > 1.5 * g) & (r > 1.5 * b)
for name, m in (('all', ins), ('red', red)):
    print(f'B={FB} A={FA} cascade {K} rect {x0},{y0}-{x1},{y1} {name}: px={int(m.sum())} shadowed_B={int((m & ~VB).sum())} shadowed_A={int((m & ~VA).sum())}'
          f' shadowed_B_lit_A={int((m & ~VB & VA).sum())} lit_B_shadowed_A={int((m & VB & ~VA).sum())} centre_shift={np.round(d, 1).tolist()}')
if len(sys.argv) > 9:  # optional PNG: HDR crop above, mask below (green lit in both, magenta shadowed in both, yellow shadowed only in B, cyan only in A)
    from PIL import Image
    t = (np.clip(a / (1 + a), 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)
    col = np.zeros(a.shape, np.uint8)
    col[ins & VB & VA] = (0, 150, 0); col[ins & ~VB & ~VA] = (160, 0, 160); col[ins & ~VB & VA] = (255, 255, 0); col[ins & VB & ~VA] = (0, 255, 255)
    Image.fromarray(np.concatenate([t, col], 0)).resize(((x1 - x0) * 2, (y1 - y0) * 4), Image.NEAREST).save(sys.argv[9])
