#!/usr/bin/env python3
"""Compare two scratch batch bakes of the same bodies, without (--no-widen) and with strut widening (defaults):
per body T_pad, the widen report (strut / widenable / widened area share of the source record at s_d, patches,
faces, alpha classes), C's drawn groups by kind, atlas size, tiles, minimum texels per pixel, layout, atlas and body
member bytes, and the per-body bake seconds (batch record). The census column recomputes the strut area share of
record 0 at s_d with the 5120x1440 focal length (1,280 px; lod-strut-widening.md 1.2 used it) through
tools/analysis/thin_patches.py from the bottle X3 catalogues (read only). Bake outputs stay local (scratch).

Usage: bake_compare.py FLAT_OUT WIDE_OUT"""
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import bob1                      # noqa: E402
import sector_fog_census as sfc  # noqa: E402
import thin_patches              # noqa: E402

GAME = Path(os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'))


def load(out):
    out = Path(out)
    rec = {b['name']: b for b in json.loads((out / 'x3m-lod-batch.json').read_text())['bodies'] if b.get('eligible')}
    man = {}
    for p in sorted((out / 'addon').glob('*.x3m-lod.json')):
        man.update({b['name']: b for b in json.loads(p.read_text())['bodies']})
    return rec, man


def kinds(m):
    """C's groups by kind (manifest: atlas / widened materials, kept light bleed, alpha = the rest)."""
    a = m.get('atlas') or {}
    return m['draws'], a.get('widened_materials', [])


def main():
    flat, wide = load(sys.argv[1]), load(sys.argv[2])
    assets = sfc.Assets(GAME)
    for name in sorted(flat[1]):
        rf, mf = flat[0][name], flat[1][name]
        rw, mw = wide[0][name], wide[1][name]
        w = mw.get('widen') or {}
        tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, name)), 8)
        rec0 = bob1.lods(tree)[0]
        geo = thin_patches.face_geometry(rec0)
        s_d = mw['pad_threshold'] / w['divisor']
        k1280 = thin_patches.FOCAL['5120x1440'] * s_d / (geo['r_raw'] * 640)
        c = thin_patches.classify(geo, k1280)
        af, aw = mf['atlas'], mw['atlas']
        bytes_ = lambda m: (sum(x['bytes'] for x in m['members'][1:]), m['members'][0]['bytes'])
        print(f'== {name}: T_pad {mw["pad_threshold"]}, s_d {s_d:g}, r_raw {geo["r_raw"]:.0f}')
        print(f'  census at s_d, F 1280 (5120x1440): k {k1280:.6f} px/unit, strut area {100 * c["strut_area"]:.2f} %,'
              f' bevel {100 * c["bevel_area"]:.2f} %, thin patches {c["thin_patches"]} of {c["patches"]}')
        print(f'  bake widen at s_d, F {w["focal"]:g} (--display default 1920x1080): k_d {w["k_d"]:.6f}, W_u'
              f' {w["w_units"]:g} units; strut area {100 * w["strut_area"]:.2f} %, widenable (opaque atlased)'
              f' {100 * w["widenable_area"]:.2f} %, widened {100 * w["widened_area"]:.2f} %; patches'
              f' {w["widened_patches"]} {w["kinds"]}, faces {w["widened_faces"]}, points {w["widened_points"]};'
              f' skipped {w["skipped"]}; op {w["seconds"]:.2f} s')
        cls = {int(k): v for k, v in w['alpha_classes'].items()}
        tot = sum(cls.values())
        lo = sum(v for k, v in cls.items() if k <= 8)
        print(f'  alpha classes: {len(cls)} of 31 used; faces at a <= 1/4: {100 * lo / tot:.1f} %;'
              f' face-weighted mean a {sum(k * v for k, v in cls.items()) / tot / 32:.3f}')
        df, dw = kinds(mf)[0], kinds(mw)[0]
        print(f'  C drawn groups {df} -> {dw} ({dw - df:+d}); widened groups {w["draws_added"]} on'
              f' {w["widened_materials"]}; kept light-bleed groups {af.get("kept_light_bleed_draws", 0)} ->'
              f' {aw.get("kept_light_bleed_draws", 0)}; split groups {len(af.get("split_groups", []))} ->'
              f' {len(aw.get("split_groups", []))}')
        alpha_tiles = sum(1 for t in aw['tiles'] if t.get('alpha'))
        fmt = lambda a: ', '.join(f'{t["slot"]} {t["format"]}' for t in a['textures'])
        print(f'  atlas {af["size"]} -> {aw["size"]}; tiles {len(af["tiles"])} -> {len(aw["tiles"])} ({alpha_tiles}'
              f' alpha); min texels/px {af["min_texels_per_px"]:.3f} -> {aw["min_texels_per_px"]:.3f}; layout'
              f' {"clamped" if af["clamped"] else "uniform"} -> {"clamped" if aw["clamped"] else "uniform"};'
              f' scale {af["scale"]:.4f} -> {aw["scale"]:.4f}; formats [{fmt(af)}] -> [{fmt(aw)}]')
        (ab0, bb0), (ab1, bb1) = bytes_(mf), bytes_(mw)
        print(f'  stored bytes: atlas {ab0:,} -> {ab1:,} ({100 * (ab1 / ab0 - 1):+.1f} %), body member'
              f' {bb0:,} -> {bb1:,} ({100 * (bb1 / bb0 - 1):+.1f} %)')
        print(f'  bake seconds {rf["seconds"]:.2f} -> {rw["seconds"]:.2f} ({rw["seconds"] - rf["seconds"]:+.2f});'
              f' light_bleed counted {rf.get("light_bleed_counted")} -> {rw.get("light_bleed_counted")}, ignored'
              f' {len(rf.get("light_bleed_ignored") or [])} -> {len(rw.get("light_bleed_ignored") or [])}')


if __name__ == '__main__':
    main()
