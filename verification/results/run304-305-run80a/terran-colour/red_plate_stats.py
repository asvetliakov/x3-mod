"""Red-plate pixel statistics in the run305 HDR bursts (FP16 scene target, 5120x1440), in screen columns where only
one ODS part's object_bounds rectangle lies (ods_rects_run305.txt): tower (record 0 in 12714, record 1 in 13309) (the
upper_core-only columns hold no red geometry pixels, so there is no same-record control; the two views differ). Geometry pixels only (depth_1 .x in [0,1)). Red = r>1.5g and r>1.5b and r+g+b>0.05 (crop_hdr.py rule).
Prints per burst the mean over its 8 frames of: lit px, red px, red-pixel mean RGB, and red-pixel luminance p50/p90."""
import numpy as np
CAP = '/tmp/x3-bottleX3-run305'
REG = {  # (x0, x1, y0, y1) exclusive columns; y from the part's own rectangle
    (12714, 'tower'): (2833, 3322, 386, 871), (13309, 'tower'): (2531, 2926, 406, 837),
}
for (first, part), (x0, x1, y0, y1) in REG.items():
    acc = []
    for f in range(first, first + 8):
        a = np.fromfile(f'{CAP}/hdr_1_{f}.rgba16f', np.float16).reshape(1440, 5120, 4)[y0:y1, x0:x1, :3].astype(np.float32)
        a = np.nan_to_num(a); r, g, b = a[..., 0], a[..., 1], a[..., 2]
        dep = np.fromfile(f'{CAP}/depth_1_{f}.rgba32f', np.float32).reshape(1440, 5120, 4)[y0:y1, x0:x1, 0]
        lit = ((r + g + b) > 0.05) & (dep >= 0) & (dep < 1); red = lit & (r > 1.5 * g) & (r > 1.5 * b)
        lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
        acc.append((lit.sum(), red.sum(), *a[red].mean(0), np.percentile(lum[red], 50), np.percentile(lum[red], 90),
                    *a[lit & ~red].mean(0)))
    m = np.mean(acc, 0)
    print(f'{part:10s} burst {first} lod={"0" if (part, first) == ("tower", 12714) else "1"} lit={m[0]:.0f} red={m[1]:.0f}'
          f' red_share={m[1]/m[0]:.4f} red_rgb=({m[2]:.3f},{m[3]:.3f},{m[4]:.3f}) red_lum_p50={m[5]:.4f} p90={m[6]:.4f}'
          f' nonred_rgb=({m[7]:.3f},{m[8]:.3f},{m[9]:.3f})')
