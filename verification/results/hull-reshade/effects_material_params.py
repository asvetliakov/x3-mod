"""Which effect parameters the hull hit re-shade's effects-pair bodies set (docs/architecture/hull-hit-look.md,
"Implementation": the effects DEFAULT PS 8360f422de08b5bd applies a preshader colour matrix in c0..c2 built from the
grading parameters `colormatrix`, `g_Brightness`, `g_Contrast`, `g_Saturation`, `g_Hue`; materials.md /
lod-overlay-mods.md). Prints, per body, every parameter name its effect material carries and whether any grading
parameter is among them. Names and values only, no texel or program bytes.

    python3 -I verification/results/hull-reshade/effects_material_params.py [--game /mnt/c/X3FL] \
        > verification/results/hull-reshade/effects_material_params_out.txt
"""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'tools/effects'))
from sector_fog_census import Assets  # noqa: E402
import bob1  # noqa: E402
import weapon_effects_census as wec  # noqa: E402

BODIES = ['v\\10659', 'v\\12000', 'v\\12001', 'v\\12002', 'v\\12003', 'v\\12004', 'v\\12005', 'v\\12006', 'v\\12007',
          'v\\12008']
GRADING = {'colormatrix', 'brightness', 'contrast', 'saturation', 'hue', 'g_brightness', 'g_contrast', 'g_saturation',
           'g_hue'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', default='/mnt/c/X3FL')
    a = Assets(pathlib.Path(parser.parse_args().game), edition='fl')
    graded = 0
    for name in BODIES:
        tree = bob1.parse(a.read_entry(bob1.resolve_body(a, name)))
        for m in bob1.materials(tree):
            if 'params' not in m:
                print(f'{name} mat {m["index"]}: classic record (no effect parameters)')
                continue
            names = [wec._str(n) for n, _, _ in m['params']]
            hits = [f'{wec._str(n)}={v}' for n, _, v in m['params'] if wec._str(n).lower() in GRADING]
            graded += bool(hits)
            print(f'{name} mat {m["index"]} effect {wec._str(m["effect"])}: {len(names)} parameters; '
                  f'grading {hits or "none"}')
    print(f'bodies {len(BODIES)}, materials with a grading parameter: {graded}')


if __name__ == '__main__':
    main()
