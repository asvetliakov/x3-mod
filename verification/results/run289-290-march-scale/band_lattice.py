"""Pitch and phase of the dot lattice in the reprojection residual of the shaft band (first burst pair per run).
Residual R (as temporal_crawl.py); motes/stars removed by clipping |R| at 5x the median |R| of the region.
Prints the 2-D autocorrelation peaks (lags 3..40 px) and the band RMS vs a background region. usage: band_lattice.py run x0 y0 w h  bx0 by0 (background region, same w h)"""
import numpy as np, re, glob, sys
exec(open(__file__.replace('band_lattice.py', 'temporal_crawl.py')).read().split("print('run pair")[0])
run, x0, y0, w, h, bx, by = sys.argv[1], *map(int, sys.argv[2:8])
name = [n for n in RUNS if run in n][0]; d, logname = RUNS[name]
frames = sorted(int(re.search(r'_(\d+)\.', p).group(1)) for p in glob.glob(f'{d}/hdr_1_*.rgba16f'))
def resid(fp, ft, x0, y0):
    cam, jit = rows(f'{d}/{logname}', {fp, ft}); Lt, Lp = lum(d, ft), lum(d, fp)
    yy, xx = [a.reshape(-1).astype(float) for a in np.mgrid[y0:y0+h, x0:x0+w]]
    x2, y2, ok = reproject(cam[ft], cam[fp], jit[ft], jit[fp], ('RtT_Rp', 'row'), 0, yy, xx)
    return (Lt[yy.astype(int), xx.astype(int)] - bilinear(Lp, x2, y2)).reshape(h, w)
for fp, ft in list(zip(frames, frames[1:]))[:3]:
    R = resid(fp, ft, x0, y0); B = resid(fp, ft, bx, by)
    c = 5*np.median(np.abs(R)); Rc = np.clip(R, -c, c) - np.clip(R, -c, c).mean()
    F = np.fft.fft2(Rc); ac = np.real(np.fft.ifft2(F*np.conj(F))); ac /= ac[0, 0]
    pk = sorted(((ac[dy % h, dx % w], dx, dy) for dx in range(-40, 41) for dy in range(0, 21) if max(abs(dx), dy) >= 3 and (dy > 0 or dx > 0) and abs(dx) < w//2 and dy < h//2), reverse=True)[:6]
    Bc = np.clip(B, -c, c)
    print(f"run{run} {fp}->{ft} band rms(clipped) {np.sqrt(np.mean(Rc**2)):.2e} background rms(clipped) {np.sqrt(np.mean((Bc-Bc.mean())**2)):.2e} clip {c:.2e}")
    print('  autocorr peaks at lag >= 3 (value,dx,dy):', ' '.join(f'({v:.2f},{dx},{dy})' for v, dx, dy in pk))
