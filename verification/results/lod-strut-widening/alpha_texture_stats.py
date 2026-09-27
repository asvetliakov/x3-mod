#!/usr/bin/env python3
"""Alpha statistics of the solar plant's lattice-card textures (material 11 and the other alpha-tested lattice
materials), read through the atlas resolver (lod_atlas.Textures): per box-filtered mip level the alpha
histogram (share < 1/255 = fails the engine's test GREATEREQUAL ref 1, share < 128, mean) and the coverage
change under a half-texel shift when the level is sampled at one texel per pixel: binary (alpha test at ref 1
and at a hypothetical ref 128, no blend) vs blended (|delta alpha|). The level the hardware selects at a size s
is log2(texels per px) with texels/px = T x (UV periods per px of the faces; 1.03 at s 65, 0.63 at s_d 106,
0.315 at T_pad 212 for material 11, uv_density_out.txt). Host-side; bottle catalogues read only."""
import sys
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import bob1, lod_atlas, body_materials, sector_fog_census as sfc  # noqa: E402

PERIODS_PER_PX = {65: 1.027, 106: 0.630, 212: 0.315, 500: 0.1335, 1000: 0.0668}   # material 11 (median face)


def pyramid(a):
    levels = [a.astype(np.float64)]
    while levels[-1].shape[0] > 1 or levels[-1].shape[1] > 1:
        x = levels[-1]
        h, w = x.shape[0] // 2 * 2, x.shape[1] // 2 * 2
        x = x[:h, :w] if h and w else x
        levels.append(x.reshape(x.shape[0] // 2, 2, x.shape[1] // 2, 2).mean((1, 3)) if h and w else x[:1, :1])
    return levels


def shift_metrics(a):
    """a: level alpha in 0..255 sampled at texel centres; a half-texel shift = bilinear midpoint along x."""
    if a.shape[1] < 2:
        return dict(flip1=0.0, flip128=0.0, dblend=0.0)
    s = 0.5 * (a[:, :-1] + a[:, 1:])
    c = a[:, :-1]
    return dict(flip1=float(np.mean((c >= 1) != (s >= 1))), flip128=float(np.mean((c >= 128) != (s >= 128))),
                dblend=float(np.mean(np.abs(c - s)) / 255))


def main():
    assets = sfc.Assets(bob1.DEFAULT_GAME)
    tex = lod_atlas.Textures(assets)
    tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, 'stations/station_scenes/others/argon_L_solarpowerplant')), 8)
    mats = bob1.materials(tree)
    for mi in (11, 9, 12, 0):
        sl = body_materials.slots(mats[mi])
        print(f'== material {mi}')
        for slot in ('diffuse', 'alpha', 'light'):
            name = sl.get(slot)
            img = tex.get(name) if name is not None and not body_materials.is_null(name) else None
            if img is None:
                print(f'  {slot}: {name} -> NULL / unresolved'); continue
            src = tex.source(name)
            img = np.asarray(img)
            print(f'  {slot}: {name.decode()} -> {src["member"]} kind {src["kind"]} shape {img.shape} dtype {img.dtype}')
            if img.ndim < 3 or img.shape[2] < 4:
                print('    no alpha channel'); continue
            A = img[:, :, 3].astype(np.float64)
            T = A.shape[1]
            levels = pyramid(A)
            for L, a in enumerate(levels):
                m = shift_metrics(a)
                print(f'    mip {L:2d} {a.shape[1]:4d}x{a.shape[0]:<4d} alpha<1 {np.mean(a < 1):.3f} <128 {np.mean(a < 128):.3f}'
                      f' mean {a.mean() / 255:.3f} min {a.min():.0f} max {a.max():.0f} | half-texel shift: flip@ref1 {m["flip1"]:.3f}'
                      f' flip@ref128 {m["flip128"]:.3f} blended |da| {m["dblend"]:.3f}')
            if slot == 'diffuse' and mi == 11:
                for s, ppp in PERIODS_PER_PX.items():
                    tpp = T * ppp
                    print(f'    at s {s}: {tpp:.1f} texels/px -> level {max(0.0, np.log2(tpp)):.1f} of {len(levels) - 1}')
    print('\nengine alpha test at these draws (run340 state rows): ALPHAFUNC 7 GREATEREQUAL, ALPHAREF 1 on every draw; '
          'MIPMAPLODBIAS 0; material 11 t_MipMapLODBias 0, aniso 8')


if __name__ == '__main__':
    main()
