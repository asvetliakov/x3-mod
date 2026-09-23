"""run289 vs run290: rotational-reprojection residual in the fogged sky by spatial scale (box means of k x k px),
plus scratch PNGs (residual, contrast-stretched; frame luminance, local-contrast) for one pair per run.
PNGs go to the directory given as argv[1] (scratch, untracked). usage: residual_bands.py <png_dir>"""
import numpy as np, re, glob, sys
from PIL import Image
exec(open(__file__.replace('residual_bands.py', 'temporal_crawl.py')).read().split("print('run pair")[0])
out = sys.argv[1]
def box(a, k):
    h, w = a.shape[0]//k*k, a.shape[1]//k*k
    return np.nanmean(a[:h, :w].reshape(h//k, k, w//k, k), (1, 3))
print('run pair rotdeg | residual rms at box 1 4 8 16 32 64 px (luminance) | per degree box16 box32')
for name, (d, logname) in RUNS.items():
    frames = sorted(int(re.search(r'_(\d+)\.', p).group(1)) for p in glob.glob(f'{d}/hdr_1_*.rgba16f'))
    cam, jit = rows(f'{d}/{logname}', set(frames))
    yy, xx = [a.reshape(-1).astype(float) for a in np.mgrid[0:H, 0:W]]
    for i, (fp, ft) in enumerate(zip(frames, frames[1:])):
        Lt, Lp = lum(d, ft), lum(d, fp); St, Sp = sky(d, ft), sky(d, fp)
        x2, y2, ok = reproject(cam[ft], cam[fp], jit[ft], jit[fp], ('RtT_Rp', 'row'), 0, yy, xx)
        ok &= (x2 >= 0) & (x2 < W-1) & (y2 >= 0) & (y2 < H-1)
        ok[ok] &= Sp[np.round(y2[ok]).astype(int), np.round(x2[ok]).astype(int)]
        Wp = np.full(H*W, np.nan); Wp[ok] = bilinear(Lp, x2[ok], y2[ok]); R = (Lt - Wp.reshape(H, W)); R[~St] = np.nan
        tr = np.trace(cam[ft][2] @ cam[fp][2].T); rot = np.degrees(np.arccos(np.clip((tr - 1)/2, -1, 1)))
        v = [np.sqrt(np.nanmean(box(R, k)**2)) for k in (1, 4, 8, 16, 32, 64)]
        print(f"{name} {fp}->{ft} {rot:.2f} | {' '.join(f'{x:.2e}' for x in v)} | {v[3]/rot:.2e} {v[4]/rot:.2e}")
        if i == 0:
            tag = name.split()[0]
            r4 = box(R, 2); s = np.nanpercentile(np.abs(r4), 99)
            Image.fromarray(np.nan_to_num(np.clip(128 + 127*r4/s, 0, 255), nan=0).astype(np.uint8)).save(f'{out}/{tag}_residual_{fp}_{ft}.png')
            # local contrast of the frame: L minus a 64-px box blur, stretched
            b = np.repeat(np.repeat(box(np.where(St, Lt, np.nan), 64), 64, 0), 64, 1)
            hp = np.where(St[:b.shape[0], :b.shape[1]], Lt[:b.shape[0], :b.shape[1]] - b, np.nan); hp = box(hp, 2)
            s = np.nanpercentile(np.abs(hp), 99)
            Image.fromarray(np.nan_to_num(np.clip(128 + 127*hp/s, 0, 255), nan=0).astype(np.uint8)).save(f'{out}/{tag}_highpass_{ft}.png')
            g = np.clip((Lt/np.percentile(Lt, 99.5))**(1/2.2)*255, 0, 255)
            Image.fromarray(g[::2, ::2].astype(np.uint8)).save(f'{out}/{tag}_lum_{ft}.png')
