#!/usr/bin/env python3
"""lod_overlay.py with the atlas mip chain as it was before 2026-09-23 (the "before" of
atlas_mip_bleed.py): 4-texel gutter and a box filter of the whole level-0 atlas per level
(lod_atlas.mip_chain / normal_mip_chain). Level 0 is baked by the current code, which gives
the same level 0 as before at the same gutter. Arguments as lod_overlay.py; use --out only.

  python3 verification/results/lod-overlay-pilot/build_box_mips.py --replace --collapse atlas ... --out DIR BODY=T ...
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import lod_atlas  # noqa: E402
import lod_overlay  # noqa: E402

_collapse, _bake = lod_atlas.collapse, lod_atlas.bake
lod_atlas.collapse = lambda *a, **k: _collapse(*a, **dict(k, gutter=4))
lod_atlas.bake = lambda layout, textures, levels=None: _bake(layout, textures, levels=1)
lod_atlas.to_levels = lambda slot, images: (lod_atlas.normal_mip_chain(images[0]) if slot == 'bump'
                                            else lod_atlas.mip_chain(images[0]))

if __name__ == '__main__':
    if '--install' in sys.argv:
        raise SystemExit('build_box_mips.py builds comparison overlays only (--out)')
    sys.exit(lod_overlay.main())
