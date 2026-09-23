"""Scratch PNG crops (full resolution) of the reprojection residual and of the frame's high-pass, contrast set by the
80th percentile of |value| so the dust-mote streaks saturate and faint structure shows. usage: residual_crop.py <png_dir> x0 y0 w h"""
import numpy as np, re, glob, sys
from PIL import Image
exec(open(__file__.replace('residual_crop.py', 'temporal_crawl.py')).read().split("print('run pair")[0])
out, x0, y0, w, h = sys.argv[1], *map(int, sys.argv[2:6])
for name, (d, logname) in RUNS.items():
    frames = sorted(int(re.search(r'_(\d+)\.', p).group(1)) for p in glob.glob(f'{d}/hdr_1_*.rgba16f'))
    fp, ft = frames[0], frames[1]
    cam, jit = rows(f'{d}/{logname}', {fp, ft})
    Lt, Lp = lum(d, ft), lum(d, fp)
    yy, xx = [a.reshape(-1).astype(float) for a in np.mgrid[y0:y0+h, x0:x0+w]]
    x2, y2, ok = reproject(cam[ft], cam[fp], jit[ft], jit[fp], ('RtT_Rp', 'row'), 0, yy, xx)
    ok &= (x2 >= 0) & (x2 < W-1) & (y2 >= 0) & (y2 < H-1)
    R = np.zeros(h*w); R[ok] = Lt[yy[ok].astype(int), xx[ok].astype(int)] - bilinear(Lp, x2[ok], y2[ok]); R = R.reshape(h, w)
    s = np.percentile(np.abs(R), 80)*3
    tag = name.split()[0]
    Image.fromarray(np.clip(128 + 127*R/s, 0, 255).astype(np.uint8)).save(f'{out}/{tag}_rescrop_{x0}_{y0}.png')
    c = Lt[y0:y0+h, x0:x0+w]; k = 16
    blur = np.cumsum(np.cumsum(np.pad(c, k), 0), 1); hp = c - (blur[2*k:2*k+h, 2*k:2*k+w] - blur[:h, 2*k:2*k+w] - blur[2*k:2*k+h, :w] + blur[:h, :w])/(4*k*k)
    s = np.percentile(np.abs(hp), 80)*3
    Image.fromarray(np.clip(128 + 127*hp/s, 0, 255).astype(np.uint8)).save(f'{out}/{tag}_hpcrop_{ft}_{x0}_{y0}.png')
    print(tag, fp, ft, 'crop', x0, y0, w, h, 'residual p80', np.percentile(np.abs(R), 80), 'hp p80', np.percentile(np.abs(hp), 80))
