#!/usr/bin/env python3
"""Sentinel-stabiliser resolve replay over a --taa-debug burst (unrouted sentinel-depth content under a pan).

docs/verification/temporal-resolve.md, "2026-09-22 run215 pan flicker: replay diagnosis". Patches
taa_resolve_replay.resolve with the Run60 camera-gate rule (the PATCH list of taa_run60_crawl_replay.py) plus the
Run61 sentinel stabiliser: an unrouted sentinel pixel gets b = max(b, S * 17x17 min camera openness), the added
strength takes the history clipped to the 7x7 box (inner 3x3 where the raw 7x7 luma maximum exceeds E), weight
min(age ramp, W). Variants: S, W, box radius / off / variance box, Keys parameter, reprojection offset.

usage: taa_sentinel_pan_replay.py <dump dir> <f0-f1> <x0,y0,x1,y1> <mode> [age dir]
  import this file with runpy / importlib from a driver: build(dump, frames, roi) returns the replay namespace.
Host only; 1280x768 dumps; no Wine.
"""
import sys, os, re, inspect, runpy
import numpy as np
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT + '/tools/analysis')


def winf(a, r, fn):
    p = np.pad(a, r, mode='edge'); h, w = a.shape
    p = fn.reduce([p[:, i:i + w] for i in range(2 * r + 1)]); return fn.reduce([p[i:i + h] for i in range(2 * r + 1)])


def crawl_patch():
    """The Run60 PATCH list, read from taa_run60_crawl_replay.py without running it."""
    text = open(ROOT + '/tools/analysis/taa_run60_crawl_replay.py').read()
    body = text[text.index('PATCH = ['):text.index("os.environ['FR']")]
    ns = {}; exec(body, ns); return ns['PATCH']


SENT_PATCH = [
 ("    speed = np.hypot(posx - xs, posy - ys)\n",
  "    unr_ = far & (mot[ys, xs, 3] == -1)\n"
  "    if opt.get('dpos') is not None: posx = posx + np.where(unr_, opt['dpos'][0], 0.); posy = posy + np.where(unr_, opt['dpos'][1], 0.)\n"
  "    speed = np.hypot(posx - xs, posy - ys)\n"),
 ("sig = np.sqrt(np.maximum(sq - mean * mean, 0));", "lo3, hi3 = lo.copy(), hi.copy(); sig = np.sqrt(np.maximum(sq - mean * mean, 0));"),
 ("        if opt.get('forceopen'): trg = tm * 1.\n",
  "        if opt.get('forceopen'): trg = tm * 1.\n"
  "        sentb = np.where(unr_, opt.get('sent', 0.) * winf(o_c, 8, np.minimum), 0.); trg = np.maximum(trg, sentb)\n"),
 ("        boxhit = np.abs(boxed - relaxed).max(-1) > 1e-4\n",
  "        if opt.get('varbox'):\n"
  "            ext_ = weigh(cur[Y0 - 3:Y1 + 3, X0 - 3:X1 + 3, :3].astype(np.float64), k); n_ = 49.\n"
  "            m7 = sum(ext_[3 + oy:3 + oy + h, 3 + ox:3 + ox + wd] for oy in range(-3, 4) for ox in range(-3, 4)) / n_\n"
  "            s7 = np.sqrt(np.maximum(sum(ext_[3 + oy:3 + oy + h, 3 + ox:3 + ox + wd] ** 2 for oy in range(-3, 4) for ox in range(-3, 4)) / n_ - m7 * m7, 0))\n"
  "            boxed = np.clip(boxed, np.maximum(blo, m7 - opt['varbox'] * s7), np.minimum(bhi, m7 + opt['varbox'] * s7))\n"
  "        if opt.get('emit', 0) > 0 and opt.get('box'):\n"
  "            raw_ = np.maximum(cur[Y0 - 3:Y1 + 3, X0 - 3:X1 + 3, :3].astype(np.float64) @ LUMA, 0)\n"
  "            rmax = winf(raw_, 3, np.maximum)[3:-3, 3:-3]; em_ = (rmax > opt['emit']) & far\n"
  "            boxed = np.where(em_[..., None], np.clip(relaxed, lo3, hi3), boxed)\n"
  "        boxhit = np.abs(boxed - relaxed).max(-1) > 1e-4\n"),
 ("    accept = ok & ~disocc\n", "    accept = ok & ~disocc\n    if opt.get('reject') is not None: accept &= ~opt['reject']\n"),
 ("trg=trg, ga=ga,", "trg=trg, unr=unr_, posx=posx, posy=posy, ga=ga,"),
]


def build(dump, frames, roi):
    os.environ['FR'] = frames
    argv = sys.argv; sys.argv = ['replay', dump, *map(str, roi), 'noop']
    try: m = runpy.run_path(ROOT + '/tools/analysis/taa_resolve_replay.py')
    finally: sys.argv = argv
    import taa_lattice_gate_replay as G
    src = inspect.getsource(m['resolve'])
    src = re.sub(r"        if tr.get\('nbhd', True\):.*\n            C_ = .*\n", "", src)
    for o, n in crawl_patch() + SENT_PATCH:
        assert src.count(o) == 1, o
        src = src.replace(o, n)
    ns = m['resolve'].__globals__; ns['wide_box'] = G.wide_box; ns['winf'] = winf
    exec(compile(src, '<sentinel>', 'exec'), ns)
    m = dict(m); m['resolve'] = ns['resolve']
    return m


FAR = (80., 130., .985, 0)


def variant(S=.7, W_=.97, E=1., **kw):
    d = dict(thinregion=dict(W=W_, lo=.03, hi=.25), far=FAR, fargate=(.03, .25), box=3, sent=S, emit=E); d.update(kw); return d


def run(m, opt, age0, conv=1):
    """Closed-loop replay: history inside the crop is the replay's own output, outside it the dumped resolve."""
    load, fr, meta = m['load'], m['frames'], m['meta']; X0, Y0, X1, Y1 = m['X0'], m['Y0'], m['X1'], m['Y1']
    outs, diags = [], []; age = age0.copy(); hist_dump = lambda f_: load('taa', f_, 'rgba16f', np.float16, 4).astype(np.float32)
    hist = load('taa', fr[0], 'rgba16f', np.float16, 4).astype(np.float32); pdep = load('depth', fr[0], 'rgba32f', np.float32, 4)[..., 0].copy()
    for f in fr[1:]:
        cur = load('hdr', f, 'rgba16f', np.float16, 4).astype(np.float32); dep = load('depth', f, 'rgba32f', np.float32, 4)[..., 0].copy()
        mot = load('motion', f, 'rgba32f', np.float32, 4); c, p = meta[f], meta[f - 1]
        o = dict(opt); o.setdefault('campath', 'rotation')
        if opt.get('oracle_reject', True):  # pixels the installed resolve handed through current-only (reactive coverage is not dumped)
            o['reject'] = (hist_dump(f)[Y0:Y1, X0:X1, :3] == cur[Y0:Y1, X0:X1, :3]).all(-1)
        res, age, dg = m['resolve'](cur, dep, mot, hist, pdep, age, c['j'], c['k'], c['w'], c['R'], p['R'], c['P'], conv, o)
        outs.append(res); diags.append(dg)
        hist = load('taa', f, 'rgba16f', np.float16, 4).astype(np.float32)
        if not opt.get('open_loop'): hist[Y0:Y1, X0:X1] = res.astype(np.float32)
        pdep = dep
    return outs, diags


if __name__ == '__main__':
    dump, frames, roi = sys.argv[1], sys.argv[2], tuple(map(int, sys.argv[3].split(',')))
    agedir = sys.argv[5] if len(sys.argv) > 5 else dump
    m = build(dump, frames, roi); fr = m['frames']; X0, Y0, X1, Y1 = roi
    # abs: a negative count is the exit reset's mark on a band pixel (seta-sky-hull-share-decay.md); the count is its magnitude.
    age0 = np.abs(np.fromfile('%s/taa_age_1_%d.r32f' % (agedir, fr[0]), np.float32).reshape(768, 1280)[Y0:Y1, X0:X1].astype(float))
    outs, diags = run(m, dict(variant(), open_loop=True), age0)
    for o, d, f in zip(outs, diags, fr[1:]):
        ref = m['load']('taa', f, 'rgba16f', np.float16, 4)[Y0:Y1, X0:X1]; k = m['meta'][f]['k']
        e = np.abs(m['codes'](o, k) - m['codes'](ref, k)); u = d['unr']
        print('frame %d unrouted-sentinel share %.3f err codes mean %.4f p99 %.3f max %.2f | rest mean %.4f' % (f, u.mean(), e[u].mean(), np.percentile(e[u], 99), e[u].max(), e[~u].mean() if (~u).any() else 0))
