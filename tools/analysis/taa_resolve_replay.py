#!/usr/bin/env python3
"""CPU replay of src/temporal/resolve.hlsl over a --taa-debug capture (numpy, crop of the frame).
usage: replay.py <dump dir> <x0> <y0> <x1> <y1> [validate|options|lattice|far|remedy]
lattice: docs/architecture/taa-lattice-crawl.md; the lattice mask, the masked current filter (resolve_lattice*.hlsl),
AgX + RCAS as agx.hlsl / rcas.hlsl apply them (HDR tonemapped write-back without bloom) and the sharpen exclusion."""
import re, sys, json, subprocess
import numpy as np
D = sys.argv[1].rstrip('/') + '/'; X0, Y0, X1, Y1 = map(int, sys.argv[2:6]); MODE = sys.argv[6] if len(sys.argv) > 6 else 'validate'
W, H = 1280, 768
LUMA = np.array([0.2126, 0.7152, 0.0722])
log = [p for p in __import__('os').listdir(D) if p.startswith('session-') and p.endswith('.log')][0]
frames = sorted(int(re.search(r'_(\d+)\.', p).group(1)) for p in __import__('os').listdir(D) if p.startswith('taa_1_'))
import os
if os.environ.get('FR'):
    lo_f, hi_f = map(int, os.environ['FR'].split('-')); frames = [f for f in frames if lo_f <= f <= hi_f]
lines = subprocess.run(['grep', '-E', r'^(motion_output_frame|camera_state) device=1 frame=(%s) ' % '|'.join(map(str, frames)), D + log], capture_output=True, text=True).stdout.splitlines()
meta = {f: {} for f in frames}
for l in lines:
    kv = dict(re.findall(r'(\w+)=([-\w.+]+)', l)); f = int(kv['frame'])
    if l.startswith('motion_output_frame'):
        meta[f].update(j=(float(kv['jitter_x']), float(kv['jitter_y'])), k=float(kv['taa_k']), hist=int(kv['taa_history']), policy=int(kv['camera_policy']), w=float(kv['taa_weight']), filt=float(kv['taa_filter']))
    else:
        meta[f].update(P=(float(kv['p00']), float(kv['p11']), float(kv['p20']), float(kv['p21'])), R=np.array([[float(kv['r%d%d' % (i, j)]) for j in range(3)] for i in range(3)]))

def load(kind, f, ext, dt, ch):
    data = np.fromfile(D + '%s_1_%d.%s' % (kind, f, ext), dtype=dt)
    if data.size != H * W * ch:  # the script assumes 1280x768 dumps (run142 / run148)
        raise SystemExit('%s_1_%d.%s: %d values, expected %dx%dx%d; this replay is fixed to 1280x768 captures' % (kind, f, ext, data.size, W, H, ch))
    return data.reshape(H, W, ch)
LINE_MARGIN = .1
FAR_P22, FAR_P32 = 1.000003, -6.000018  # the game's projection rows (taa-distant-line-fade.md section 4); camera_state logs p00 only
M = 8  # margin around the crop for taps
cy0, cy1, cx0, cx1 = Y0 - M, Y1 + M, X0 - M, X1 + M
ys, xs = np.mgrid[Y0:Y1, X0:X1]

def valid(d): return (d >= 0) & (d <= 1)
def weigh(c, k): return c / (1 + k * np.maximum(c @ LUMA, 0))[..., None]
def unweigh(c, k): return c / np.maximum(1 - k * np.maximum(c @ LUMA, 0), 1 / 65504.)[..., None]

def resolve(cur, dep, mot, hist, pdep, age, j, k, w, Rc, Rp, P, conv, opt):
    """All full-frame arrays (float64 / float32); returns crop output (h, w, 4), crop age, diagnostics."""
    jx, jy = j; h, wd = ys.shape
    c = cur[ys, xs, :3].astype(np.float64); alpha = cur[ys, xs, 3]
    dc = dep[ys, xs]; far = dc <= -.5
    nearest = np.where(far, 1., dc).astype(np.float64); dil = np.zeros((h, wd, 2), int)
    sawV = ~far; sawS = far.copy()
    for ky in (-1, 0, 1):
        for kx in (-1, 0, 1):
            if kx == 0 and ky == 0: continue
            nb = dep[ys + ky, xs + kx]; v = valid(nb); sawV |= v; sawS |= nb <= -.5
            take = v & (nb < nearest); nearest = np.where(take, nb, nearest); dil[take] = (kx, ky)
    thin = sawV & sawS
    glass = np.zeros_like(far)  # lattice mask: 3x3 holds (sentinel depth and motion alpha 1) and a valid depth
    for ky in (-1, 0, 1):
        for kx in (-1, 0, 1): glass |= (dep[ys + ky, xs + kx] <= -.5) & (mot[ys + ky, xs + kx, 3] == 1)
    lattice = glass & sawV
    # general line mask (depth only): geometry whose opposite neighbours along one of four directions are both background
    # (sentinel, or farther: (1 - q)(1 + LINE_MARGIN) < 1 - d), at distance 1 (line1) or 1 or 2 (line2: <= 2 px wide);
    # dual: a background pixel between two geometry pixels nearer than it (the gaps of a dense lattice)
    def bgof(q, d_): return (q <= -.5) | (valid(q) & ((1 - q) * (1 + LINE_MARGIN) < 1 - d_))
    yg, xg = np.mgrid[Y0 - 1:Y1 + 1, X0 - 1:X1 + 1]; dg_ = dep[yg, xg]; fg = dg_ <= -.5; dgv = np.where(fg, 1., dg_)  # one px wider for the 3x3 dilation
    line1 = np.zeros_like(fg); line2 = np.zeros_like(fg); dual = np.zeros_like(fg)
    hv = np.zeros_like(fg)
    for ax, ay in ((1, 0), (0, 1)): hv |= valid(dg_) & bgof(dep[yg - ay, xg - ax], dg_) & bgof(dep[yg + ay, xg + ax], dg_)
    for ax, ay in ((1, 0), (0, 1), (1, 1), (1, -1)):
        n = {s_: dep[yg + s_ * ay, xg + s_ * ax] for s_ in (-2, -1, 1, 2)}
        line1 |= valid(dg_) & bgof(n[-1], dg_) & bgof(n[1], dg_)
        line2 |= valid(dg_) & (bgof(n[-1], dg_) | bgof(n[-2], dg_)) & (bgof(n[1], dg_) | bgof(n[2], dg_))
        dual |= valid(n[-1]) & valid(n[1]) & bgof(dgv, n[-1]) & bgof(dgv, n[1])
    def grow(m): return sum(m[1 + ky:m.shape[0] - 1 + ky, 1 + kx:m.shape[1] - 1 + kx] for ky in (-1, 0, 1) for kx in (-1, 0, 1)) > 0
    linehvx = grow(hv)
    line1x, line2x, line1dx = grow(line1), grow(line2), grow(line1 | dual); line1, line2, dual = line1[1:-1, 1:-1], line2[1:-1, 1:-1], dual[1:-1, 1:-1]
    masks = dict(thin=thin, lattice=lattice, line1=line1, line2=line2, line1d=line1 | dual, line2d=line2 | dual, linehvx=linehvx, line1x=line1x, line2x=line2x, line1dx=line1dx, union=lattice | line1x)
    dx, dy = xs + dil[..., 0], ys + dil[..., 1]
    # camera path (far plane, rotation only)
    p00, p11, p20, p21 = P
    nx, ny = 2 * (dx - jx) / W - 1, 1 - 2 * (dy - jy) / H
    dv = np.stack([(nx - p20) / p00, (ny - p21) / p11, np.ones_like(nx)], -1)
    world = dv @ Rc if conv == 0 else dv @ Rc.T
    pv = world @ Rp.T if conv == 0 else world @ Rp
    okcam = pv[..., 2] > 1e-6
    pnx, pny = pv[..., 0] / pv[..., 2] * p00 + p20, pv[..., 1] / pv[..., 2] * p11 + p21
    posx, posy = (pnx * .5 + .5) * W + jx, (.5 - pny * .5) * H + jy
    exp = np.ones_like(posx); ok = okcam.copy()
    m = mot[dy, dx]
    routed = m[..., 3] == 1
    posx = np.where(routed, m[..., 0] * W + jx - .5, posx); posy = np.where(routed, m[..., 1] * H + jy - .5, posy)
    exp = np.where(routed, m[..., 2], exp); ok = np.where(routed, True, ok)
    other = ~routed & (m[..., 3] != 0)
    ok &= ~(other & ~(far & (dil[..., 0] == 0) & (dil[..., 1] == 0) & (m[..., 3] == -1)))
    posx -= dil[..., 0]; posy -= dil[..., 1]
    ok &= valid(exp) & (posx + .5 >= 0) & (posx + .5 <= W) & (posy + .5 >= 0) & (posy + .5 <= H)
    speed = np.hypot(posx - xs, posy - ys)
    bx, by = np.floor(posx), np.floor(posy); fx, fy = posx - bx, posy - by
    for b, f in ((bx, fx), (by, fy)):
        up = f > 1 - 1e-4; b[up] += 1; f[up] = 0; f[f < 1e-4] = 0
    bx = bx.astype(int); by = by.astype(int)
    tol = np.maximum(.0001, .02 * np.abs(exp)); cons = np.zeros_like(exp); prov = np.zeros_like(exp); prevValid = np.zeros_like(far)
    for ty in (0, 1):
        for tx in (0, 1):
            wt = (fx if tx else 1 - fx) * (fy if ty else 1 - fy); use = wt > .01
            pd = pdep[np.clip(by + ty, 0, H - 1), np.clip(bx + tx, 0, W - 1)]
            good = (valid(pd) & (pd >= exp - tol)) | ((pd <= -.5) & (pd >= -1e30))
            cons += np.where(use, wt, 0); prov += np.where(use & good, wt, 0); prevValid |= use & valid(pd)
    disocc = ok & (prov < cons - .001)
    remedy_px = far & ~sawV & prevValid
    if opt.get('remedy'): disocc &= ~remedy_px
    accept = ok & ~disocc
    # Catmull-Rom history
    def crw(f):
        f2, f3 = f * f, f * f * f
        return [-.5 * f + f2 - .5 * f3, 1 - 2.5 * f2 + 1.5 * f3, .5 * f + 2 * f2 - 1.5 * f3, -.5 * f2 + .5 * f3]
    wx, wy = crw(fx), crw(fy); acc = np.zeros((h, wd, 4)); tot = np.zeros((h, wd))
    for jj in range(4):
        for ii in range(4):
            wt = wx[ii] * wy[jj]
            if not wt.any(): continue
            t = hist[np.clip(by + jj - 1, 0, H - 1), np.clip(bx + ii - 1, 0, W - 1)].astype(np.float64)
            acc[..., :3] += weigh(t[..., :3], k) * wt[..., None]; acc[..., 3] += t[..., 3] * wt; tot += wt
    accept &= tot >= .5
    old = acc[..., :3] / np.where(tot == 0, 1, tot)[..., None]
    wc = weigh(c, k); lo = wc.copy(); hi = wc.copy(); mean = np.zeros_like(wc); sq = np.zeros_like(wc); filt = np.zeros_like(wc); ft = np.zeros((h, wd))
    A = opt.get('filter', 0.)
    for ny_ in (-1, 0, 1):
        for nx_ in (-1, 0, 1):
            nb = weigh(cur[ys + ny_, xs + nx_, :3].astype(np.float64), k)
            lo = np.minimum(lo, nb); hi = np.maximum(hi, nb); mean += nb / 9; sq += nb * nb / 9
            if A: g = np.exp(-A * ((nx_ - jx) ** 2 + (ny_ - jy) ** 2)); filt += nb * g; ft += g
    sig = np.sqrt(np.maximum(sq - mean * mean, 0)); lo = np.maximum(lo, mean - 1.25 * sig); hi = np.minimum(hi, mean + 1.25 * sig)
    clamped = np.clip(old, lo, hi)
    moved = np.abs(clamped - old).max(-1)  # clamp distance, weighted domain
    S = opt.get('thin', 0.); mask = thin | (remedy_px if opt.get('remedy') else False)
    if opt.get('allthin'): mask = np.ones_like(thin)
    soft = np.where(mask, S * (1 - np.clip((speed - 2) * .5, 0, 1)), 0.)
    old = clamped + soft[..., None] * (old - clamped)
    keep = np.full((h, wd), w)
    newage = np.ones((h, wd))
    if opt.get('wmax'):
        a = age[np.clip(by + (fy >= .5), 0, H - 1) - Y0 + 0, 0] if False else None
        ay = np.clip(by + (fy >= .5) - Y0, 0, h - 1); ax = np.clip(bx + (fx >= .5) - X0, 0, wd - 1)
        a = age[ay, ax]; a = np.where((a >= 1) & (a <= 64), a, 1)
        t = np.clip((speed - opt['lo']) / (opt['hi'] - opt['lo']), 0, 1)
        keep = np.minimum(a / (a + 1), opt['wmax'] + t * (w - opt['wmax'])); newage = np.where(accept, np.minimum(a + 1, 64), 1)
    farw = np.zeros((h, wd))
    if opt.get('far'):  # docs/architecture/taa-distant-line-fade.md: farw = saturate((d - d0) * inv), 0 on the sentinel; F1 <= F0: 1 everywhere
        F0, F1, WF, AF = opt['far']; dof = lambda F: FAR_P22 + FAR_P32 / (F * P[0] * W / 2)
        farw = np.where(far, 0., np.clip((dc - dof(F0)) / (dof(F1) - dof(F0)), 0, 1)) if F1 > F0 else np.ones((h, wd))
        if WF:
            ay = np.clip(by + (fy >= .5) - Y0, 0, h - 1); ax = np.clip(bx + (fx >= .5) - X0, 0, wd - 1); a = age[ay, ax]; a = np.where((a >= 1) & (a <= 64), a, 1)
            keep = w + farw * (1 - np.clip((speed - .5) / 1.5, 0, 1)) * (np.minimum(a / (a + 1), WF) - w); newage = np.where(accept, np.minimum(a + 1, 64), 1)
        if AF:
            g9 = np.zeros_like(wc); gt = 0.
            for ny_ in (-1, 0, 1):
                for nx_ in (-1, 0, 1): g = np.exp(-AF * ((nx_ - jx) ** 2 + (ny_ - jy) ** 2)); g9 += weigh(cur[ys + ny_, xs + nx_, :3].astype(np.float64), k) * g; gt += g
            wc = wc + farw[..., None] * (g9 / gt - wc)
    blendc = filt / ft[..., None] if A else wc
    if A and opt.get('fmask'): blendc = np.where(masks[opt['fmask']][..., None], blendc, wc)
    # Remedy candidates of taa-lattice-crawl.md section 10, all on masks[opt['rmask']] (default line2x):
    rm = masks[opt.get('rmask', 'line2x')]
    def wtap(oy, ox):  # weighted current colour at a fractional offset, bilinear over the full frame
        yy = ys + oy; xx = xs + ox; y0_ = np.floor(yy).astype(int); x0_ = np.floor(xx).astype(int); fy_ = (yy - y0_)[..., None]; fx_ = (xx - x0_)[..., None]
        t = lambda a_, b_: weigh(cur[np.clip(a_, 0, H - 1), np.clip(b_, 0, W - 1), :3].astype(np.float64), k)
        return (t(y0_, x0_) * (1 - fx_) + t(y0_, x0_ + 1) * fx_) * (1 - fy_) + (t(y0_ + 1, x0_) * (1 - fx_) + t(y0_ + 1, x0_ + 1) * fx_) * fy_
    if opt.get('wide'):  # 5x5 isotropic Gaussian exp(-A d^2) about the jittered sample position
        acc_ = np.zeros_like(wc); tot_ = 0.
        for oy in range(-2, 3):
            for ox in range(-2, 3): g = np.exp(-opt['wide'] * ((ox - jx) ** 2 + (oy - jy) ** 2)); acc_ += wtap(oy, ox) * g; tot_ += g
        blendc = np.where(rm[..., None], acc_ / tot_, blendc)
    if opt.get('along'):  # 1-D Gaussian ALONG the local line direction (structure tensor of the 5x5 depth-validity image), sigma px, +-3 taps
        sigma, across = opt['along']; ind = valid(dep).astype(np.float64); gy_, gx_ = np.gradient(ind); J = lambda a_: sum(a_[ys + oy, xs + ox] for oy in range(-2, 3) for ox in range(-2, 3))
        jxx, jyy, jxy = J(gx_ * gx_), J(gy_ * gy_), J(gx_ * gy_); th = .5 * np.arctan2(2 * jxy, jxx - jyy) + np.pi / 2; dxl, dyl = np.cos(th), np.sin(th)  # line direction = across-gradient + 90 deg
        coh = np.hypot(jxx - jyy, 2 * jxy) / np.maximum(jxx + jyy, 1e-9); acc_ = np.zeros_like(wc); tot_ = 0.
        for t_ in range(-3, 4):
            for u_ in ((-1, 0, 1) if across else (0,)):
                g = np.exp(-.5 * (t_ / sigma) ** 2) * (np.exp(-across * u_ * u_) if across else 1.); acc_ += wtap(t_ * dyl + u_ * dxl - jy * 0, t_ * dxl - u_ * dyl) * g; tot_ += g
        blendc = np.where((rm & (coh > opt.get('coh', .3)))[..., None], acc_ / tot_, blendc)
    if opt.get('dim'):  # coverage-to-alpha style: the current sample of line pixels pulled toward the 3x3 minimum (the background) by dim
        blendc = np.where(rm[..., None], blendc + opt['dim'] * (lo - blendc), blendc)
    if opt.get('wmask'): keep = np.where(rm, opt['wmask'], keep)
    out = unweigh(blendc + keep[..., None] * (old - blendc), k)
    out = np.where(accept[..., None], out, c)
    res = np.concatenate([out, alpha[..., None]], -1).astype(np.float16)
    return res, newage, dict(far=far, thin=thin, disocc=disocc, accept=accept, moved=moved, validc=~far, speed=speed, remedy_px=remedy_px, oob=~ok, lattice=lattice, masks=masks, farw=farw)

def run(opt, conv, nframes=None, seed_each=False):
    outs = []; diags = []; age = np.full(ys.shape, 64.)  # steady state: the capture starts on a long-lived history
    hist = load('taa', frames[0], 'rgba16f', np.float16, 4).astype(np.float32)
    pdep = load('depth', frames[0], 'rgba32f', np.float32, 4)[..., 0].copy()
    for f in frames[1:nframes]:
        cur = load('hdr', f, 'rgba16f', np.float16, 4).astype(np.float32); dep = load('depth', f, 'rgba32f', np.float32, 4)[..., 0].copy(); mot = load('motion', f, 'rgba32f', np.float32, 4)
        m = meta[f]; mp = meta[f - 1]
        res, age, dg = resolve(cur, dep, mot, hist, pdep, age, m['j'], m['k'], m['w'], m['R'], mp['R'], m['P'], conv, opt)
        outs.append(res); diags.append(dg)
        hist = load('taa', f, 'rgba16f', np.float16, 4).astype(np.float32)   # outside the crop: the dumped resolve
        if not seed_each: hist[Y0:Y1, X0:X1] = res.astype(np.float32)
        pdep = dep
    return outs, diags, age

def codes(img, k):  # display-relative bounded luma in 8-bit codes
    l = np.maximum(img[..., :3].astype(np.float64) @ LUMA, 0); return 255 * k * l / (1 + k * l)

if MODE == 'validate':
    for conv in (0, 1):
        outs, diags, _ = run({}, conv, nframes=6)
        for i, f in enumerate(frames[1:6]):
            ref = load('taa', f, 'rgba16f', np.float16, 4)[Y0:Y1, X0:X1]; k = meta[f]['k']
            e = np.abs(codes(outs[i], k) - codes(ref, k)); far = diags[i]['far']
            print('conv', conv, 'frame', f, 'hist', meta[f]['hist'], 'policy', meta[f]['policy'], 'max %.3f mean %.4f p99 %.3f | geometry px max %.3f mean %.4f | far px max %.3f mean %.4f | >1code %d of %d' % (
                e.max(), e.mean(), np.percentile(e, 99), e[~far].max(), e[~far].mean(), e[far].max(), e[far].mean(), (e > 1).sum(), e.size))

if MODE == 'options':
    conv = 1
    deps = np.stack([load('depth', f, 'rgba32f', np.float32, 4)[Y0 - 1:Y1 + 1, X0 - 1:X1 + 1, 0] for f in frames])
    v = valid(deps); vc = v[:, 1:-1, 1:-1]
    flip = vc.any(0) & ~vc.all(0)
    allv = np.ones_like(flip)
    for dy in (0, 1, 2):
        for dx in (0, 1, 2): allv &= v[:, dy:dy + vc.shape[1], dx:dx + vc.shape[2]].all(0)
    print('crop px', flip.size, 'flip px', int(flip.sum()), 'stable interior px', int(allv.sum()))
    dv = deps[v]; print('valid depth min/median/max %.5f %.5f %.5f; far-plane disocclusion threshold 1 - 0.02 = 0.98 -> previous depth below it: %.6f of valid' % (dv.min(), np.median(dv), dv.max(), (dv < .98).mean()))
    configs = [('base', {}), ('no clip (bound)', dict(thin=1., allthin=True)), ('thin0.75', dict(thin=.75)), ('thin+w0.97 narrow', dict(thin=.75, wmax=.97, lo=.1, hi=.5)), ('thin+w0.97 wide', dict(thin=.75, wmax=.97, lo=.8, hi=1.5)),
               ('filter1.0', dict(filter=1.)), ('remedy+thin0.75', dict(thin=.75, remedy=True)), ('remedy+thin+w0.97 wide', dict(thin=.75, remedy=True, wmax=.97, lo=.8, hi=1.5)),
               ('w0.97 wide no thin', dict(wmax=.97, lo=.8, hi=1.5))]
    if os.environ.get('ONLY'): configs = [c for c in configs if c[0] == 'base' or c[0] in os.environ['ONLY'].split('|')]  # ONLY='thin+w0.97 narrow'
    def bands(series):  # (N, n) -> rms per band, codes
        N = series.shape[0]; X = np.fft.rfft(series - series.mean(0), axis=0); p = 2 * np.abs(X) ** 2 / N ** 2
        per = np.array([N / k if k else np.inf for k in range(X.shape[0])]); out = []
        for lo_, hi_ in ((2, 4), (4, 8), (8, 32)):
            sel = (per >= lo_ if lo_ == 2 else per > lo_) & (per <= hi_); out.append(float(np.sqrt(p[sel].sum(0).mean())))
        return out
    h, wd = flip.shape; bh, bw = h // 8, wd // 8
    blockmask = flip[:bh * 8, :bw * 8].reshape(bh, 8, bw, 8).sum((1, 3)) >= 8
    base = None
    raw = np.nan_to_num(np.stack([codes(load('hdr', f, 'rgba16f', np.float16, 4)[Y0:Y1, X0:X1], meta[f]['k']) for f in frames[1:]]))
    rb = raw[:, :bh * 8, :bw * 8].reshape(raw.shape[0], bh, 8, bw, 8).mean((2, 4))
    print('RAW INPUT (unresolved) px bands %s block bands %s' % (['%.2f' % b for b in bands(raw[:, flip])], ['%.2f' % b for b in bands(rb[:, blockmask])]))
    ks = [meta[f]['k'] for f in frames]; print('k range %.4f..%.4f' % (min(ks), max(ks)))
    for name, opt in configs:
        outs, diags, age = run(opt, conv)
        img = np.nan_to_num(np.stack([codes(o, meta[f]['k']) for o, f in zip(outs, frames[1:])]))
        pb = bands(img[:, flip]); blk = img[:, :bh * 8, :bw * 8].reshape(len(outs), bh, 8, bw, 8).mean((2, 4)); bb = bands(blk[:, blockmask])
        contrast = float(np.mean([im[flip].std() for im in img[8:]]))
        gx = np.diff(img, axis=2)[:, allv[:, 1:] & allv[:, :-1]]; sharp = float((gx ** 2).mean())
        if name == 'base':
            base = dict(contrast=contrast, sharp=sharp)
            ref = np.nan_to_num(np.stack([codes(load('taa', f, 'rgba16f', np.float16, 4)[Y0:Y1, X0:X1], meta[f]['k']) for f in frames[1:]])); e = np.abs(img - ref)
            print('VALIDATION 31 frames free-running: max %.3f mean %.4f codes; flip px max %.3f mean %.4f; last frame max %.3f mean %.4f; dumped-resolve bands on flip px %s' % (e.max(), e.mean(), e[:, flip].max(), e[:, flip].mean(), e[-1].max(), e[-1].mean(), ['%.2f' % b for b in bands(ref[:, flip])]))
            tot = dict(n=0, far=0, miss=0, thin=0, disocc=0, oob=0, moved=0, movedsum=0., miss_moved=0, miss_movedsum=0., remedy=0, speed=[])
            for dg in diags:
                F = flip; tot['n'] += F.sum(); tot['far'] += (dg['far'] & F).sum(); miss = dg['far'] & ~dg['thin'] & F; tot['miss'] += miss.sum(); tot['thin'] += (dg['thin'] & F).sum()
                tot['disocc'] += (dg['disocc'] & F).sum(); tot['oob'] += (dg['oob'] & F).sum(); mv = dg['accept'] & (dg['moved'] * 255 > 1)
                tot['moved'] += (mv & F).sum(); tot['movedsum'] += (dg['moved'] * 255)[mv & F].sum(); tot['miss_moved'] += (mv & miss).sum(); tot['miss_movedsum'] += (dg['moved'] * 255)[mv & miss].sum()
                dil = dg['thin'] & F; tot.setdefault('gate', []).append(float((dg['speed'][dil] > .1).mean()))  # thin px whose screen speed passes the default LO
                tot['remedy'] += (dg['remedy_px'] & F).sum(); tot['speed'].append(float(np.median(dg['speed'][F & ~dg['far']])))
            n = tot['n']
            print('SPEED_GATE share of thin flip px with screen speed > 0.1 px/frame (default LO): %.4f; > 0.5 (HI): see median speed' % float(np.mean(tot['gate'])))
            print('CLASSIFY flip px-frames %d: far(sentinel centre) %.3f; total-miss (far, no geometry in 3x3) %.3f; thin mask %.3f; disocclusion-rejected %.5f; other reject %.5f; clamp moved >1 code %.3f (mean move %.1f codes); among total-miss: clamp moved %.3f (mean %.1f codes); previous-depth-valid total-miss %.3f; median strut speed %.2f px/frame' % (
                n, tot['far'] / n, tot['miss'] / n, tot['thin'] / n, tot['disocc'] / n, tot['oob'] / n, tot['moved'] / n, tot['movedsum'] / max(tot['moved'], 1), tot['miss_moved'] / max(tot['miss'], 1), tot['miss_movedsum'] / max(tot['miss_moved'], 1), tot['remedy'] / n, float(np.median(tot['speed']))))
        line = 'OPTION %-24s px p2-4/4-8/8-32 %5.2f %5.2f %5.2f | block %5.2f %5.2f %5.2f | contrast x%.3f | stable-px sharpness x%.4f' % (name, *pb, *bb, contrast / base['contrast'], sharp / base['sharp'])
        if opt.get('wmax'):
            a = age[flip]; line += ' | age on flip px: 1:%.2f 2-3:%.2f 4-7:%.2f 8-15:%.2f 16-31:%.2f 32+:%.2f' % tuple(((a >= lo_) & (a < hi_)).mean() for lo_, hi_ in ((1, 2), (2, 4), (4, 8), (8, 16), (16, 32), (32, 99)))
        print(line, flush=True)

# --- lattice crawl (docs/architecture/taa-lattice-crawl.md) -----------------------------------------------------------
def agx(rgb, ev):
    """agx.hlsl agxTonemap, decode gamma2.2, look none, clamp off: engine-space FP16 -> display-encoded [0, 1]."""
    import agx_reference as ar
    v = np.maximum(np.nan_to_num(rgb.astype(np.float64)), 1e-10) ** 2.2 * 2. ** ev
    v = np.clip(np.log2(np.maximum(v @ np.array(ar.M_IN).T, ar.LOG_FLOOR)), ar.MIN_EV, ar.MAX_EV)
    v = (v - ar.MIN_EV) / (ar.MAX_EV - ar.MIN_EV); c = ar.CONTRAST_COEFFICIENTS
    v = sum(ck * v ** (6 - i) for i, ck in enumerate(c))
    return np.clip(v @ np.array(ar.M_OUT).T, 0, 1)

def rcas(t, gain, skip=None):
    """rcas.hlsl over a display image padded by one pixel; skip = mask of pixels the sharpen leaves at the centre tap."""
    t = np.clip(t, 0, 1); b, d, e, f, h = t[:-2, 1:-1], t[1:-1, :-2], t[1:-1, 1:-1], t[1:-1, 2:], t[2:, 1:-1]
    lum = lambda c: .5 * c[..., 0] + c[..., 1] + .5 * c[..., 2]
    L = np.stack([lum(x) for x in (b, d, e, f, h)]); nz = np.clip(np.abs(.25 * (L[0] + L[1] + L[3] + L[4]) - L[2]) / np.maximum(L.max(0) - L.min(0), 1 / 256.), 0, 1)
    mn = np.minimum(np.minimum(b, d), np.minimum(f, h)); mx = np.maximum(np.maximum(b, d), np.maximum(f, h))
    lobe = np.maximum(-mn / np.maximum(4 * mx, 1 / 4096.), (1 - mx) / np.minimum(4 * mn - 4, -1 / 4096.)).max(-1)
    lobe = (np.maximum(-.1875, np.minimum(lobe, 0)) * gain * (1 - .5 * nz) * (1 if skip is None else 1 - skip.astype(np.float64)))[..., None]  # skip: mask or weight in [0, 1] taken off the lobe
    pix = np.clip(((b + d + f + h) * lobe + e) / (4 * lobe + 1), np.minimum(mn, e), np.maximum(mx, e))
    return pix

if MODE == 'lattice':
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    GAIN = 2. ** -float(os.environ.get('SHARPEN', '0.75'))  # X3M_TAA_SHARPEN stops of run142 / run148
    ev = {int(re.search(r'frame=(\d+)', l).group(1)): float(re.search(r'ev_adapted=([-\d.]+)', l).group(1)) for l in
          subprocess.run(['grep', '-E', r'^hdr_frame device=1 frame=(%s) ' % '|'.join(map(str, frames)), D + log], capture_output=True, text=True).stdout.splitlines()}
    fr = frames[1:]; R, T, Mg = 8, 16, 10; dcode = lambda rgb: 255 * (rgb @ LUMA)
    def box(a, r):
        c = np.cumsum(np.cumsum(np.pad(a.astype(np.float64), ((r + 1, r), (r + 1, r))), 0), 1); n = 2 * r + 1
        return (c[n:, n:] - c[:-n, n:] - c[n:, :-n] + c[:-n, :-n]) / (n * n)
    def fshift(a, dx, dy):
        fy = np.fft.fftfreq(a.shape[0])[:, None]; fx = np.fft.fftfreq(a.shape[1])[None, :]
        return np.real(np.fft.ifft2(np.fft.fft2(a) * np.exp(2j * np.pi * (fx * dx + fy * dy))))
    regs, vel = [], []
    for f in fr:
        dep = load('depth', f, 'rgba32f', np.float32, 4)[..., 0]; mot = load('motion', f, 'rgba32f', np.float32, 4); v = valid(dep); cell = (mot[..., 3] == 1) & ~v
        regs.append(((box(cell, 5) > .55) & (box(v, 2) < .65))[Y0:Y1, X0:X1]); mm = mot[Y0:Y1, X0:X1]; jx, jy = meta[f]['j']
        vel.append(np.stack([xs - (mm[..., 0] * W + jx - .5), ys - (mm[..., 1] * H + jy - .5), mm[..., 3] == 1], -1))
        if f == fr[R]: cellR = ((mot[..., 3] == 1) & (dep <= -.5))[Y0:Y1, X0:X1]
    tiles = [(tx, ty) for ty in range(Mg + 2, (Y1 - Y0) - T - Mg - 16, T) for tx in range(Mg + 2, (X1 - X0) - T - Mg - 16, T) if cellR[ty:ty + T, tx:tx + T].mean() > .55]
    def tile_stack(S, tx, ty, dv=(0, 0)):  # motion-compensated (lattice-frame) tile over frames R.., velocity from RT1 plus a refinement
        o = []; cx = cy = 0.
        for i in range(R, len(fr)):
            if i > R: vv = vel[i][ty:ty + T, tx:tx + T]; ok = vv[..., 2] == 1; cx += np.median(vv[..., 0][ok]) + dv[0]; cy += np.median(vv[..., 1][ok]) + dv[1]
            o.append(fshift(S[i][ty - Mg:ty + T + Mg + 16, tx - Mg:tx + T + Mg + 16], cx, cy)[Mg:Mg + T, Mg:Mg + T])
        return np.array(o)
    def bands(S):
        N = S.shape[0]; dev = S - S.mean(0); p = 2 * np.abs(np.fft.rfft(dev, axis=0)) ** 2 / N ** 2; per = np.array([N / k if k else np.inf for k in range(p.shape[0])])
        return [np.sqrt((dev ** 2).mean())] + [np.sqrt(p[(per >= lo_) & (per < hi_)].sum(0).mean()) for lo_, hi_ in ((2, 4), (4, 8), (8, 99))]
    def peaks(Lm, reg):  # per-column line crossings: sub-pixel centroid, peak excess
        c, u1, u2, d1, d2 = Lm[2:-2], Lm[1:-3], Lm[:-4], Lm[3:-1], Lm[4:]; bg = np.minimum(u2, d2); pk = (c >= u1) & (c > d1) & (c > 1.3 * bg) & reg[2:-2]
        e = (u1 - bg).clip(0) + (c - bg) + (d1 - bg).clip(0); return (((d1 - bg).clip(0) - (u1 - bg).clip(0)) / np.maximum(e, 1e-9))[pk], (c - bg)[pk]
    dvs = None; ref = {}
    print('tiles %d frames %d sharpen gain %.4f ev %.3f..%.3f' % (len(tiles), len(fr) - R, GAIN, min(ev.values()), max(ev.values())))
    # MASKS=lattice,line2d ... selects the masks compared (default the note's); flip px / big-object edge as in 'options'
    V = np.stack([valid(load('depth', f, 'rgba32f', np.float32, 4)[Y0:Y1, X0:X1, 0]) for f in fr]); flip = V.any(0) & ~V.all(0)
    def fbands(S): N = S.shape[0]; p_ = 2 * np.abs(np.fft.rfft(S - S.mean(0), axis=0)) ** 2 / N ** 2; per = np.array([N / k if k else np.inf for k in range(p_.shape[0])]); return [float(np.sqrt(p_[(per >= a) & (per <= b)].sum(0).mean())) for a, b in ((2, 4), (4.01, 8), (8.01, 32))]
    Vw = np.stack([valid(load('depth', f, 'rgba32f', np.float32, 4)[Y0 - 1:Y1 + 1, X0 - 1:X1 + 1, 0]) for f in fr]).all(0); stable = np.ones_like(flip)  # 3x3 always geometry
    for ky in (0, 1, 2):
        for kx in (0, 1, 2): stable &= Vw[ky:ky + flip.shape[0], kx:kx + flip.shape[1]]
    stable = stable[:, 1:] & stable[:, :-1]
    mnames = os.environ.get('MASKS', 'lattice').split(',')
    # INSTALLED="dict(filter=1., fmask='line2x')": the options the capture was flown with (the 'installed' row then replays the dump itself)
    for name, opt in [('installed', eval(os.environ.get('INSTALLED', '{}')))] + [('%s A=%g' % (m_, A_), dict(filter=A_, fmask=m_)) for m_ in mnames for A_ in (2., 1.)]:
        outs, diags, _ = run(opt, 1); stages = {'resolve': [], 'agx': [], 'agx+rcas': [], 'agx+rcas excl': []}
        for o, dg, f in zip(outs, diags, fr):
            pad = load('taa', f, 'rgba16f', np.float16, 4)[Y0 - 1:Y1 + 1, X0 - 1:X1 + 1, :3].astype(np.float32); pad[1:-1, 1:-1] = o[..., :3]; t = agx(pad, ev[f])
            stages['resolve'].append(codes(o, meta[f]['k'])); stages['agx'].append(dcode(t[1:-1, 1:-1])); stages['agx+rcas'].append(dcode(rcas(t, GAIN))); stages['agx+rcas excl'].append(dcode(rcas(t, GAIN, dg['lattice'])))
            if name == 'installed' and f == fr[-1]:
                pr = np.fromfile(D + 'present_1_%d.bgra8' % f, dtype=np.uint8).reshape(H, W, 4)[Y0:Y1, X0:X1, 2::-1] / 255.
                print('presented dump vs AgX+RCAS(replayed resolve), frame %d: mean abs %.2f codes (bloom and 8-bit rounding not modelled)' % (f, np.abs(dcode(pr) - stages['agx+rcas'][-1]).mean()))
        mk = [dg['masks'][opt.get('fmask', 'lattice')] for dg in diags]; off = [~(box(m_, 2) > 0) & ~dg['far'] for m_, dg in zip(mk, diags)]
        # big-object edge: thin px within 1 px of geometry surviving a 5x5 erosion, no small geometry within 3 px
        edge = []
        for v_, dg in zip(V, diags): big = box(box(v_, 2) > .999, 3) > 0; edge.append(dg['thin'] & (box(big, 1) > 0) & ~(box(v_ & ~big, 3) > 0))
        if name == 'installed':
            L_ = np.array([dg['lattice'] for dg in diags])
            for m_ in dg['masks']:
                G_ = np.array([dg['masks'][m_] for dg in diags]); print('MASK %-8s coverage %.4f | of lattice covered %.3f | of mask inside lattice %.3f | of flip px %.3f | of big-object edge px %.4f (edge px %.4f of crop)' % (
                    m_, G_.mean(), (G_ & L_).sum() / max(L_.sum(), 1), (G_ & L_).sum() / max(G_.sum(), 1), G_[:, flip].mean() if flip.any() else 0, G_[np.array(edge)].mean() if np.array(edge).any() else 0, np.mean(edge)))
        for st, S in stages.items():
            S = np.nan_to_num(np.array(S))
            ge = np.mean([(np.diff(S[i], axis=1) ** 2)[edge[i][:, 1:] | edge[i][:, :-1]].mean() for i in range(R, len(fr)) if edge[i].any()] or [0]); fb = fbands(S[:, flip]) if flip.any() else [0, 0, 0]
            sg = float((np.diff(S[R:], axis=2) ** 2)[:, stable].mean()) if stable.any() else 0.
            extra = ' | stable-geometry gradient x%.4f' % (sg / ref.setdefault(st + 's', sg or 1)) + ' | edge gradient x%.4f | flip px %d bands %.2f %.2f %.2f' % (ge / ref.setdefault(st + 'e', ge or 1), flip.sum(), *fb)
            if not tiles:
                g = np.mean([(np.diff(S[i], axis=1) ** 2)[off[i][:, 1:] & off[i][:, :-1]].mean() for i in range(R, len(fr))]); ref.setdefault(st, (1, g)); print('%-20s %-14s off-mask gradient energy x%.4f%s' % (name, st, g / ref[st][1], extra), flush=True); continue
            if dvs is None: dvs = [min(((bands(tile_stack(S, tx, ty, (a, b)))[0], a, b) for a in np.arange(-.06, .061, .02) for b in np.arange(-.06, .061, .02)))[1:] for tx, ty in tiles]
            B = np.sqrt((np.array([bands(tile_stack(S, tx, ty, dv)) for (tx, ty), dv in zip(tiles, dvs)]) ** 2).mean(0))
            C, P = map(np.concatenate, zip(*[peaks(np.clip(S[i] / 255, 0, 1) ** 2.2, regs[i]) for i in range(R, len(fr))])); pc, ps = np.median(P[np.abs(C) < .1]), np.median(P[np.abs(C) >= .4])
            g = np.mean([(np.diff(S[i], axis=1) ** 2)[off[i][:, 1:] & off[i][:, :-1]].mean() for i in range(R, len(fr))]); ref.setdefault(st, (B[0], g))
            print('%-20s %-14s crawl %.2f (x%.2f) fast %.2f mid %.2f slow %.2f | line peak centred %.4f straddling %.4f ratio %.2f | off-mask gradient energy x%.4f | mask share %.3f' % (
                name, st, B[0], B[0] / ref[st][0], B[1], B[2], B[3], pc, ps, ps / pc, g / ref[st][1], np.mean(mk)) + extra, flush=True)

# --- far-gated stabiliser (docs/architecture/taa-distant-line-fade.md) ----------------------------------------------
# FAR=F0,F1 (units/px; default 90,150; F1 <= F0: whole crop). Components separately: weight W_FAR 0.985, filter A = 1, sharpen off.
if MODE == 'far':
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    GAIN = 2. ** -float(os.environ.get('SHARPEN', '0.75')); F0, F1 = map(float, os.environ.get('FAR', '90,150').split(','))
    ev = {int(re.search(r'frame=(\d+)', l).group(1)): float(re.search(r'ev_adapted=([-\d.]+)', l).group(1)) for l in
          subprocess.run(['grep', '-E', r'^hdr_frame device=1 frame=(%s) ' % '|'.join(map(str, frames)), D + log], capture_output=True, text=True).stdout.splitlines()}
    fr = frames[1:]; SK = 12; dcode = lambda rgb: 255 * (rgb @ LUMA)
    V = np.stack([valid(load('depth', f, 'rgba32f', np.float32, 4)[Y0 - 1:Y1 + 1, X0 - 1:X1 + 1, 0]) for f in fr]).all(0); inter = np.ones(ys.shape, bool)
    for ky in (0, 1, 2):
        for kx in (0, 1, 2): inter &= V[ky:ky + ys.shape[0], kx:kx + ys.shape[1]]
    inter[:6] = inter[-6:] = False; inter[:, :6] = inter[:, -6:] = False
    mots = [load('motion', f, 'rgba32f', np.float32, 4)[Y0:Y1, X0:X1] for f in fr[SK:]]
    vx = float(np.median([np.median((xs - (m_[..., 0] * W + meta[f]['j'][0] - .5))[m_[..., 3] == 1]) for m_, f in zip(mots, fr[SK:]) if (m_[..., 3] == 1).any()] or [0]))
    vy = float(np.median([np.median((ys - (m_[..., 1] * H + meta[f]['j'][1] - .5))[m_[..., 3] == 1]) for m_, f in zip(mots, fr[SK:]) if (m_[..., 3] == 1).any()] or [0]))
    def shift(im, sx, sy):
        fy = np.fft.fftfreq(im.shape[0])[:, None]; fx = np.fft.fftfreq(im.shape[1])[None, :]; return np.fft.ifft2(np.fft.fft2(im) * np.exp(-2j * np.pi * (fx * sx + fy * sy))).real
    mc = np.hypot(vx, vy) > .01  # drifting capture: one global translation (the routed median), as the note's metmc.py
    print('interior px %d of %d; routed median velocity %.3f %.3f px/frame%s; gate F0 %g F1 %g units/px; sharpen gain %.4f' % (inter.sum(), inter.size, vx, vy, ' (motion-compensated)' if mc else '', F0, F1, GAIN))
    ref = {}; hot = {}
    for name, opt, soff in (('base', {}, False), ('weight 0.985', dict(far=(F0, F1, .985, 0)), False), ('filter A=1', dict(far=(F0, F1, 0, 1.)), False), ('weight+filter', dict(far=(F0, F1, .985, 1.)), False),
                            ('sharpen off', dict(far=(F0, F1, 0, 0)), True), ('weight+sharpen off', dict(far=(F0, F1, .985, 0)), True), ('all three', dict(far=(F0, F1, .985, 1.)), True)):
        if os.environ.get('ONLY') and name != 'base' and name not in os.environ['ONLY'].split('|'): continue
        outs, diags, _ = run(opt, 1); st = {'resolve': [], 'presented': []}
        for o, dg, f in zip(outs, diags, fr):
            pad = load('taa', f, 'rgba16f', np.float16, 4)[Y0 - 1:Y1 + 1, X0 - 1:X1 + 1, :3].astype(np.float32); pad[1:-1, 1:-1] = o[..., :3]
            st['resolve'].append(codes(o, meta[f]['k'])); st['presented'].append(dcode(rcas(agx(pad, ev[f]), GAIN, dg['farw'] if soff else None)))
        fw = np.mean([dg['farw'] for dg in diags[SK:]], 0)
        if 'far' in opt and not ref.get('binned'): ref['binned'] = 1; print('farw on interior px: 0: %.3f, (0, 0.5]: %.3f, (0.5, 1): %.3f, 1: %.3f' % ((fw[inter] == 0).mean(), ((fw[inter] > 0) & (fw[inter] <= .5)).mean(), ((fw[inter] > .5) & (fw[inter] < 1)).mean(), (fw[inter] == 1).mean()))
        for stage, S in st.items():
            if soff and stage == 'resolve': continue
            S = np.nan_to_num(np.array(S))[SK:]
            if mc: S = np.stack([shift(im, -vx * i, -vy * i) for i, im in enumerate(S)])  # content moves +v per frame: sample frame i at x + v i
            sd = S.std(0); g2 = (np.diff(S, axis=2) ** 2).mean(0); pair = inter[:, 1:] & inter[:, :-1]
            if 'far' in opt: ref['fwp'] = np.minimum(fw[:, 1:], fw[:, :-1])
            if stage not in hot: hot[stage] = inter & (sd >= np.percentile(sd[inter], 95))
            r = dict(hot=sd[hot[stage]].mean(), inter=np.sqrt((sd[inter] ** 2).mean()), n2=int((sd[inter] > 2).sum()), hm=S.mean(0)[hot[stage]].mean(), sd=sd, g2=g2)
            b_ = ref.setdefault(stage, r); line = ''
            if 'fwp' in ref:
                fwp = ref['fwp']
                for b, m_ in (('all', pair), ('farw 0', pair & (fwp == 0)), ('(0,.5]', pair & (fwp > 0) & (fwp <= .5)), ('>.5', pair & (fwp > .5))): line += ' %s x%.3f' % (b, g2[m_].mean() / b_['g2'][m_].mean()) if m_.any() else ''
            print('%-19s %-9s hot std %.2f (x%.2f) | interior rms %.3f (x%.2f) | px>2 codes %d | hot mean x%.2f | per-frame gradient energy%s' % (
                name, stage, r['hot'], r['hot'] / b_['hot'], r['inter'], r['inter'] / b_['inter'], r['n2'], r['hm'] / b_['hm'], line), flush=True)

# --- lattice crawl remedies against the VISIBLE metric (taa-lattice-crawl.md section 10) -------------------------------
# Static roping: in each 8-frame jitter-cycle mean of the presented image (AgX + RCAS), line crossings by sub-pixel phase; the
# bead contrast (centred - straddling) / (centred + straddling) is what creeps along the lines when the ship drifts slowly.
if MODE == 'remedy':
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    GAIN = 2. ** -float(os.environ.get('SHARPEN', '0.75')); fr = frames[1:]; dcode = lambda rgb: 255 * (rgb @ LUMA)
    ev = {int(re.search(r'frame=(\d+)', l).group(1)): float(re.search(r'ev_adapted=([-\d.]+)', l).group(1)) for l in
          subprocess.run(['grep', '-E', r'^hdr_frame device=1 frame=(%s) ' % '|'.join(map(str, frames)), D + log], capture_output=True, text=True).stdout.splitlines()}
    def box(a, r):
        c = np.cumsum(np.cumsum(np.pad(a.astype(np.float64), ((r + 1, r), (r + 1, r))), 0), 1); n = 2 * r + 1
        return (c[n:, n:] - c[:-n, n:] - c[n:, :-n] + c[:-n, :-n]) / (n * n)
    regs = []
    for f in fr:
        dep = load('depth', f, 'rgba32f', np.float32, 4)[..., 0]; mot = load('motion', f, 'rgba32f', np.float32, 4); v = valid(dep); regs.append(((box((mot[..., 3] == 1) & ~v, 5) > .45) & (box(v, 2) < .65))[Y0:Y1, X0:X1])
    def peaks(Lm, reg):
        c, u1, u2, d1, d2 = Lm[2:-2], Lm[1:-3], Lm[:-4], Lm[3:-1], Lm[4:]; bg = np.minimum(u2, d2); pk = (c >= u1) & (c > d1) & (c > 1.3 * bg) & reg[2:-2]
        e = (u1 - bg).clip(0) + (c - bg) + (d1 - bg).clip(0); return (((d1 - bg).clip(0) - (u1 - bg).clip(0)) / np.maximum(e, 1e-9))[pk], (c - bg)[pk]
    def beads(S):  # cycles start after the 7-frame transient of a changed filter
        C, P = [], []
        for c0 in range(7, len(S) - 7, 8):
            c_, p_ = peaks(np.clip(np.mean(S[c0:c0 + 8], 0) / 255, 0, 1) ** 2.2, regs[c0 + 4]); C.append(c_); P.append(p_)
        C, P = np.abs(np.concatenate(C)), np.concatenate(P); pc, ps = np.median(P[C < .1]), np.median(P[C >= .4]); return pc, ps, (pc - ps) / (pc + ps), len(P)
    def fshift(a, dx, dy):
        fy = np.fft.fftfreq(a.shape[0])[:, None]; fx = np.fft.fftfreq(a.shape[1])[None, :]; return np.real(np.fft.ifft2(np.fft.fft2(a) * np.exp(-2j * np.pi * (fx * dx + fy * dy))))
    def creep(S, lat):
        """The VISIBLE crawl: how far the lattice image is from a rigid translation of itself. Jitter-cycle means A (frames 7..14) and
        B (the last 8); per 28-px lattice tile the best sub-pixel translation of A onto B (Fourier shift); rms residual / lattice contrast."""
        A_, B_ = S[7:15].mean(0), S[-8:].mean(0); T = 28; res = []; sh = []
        for ty in range(10, A_.shape[0] - T - 9, T // 2):
            for tx in range(10, A_.shape[1] - T - 9, T // 2):
                mm = lat[ty:ty + T, tx:tx + T]
                if mm.mean() < .6: continue
                a_ = A_[ty - 10:ty + T + 10, tx - 10:tx + T + 10]  # 10-px pad: the wrap-around of the Fourier shift stays outside the tile
                r_, dx_, dy_ = min((((fshift(a_, dx, dy)[10:-10, 10:-10] - B_[ty:ty + T, tx:tx + T])[mm] ** 2).mean(), dx, dy) for dx in np.arange(-1.2, 1.21, .1) for dy in np.arange(-.6, .61, .1))
                r_, dx_, dy_ = min((((fshift(a_, dx, dy)[10:-10, 10:-10] - B_[ty:ty + T, tx:tx + T])[mm] ** 2).mean(), dx, dy) for dx in np.arange(dx_ - .1, dx_ + .11, .025) for dy in np.arange(dy_ - .1, dy_ + .11, .025))
                res.append(r_); sh.append((dx_, dy_))
        return float(np.sqrt(np.mean(res))), float(B_[lat].std()), len(res), np.median(np.array(sh), 0)
    configs = [('installed', {}), ('line2x A=1 (flown)', dict(filter=1., fmask='line2x')), ('line2x A=2 (flown)', dict(filter=2., fmask='line2x')), ('line2x A=0.5', dict(filter=.5, fmask='line2x')),
               ('5x5 Gaussian A=0.5', dict(wide=.5)), ('5x5 Gaussian A=0.25', dict(wide=.25)), ('along-line s=1.5', dict(along=(1.5, 0))), ('along-line s=2.5', dict(along=(2.5, 0))),
               ('along s=1.5 + across A=1', dict(along=(1.5, 1.))), ('mask weight 0.97', dict(wmask=.97)), ('dim 0.5', dict(dim=.5)), ('dim 0.5 + line2x A=1', dict(dim=.5, filter=1., fmask='line2x'))]
    configs += [('line2x A=1 + mask weight 0.97', dict(filter=1., fmask='line2x', wmask=.97)), ('line2x A=1, no clip', dict(filter=1., fmask='line2x', thin=1., allthin=True)),
                ('line2x A=1 + weight 0.97, no clip', dict(filter=1., fmask='line2x', wmask=.97, thin=1., allthin=True)), ('no clip', dict(thin=1., allthin=True))]
    if os.environ.get('ONLY'): configs = [c for c in configs if c[0] == 'installed' or c[0] in os.environ['ONLY'].split('|')]
    ref = None
    for name, opt in configs:
        outs, diags, _ = run(opt, 1); pres = []; nosh = []
        for o, f in zip(outs, fr):
            pad = load('taa', f, 'rgba16f', np.float16, 4)[Y0 - 1:Y1 + 1, X0 - 1:X1 + 1, :3].astype(np.float32); pad[1:-1, 1:-1] = o[..., :3]; t = agx(pad, ev[f]); pres.append(dcode(rcas(t, GAIN))); nosh.append(dcode(t[1:-1, 1:-1]))
        pres = np.nan_to_num(np.array(pres)); pc, ps, bc, n = beads(pres); pc0, ps0, bc0, _ = beads(np.nan_to_num(np.array(nosh)))
        lat = np.array([dg['masks']['line2x'] for dg in diags]); rip = float(np.sqrt(((pres[8:] - pres[8:].mean(0)) ** 2)[lat[8:]].mean()))
        gsel = ~(box(lat.any(0), 2) > 0); g = float((np.diff(pres[8:], axis=2) ** 2)[:, gsel[:, 1:] & gsel[:, :-1]].mean()); gl = float((np.diff(pres[8:], axis=1) ** 2)[:, lat.all(0)[1:] | lat.all(0)[:-1]].mean())
        latc = box(np.array([dg['masks']['lattice'] for dg in diags]).mean(0), 5) > .5; cr, cc, ct, cs = creep(pres, latc)
        ref = ref or dict(bc=bc, pc=pc, amp=pc - ps, rip=rip, g=g, gl=gl, cr=cr, cc=cc)
        print('%-26s CREEP residual %.2f codes (x%.2f) = %.3f of the lattice contrast %.1f (x%.2f); %d tiles, shift (%.2f, %.2f) px' % (name, cr, cr / ref['cr'], cr / cc, cc, cc / ref['cc'], ct, cs[0], cs[1]), end=' || ')
        print('%-26s crossings %d | bead contrast %.3f (x%.2f), amplitude %.4f (x%.2f) | before the sharpen %.3f | line peak x%.2f | temporal rms on the mask x%.2f | across-line gradient energy on the mask x%.2f | off-mask gradient x%.4f' % (
            name, n, bc, bc / ref['bc'], pc - ps, (pc - ps) / ref['amp'], bc0, pc / ref['pc'], rip / ref['rip'], gl / ref['gl'], g / ref['g']), flush=True)
