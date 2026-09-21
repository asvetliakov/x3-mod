#!/usr/bin/env python3
"""Run60 resolve model over an F8 burst: fidelity against the dumped TAA output, crawl metrics, variants.

docs/architecture/taa-lattice-crawl.md section 32.5. Patches taa_resolve_replay.resolve with the installed
Run60 rule: gate openness per pixel as line_mask_ps.hlsl gateOpen() forms it (own pixel, own depth; a routed
pixel with a SENTINEL depth takes the far-plane camera path), 17x17 minimum, a = screen gate, b = camera gate,
old = clip3 + a (old - clip3) + (b - a) (box(old) - clip3), thin weight W by age, far stabiliser 0.985 with its
own 0.03-0.25 screen-speed gate, keep = max of the two. Variants: sentfix (sentinel-depth routed pixels do not
vote in the camera gate), box radius, W, Keys parameter, forced-open and screen-only gates.

usage: taa_run60_crawl_replay.py <name> <dump dir> <f0-f1> <x0,y0,x1,y1> <track x0,y0,x1,y1> [variant|variant...]
Writes crawl-<name>.json to the current directory (scratch; the tracked record is hand-assembled).
Metrics: tracked_rms / gradient / step_rms follow material points by the routed homography (section 15 / 32);
pairwise = frame f against frame f-1 fetched through each strut pixel's own routed motion vector (usable where
the homography does not fit, biased by one bilinear fetch). Host only; 1280x768 dumps.
"""
import sys, os, re, json, time, inspect, runpy
import numpy as np
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT+'/tools/analysis')
name, DUMP, frames, roi, track = sys.argv[1], sys.argv[2].rstrip('/'), sys.argv[3], tuple(map(int, sys.argv[4].split(','))), tuple(map(int, sys.argv[5].split(',')))
only = sys.argv[6].split('|') if len(sys.argv) > 6 else None
import taa_lattice_gate_replay as G

def winf(a, r, fn):
    p = np.pad(a, r, mode='edge'); h, w = a.shape
    p = fn.reduce([p[:, i:i + w] for i in range(2 * r + 1)]); return fn.reduce([p[i:i + h] for i in range(2 * r + 1)])

PATCH = [
 ("    ka = -.5\n", "    ka = opt.get('keys', -.5)\n"),
 ("    speed = np.hypot(posx - xs, posy - ys)\n",
  "    speed = np.hypot(posx - xs, posy - ys)\n"
  "    nx0, ny0 = 2 * (xs - jx) / W - 1, 1 - 2 * (ys - jy) / H\n"
  "    c0x, c0y, _ = camera_previous_ndc(nx0, ny0, np.where(far, 1., dc).astype(np.float64), Rc, Rp, P, conv, opt.get('cam'), opt.get('campath', 'full'))\n"
  "    c0x, c0y = (c0x * .5 + .5) * W + jx, (.5 - c0y * .5) * H + jy\n"
  "    m0 = mot[ys, xs]; r0 = m0[..., 3] == 1\n"
  "    p0x = np.where(r0, m0[..., 0] * W + jx - .5, c0x); p0y = np.where(r0, m0[..., 1] * H + jy - .5, c0y)\n"
  "    scr0 = np.hypot(p0x - xs, p0y - ys); rel0 = np.where(r0, np.hypot(p0x - c0x, p0y - c0y), 0.)\n"
  "    if opt.get('sentfix'): rel0 = np.where(r0 & far, 0., rel0)\n"),
 ("        trg = tm * (1 - tcl)\n",
  "        op_ = lambda s_: np.clip(1 - (s_ - tr['lo']) / (tr['hi'] - tr['lo']), 0, 1)\n"
  "        o_s = op_(scr0); o_c = np.maximum(o_s, op_(rel0))\n"
  "        ga = tm * winf(o_s, R_ + G_, np.minimum); trg = tm * winf(o_c, R_ + G_, np.minimum)\n"
  "        if opt.get('forceopen'): trg = tm * 1.\n"
  "        if opt.get('screenonly'): trg = ga\n"
  "        if opt.get('hold') is not None: trg = np.maximum(trg, opt['hold']); ga = np.minimum(ga, trg)\n"),
 ("old = old + trg[..., None] * (relaxed - old); keep = keep",
  "boxed = relaxed\n"
  "        if opt.get('box'):\n"
  "            blo, bhi = wide_box(cur, ys, xs, opt['box'], weigh, k); boxed = np.clip(relaxed, blo, bhi)\n"
  "        boxhit = np.abs(boxed - relaxed).max(-1) > 1e-4\n"
  "        old = old + ga[..., None] * (relaxed - old) + (trg - ga)[..., None] * (boxed - old); keep = keep"),
 ("keep = w + farw * (1 - np.clip((speed - glo) / (ghi - glo), 0, 1)) * (np.minimum(a / (a + 1), WF) - w);",
  "keep = np.maximum(keep, w + farw * (1 - np.clip((speed - glo) / (ghi - glo), 0, 1)) * (np.minimum(a / (a + 1), WF) - w));"),
 ("    trg = np.zeros((h, wd))\n", "    trg = np.zeros((h, wd)); ga = trg; tm = trg > 1; boxhit = tm\n"),
 ("farw=farw, trg=trg)", "farw=farw, trg=trg, ga=ga, tm=tm, keep=keep, boxhit=boxhit, scr0=scr0, rel0=rel0, r0=r0)"),
]
os.environ['FR'] = frames
sys.argv = ['replay', DUMP, *map(str, roi), 'noop']
m = runpy.run_path(ROOT + '/tools/analysis/taa_resolve_replay.py')
src = inspect.getsource(m['resolve'])
# drop the 289-slice neighbourhood max (unused by the run60 rule, expensive)
src = re.sub(r"        if tr.get\('nbhd', True\):.*\n            C_ = .*\n", "", src)
for o, n in PATCH:
    assert src.count(o) == 1, o
    src = src.replace(o, n)
ns = m['resolve'].__globals__; ns['wide_box'] = G.wide_box; ns['winf'] = winf
exec(compile(src, '<crawl>', 'exec'), ns)

load, fr = m['load'], m['frames']; X0, Y0, X1, Y1 = roi; h, w = m['ys'].shape
ev = G.m_exposure(m)
def present(res, f):
    pad = load('taa', f, 'rgba16f', np.float16, 4)[Y0 - 1:Y1 + 1, X0 - 1:X1 + 1, :3].astype(np.float32)
    pad[1:-1, 1:-1] = res[..., :3]
    return 255 * (m['rcas'](m['agx'](pad, ev[f]), 2 ** -.75) @ m['LUMA'])
FAR = (80., 130., .985, 0)
def V(W_=.97, **kw):
    d = dict(thinregion=dict(W=W_, lo=.03, hi=.25), far=FAR, fargate=(.03, .25), run60=True, box=3); d.update(kw); return d
VARS = {
 'run60': V(),
 'screen_gate_only': V(screenonly=True),
 'run60_W094': V(.94),
 'sentfix_box7': V(sentfix=True),
 'sentfix_boxoff': V(sentfix=True, box=0),
 'sentfix_box5': V(sentfix=True, box=2),
 'sentfix_box11': V(sentfix=True, box=5),
 'sentfix_W094': V(.94, sentfix=True),
 'sentfix_W094_keys065': V(.94, sentfix=True, keys=-.65),
 'sentfix_keys065': V(sentfix=True, keys=-.65),
 'forceopen_box7': V(forceopen=True),
 'forceopen_boxoff': V(forceopen=True, box=0),
}
if only: VARS = {k: v for k, v in VARS.items() if k in only}
deps = np.array([load('depth', f, 'rgba32f', np.float32, 4)[Y0:Y1, X0:X1, 0] for f in fr[1:]])
tracks, resid = G.routed_tracks(m, track)
SF = G.SCORE_FROM
inside = np.logical_and.reduce([(x > 12) & (x < w - 12) & (y > 12) & (y < h - 12) for x, y in tracks[SF:]])
material = np.array([G.bilinear((d >= 0).astype(float), x, y) for d, (x, y) in zip(deps, tracks)])
support = (material.mean(0) > .08) & inside
rts = np.array([load('motion', f, 'rgba32f', np.float32, 4)[Y0:Y1, X0:X1, 3] == 1 for f in fr[1:]])
bg = np.array([G.erode((d < -.5) & ~r_, 3) for d, r_ in zip(deps, rts)]); bg[:, :, :12] = False; bg[:, :, -12:] = False; bg[:, :12] = False; bg[:, -12:] = False
def score(images):
    tr_ = np.array([G.bilinear(im, x, y) for im, (x, y) in zip(images, tracks)]); tail = tr_[SF:]; dev = tail - tail.mean(0)
    pair = support[:, 1:] & support[:, :-1]
    step = np.diff(tail, axis=0)
    return dict(tracked_rms=float(np.sqrt((dev[:, support] ** 2).mean())), gradient=float((np.diff(tail, axis=2) ** 2)[:, pair].mean()),
                step_rms=float(np.sqrt((step[:, support] ** 2).mean())))
out = dict(dump=DUMP, frames=[fr[0], fr[-1]], roi=list(roi), track=list(track), support_pixels=int(support.sum()),
           routed_fit_residual_p50_p99_px=[float(np.nanmedian(resid[:, 0])), float(np.nanmedian(resid[:, 1]))], variants={})
# dumped streams
dump_taa = [load('taa', f, 'rgba16f', np.float16, 4)[Y0:Y1, X0:X1].astype(np.float32) for f in fr[1:]]
dump_hdr = [load('hdr', f, 'rgba16f', np.float16, 4)[Y0:Y1, X0:X1].astype(np.float32) for f in fr[1:]]
im_taa = np.array([present(o, f) for o, f in zip(dump_taa, fr[1:])])
def present_in(f):
    pad = load('hdr', f, 'rgba16f', np.float16, 4)[Y0 - 1:Y1 + 1, X0 - 1:X1 + 1, :3].astype(np.float32)
    return 255 * (m['rcas'](m['agx'](pad, ev[f]), 2 ** -.75) @ m['LUMA'])
im_hdr = np.array([present_in(f) for f in fr[1:]])
im_prs = np.array([np.fromfile('%s/present_1_%d.bgra8' % (DUMP, f), np.uint8).reshape(768, 1280, 4)[Y0:Y1, X0:X1, [2, 1, 0]].astype(float) @ m['LUMA'] for f in fr[1:]])
mots = [load('motion', f, 'rgba32f', np.float32, 4)[Y0:Y1, X0:X1] for f in fr[1:]]
yy_, xx_ = np.mgrid[0:h, 0:w]
def pairwise(images, jit=0):
    res = []
    for i in range(1, len(images)):
        mv = mots[i].astype(np.float64); px = mv[..., 0] * 1280 - .5 - X0; py = mv[..., 1] * 768 - .5 - Y0
        if jit:
            jc, jp = m['meta'][fr[1 + i]]['j'], m['meta'][fr[i]]['j']; px = px + jit * (jc[0] - jp[0]); py = py + jit * (jc[1] - jp[1])
        px = np.where(np.isfinite(px) & (mv[..., 3] == 1), px, 0.); py = np.where(np.isfinite(py) & (mv[..., 3] == 1), py, 0.)
        sel = (mv[..., 3] == 1) & (deps[i] >= 0) & (px > 12) & (px < w - 13) & (py > 12) & (py < h - 13) & (xx_ > 12) & (xx_ < w - 13) & (yy_ > 12) & (yy_ < h - 13) & thinm[i]
        dlt = images[i] - G.bilinear(images[i - 1], px, py); spd = float(np.median(np.hypot(px - xx_, py - yy_)[sel])) if sel.any() else 0.
        res.append((fr[1 + i], spd, float((dlt[sel] ** 2).mean()) if sel.any() else 0., int(sel.sum())))
    def agg(rows): n = sum(r[3] for r in rows); return float(np.sqrt(sum(r[2] * r[3] for r in rows) / n)) if n else None
    return dict(all=agg(res), fast_gt20=agg([r for r in res if r[1] > 20]), mid_5_20=agg([r for r in res if 5 < r[1] <= 20]), slow_le5=agg([r for r in res if r[1] <= 5]), frames=[len([r for r in res if r[1] > 20]), len([r for r in res if 5 < r[1] <= 20]), len([r for r in res if r[1] <= 5])])
_o, _d, _ = m['run'](dict(thinregion=dict(W=.97, lo=.03, hi=.25)), 1, nframes=None); thinm = [d_['tm'] for d_ in _d]
out['dumped_pairwise'] = dict(taa_output=pairwise(im_taa), present=pairwise(im_prs), jittered_input=min((pairwise(im_hdr, s_) for s_ in (-1, 0, 1)), key=lambda r_: r_['all']))
print('PAIR', json.dumps(out['dumped_pairwise']), flush=True)
out['dumped'] = dict(jittered_input=score(im_hdr), taa_output=score(im_taa), present=score(im_prs))
print(json.dumps(out['dumped']), flush=True)
plain_o, _, _ = m['run']({}, 1); im_plain = np.array([present(o, f) for o, f in zip(plain_o, fr[1:])])
out['plain_w090_replay'] = score(im_plain)
inner = np.zeros((h, w), bool); inner[12:-12, 12:-12] = True
for key, opt in VARS.items():
    t0 = time.monotonic()
    outs, diags, _ = m['run'](opt, 1)
    images = np.array([present(o, f) for o, f in zip(outs, fr[1:])])
    rec = score(images); rec['pairwise'] = pairwise(images)
    rec['bg_trail_p99_vs_plain_codes'] = float(np.percentile(np.abs(images - im_plain)[bg], 99)); rec['bg_trail_max'] = float(np.abs(images - im_plain)[bg].max()); rec['bg_px'] = int(bg.sum())
    tmm = np.array([d['tm'] for d in diags]) & inner; strut = tmm & (deps >= 0)
    b = np.array([d['trg'] for d in diags]); a = np.array([d['ga'] for d in diags]); kp = np.array([d['keep'] for d in diags]); bh = np.array([d['boxhit'] for d in diags]); acc = np.array([d['accept'] for d in diags])
    S = slice(SF, None)
    rec.update(region_share_of_crop=float(tmm[S].mean()), strut_pixel_frames=int(strut[S].sum()),
               gate_b_mean_on_struts=float(b[S][strut[S]].mean()), gate_b_open_share_on_struts=float((b[S][strut[S]] > .5).mean()),
               gate_a_mean_on_struts=float(a[S][strut[S]].mean()),
               keep_p10_p50_mean_on_struts=[float(np.percentile(kp[S][strut[S]], 10)), float(np.percentile(kp[S][strut[S]], 50)), float(kp[S][strut[S]].mean())],
               accept_share_on_struts=float(acc[S][strut[S]].mean()),
               boxhit_share_where_camera_opens=float(bh[S][strut[S] & ((b - a)[S] > .01)].mean()) if (strut[S] & ((b - a)[S] > .01)).any() else None,
               elapsed=time.monotonic() - t0)
    if key == 'run60':
        # model fidelity against the dump, per-frame seeded from the dumped history
        so, sd, _ = m['run'](opt, 1, seed_each=True)
        e = np.array([np.abs(present(o, f) - it) for o, f, it in zip(so, fr[1:], im_taa)])
        eh = np.array([np.abs(o[..., :3].astype(np.float32) - t[..., :3]).max(-1) for o, t in zip(so, dump_taa)])
        rec['seeded_vs_dump_codes_strut_p50_p90_p99_max'] = [float(v) for v in np.percentile(e[strut], [50, 90, 99, 100])]
        rec['seeded_vs_dump_hdr_strut_p50_p99'] = [float(v) for v in np.percentile(eh[strut], [50, 99])]
        rec['seeded_vs_dump_codes_region_p50_p99'] = [float(v) for v in np.percentile(e[tmm], [50, 99])]
        e2 = np.abs(images - im_taa); rec['freerun_vs_dump_codes_strut_p50_p99'] = [float(v) for v in np.percentile(e2[strut], [50, 99])]
        # mask stability along tracks: thin mask and gate sampled at material points
        tmt = np.array([G.bilinear(d['tm'].astype(float), x, y) for d, (x, y) in zip(diags, tracks)])[S][:, support]
        bt = np.array([G.bilinear(d['trg'], x, y) for d, (x, y) in zip(diags, tracks)])[S][:, support]
        rec['mask_tm_on_support_mean_and_flicker'] = [float(tmt.mean()), float(np.abs(np.diff(tmt, axis=0)).mean())]
        rec['gate_b_on_support_mean_and_flicker'] = [float(bt.mean()), float(np.abs(np.diff(bt, axis=0)).mean())]
        r0 = np.array([d['r0'] for d in diags]); rel0 = np.array([d['rel0'] for d in diags]); sent = r0 & (deps < -.5) & inner
        rec['sentinel_routed_share_of_crop'] = float(sent[S].mean()); rec['sentinel_routed_rel_speed_p50_p90'] = [float(v) for v in np.percentile(rel0[S][sent[S]], [50, 90])] if sent[S].any() else None
        vr = r0 & (deps >= 0) & inner; rec['valid_routed_rel_speed_p50_p99'] = [float(v) for v in np.percentile(rel0[S][vr[S]], [50, 99])]
        rec['screen_speed_struts_p50_p90'] = [float(v) for v in np.percentile(np.array([d['scr0'] for d in diags])[S][strut[S]], [50, 90])]
    if key == 'sentfix_box7':
        so, _, _ = m['run'](opt, 1, seed_each=True)
        e = np.array([np.abs(present(o, f) - it) for o, f, it in zip(so, fr[1:], im_taa)])
        rec['seeded_vs_dump_codes_strut_p50_p90_p99_max'] = [float(v) for v in np.percentile(e[strut], [50, 90, 99, 100])]
    out['variants'][key] = rec
    print(key, json.dumps(rec), flush=True)
json.dump(out, open('crawl-%s.json' % name, 'w'), indent=1)
