#!/usr/bin/env python3
"""The muzzle re-shade's per-body constants for every classic muzzle flash body (docs/architecture/muzzle-flash-look.md;
the census: verification/results/fl-muzzle-flash-draw/fl_muzzle_flash_census_out.txt), measured the way body 445's were
(muzzle_inputs.py, the same functions): the body's textured faces and the UV rectangle they map, that cell of the
body's own atlas (the MATERIAL3/5 texture id, the lab's decode, untracked), and from them

  mean_fit    445's method: the radial fit's amplitude a of a (1 - smoothstep(r0, 1, r)) (the cell's centre luminance)
  lum_glow    the texel luminance over the mesh area weighted by the glow g = (1 - d^2)^2 at the baked size
              (d = distance from the gun / (mz_size x lit radius)), the 24 x 24 samples of mi.lit_extent
  mean        the row's value for the added bodies: the body's own lum_glow, so the texel modulator
              t = lerp(1, lum / mean, 1 - smooth) keeps the body's grain at full strength (its glow-weighted t is 1 at
              every mz_smooth). 445 keeps its measured 0.166 (accepted in flight), its glow-weighted t is t445 =
              1 + (lum_glow_445 / 0.166 - 1)(1 - smooth), 0.255 at the baked smooth 0.1. 445's radial fit (mean_fit)
              assumes a round blob centred in its cell; on the strip cells (444, 446, 447, 314, 10463) the centre is
              dark or the fit sits at its r0 bound, so mean_fit is printed only.
  t_max       the modulator at the cell's brightest texel and at the brightest glow-region sample (the grain's reach)
  hue         the luminance-weighted hue of the lit texels, at luminance 1
  lit_radius  the 90 % energy radius from the gun (body origin), model units
  ray_mean    the summed glow a ray through the gun crosses at the default size, mean over 500 directions

overlap: measured for 445 only (fitted to the rendered real mesh, overlap_by_angle_out.txt). For the others it is
INFERRED and carries the energy normalisation: 445's rendered fit times (the body's ray_mean over 445's: the fan's
quads at the gun) times (1 / t445: the body's glow-weighted t, 1, over 445's), so gain x g x t / N, the fan's summed glow
luminance at the gun, equals 445's at the baked mz_smooth. At another mz_smooth s the ratio is
1 / (1 + (lum_glow_445 / 0.166 - 1)(1 - s)) against the baked 3.92: at s 1 the added bodies draw 3.92x dimmer than 445
relative to it, at s 0 1.48x brighter. The last block prints the muzzle_reshade_core.h rows.

  python3 -I verification/results/muzzle-reshade/muzzle_bodies.py [--game /mnt/c/X3FL] [--lab DIR] \
    > verification/results/muzzle-reshade/muzzle_bodies_out.txt
The AP Mayhem flash bodies (docs/reverse-engineering/ap-bullet-batch-site.md section 3: scenes 08002 / 08004 / 08005 /
08006 / 08311 and the 04437 override; 12011 is MATERIAL6 effects.fx like 12009, not this pair: out), the same way, 445
first so the normalisation is 445's (its row must equal the FL run's):
  python3 -I verification/results/muzzle-reshade/muzzle_bodies.py --game /mnt/c/X3 --edition ap \
    --lab <checkout>/verification/results/weapon-effects-lab/mayhem3 --bodies 445,12012,12013,12014,12016,12017,12018 \
    > verification/results/muzzle-reshade/muzzle_bodies_ap_out.txt
Prints derived numbers only; no game bytes are written; nothing launches.
"""
import argparse
import contextlib
import io
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import muzzle_inputs as mi  # noqa: E402

BODIES = (445, 444, 446, 447, 450, 314, 10463)  # the classic standard_lighting flash bodies (12009 is effects.fx: out)
OVERLAP_445 = (8.8915, 1.3111)  # bodies[0].overlap, measured (overlap_by_angle_out.txt)
SIZE, SMOOTH = 0.2, 0.1         # the baked mz_size and mz_smooth (muzzle-flash-look.md section 3)


def glow_luminance(png, tris, radius):
    """The texel luminance averaged over the mesh area weighted by the glow g = (1 - d^2)^2 at the baked size (d =
    distance from the gun / (size x lit radius)), the same 24 x 24 barycentric samples as mi.lit_extent."""
    import numpy as np
    from PIL import Image
    img = np.asarray(Image.open(png).convert('RGB')).astype(np.float64) / 255
    h, w = img.shape[:2]
    lum = img @ np.array(mi.W)
    num = den = top = 0.0
    n = 24
    for tri in tris:
        P = np.array([t[0] for t in tri], float)
        UV = np.array([t[1] for t in tri], float)
        area = 0.5 * np.linalg.norm(np.cross(P[1] - P[0], P[2] - P[0]))
        for i in range(n):
            for j in range(n - i):
                a, b = (i + 1 / 3) / n, (j + 1 / 3) / n
                c = 1 - a - b
                if c < 0:
                    continue
                d = np.linalg.norm(a * P[0] + b * P[1] + c * P[2]) / (SIZE * radius)
                if d >= 1:
                    continue
                uv = a * UV[0] + b * UV[1] + c * UV[2]
                x = min(w - 1, max(0, int(uv[0] * w)))
                y = min(h - 1, max(0, int(uv[1] * h)))
                g = (1 - d * d) ** 2 * area
                num += g * lum[y, x]
                den += g
                top = max(top, lum[y, x])
    return (num / den if den else 0.0), top


def measure(game, lab, body):
    src, verts, faces, tex = mi.body_mesh(game, body)
    hits = sorted((pathlib.Path(lab) / 'tex').glob(f'{tex}-*.png'))
    if not hits:
        raise SystemExit(f'body {body}: atlas tex/true/{tex} not in {lab}/tex')
    png = str(hits[0])
    log = io.StringIO()
    with contextlib.redirect_stdout(log):
        rect, tris = mi.body445(game, body)
        c = mi.cell(png, rect)
        radii = mi.lit_extent(png, tris)
        ray = mi.overlap(tris, radii[0.9])
    lum_glow, glow_max = glow_luminance(png, tris, radii[0.9])
    return dict(lum_glow=lum_glow, glow_max=glow_max, body=body, src=src, tex=tex, png=pathlib.Path(png).name, faces=len(faces), quads=len(tris) // 2,
                rect=rect, radius=radii[0.9], r50=radii[0.5], ray=ray, log=log.getvalue(), **c)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--game', default='/mnt/c/X3FL')
    parser.add_argument('--lab', default=str(mi.ROOT / 'verification/results/weapon-effects-lab/fl'))
    parser.add_argument('--edition', choices=('fl', 'ap'), default='fl')
    parser.add_argument('--bodies', default=','.join(str(b) for b in BODIES),
                        help='comma-separated body ids, 445 first (its ray sum and modulator normalise the others)')
    args = parser.parse_args()
    mi.EDITION = args.edition
    bodies = [int(b) for b in args.bodies.split(',')]
    if bodies[0] != 445:
        raise SystemExit('--bodies must start with 445')
    rows = [measure(args.game, args.lab, b) for b in bodies]
    for r in rows:
        print(f'=== body {r["body"]} ({r["src"]}), atlas tex/true/{r["tex"]} ({r["png"]})')
        print('\n'.join('  ' + line for line in r['log'].splitlines()))
    base = rows[0]['ray']

    def t_gun(lum, mean):
        return 1 + (lum / mean - 1) * (1 - SMOOTH)
    for r in rows:
        r['mean_fit'] = r['mean']
        r['mean'] = r['mean_fit'] if r['body'] == 445 else r['lum_glow']
    t445 = t_gun(rows[0]['lum_glow'], rows[0]['mean'])
    print('\nbody  tex quads  UV rect u0 u1 v0 v1         lit_radius    r50 mean_fit centre lum_glow t_gun(fit)  mean  '
          't_gun  hue K                     ray_mean N/N445  overlap (N0, N1)            t_max cell / glow')
    for r in rows:
        k = r['ray'] / base * (t_gun(r['lum_glow'], r['mean']) / t445)
        r['overlap'] = OVERLAP_445 if r['body'] == 445 else (OVERLAP_445[0] * k, OVERLAP_445[1] * k)
        u0, u1, v0, v1 = r['rect']
        print(f'{r["body"]:5d} {r["tex"]:4d} {r["quads"]:5d}  {u0:6.3f} {u1:6.3f} {v0:6.3f} {v1:6.3f}  '
              f'{r["radius"]:8.0f}  {r["r50"]:6.0f}  {r["mean_fit"]:.3f}  {r["centre"]:.4f}  {r["lum_glow"]:.4f}  '
              f'{t_gun(r["lum_glow"], r["mean_fit"]):7.3f}  {r["mean"]:.4f}  {t_gun(r["lum_glow"], r["mean"]):.3f}  '
              f'({r["hue"][0]:.4f}, {r["hue"][1]:.4f}, {r["hue"][2]:.4f})  {r["ray"]:7.2f}  {k:6.3f}  '
              f'({r["overlap"][0]:.4f}, {r["overlap"][1]:.4f}) {"measured" if r["body"] == 445 else "inferred"}  '
              f'{t_gun(r["max"], r["mean"]):6.2f} / {t_gun(r["glow_max"], r["mean"]):5.2f}')
    print(f't_gun: the glow-weighted modulator at mz_size {SIZE}, mz_smooth {SMOOTH} (fit: with mean_fit; the row: with '
          f'mean); t445 {t445:.4f}, the overlap factor 1 / t445 {1 / t445:.4f}; t_max: the modulator at the cell\'s '
          f'brightest texel / the brightest glow-region sample')
    print('\nmuzzle_reshade_core.h rows:')
    for r in rows:
        print(f'    {{{r["body"]}u, {r["radius"]:.0f}.f, {r["mean"]:.4f}f, {{{r["overlap"][0]:.4f}f, {r["overlap"][1]:.4f}f}}, '
              f'{{{r["hue"][0]:.4f}f, {r["hue"][1]:.4f}f, {r["hue"][2]:.4f}f}}}}, // tex/true/{r["tex"]}')


if __name__ == '__main__':
    main()
