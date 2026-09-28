#!/usr/bin/env python3
"""Classic (non-effect) material bake, 2026-09-29: the original pixel formula against the merged material's,
over the atlas texels, per material shape.

  python3 nonfx_bake_formula.py AFTER_OUT [--game GAME] [--jobs N] [--only NAME ...] > nonfx_bake_formula_out.txt

AFTER_OUT is a `lod_overlay.py --batch --only FILE --out DIR` root. Every body of it with a merged classic
material is planned again with the recorded options (lod_overlay.plan_body, nothing written; the rebuilt
atlases must hash like the baked members). Per atlas tile of a classic material and per lighting condition the
two sides of docs/reverse-engineering/non-effect-materials.md section 3 are evaluated:

  original  rgb = sat(C(N_o) * D + L)   D, L = the source textures area-resampled to the tile (lod_atlas.bake,
            before encoding; an untextured record: the NONE_GRAY texel; no light map: black);
            N_o = the vertex normal (technique DEFAULT) or normalize(b.x B + b.y T + b.z N), b = 2 rgb - 1 of the
            NONE_NORMAL placeholder (technique BUMPMAP_LOW)
  merged    rgb = sat(C(N_m) * D' + L') D', L' = the decoded atlas texels; N_m = the vertex normal (DEFAULT) or
            normalize(x B + y T + z N), (x, y) = 2 (a, g) - 1 of the decoded bump atlas texel, z = sqrt(1 - x^2 -
            y^2) (BUMPMAP)
  C(N)      = sum_i sat(N.L_i) c_i Kd + sum_i pow(sat(R_i.V), p) sat(3 N.L_i) c_i Ks + sat(E), R_i = 2 (N.L_i) N - L_i
            (specular texture NONE_WHITE on both sides; reflection 0; g_MatColor the identity on both sides)

Kd, Ks, p, E come from the record words (original) and from the written merged material (merged). Conditions:
14 directions of light 0 (white) x 4 view directions, light 1 fixed (0.35, 0.35, 0.45), no point lights. A model
with the tangent declaration is evaluated with an orthonormal tangent frame (a group with tangent records) and
with zero tangents (a group without); a model without it has no tangent stream (zero). Differences are in 8-bit
steps (255 x |original - merged|), maximum and mean over texels, channels and conditions:
  formula  both sides on the source texels (the constants, the technique and the normal only)
  total    the merged side on the decoded atlas (adds the resampling-free DXT encoding error of the atlas)
  alpha    |diffuse.a - diffuse'.a| and |light.a - light'.a| (the output alpha is one of them times fog)
"""
import argparse
import collections
import hashlib
import json
import multiprocessing
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'tools/analysis'), str(ROOT / 'verification/probe')]
import bob1                                          # noqa: E402
import lod_atlas                                     # noqa: E402
import lod_overlay                                   # noqa: E402

unit = lambda v: np.asarray(v, np.float64) / np.linalg.norm(v)
L0 = [unit(v) for v in [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)] +
      [(x, y, z) for x in (1, -1) for y in (1, -1) for z in (1, -1)]]
VIEW = [unit(v) for v in [(0, 0, 1), (0.6, 0, 0.8), (0, -0.7, 0.7), (-0.5, 0.5, 0.7)]]
L1, C0, C1 = unit((-0.5, 0.3, 0.4)), np.ones(3), np.array((0.35, 0.35, 0.45))
CONDITIONS = [(l, v) for l in L0 for v in VIEW]
FRAMES = {'tangent records': (np.array((1.0, 0, 0)), np.array((0, 1.0, 0))), 'zero tangents': (np.zeros(3), np.zeros(3))}
NV = np.array((0.0, 0.0, 1.0))
sat = lambda x: np.clip(x, 0.0, 1.0)
_W = {}


def lit(n, consts):
    """(conditions, 3) C(N) for a unit normal and 16.16 constants (Kd, Ks, p, E rgb)."""
    kd, ks, p = (consts[k] / 65536 for k in range(3))
    e = sat(np.array(consts[3:6]) / 65536)
    out = []
    for l0, v in CONDITIONS:
        c = np.zeros(3)
        for l, col in ((l0, C0), (L1, C1)):
            d = float(sat(n @ l))
            r = 2 * (n @ l) * n - l
            c += d * col * kd + float(sat(r @ v)) ** p * float(sat(3 * d)) * col * ks
        out.append(c + e)
    return np.array(out)


def normal(b, frame):
    t, bn = FRAMES[frame]
    v = b[0] * bn + b[1] * t + b[2] * NV
    return v / np.linalg.norm(v)


def record_constants(m):
    fx = lambda x: int(round(x * 65536))
    c, si = m['colors'], m['colors'][11]
    power = (m['w24'] if m['w24'] else sum(c[6:9]) / 768 * 100) + 1
    return (0 if si else fx(m['w2c'] * 0.01), fx(m['w26'] * 0.01), fx(power)) + tuple(
        fx(x * si / 25500) if si else 0 for x in c[3:6])


def init(game):
    _W['assets'] = lod_overlay.original_assets(Path(game))[0]


def body(job):
    name, t_pad, opts, marker = job
    assets = _W['assets']
    opts = dict(opts, sizes=tuple(opts['sizes']))
    p = lod_overlay.plan_body(assets, name, t_pad, 'compact', False, 'atlas', False, lod_overlay.GLOW_LUMA,
                              lod_overlay.GLOW_SHARE, None, True, opts, 0, None)
    res = p['atlas_build']
    same = {t['slot']: t['dds_sha256'] for t in marker['atlas']['textures']} == \
        {s: e['sha256'] for s, e in res['encoded'].items()}
    src = bob1.materials(bob1.parse(assets.read_entry(bob1.resolve_body(assets, name)), lod_overlay.MAX_TRAILING))
    view = lod_atlas.classic_view(assets, src)
    written = bob1.materials(bob1.parse(lod_overlay.gzip.decompress(p['stored']) if p['stored'][:2] == b'\x1f\x8b'
                                        else p['stored']))
    declared = lod_atlas.tangent_declared(src)
    layout = res['layout']
    ref = {s: v[0] for s, v in lod_atlas.bake(layout, res['textures'], levels=1).items()}
    dec = {s: e['decoded'].astype(np.float32) for s, e in res['encoded'].items()}
    ph = res['textures'].assets.read_entry(lod_atlas.lookup(assets, lod_atlas.CLASSIC_BUMP_SOURCE)[0])
    b_low = lod_atlas.decode_dds(ph)[0, 0, :3] * (2 / 255) - 1
    assets.cache.clear()
    out = {}
    for t in layout['tiles']:
        mis = [m for m in t['mats'] if 'classic' in view[m]]
        if not mis:
            continue
        if len(mis) != len(t['mats']):
            raise SystemExit(f'{name}: tile shared by classic and effect materials {t["mats"]}')
        (cx, cy), (cw, ch) = t['origin'], t['content']
        box = lambda img: img[cy:cy + ch, cx:cx + cw].reshape(-1, img.shape[-1]).astype(np.float64)
        d_o, d_m = box(ref['diffuse']) / 255, box(dec['diffuse']) / 255
        l_o, l_m = box(ref['light']) / 255, box(dec['light']) / 255
        for mi in mis:
            shape = view[mi]['classic']
            merged = {n.lower(): v for n, _, v in written[res['atlas_of'][mi]]['params']}
            bumped = b't_bumptexture' in merged and not lod_atlas.body_materials.is_null(merged[b't_bumptexture'])
            c_m = (merged[b'g_matdiffusestrength'][0], merged[b'g_matspecularstrength'][0],
                   merged[b'g_matspecularpower'][0]) + tuple(merged[b'g_matemissivecolor'][:3])
            c_o = record_constants(src[mi])
            texels = None
            if bumped:
                bump = box(dec['bump'])[:, [3, 1]]                      # x in A, y in G
                values, inverse = np.unique(bump, axis=0, return_inverse=True)
                texels = [(v, np.flatnonzero(inverse.reshape(-1) == k)) for k, v in enumerate(values)]
            for frame in (('tangent records', 'zero tangents') if declared else ('zero tangents',)):
                key = ('untextured' if not shape['textured'] else 'textured',
                       'self-illuminated' if any(c_o[3:]) else 'lit',
                       'BUMPMAP_LOW' if shape['low'] else 'DEFAULT',
                       ('declared, ' + frame) if declared else 'no tangent declaration',
                       'BUMPMAP' if bumped else 'DEFAULT', f'Kd {c_o[0] / 65536:g} Ks {c_o[1] / 65536:g}'
                       f' p {c_o[2] / 65536:g}')
                st = out.setdefault(key, dict(formula=[0.0, 0.0, 0], total=[0.0, 0.0, 0], alpha=[0.0, 0.0, 0],
                                              angle=0.0, materials=0, texels=0))
                st['materials'] += 1
                st['texels'] += len(d_o)
                n_o = normal(b_low, frame) if shape['low'] else NV
                co = lit(n_o, c_o)
                groups = texels or [(None, np.arange(len(d_o)))]
                for value, idx in groups:
                    if value is None:
                        n_m = NV
                    else:
                        x, y = value * (2 / 255) - 1
                        n_m = normal(np.array((x, y, np.sqrt(max(0.0, 1 - x * x - y * y)))), frame)
                    st['angle'] = max(st['angle'], float(np.degrees(np.arccos(np.clip(n_o @ n_m, -1, 1)))))
                    cm = lit(n_m, c_m)
                    for k in range(len(CONDITIONS)):
                        o = sat(co[k] * d_o[idx, :3] + l_o[idx, :3])
                        f = np.abs(o - sat(cm[k] * d_o[idx, :3] + l_o[idx, :3])) * 255
                        g = np.abs(o - sat(cm[k] * d_m[idx, :3] + l_m[idx, :3])) * 255
                        for s, e in (('formula', f), ('total', g)):
                            st[s][0] = max(st[s][0], float(e.max()))
                            st[s][1] += float(e.sum())
                            st[s][2] += e.size
                a = np.abs(np.stack([d_o[:, 3] - d_m[:, 3], l_o[:, 3] - l_m[:, 3]])) * 255
                st['alpha'][0] = max(st['alpha'][0], float(a.max()))
                st['alpha'][1] += float(a.sum())
                st['alpha'][2] += a.size
    return name, same, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('after')
    ap.add_argument('--game', type=Path, default=bob1.DEFAULT_GAME)
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--only', nargs='*')
    a = ap.parse_args()
    root = Path(a.after)
    marker = json.loads(next((root / 'addon').glob('*.x3m-lod.json')).read_text())
    record = json.loads((root / 'x3m-lod-batch.json').read_text())
    rows = {b['name']: b for b in record['bodies']}
    jobs = [(b['name'], rows[b['name']]['t_pad'], marker['atlas_options'], b) for b in marker['bodies']
            if b['atlas'].get('classic_materials') and (not a.only or b['name'] in a.only)]
    print(f'bodies with a merged classic material: {len(jobs)}; conditions {len(CONDITIONS)}'
          f' ({len(L0)} light-0 directions x {len(VIEW)} views)')
    with multiprocessing.Pool(a.jobs, init, (str(a.game),)) as pool:
        results = pool.map(body, jobs, chunksize=1)
    print(f'rebuilt atlases hash like the baked members: {sum(1 for _, s, _ in results if s)} of {len(results)}')
    total, bodies = {}, collections.defaultdict(set)
    for name, _, out in results:
        for key, st in out.items():
            bodies[key].add(name)
            t = total.setdefault(key, dict(formula=[0.0, 0.0, 0], total=[0.0, 0.0, 0], alpha=[0.0, 0.0, 0],
                                           angle=0.0, materials=0, texels=0))
            for s in ('formula', 'total', 'alpha'):
                t[s] = [max(t[s][0], st[s][0]), t[s][1] + st[s][1], t[s][2] + st[s][2]]
            t['angle'] = max(t['angle'], st['angle'])
            t['materials'] += st['materials']
            t['texels'] += st['texels']
    print('shape (diffuse, lighting, original technique, tangents, merged technique, constants): bodies,'
          ' materials, tile texels; max normal angle; formula max / mean; total max / mean; alpha max / mean')
    for key, t in sorted(total.items()):
        mean = lambda s: t[s][1] / t[s][2] if t[s][2] else 0.0
        print(f'  {" | ".join(key)}: bodies {len(bodies[key])}, materials {t["materials"]}, texels {t["texels"]};'
              f' angle {t["angle"]:.2f} deg; formula {t["formula"][0]:.2f} / {mean("formula"):.3f};'
              f' total {t["total"][0]:.2f} / {mean("total"):.3f}; alpha {t["alpha"][0]:.2f} / {mean("alpha"):.3f}')
    print('sha256 of this script: ' + hashlib.sha256(Path(__file__).read_bytes()).hexdigest()[:16])


if __name__ == '__main__':
    main()
