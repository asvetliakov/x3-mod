"""The hull hit re-shade's per-body table (docs/architecture/hull-hit-look.md section 3; src/proxy/hull_reshade_core.h
bodies[]), from the census output beside this script (fl_hull_hit_census_out.txt: each impact body's pair, blend,
texture and flipbook) and the local, untracked lab textures (verification/results/weapon-effects-lab/fl/tex, the
level-0 decode of the FL catalogue textures; tools/effects/weapon_effects_lab_assets.py).

Per body: the texture's luminance p99 over its lit level-0 texels (luminance > 0.02, Rec. 709 weights) and the
normaliser luminance_reference / that p99 (luminance_reference = 2.75 x 0.6169, bullet_reshade_core.h). The design's
rule took the p99 over every texel (the bullets' texture_scale rule); on a sparse texture (the sparks: 8 % of texels lit)
that p99 falls in the orange haze, which then saturates l and takes the white core term: the fixture's sparks f02 frame
drew a flat beige disc (2026-11-13). Over the lit texels the p99 is the spark points' level (0.558 against 0.202) and
the other textures move by +0 to +4 % where the all-texel p99 was already >= 0.93, +10 % (376), +15 % (the
shockwave, the green explosion), +28.5 % (383) and 1.8-4x on the sparse ones (sparks, electric, implosion; per body in
the output). That p99 is floored at HAZE_FLOOR 0.5: a texture whose lit texels stay below half
of white is a haze by design (exp_glow, the IRE's second element, 0.133; the lightning and wave sprites 0.16-0.24), and
lifting it to 0..1 saturated l over a wide disc that the core term turned grey-beige with the 8-bit steps showing as
rings (the fixture's ire-100ms frame, 2026-11-13); floored, its peak stays below the core's range and keeps its hue
under the body term and the fade. The 4 x 4 cell
flag (a negative group / diffuse index naming a types/Animations row with TATF_COORDS), the pair (effects DEFAULT or
standard_lighting DEFAULT), the admitted blend, whether a non-impact row also draws the body (shared), and, reported
only, the texture's peak and the luminance-weighted hue of its lit texels at luminance 1. Flipbooks use their first
page (the note: the sparks pages differ by 2 % in p99). Excluded by the design: the three death-shared bodies
(v/10656, 10664, 10670; the explosions family) and bullet_ElectroSpike (the bullet route's own body). The first part
reproduces the note's measured texture statistics. Prints names, ids and figures only; no texel bytes.

    python3 -I verification/results/fl-hull-hit-draw/hull_hit_bodies.py \
        > verification/results/fl-hull-hit-draw/hull_hit_bodies_out.txt
"""
import os
import pathlib
import re

import numpy as np
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
LAB = ROOT / 'verification/results/weapon-effects-lab/fl/tex'
LAB = pathlib.Path(os.environ.get('X3M_LAB_TEX', LAB))  # a linked worktree: the main checkout's untracked copy
CENSUS = HERE / 'fl_hull_hit_census_out.txt'
LUMINANCE_REFERENCE = 2.75 * 0.6169  # bullet_reshade_core.h luminance_reference
HAZE_FLOOR = 0.5  # the lit p99 below which a texture is a haze: its normaliser stops at LUMINANCE_REFERENCE / 0.5
EXCLUDED = {'10656': 'death', '10664': 'death', '10670': 'death', 'effects\\weapons\\bullet_ElectroSpike': 'bullet'}
W = np.array([0.2126, 0.7152, 0.0722])


def texture(stem):
    hits = sorted(p for p in LAB.glob(stem + '-*.png') if re.fullmatch(re.escape(stem) + r'-[0-9a-f]{8}', p.stem))
    if not hits:
        raise SystemExit(f'lab texture {stem} missing under {LAB}')
    return hits[0]


def stats(stem):
    a = np.asarray(Image.open(texture(stem)).convert('RGBA')).astype(np.float64) / 255.0
    rgb = a[..., :3].reshape(-1, 3)
    lum = rgb @ W
    p99 = float(np.percentile(lum, 99))
    lit = lum > 0.02
    lit_p99 = float(np.percentile(lum[lit], 99)) if lit.any() else p99
    hue = (rgb[lit] * lum[lit, None]).sum(0) / max(lum[lit].sum(), 1e-9) if lit.any() else np.ones(3)
    hue = hue / max(float(hue @ W), 1e-9)
    return dict(p99=p99, lit_p99=lit_p99, peak=float(lum.max()), mean=float(lum.mean()), hue=hue,
                lit=float(lit.mean()))


def bodies():
    text = CENSUS.read_text()
    body_part = text.split('bodies (resolved once):', 1)[1]
    sections = re.split(r'\n  (?=v\\\d+:|effects\\)', '\n' + body_part)
    out = {}
    for s in sections:
        m = re.match(r'(v\\(\d+)|effects\\\S+?):', s.strip())
        if not m:
            continue
        name = m.group(1)
        key = m.group(2) or name
        if 'mat 0 effect effects.fx' in s:
            pair = 'effects'
        elif 'standard_lighting' in s:
            pair = 'classic'
        else:
            continue
        b = re.search(r'blend (\w+)/(\w+)', s)
        blend = f'{b.group(1)}/{b.group(2)}' if b else ('ONE/ONE' if 'MPF_DESTINATIONBLEND' in s else 'unknown')
        anim = re.search(r"textures \['([^']+)'", s)
        if anim:
            stem, cells = anim.group(1).split('\\')[-1].removesuffix('.tga').removesuffix('.dds'), 4
        else:
            cells = 0
            d = re.search(r"diffuse '([^']+)'", s)
            t = re.search(r'texture id (\d+)|texture (\d+),', s)
            if d and not d.group(1).startswith('-'):
                stem = d.group(1).split('\\')[-1].rsplit('.', 1)[0]
            elif t:
                stem = t.group(1) or t.group(2)
            else:
                raise SystemExit(f'{name}: no texture')
        out[key] = dict(name=name, pair=pair, blend=blend, stem=stem, cells=cells)
    # shared: a body some non-impact row draws ([OTHER] rows) or an impact row a muzzle column (c21) also names
    shared = set()
    for line in text.splitlines():
        r = re.match(r'\s+\[(impact|OTHER)\] row \d+: ([^;]+);(.*)', line)
        if not r:
            continue
        names = [n.strip() for n in r.group(2).split(',')]
        if r.group(1) == 'OTHER' or 'TBullets c21' in r.group(3):
            shared.update(n.removeprefix('v\\') for n in names)
    # how many TBullets rows draw the body (the scan order: most frequent first)
    weapons = {m.group(1).lstrip('0') or '0': int(m.group(2))
               for m in re.finditer(r'^  v\\(\d+): (\d+) TBullets rows', text, re.M)}
    for k, v in out.items():
        v['shared'] = k in shared
        v['weapons'] = weapons.get(k.lstrip('0'), 0)
    return out


def main():
    print('# the note\'s measured texture statistics (hull-hit-look.md section 3): p99 and peak luminance, level 0')
    for stem in ['380', '383', '131', '376', 'exp_sparks1_diff', 'exp_sparks2_diff', 'exp_glow_diff', 'exp_glow2_diff',
                 'exp_impact_glow_diff', 'exp_PL_imp_diff', 'exp_small_diff']:
        s = stats(stem)
        print(f'{stem:24s} p99 {s["p99"]:.4f} lit_p99 {s["lit_p99"]:.4f} peak {s["peak"]:.4f} mean {s["mean"]:.4f} '
              f'lit {s["lit"]:.3f} '
              f'hue {s["hue"][0]:.3f},{s["hue"][1]:.3f},{s["hue"][2]:.3f}')
    table = bodies()
    print(f'# census bodies {len(table)} (bullet_ElectroSpike included); luminance_reference {LUMINANCE_REFERENCE:.6f}')
    rows = []
    for key, b in sorted(table.items(), key=lambda kv: (-kv[1]['weapons'], int(kv[0]) if kv[0].isdigit() else 0)):
        if key in EXCLUDED:
            print(f'excluded {b["name"]:40s} pair {b["pair"]} blend {b["blend"]} texture {b["stem"]} reason {EXCLUDED[key]}')
            continue
        s = stats(b['stem'])
        n = LUMINANCE_REFERENCE / max(s['lit_p99'], HAZE_FLOOR)
        admitted = 'screen' if b['blend'] == 'ONE/INVSRCCOLOR' else 'additive'  # unknown (v/10653): ONE / ONE only
        print(f'body {int(key):5d} weapons {b["weapons"]:2d} pair {b["pair"]:7s} blend {b["blend"]:15s} admitted {admitted:8s} cells {b["cells"]} '
              f'shared {int(b["shared"])} texture {b["stem"]:26s} p99 {s["p99"]:.4f} lit_p99 {s["lit_p99"]:.4f} peak {s["peak"]:.4f} '
              f'normaliser {n:.4f} hue {s["hue"][0]:.3f},{s["hue"][1]:.3f},{s["hue"][2]:.3f}')
        rows.append((int(key), n, b['cells'], b['pair'], admitted, b['shared'], b['stem'], s['lit_p99']))
    print(f'# bodies[] ({len(rows)} rows; hull_reshade_core.h, held to these lines by '
          'verification/analysis/test_weapon_hull_reshade.py)')
    for i, n, cells, pair, admitted, shared, stem, p99 in rows:
        print(f'    {{{i}u, {n:.4f}f, {cells}u, pair_{pair}, blend_{admitted}, {str(shared).lower()}}}, '
              f'// {stem}, lit p99 {p99:.4f}')


if __name__ == '__main__':
    main()
