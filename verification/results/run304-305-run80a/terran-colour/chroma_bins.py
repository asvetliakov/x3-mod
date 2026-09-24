"""Chromaticity of the tower-only screen columns of the two run305 bursts (ods_rects_run305.txt), geometry pixels only
(depth_1 .x in [0,1)), by luminance bin, mean over the 8 frames: share of px, mean RGB, b/r ratio. Tower record 0 in 12714,
record 1 in 13309. Usage: python3 chroma_bins.py"""
import numpy as np
CAP = '/tmp/x3-bottleX3-run305'
REG = {12714: (2833, 3322, 386, 871), 13309: (2531, 2926, 406, 837)}
BINS = [(0, 0.02), (0.02, 0.06), (0.06, 0.15), (0.15, 0.4), (0.4, 100)]
for first, (x0, x1, y0, y1) in REG.items():
    acc = {bn: [] for bn in BINS}
    for f in range(first, first + 8):
        a = np.nan_to_num(np.fromfile(f'{CAP}/hdr_1_{f}.rgba16f', np.float16).reshape(1440, 5120, 4)[y0:y1, x0:x1, :3].astype(np.float32))
        d = np.fromfile(f'{CAP}/depth_1_{f}.rgba32f', np.float32).reshape(1440, 5120, 4)[y0:y1, x0:x1, 0]
        geo = (d >= 0) & (d < 1); r, g, b = a[..., 0], a[..., 1], a[..., 2]
        red = (r > 1.5 * g) & (r > 1.5 * b); lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
        for lo, hi in BINS:
            m = geo & ~red & (lum >= lo) & (lum < hi)
            acc[(lo, hi)].append((m.sum() / geo.sum(), *(a[m].mean(0) if m.any() else (np.nan,) * 3)))
    print(f'burst {first} lod={"0" if first == 12714 else "1"} (tower-only columns, non-red geometry px)')
    for bn, v in acc.items():
        s, r, g, b = np.nanmean(v, 0)
        print(f'  lum {bn[0]}-{bn[1]}: share={s:.3f} rgb=({r:.4f},{g:.4f},{b:.4f}) b/r={b / r:.3f}')
