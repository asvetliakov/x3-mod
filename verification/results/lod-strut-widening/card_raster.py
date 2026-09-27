#!/usr/bin/env python3
"""Orthographic 8-phase raster (lod_raster.py of the strut-widening worktree) of a body's record 0 = the source
of C, with the alpha-tested cards modelled on the real textures: a card face is blended (source-over, z-write per
its material) with alpha = the mean alpha of its diffuse texture's mip level at the size (texels/px from the
face's UV density; the engine's test GREATEREQUAL ref 1 passes at those levels), colour = the texture's mean
luminance; opaque faces flat at their texture's mean luminance. Variants: before; 'opaque' = widen_thin_patches
with the alpha materials excluded (the worktree's bake, shell rule on, k_d at the bake's F 960); 'all' = the
same op with the z-writing alpha cards eligible (widened faces: alpha x q). Sizes at F 1280 (5120x1440).
Metrics per size, pixel-weighted over the three axis views: luminance after/before, flip share (pixels covered in
some phases and not all, over pixels ever covered), flicker (sum of 8-phase std / sum of mean image).
Usage: card_raster.py WORKTREE NAME=s,s[,s] ... [--variants before,opaque,all] [--axes 0,1,2]"""
import argparse, sys, time
from pathlib import Path
import numpy as np

a = argparse.ArgumentParser()
a.add_argument('worktree'); a.add_argument('specs', nargs='+')
a.add_argument('--variants', default='before,opaque,all'); a.add_argument('--axes', default='0,1,2')
a.add_argument('--record', type=int, default=0)
a.add_argument('--divisor', type=float, default=2.0); a.add_argument('--px', type=float, default=1.0)
args = a.parse_args()
sys.path.insert(0, str(Path(args.worktree) / 'tools' / 'analysis'))
import bob1, lod_atlas, lod_overlay, lod_raster, body_materials, sector_fog_census as sfc  # noqa: E402

FOCAL, FBAKE = 1280.0, 960.0
LUMA = np.array([0.2126, 0.7152, 0.0722])
assets = sfc.Assets(bob1.DEFAULT_GAME)
tex = lod_atlas.Textures(assets)


def flag(m, key):
    for n, t, v in m.get('params', ()):
        if (n.decode('latin1') if isinstance(n, bytes) else n).lower() == key.lower() and t != 8:
            return v[0] if v else None


def pyramid_means(A):
    means = [A.mean()]
    x = A.astype(np.float64)
    while x.shape[0] > 1 and x.shape[1] > 1:
        x = x[:x.shape[0] // 2 * 2, :x.shape[1] // 2 * 2].reshape(x.shape[0] // 2, 2, x.shape[1] // 2, 2).mean((1, 3))
        means.append(x.mean())
    return means


def material_model(mats, record):
    """per material: luminance, alpha pyramid means (or None), blended flag, zwrite flag, texel size."""
    out = {}
    for i, m in enumerate(mats):
        if 'params' not in m:
            out[i] = dict(lum=0.5, alpha=None, blend=False, zwrite=True, T=1); continue
        sl = body_materials.slots(m)
        img = None
        try:
            img = tex.get(sl.get('diffuse'))
        except Exception:  # noqa: BLE001
            pass
        img = None if img is None else np.asarray(img)
        lum = float((img[:, :, :3].reshape(-1, 3).mean(0) @ LUMA) / 255) if img is not None and img.ndim == 3 else 0.5
        tested, blend, zw = flag(m, 'g_ALPHATESTENABLE') == 1, flag(m, 'g_AlphaBlendEnable') == 1, flag(m, 'g_ZWriteEnable') != 0
        alpha = None
        if (tested or blend) and img is not None and img.ndim == 3 and img.shape[2] >= 4:
            alpha = pyramid_means(img[:, :, 3])
        out[i] = dict(lum=lum, alpha=alpha, blend=blend or tested, zwrite=zw, T=(img.shape[1] if img is not None else 1))
    return out


def face_arrays(record, mats, model, k, P_uv_density):
    f = lod_raster.record_faces(record, mats)
    n = len(f['M'])
    colour = np.array([model[m]['lum'] for m in f['M']])
    alpha = np.ones(n); blended = np.zeros(n, bool)
    # q of widened groups
    q = np.ones(n)
    for j, (pi, gi, fi) in enumerate(f['where']):
        g = record['parts'][pi]['groups'][gi]
        if 'widen' in g:
            q[j] = g['widen'] / 32 if g['widen'] > 1 else g['widen']
            blended[j] = True
    for j, m in enumerate(f['M']):
        mm = model[m]
        if mm['alpha'] is not None:
            tpp = mm['T'] * P_uv_density.get(m, 0.0) / k          # texels per px
            L = int(min(len(mm['alpha']) - 1, max(0, round(np.log2(max(tpp, 1e-9))))))
            a = mm['alpha'][L] / 255
            alpha[j] = a * q[j]; blended[j] = True
        elif blended[j]:
            alpha[j] = q[j]
    return f, colour, alpha, blended


def uv_density(record):
    pts = record['points']; P = np.array([p[1:4] for p in pts], float)
    dens = {}
    for part in record['parts']:
        for g in part['groups']:
            F = np.array([f[:3] for f in g['faces']])
            if not len(F):
                continue
            UV = np.array([[lod_atlas.point_uv(pts[i]) or (0, 0) for i in f] for f in F], float)
            wa = 0.5 * np.linalg.norm(np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]]), axis=1)
            d = UV[:, 1] - UV[:, 0]; e = UV[:, 2] - UV[:, 0]
            ua = 0.5 * np.abs(d[:, 0] * e[:, 1] - d[:, 1] * e[:, 0])
            ok = wa > 0
            if ok.any():
                dens.setdefault(g['material'], []).append(np.median(np.sqrt(ua[ok] / wa[ok])))
    return {m: float(np.median(v)) for m, v in dens.items()}


def stats(f, colour, alpha, blended, axis, k, model):
    tri, d, front = lod_raster.view(f, axis)
    origin, size = lod_raster.frame(tri[front], k)
    imgs, covs = [], []
    for jit in lod_raster.JITTER8:
        r = lod_raster.render(tri[front], d[front], colour[front], alpha[front], blended[front], k, origin, size, jitter=jit)
        imgs.append(r['image']); covs.append(r['cover'] > 0)
    imgs, covs = np.stack(imgs), np.stack(covs)
    ever, always = covs.any(0), covs.all(0)
    mean = imgs.mean(0)
    sd = imgs.std(0)[ever]
    return dict(lum=float(mean.sum()), flip=float((ever & ~always).sum() / max(ever.sum(), 1)),
                flicker=float(imgs.std(0).sum() / max(mean.sum(), 1e-9)), px=int(ever.sum()),
                amp=float(sd.mean()) if sd.size else 0.0, amp90=float(np.percentile(sd, 90)) if sd.size else 0.0)


for spec in args.specs:
    name, ss = spec.split('='); sizes = [float(x) for x in ss.split(',')]
    t0 = time.time()
    tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, name)), 8)
    mats = bob1.materials(tree); ladder = bob1.lods(tree)
    src = ladder[args.record]
    r_raw = max(np.linalg.norm(p[1:4]) for p in src['points'] if p[0] & 1)
    model = material_model(mats, src)
    dens = uv_density(src)
    alpha_set = set(lod_overlay.alpha_materials(mats, assets, record=src))
    zoff = {i for i in alpha_set if not model[i]['zwrite']}
    t_pad = None
    for p in (bob1.DEFAULT_GAME / 'addon').glob('[0-9][0-9].x3m-lod.json'):
        import json
        for b in json.load(open(p))['bodies']:
            if b['name'].lower() == name.lower():
                t_pad = int(b['pad_threshold'])
    k_d = FBAKE * (t_pad / args.divisor) / (r_raw * 640)
    variants = {}
    for v in args.variants.split(','):
        if v == 'before' or args.record != 0:
            variants[v] = (src, None)
        else:
            excl = alpha_set if v == 'opaque' else zoff
            rec, rep = lod_overlay.widen_thin_patches(src, mats, k_d, args.px, exclude=frozenset(excl))
            variants[v] = (rec, rep)
    print(f'== {name} record {args.record}: r_raw {r_raw:.0f} T_pad {t_pad} divisor {args.divisor} px {args.px} k_d {k_d:.6f} W_u {args.px / k_d:.0f} units alpha materials {sorted(alpha_set)} z-write-off {sorted(zoff)}')
    for v, (rec, rep) in variants.items():
        if rep:
            print(f'   {v}: widened area {100 * rep.get("widened_area", 0):.2f} % strut {100 * rep.get("strut_area", 0):.2f} %'
                  f' widenable {100 * rep.get("widenable_area", 0):.2f} % patches {rep.get("patches")} faces {rep.get("faces")} skipped {rep.get("skipped")}')
    for s in sizes:
        k = FOCAL * s / (r_raw * 640)
        base = None
        for v, (rec, rep) in variants.items():
            f, colour, alpha, blended = face_arrays(rec, mats, model, k, dens)
            tot = dict(lum=0.0, flipw=0.0, px=0, flk=0.0, amp=0.0, amp90=0.0)
            rows = []
            for axis in [int(x) for x in args.axes.split(',')]:
                st = stats(f, colour, alpha, blended, axis, k, model)
                tot['lum'] += st['lum']; tot['flipw'] += st['flip'] * st['px']; tot['px'] += st['px']; tot['flk'] += st['flicker'] * st['px']; tot['amp'] += st['amp'] * st['px']; tot['amp90'] += st['amp90'] * st['px']
                rows.append(f'ax{axis} flip {st["flip"]:.3f} flk {st["flicker"]:.4f} amp {st["amp"]:.4f}/{st["amp90"]:.4f} px {st["px"]}')
            if base is None:
                base = tot['lum']
            print(f'  s {s:g} (k {k:.6f}, radius {k * r_raw:.0f} px) {v:6s}: lum/before {tot["lum"] / base:.4f} flip {tot["flipw"] / max(tot["px"], 1):.3f}'
                  f' flicker {tot["flk"] / max(tot["px"], 1):.4f} amp mean/p90 {tot["amp"] / max(tot["px"], 1):.4f}/{tot["amp90"] / max(tot["px"], 1):.4f} | ' + '; '.join(rows))
    print(f'  ({time.time() - t0:.0f} s)')
