#!/usr/bin/env python3
"""What the bump maps of the pilot bodies' coarse records are (mip 0; numbers only).

Per distinct t_BumpTexture of the coarsest record's materials: size, DDS format, mean RGBA,
the length of the decoded tangent-space vector n = 2*rgb/255 - 1 (mean, p5, p95; ~1 for a
normal map), the share of texels with n.z > 0 (blue up), and the alpha spread (min, max,
share 255). Swizzle test (x in A, y in G, as DXT5 normal maps store them): corr(R, G),
corr(G, B), corr(A, G) and the share of texels with x^2 + y^2 <= 1.02 for x = 2A/255 - 1,
y = 2G/255 - 1. A height map would show one channel only; a swizzled normal map shows
R = G = B, A independent of G, and x^2 + y^2 <= 1.

  python3 verification/results/lod-overlay-pilot/bump_maps.py <body> ...
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import numpy as np  # noqa: E402
import bob1  # noqa: E402
import body_materials  # noqa: E402
import lod_atlas  # noqa: E402
import lod_overlay  # noqa: E402


def main(bodies):
    assets, skipped = lod_overlay.original_assets(bob1.DEFAULT_GAME)
    print(f'sources read without {skipped}')
    seen, rows = set(), []
    for body in bodies:
        tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, body)))
        mats, rec = bob1.materials(tree), bob1.lods(tree)[-1]
        alpha = lod_overlay.alpha_materials(mats)
        for mi in sorted({g['material'] for p in rec['parts'] for g in p['groups']} - alpha):
            name = body_materials.slots(mats[mi]).get('bump')
            if name is None or body_materials.is_null(name):
                print(f'{body} mat{mi}: bump NULL')
                continue
            if name.lower() in seen:
                continue
            seen.add(name.lower())
            data = lod_atlas.texture_bytes(assets, name)
            w, h, mips, fmt = lod_atlas.dds_format(data)
            img = lod_atlas.decode_dds(data).reshape(-1, 4).astype(np.float64)
            n = 2 * img[:, :3] / 255 - 1
            ln = np.linalg.norm(n, axis=1)
            grey = np.mean((np.abs(img[:, 0] - img[:, 1]) < 3) & (np.abs(img[:, 1] - img[:, 2]) < 3))
            a = img[:, 3]
            x, y = 2 * a / 255 - 1, 2 * img[:, 1] / 255 - 1
            corr = lambda p, q: np.corrcoef(p, q)[0, 1] if p.std() > 0 and q.std() > 0 else float('nan')
            swz = (corr(img[:, 0], img[:, 1]), corr(img[:, 1], img[:, 2]), corr(a, img[:, 1]),
                   np.mean(x * x + y * y <= 1.02))
            rows.append((ln.mean(), grey, swz))
            print(f'{body_materials.short(name)} {w}x{h} {fmt} mips {mips} mean_rgba {np.round(img.mean(0)).astype(int).tolist()}'
                  f' |n| mean {ln.mean():.3f} p5 {np.percentile(ln, 5):.3f} p95 {np.percentile(ln, 95):.3f}'
                  f' nz>0 {np.mean(n[:, 2] > 0):.3f} grey {grey:.3f}'
                  f' alpha min {a.min():.0f} max {a.max():.0f} share255 {np.mean(a == 255):.3f}'
                  f' corr(R,G) {swz[0]:.3f} corr(G,B) {swz[1]:.3f} corr(A,G) {swz[2]:.3f} xy_in_unit {swz[3]:.4f}')
    ln = np.array([r[0] for r in rows])
    sw = np.array([r[2] for r in rows])
    print(f'distinct bump maps {len(rows)}: mean |n| {ln.min():.3f}..{ln.max():.3f},'
          f' grey share {min(r[1] for r in rows):.3f}..{max(r[1] for r in rows):.3f};'
          f' corr(R,G) min {np.nanmin(sw[:, 0]):.3f} corr(G,B) min {np.nanmin(sw[:, 1]):.3f}'
          f' |corr(A,G)| max {np.nanmax(np.abs(sw[:, 2])):.3f} xy_in_unit min {sw[:, 3].min():.4f}')


if __name__ == '__main__':
    main(sys.argv[1:])
