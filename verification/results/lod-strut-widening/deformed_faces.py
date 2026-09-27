#!/usr/bin/env python3
"""Non-widened faces deformed by widen_thin_patches on record 0 of real bodies (bottle X3 catalogues, read only),
with the bake's exclusions (alpha materials, kept effects; the light-bleed rebuild is not modelled) at s_d =
T_pad / 2 on the default 1920x1080 display (screen width 1800). Prints per body: widened faces, points duplicated
because a non-widened face uses them too, non-widened faces that use such a point (they would have been dragged
before the duplication), and non-widened faces whose points differ from record 0 now (expected 0).

Usage: deformed_faces.py NAME=T_PAD [NAME=T_PAD ...]"""
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import bob1          # noqa: E402
import lod_atlas     # noqa: E402
import lod_overlay   # noqa: E402

GAME = Path(os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'))


def main():
    assets, _ = lod_overlay.original_assets(GAME, lod_overlay.installed_markers(GAME))
    for spec in sys.argv[1:]:
        name, t_pad = spec.split('=')
        tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, name)), 8)
        rec, mats = bob1.lods(tree)[0], bob1.materials(tree)
        alpha = lod_overlay.alpha_materials(mats, assets, record=rec)
        exclude = frozenset(set(alpha) | set(lod_atlas.excluded_materials(mats, rec, alpha)))
        k_d = lod_overlay.design_scale(rec, int(t_pad), lod_overlay.WIDEN_DEFAULTS, 1800)[0]
        src, rep = lod_overlay.widen_thin_patches(rec, mats, k_d, exclude=exclude)
        n = len(rec['points'])
        dup_of = set()
        for p in src['parts']:
            for g in p['groups']:
                if g.get('widen'):
                    dup_of |= {i for f in g['faces'] for i in f[:3] if i >= n}
        rest = [f for p in src['parts'] for g in p['groups'] if not g.get('widen') for f in g['faces']]
        # widened faces in record-0 indices: the faces the op moved out of the non-widened set
        orig_faces = [tuple(f) for p in rec['parts'] for g in p['groups'] for f in g['faces']]
        rest_set = {tuple(x) for x in rest}
        widened_orig = [f for f in orig_faces if f not in rest_set]
        used_rest = {i for f in rest for i in f[:3]}
        shared = {i for f in widened_orig for i in f[:3]} & used_rest
        would = sum(1 for f in rest if any(i in shared for i in f[:3]))
        now = sum(1 for f in rest if any(i >= n or src['points'][i] != rec['points'][i] for i in f[:3]))
        print(f'{name} (T_pad {t_pad}): widened faces {rep["widened_faces"]}, duplicated points'
              f' {rep["duplicated_points"]} ({len(dup_of)} referenced by widened faces), non-widened faces on a shared'
              f' point (dragged without the duplication) {would}, non-widened faces deformed now {now}'
              f' (of {len(rest)})')


if __name__ == '__main__':
    main()
