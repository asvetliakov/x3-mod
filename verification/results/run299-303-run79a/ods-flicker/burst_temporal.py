"""Temporal instability inside a static F8 burst (camera and objects fixed, only the TAA jitter changes): per-pixel
luminance of the eight hdr_1_<frame>.rgba16f captures (FP16 target read before the first write-back of the frame),
relative std over the burst, summarised per census body over the union of its draws' object_bounds rectangles in the
first burst frame. Pixels of a rectangle that belong to other objects are counted too (bbox attribution, stated).
usage: burst_temporal.py LOG CAPDIR FIRST_FRAME N [W H]"""
import sys, re, collections
import numpy as np
KV = re.compile(r'(\w+)=(\S*)')
log, cap, first, n = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
W, H = (int(sys.argv[5]), int(sys.argv[6])) if len(sys.argv) > 6 else (5120, 1440)
body = {}; rects = collections.defaultdict(list)
for line in open(log, errors='replace'):
    if line.startswith(f'cull_census device=1 frame={first} '):
        d = dict(KV.findall(line)); body[d['node']] = (d['body'].split('\\')[-1], d['lod'], d['s'])
    elif line.startswith(f'object_bounds device=1 frame={first} '):
        d = dict(KV.findall(line)); rects[d['node']].append(tuple(float(d[k]) for k in ('sx0', 'sy0', 'sx1', 'sy1')))
lum = []
for f in range(first, first + n):
    a = np.fromfile(f'{cap}/hdr_1_{f}.rgba16f', dtype=np.float16).reshape(H, W, 4).astype(np.float32)
    lum.append(0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2])
lum = np.stack(lum)
mean = lum.mean(0); std = lum.std(0)
# frame-to-frame absolute change relative to mean, max over consecutive pairs
step = np.abs(np.diff(lum, axis=0)).max(0)
rel = np.where(mean > 1e-3, std / np.maximum(mean, 1e-3), 0)
relstep = np.where(mean > 1e-3, step / np.maximum(mean, 1e-3), 0)
print(f'== {log} frames {first}..{first+n-1} whole-frame: px mean>1e-3 {(mean>1e-3).sum()} rel_std p50 {np.median(rel[mean>1e-3]):.4f} p99 {np.percentile(rel[mean>1e-3],99):.4f}')
rows = []
for node, rs in rects.items():
    m = np.zeros((H, W), bool)
    for x0, y0, x1, y1 in rs:
        x0, y0 = max(0, int(x0)), max(0, int(y0)); x1, y1 = min(W, int(np.ceil(x1))), min(H, int(np.ceil(y1)))
        if x1 > x0 and y1 > y0: m[y0:y1, x0:x1] = True
    m &= mean > 1e-3
    if m.sum() < 200: continue
    b = body.get(node, ('?', '?', '?'))
    rows.append((np.percentile(rel[m], 99), b[0], node, b[1], b[2], int(m.sum()), np.median(rel[m]), (rel[m] > 0.1).mean(), (relstep[m] > 0.25).mean()))
rows.sort(reverse=True)
print('rel_std_p99 body node lod s px rel_std_p50 share_relstd>0.1 share_step>0.25')
for r in rows[:25]:
    print(f'{r[0]:.4f} {r[1]} {r[2]} lod={r[3]} s={r[4]} px={r[5]} p50={r[6]:.4f} >0.1:{r[7]:.4f} step>0.25:{r[8]:.4f}')
ods = [r for r in rows if 'usc_dock_e' in r[1]]
print('ODS rows:'); [print(f'  {r[0]:.4f} {r[1]} lod={r[3]} s={r[4]} px={r[5]} p50={r[6]:.4f} >0.1:{r[7]:.4f} step>0.25:{r[8]:.4f} rank={rows.index(r)+1}/{len(rows)}') for r in ods]
