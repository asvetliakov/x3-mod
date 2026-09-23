#!/usr/bin/env python3
"""Texel rule before/after the area-weighted texel floor (2026-09-23), over the bodies of
eligible_bodies.txt (NAME=T_pad@0) at the batch defaults: --display 1920x1080 (reference width
1800), atlas sizes 1024/2048, --min-texels 0.5, --texel-floor-share 0.10.

old: the uniform layout of lod_atlas.py at git revision OLD_REV (loaded from `git show`), refused
     when its minimum tile ratio < 0.5 (the old texel_floor).
new: the working-tree lod_atlas.plan_layout + texel_floor.
The slot set follows lod_atlas.collapse with --atlas-specular (the batch). Atlas bytes are the census
estimate (lod_batch_census.atlas_bytes: DDS with full mips, diffuse DXT1, other slots DXT5), not the
stored gzip bytes. Read-only; nothing is baked.

  python3 verification/results/lod-overlay-batch/texel_share_compare.py [--jobs N] > texel_share_compare_out.txt
"""
import argparse
import importlib.util
import multiprocessing
import os
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / 'tools' / 'analysis'))
import bob1            # noqa: E402
import lod_atlas       # noqa: E402
import lod_overlay     # noqa: E402
import lod_batch_census as census  # noqa: E402

OLD_REV = '4ff60c8a'
WIDTH, SIZES, MIN_TEXELS, SHARE = 1800, (1024, 2048), 0.5, 0.10
_W = {}


def old_module():
    src = subprocess.run(['git', '-C', str(REPO), 'show', f'{OLD_REV}:tools/analysis/lod_atlas.py'],
                         capture_output=True, check=True).stdout
    path = Path(tempfile.gettempdir()) / f'lod_atlas_{OLD_REV}_{os.getpid()}.py'
    path.write_bytes(src)
    spec = importlib.util.spec_from_file_location(f'lod_atlas_{OLD_REV}', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    path.unlink()
    return mod


def _init():
    _W['assets'] = lod_overlay.original_assets(bob1.DEFAULT_GAME)[0]
    _W['old'] = old_module()


def one(item):
    name, t = item
    assets, old = _W['assets'], _W['old']
    try:
        won = bob1.resolve_body(assets, name)
        data = assets.read_entry(won)
        tree = (bob1.parse_text(data) if won['path'].lower().endswith(('.pbd', '.bod'))
                else bob1.parse(data, lod_overlay.MAX_TRAILING))
        r0, mats = bob1.lods(tree)[0], bob1.materials(tree)
        alpha = lod_overlay.alpha_materials(mats)
        opaque = [g for p in r0['parts'] if not p['flags'] & lod_atlas.HIDDEN_PART for g in p['groups']
                  if g['material'] not in alpha]
        dom = lod_atlas.dominant(opaque)
        has = lambda m, slot: any(ty == 8 and n.lower() == lod_atlas.SLOT_NAMES[slot] for n, ty, _ in mats[m].get('params', ()))
        slots = (('diffuse', 'light') + (('bump',) if has(dom, 'bump') else ())
                 + (('specular',) if any(has(g['material'], 'specular') for g in opaque) else ()))
        px = t * WIDTH / 1280
        lo = old.plan_layout(r0, mats, alpha, old.Textures(assets), px, SIZES, old.GUTTER, slots)
        ln = lod_atlas.plan_layout(r0, mats, alpha, lod_atlas.Textures(assets), px, SIZES, lod_atlas.GUTTER, slots)
        x = lod_atlas.texel_floor(lod_atlas.tile_rows(ln), MIN_TEXELS, SHARE)
        return dict(name=name, old_size=lo['size'], old_min=lo['min_ratio'], new_size=ln['size'], new_min=ln['min_ratio'],
                    old_bytes=census.atlas_bytes(lo['size'], slots), new_bytes=census.atlas_bytes(ln['size'], slots),
                    weighted=x['weighted_texels_per_px'], starved=x['starved_share'], clamped=ln['clamped'],
                    old_ok=not lo['min_ratio'] < MIN_TEXELS, new_ok=not x['refuse'],
                    tiles_clamped=len(x['texel_clamped']))
    except Exception as exc:                        # noqa: BLE001 - reported per body
        return dict(name=name, error=f'{type(exc).__name__}: {exc}'[:160])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--jobs', type=int, default=4)
    a = ap.parse_args()
    items = []
    for line in (Path(__file__).with_name('eligible_bodies.txt')).read_text().splitlines():
        line = line.split('#', 1)[0].strip()
        if '=' in line:
            name, rest = line.split('=', 1)
            items.append((name, float(rest.split('@', 1)[0])))
    with multiprocessing.get_context('spawn').Pool(a.jobs, _init) as pool:
        rows = pool.map(one, items, chunksize=1)
    ok = [r for r in rows if 'error' not in r]
    print(f'bodies {len(rows)} (eligible_bodies.txt); layout errors {len(rows) - len(ok)}; width {WIDTH}, sizes'
          f' {list(SIZES)}, --min-texels {MIN_TEXELS}, --texel-floor-share {SHARE}; old = lod_atlas.py@{OLD_REV}')
    print(f'old texel_floor refusals {sum(not r["old_ok"] for r in ok)}; new {sum(not r["new_ok"] for r in ok)};'
          f' clamped layouts {sum(r["clamped"] for r in ok)}; bodies with texel_clamped tiles'
          f' {sum(r["new_ok"] and r["tiles_clamped"] > 0 for r in ok)}')
    sizes = lambda k: {n: sum(1 for r in ok if r[k] == n) for n in SIZES}
    print(f'atlas sizes old {sizes("old_size")}, new {sizes("new_size")}; atlas bytes (census estimate) old'
          f' {sum(r["old_bytes"] for r in ok)} -> new {sum(r["new_bytes"] for r in ok)}')
    cl = [r for r in ok if r['clamped']]
    print(f'clamped layouts by old uniform ratio: >= 2 {sum(1 for r in cl if r["old_min"] >= 2)}'
          f' (of which 2048 -> 1024 {sum(1 for r in cl if r["old_min"] >= 2 and r["old_size"] == 2048 and r["new_size"] == 1024)}),'
          f' 0.5..2 {sum(1 for r in cl if 0.5 <= r["old_min"] < 2)}, < 0.5 {sum(1 for r in cl if r["old_min"] < 0.5)}')
    print('eligibility changes (old -> new):')
    for r in sorted(ok, key=lambda r: r['name']):
        if r['old_ok'] != r['new_ok']:
            print(f'  {r["name"]} {"ok" if r["old_ok"] else "texel_floor"} -> {"ok" if r["new_ok"] else "texel_floor"}:'
                  f' old min {r["old_min"]:.3f} @{r["old_size"]}, new min {r["new_min"]:.3f} weighted'
                  f' {r["weighted"]:.3f} starved {100 * r["starved"]:.2f} % @{r["new_size"]}'
                  f'{" clamped" if r["clamped"] else ""}')
    print('per body: name old_min@size new_min@size weighted starved% layout old_bytes->new_bytes')
    for r in sorted(rows, key=lambda r: r['name']):
        if 'error' in r:
            print(f'  {r["name"]} error {r["error"]}')
            continue
        print(f'  {r["name"]} {r["old_min"]:.3f}@{r["old_size"]} {r["new_min"]:.3f}@{r["new_size"]}'
              f' {r["weighted"] if r["weighted"] is None else round(r["weighted"], 3)} {100 * r["starved"]:.2f}'
              f' {"clamped" if r["clamped"] else "uniform"} {r["old_bytes"]}->{r["new_bytes"]}')


if __name__ == '__main__':
    main()
