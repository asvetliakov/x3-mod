"""Scratch PNG: reprojection residual of the first burst pair in a crop, fixed absolute scale (+-S luminance -> 0..255),
nearest-upscaled by Z; prints residual rms and the rms of the residual after removing a 9x9 local mean (fine structure).
usage: residual_zoom.py <png_dir> run(289|290) x0 y0 w h S Z"""
import numpy as np, re, glob, sys
from PIL import Image
exec(open(__file__.replace('residual_zoom.py', 'temporal_crawl.py')).read().split("print('run pair")[0])
out, run, x0, y0, w, h = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:7]); S = float(sys.argv[7]); Z = int(sys.argv[8])
name = [n for n in RUNS if run in n][0]; d, logname = RUNS[name]
frames = sorted(int(re.search(r'_(\d+)\.', p).group(1)) for p in glob.glob(f'{d}/hdr_1_*.rgba16f'))
fp, ft = frames[0], frames[1]; cam, jit = rows(f'{d}/{logname}', {fp, ft}); Lt, Lp = lum(d, ft), lum(d, fp)
yy, xx = [a.reshape(-1).astype(float) for a in np.mgrid[y0:y0+h, x0:x0+w]]
x2, y2, ok = reproject(cam[ft], cam[fp], jit[ft], jit[fp], ('RtT_Rp', 'row'), 0, yy, xx)
R = (Lt[yy.astype(int), xx.astype(int)] - bilinear(Lp, x2, y2)).reshape(h, w)
img = np.clip(128 + 127*R/S, 0, 255).astype(np.uint8).repeat(Z, 0).repeat(Z, 1)
Image.fromarray(img).save(f'{out}/run{run}_zoom_{x0}_{y0}.png')
print(run, fp, ft, x0, y0, w, h, 'rms', np.sqrt(np.mean(R**2)), 'median|r|', np.median(np.abs(R)), 'mean L', Lt[y0:y0+h, x0:x0+w].mean())
