#!/usr/bin/env python3
"""CPU replay of src/temporal/resolve.hlsl over a --taa-debug capture (numpy, crop of the frame).
usage: replay.py <dump dir> <x0> <y0> <x1> <y1> [validate|options]"""
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
    return np.fromfile(D + '%s_1_%d.%s' % (kind, f, ext), dtype=dt).reshape(H, W, ch)
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
    blendc = filt / ft[..., None] if A else wc
    out = unweigh(blendc + keep[..., None] * (old - blendc), k)
    out = np.where(accept[..., None], out, c)
    res = np.concatenate([out, alpha[..., None]], -1).astype(np.float16)
    return res, newage, dict(far=far, thin=thin, disocc=disocc, accept=accept, moved=moved, validc=~far, speed=speed, remedy_px=remedy_px, oob=~ok)

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
                tot['remedy'] += (dg['remedy_px'] & F).sum(); tot['speed'].append(float(np.median(dg['speed'][F & ~dg['far']])))
            n = tot['n']
            print('CLASSIFY flip px-frames %d: far(sentinel centre) %.3f; total-miss (far, no geometry in 3x3) %.3f; thin mask %.3f; disocclusion-rejected %.5f; other reject %.5f; clamp moved >1 code %.3f (mean move %.1f codes); among total-miss: clamp moved %.3f (mean %.1f codes); previous-depth-valid total-miss %.3f; median strut speed %.2f px/frame' % (
                n, tot['far'] / n, tot['miss'] / n, tot['thin'] / n, tot['disocc'] / n, tot['oob'] / n, tot['moved'] / n, tot['movedsum'] / max(tot['moved'], 1), tot['miss_moved'] / max(tot['miss'], 1), tot['miss_movedsum'] / max(tot['miss_moved'], 1), tot['remedy'] / n, float(np.median(tot['speed']))))
        line = 'OPTION %-24s px p2-4/4-8/8-32 %5.2f %5.2f %5.2f | block %5.2f %5.2f %5.2f | contrast x%.3f | stable-px sharpness x%.4f' % (name, *pb, *bb, contrast / base['contrast'], sharp / base['sharp'])
        if opt.get('wmax'):
            a = age[flip]; line += ' | age on flip px: 1:%.2f 2-3:%.2f 4-7:%.2f 8-15:%.2f 16-31:%.2f 32+:%.2f' % tuple(((a >= lo_) & (a < hi_)).mean() for lo_, hi_ in ((1, 2), (2, 4), (4, 8), (8, 16), (16, 32), (32, 99)))
        print(line, flush=True)
