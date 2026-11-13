#!/usr/bin/env python3
"""The muzzle flash re-shade's inputs (docs/architecture/muzzle-flash-look.md; docs/reverse-engineering/fl/
muzzle-flash-draw.md): derived numbers only.

1. The pair the flash draws with: the FL `standard_lighting2s` DEFAULT programs (the flash's Materials row 384 carries
   MPF_2SIDED, so the two-sided effect; root + addon2, tools/analysis/effect_passes.py _archive_effects), with every
   standard_lighting DEFAULT variant's pair, so the re-shade's vertex-shader gate (VS 494fe349b8bc12ec) is known to be
   the one partner of all of them.
2. Body 445 (objects/v/00445, the FL catalogues): its textured faces, the UV rectangle every quad maps (the atlas cell's
   bounds), and which UV edge lies at the gun (the smaller model z).
3. The atlas cell of tex/true/384 (the lab's decode, untracked): its luminance statistics, the radial mean of the
   luminance against r (the distance from the cell centre over the cell's half extents), the fit of that mean that the
   program uses (a (1 - smoothstep(r0, 1, r))), the vanilla lit radius r_v (the mean below 5 % of its centre value),
   the luminance-weighted hue and the luminance centroid.

  python3 -I verification/results/muzzle-reshade/muzzle_inputs.py [--game /mnt/c/X3FL] [--texture PNG] \
    > verification/results/muzzle-reshade/muzzle_inputs_out.txt
No game bytes are written; nothing launches.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/results/fl-muzzle-flash-draw'))
W = (0.2126, 0.7152, 0.0722)
EDITION = 'fl'  # the catalogue view body_mesh mounts (muzzle_bodies.py --edition ap: every catalogue an AP tree mounts)


def pairs(game):
    from effect_passes import _archive_effects, parse_container, VERTEX_SHADER_STATE, PIXEL_SHADER_STATE
    from shader_coverage_offline import _program
    print('standard_lighting DEFAULT pairs (FL root + addon2, shader/3_0 and its variants):')
    seen = {}
    for _, path, data in _archive_effects(game, edition='fl'):
        if '/3_0/' not in path or '/standard_lighting' not in path:
            continue
        c = parse_container(data)
        for ti, (tech, tps) in enumerate(c['techniques']):
            if tech != 'DEFAULT':
                continue
            for pi, (_, states) in enumerate(tps):
                pair = {}
                for si, st in enumerate(states):
                    if st[0] in (VERTEX_SHADER_STATE, PIXEL_SHADER_STATE):
                        prog = _program(data, c, ti, pi, si)
                        if prog:
                            pair['vs' if st[0] == VERTEX_SHADER_STATE else 'ps'] = (prog[0], prog[2])
                key = (pair.get('vs'), pair.get('ps'))
                seen.setdefault(key, []).append(path.split('shader/3_0/')[1])
    for (vs, ps), paths in sorted(seen.items(), key=str):
        print(f'  vs {vs[0]} ({vs[1] >> 8 & 0xff}_{vs[1] & 0xff}) ps {ps[0]} ({ps[1] >> 8 & 0xff}_{ps[1] & 0xff})'
              f' {len(paths)} effects, e.g. {sorted(paths)[0]}')


def body_mesh(game, body):
    """A classic body's textured triangles (objects/v/<body>.bod of the FL catalogues): the vertex block up to the
    `-1; -1; -1;` terminator (any spacing), the faces that carry a UV set (`<mat>; i; j; k; <flags>; 1; u v u v u v`;
    the untextured `-9` faces and the parts' dummies drop out), and the MATERIAL3/5 texture id. Returns (source,
    vertices, faces, texture id)."""
    import fl_muzzle_flash_census as census
    a = census.mount(game, EDITION)
    text, src = census.text(a, 'objects/v/%05d.bod' % body)
    coords, rest = re.split(r'^\s*-1;\s*-1;\s*-1;', text, maxsplit=1, flags=re.M)
    verts = [tuple(int(x) for x in m) for m in
             re.findall(r'^\s*(-?\d+);\s*(-?\d+);\s*(-?\d+);\s*/\d+', coords, re.M)]
    faces = re.findall(r'^\s*(\d+);\s*(\d+);\s*(\d+);\s*(\d+);\s*-?\d+;\s*1;\s*([-\d.;\s]+?)/\((\d+)\)', rest, re.M)
    tex = re.search(r'^\s*MATERIAL[35]:\s*\d+;\s*(\d+);', text, re.M)
    return src, verts, faces, int(tex.group(1)) if tex else None


def body445(game, body=445):
    src, verts, faces, _ = body_mesh(game, body)
    us, vs, gun, far = [], [], [], []
    for f in faces:
        idx = [int(f[1]), int(f[2]), int(f[3])]
        uv = [float(x) for x in f[4].replace(';', ' ').split()]
        for k in range(3):
            u, v = uv[2 * k], uv[2 * k + 1]
            us.append(u)
            vs.append(v)
            (gun if v > 0.25 else far).append(verts[idx[k]][2])
    print(f'body {body} ({src}): {len(verts)} vertices, {len(faces)} textured faces')
    print(f'  UV rectangle u {min(us):.6f}..{max(us):.6f}, v {min(vs):.6f}..{max(vs):.6f}; '
          f'centre ({(min(us) + max(us)) / 2:.7f}, {(min(vs) + max(vs)) / 2:.7f}), '
          f'half extents ({(max(us) - min(us)) / 2:.7f}, {(max(vs) - min(vs)) / 2:.7f})')
    if body == 445:
        print(f'  model z of the v ~0.51 edge: mean {sum(gun) / len(gun):.0f} (range {min(gun)}..{max(gun)}); '
              f'of the v ~0 edge: mean {sum(far) / len(far):.0f} (range {min(far)}..{max(far)}): the v ~0.51 edge is at the gun')
    tris = []
    for f in faces:
        idx = [int(f[1]), int(f[2]), int(f[3])]
        uv = [float(x) for x in f[4].replace(';', ' ').split()]
        tris.append([(verts[idx[k]], (uv[2 * k], uv[2 * k + 1])) for k in range(3)])
    return (min(us), max(us), min(vs), max(vs)), tris


def lit_extent(png, tris):
    """Vanilla's lit extent from the gun: every textured triangle sampled on a 24 x 24 barycentric grid, each sample's
    emitted energy its texel luminance x the triangle's area share, binned by the distance from the body origin (the
    gun, model units); the radii holding 50, 90 and 95 % of the energy."""
    import numpy as np
    from PIL import Image
    img = np.asarray(Image.open(png).convert('RGB')).astype(np.float64) / 255
    h, w = img.shape[:2]
    lum = img @ np.array(W)
    dist, energy = [], []
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
                pos = a * P[0] + b * P[1] + c * P[2]
                uv = a * UV[0] + b * UV[1] + c * UV[2]
                x = min(w - 1, max(0, int(uv[0] * w)))
                y = min(h - 1, max(0, int(uv[1] * h)))
                dist.append(float(np.linalg.norm(pos)))
                energy.append(lum[y, x] * area)
    dist, energy = np.array(dist), np.array(energy)
    order = np.argsort(dist)
    cum = np.cumsum(energy[order]) / energy.sum()
    radii = {q: float(dist[order][np.searchsorted(cum, q)]) for q in (0.5, 0.9, 0.95)}
    print(f'vanilla energy radius from the gun (model units): 50 % {radii[0.5]:.0f}, 90 % {radii[0.9]:.0f}, '
          f'95 % {radii[0.95]:.0f}; the textured vertices reach {max(float(np.linalg.norm(np.array(t[0]))) for tri in tris for t in tri):.0f}')
    return radii


def overlap(tris, radius):
    """The fan's overlap at the gun: the number of quads (triangle pairs) that pass within 0.1 x the lit radius of the
    body origin (a ray through the gun's screen position crosses about that many quads, every one adding its glow), and
    the mean over view directions (500, a Fibonacci lattice) of the glow a ray through the origin sums at the default
    size: each crossed quad adds g = (1 - (t / (0.5 r))^2)^2 at its crossing distance t (0 past 0.5 r). The re-shade
    divides each quad's radiance by that mean so mz_gain is the summed glow luminance at the gun."""
    import numpy as np
    quads = [tris[i:i + 2] for i in range(0, len(tris), 2)]

    def tri_point_dist(P, q):
        # the distance from q to triangle P (3x3), by projection and the edges
        a, b, c = P
        n = np.cross(b - a, c - a)
        nn = np.linalg.norm(n)
        if nn == 0:
            return float('inf')
        n = n / nn
        d = float(np.dot(q - a, n))
        proj = q - d * n
        v0, v1, v2 = b - a, c - a, proj - a
        d00, d01, d11, d20, d21 = v0 @ v0, v0 @ v1, v1 @ v1, v2 @ v0, v2 @ v1
        den = d00 * d11 - d01 * d01
        v = (d11 * d20 - d01 * d21) / den
        w = (d00 * d21 - d01 * d20) / den
        if v >= 0 and w >= 0 and v + w <= 1:
            return abs(d)

        def seg(p0, p1):
            t = np.clip(np.dot(q - p0, p1 - p0) / np.dot(p1 - p0, p1 - p0), 0, 1)
            return float(np.linalg.norm(q - (p0 + t * (p1 - p0))))
        return min(seg(a, b), seg(b, c), seg(c, a))
    origin = np.zeros(3)
    near = 0
    for qd in quads:
        dist = min(tri_point_dist(np.array([t[0] for t in tri], float), origin) for tri in qd)
        near += dist < 0.1 * radius

    def ray_hits(direction):
        hits, glow = 0, 0.0
        for qd in quads:
            hit = False
            for tri in qd:
                a, b, c = (np.array(t[0], float) for t in tri)
                e1, e2 = b - a, c - a
                h = np.cross(direction, e2)
                det = e1 @ h
                if abs(det) < 1e-9:
                    continue
                f = 1 / det
                sv = -a
                u = f * (sv @ h)
                qv = np.cross(sv, e1)
                v = f * (direction @ qv)
                t = f * (e2 @ qv)
                if 0 <= u <= 1 and 0 <= v and u + v <= 1 and abs(t) < 0.5 * radius:
                    hit = True
                    x2 = (t / (0.5 * radius)) ** 2
                    glow += (1 - x2) ** 2
            hits += hit
        return hits, glow
    n = 500
    counts, glows = [], []
    for i in range(n):
        z = 1 - 2 * (i + 0.5) / n
        r = np.sqrt(1 - z * z)
        phi = i * np.pi * (3 - np.sqrt(5))
        h, g = ray_hits(np.array([r * np.cos(phi), r * np.sin(phi), z]))
        counts.append(h)
        glows.append(g)
    counts, glows = np.array(counts), np.array(glows)
    print(f'fan overlap at the gun: {near} of {len(quads)} quads pass within 0.1 x the lit radius; a ray through the gun '
          f'crosses {counts.mean():.1f} quads within 0.5 x the radius on average over {n} directions (min {counts.min()}, '
          f'max {counts.max()}); summed glow at the default size: mean {glows.mean():.2f} (p10 {np.percentile(glows, 10):.2f}, '
          f'p90 {np.percentile(glows, 90):.2f})')
    return float(glows.mean())


def cell(png, rect):
    import numpy as np
    from PIL import Image
    img = np.asarray(Image.open(png).convert('RGBA')).astype(np.float64) / 255
    h, w = img.shape[:2]
    u0, u1, v0, v1 = rect
    cu, cv, hu, hv = (u0 + u1) / 2, (v0 + v1) / 2, (u1 - u0) / 2, (v1 - v0) / 2
    ys, xs = np.mgrid[0:h, 0:w]
    U, V = (xs + 0.5) / w, (ys + 0.5) / h
    inside = (U >= u0) & (U <= u1) & (V >= v0) & (V <= v1)
    L = img[:, :, :3] @ np.array(W)
    Li = L[inside]
    print(f'texture {w}x{h}; cell texels {int(inside.sum())}: luminance max {Li.max():.3f} mean {Li.mean():.4f} '
          f'p99 {np.percentile(Li, 99):.3f}; alpha {img[:, :, 3][inside].min():.3f}..{img[:, :, 3][inside].max():.3f}')
    cx, cy = (U[inside] * Li).sum() / Li.sum(), (V[inside] * Li).sum() / Li.sum()
    print(f'luminance centroid u {cx:.4f} v {cy:.4f} (normalised {(cx - cu) / hu:+.3f}, {(cy - cv) / hv:+.3f}: '
          f'towards the gun edge)')
    R = np.hypot((U - cu) / hu, (V - cv) / hv)
    edges = np.arange(0.0, 1.45, 0.05)
    centres, means = [], []
    print('radial mean luminance (r over the half extents):')
    for a, b in zip(edges[:-1], edges[1:]):
        m = inside & (R >= a) & (R < b)
        if m.sum():
            centres.append((a + b) / 2)
            means.append(L[m].mean())
    centres, means = np.array(centres), np.array(means)
    print('  ' + ' '.join(f'{c:.3f}:{m:.4f}' for c, m in zip(centres, means)))
    centre_mean = means[centres < 0.3].mean()
    below = centres[(means < 0.05 * centre_mean) & (centres > 0.3)]
    r_v = float(below.min() - 0.025) if len(below) else 1.0
    print(f'centre value (r < 0.3) {centre_mean:.4f}; vanilla lit radius r_v (first band under 5 %, its inner edge) '
          f'{r_v:.3f}')

    def smooth(x):
        x = np.clip(x, 0, 1)
        return x * x * (3 - 2 * x)
    best = None
    for r0 in np.arange(0.0, 0.9, 0.01):
        for a in np.arange(0.02, 1.0, 0.002):
            fit = a * (1 - smooth((centres - r0) / (1 - r0)))
            err = float(((fit - means) ** 2)[centres <= 1.1].sum())
            if best is None or err < best[0]:
                best = (err, a, r0)
    err, a, r0 = best
    fit = a * (1 - smooth((centres - r0) / (1 - r0)))
    print(f'fit a (1 - smoothstep(r0, 1, r)): a {a:.3f} r0 {r0:.2f}, rms {np.sqrt(err / (centres <= 1.1).sum()):.4f}, '
          f'worst |fit - mean| {np.abs(fit - means)[centres <= 1.1].max():.4f}')
    lit = inside & (L > 0.02)
    rgb = (img[:, :, :3][lit] * L[lit][:, None]).sum(axis=0) / L[lit].sum()
    k = rgb / float(rgb @ np.array(W))
    print(f'luminance-weighted hue of the lit texels (luminance > 0.02, {int(lit.sum())}): rgb/max '
          f'{tuple(round(float(x), 3) for x in rgb / rgb.max())}; at luminance 1 K = '
          f'({k[0]:.4f}, {k[1]:.4f}, {k[2]:.4f})')
    sd = [float(L[inside & (R >= x) & (R < x + 0.1)].std()) for x in (0.0, 0.3, 0.6)]
    print(f'texel noise: luminance sd in r bands 0-0.1 {sd[0]:.3f}, 0.3-0.4 {sd[1]:.3f}, 0.6-0.7 {sd[2]:.3f}')
    return {'mean': float(a), 'centre': float(centre_mean), 'hue': tuple(float(x) for x in k), 'r0': float(r0),
            'max': float(Li.max())}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--game', default='/mnt/c/X3FL')
    parser.add_argument('--texture', default=str(ROOT / 'verification/results/weapon-effects-lab/fl/tex/384-bd79416d.png'))
    args = parser.parse_args()
    pairs(args.game)
    rect, tris = body445(args.game)
    cell(args.texture, rect)
    radii = lit_extent(args.texture, tris)
    overlap(tris, radii[0.9])


if __name__ == '__main__':
    main()
