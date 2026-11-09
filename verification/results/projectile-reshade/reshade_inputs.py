#!/usr/bin/env python3
"""The bullet re-shade's inputs (docs/reverse-engineering/fl/bullet-draw-batch.md section 6): derived numbers only.

1. The INSTANCE_BULLETS programs of the FL effective archive namespace (root + addon2; tools/analysis/effect_passes.py
   _archive_effects): per effect the (VS, PS) pair hashes (the proxy's FNV-1a 64 of the program bytes) and per program
   the FNV-1a 32 of its instruction tokens (comments skipped) and its decoded instructions.
2. The IRE bolt mesh (objects/effects/weapons/bullet_LS_alpha, from the untracked lab assets of the fl view,
   tools/effects/weapon_effects_lab_assets.py) against effects\\weapons\\fx_bullets_diff.tga (the lab's decode of
   addon2's dds/fx_bullets_diff, level 0, 512x512): its parts' UV rectangles and positions, and the luminance / alpha
   statistics and profiles of the texels each part samples.

  python3 -I verification/results/projectile-reshade/reshade_inputs.py [--game /mnt/c/X3FL] [--lab DIR] \
    > verification/results/projectile-reshade/reshade_inputs_out.txt
No game bytes are written; nothing launches.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
W = (0.2126, 0.7152, 0.0722)


def body(words):
    out, i = [], 1
    while words[i] != 0xffff:
        if words[i] & 0xffff == 0xfffe:
            i += 1 + (words[i] >> 16)
            continue
        out.append(words[i])
        i += 1
    return out


def fnv32(tokens):
    h = 2166136261
    for t in tokens:
        for k in range(4):
            h = ((h ^ ((t >> (8 * k)) & 0xff)) * 16777619) & 0xffffffff
    return h


def programs(game):
    from effect_passes import _archive_effects, parse_container, VERTEX_SHADER_STATE, PIXEL_SHADER_STATE
    from shader_coverage_offline import _program
    pairs, bodies = {}, {}
    for _, path, data in _archive_effects(game, edition='fl'):
        c = parse_container(data)
        for ti, (tech, tps) in enumerate(c['techniques']):
            if tech != 'INSTANCE_BULLETS':
                continue
            for pi, (_, states) in enumerate(tps):
                pair = {}
                for si, st in enumerate(states):
                    if st[0] not in (VERTEX_SHADER_STATE, PIXEL_SHADER_STATE):
                        continue
                    prog = _program(data, c, ti, pi, si)
                    if not prog:
                        continue
                    stage = 'vs' if st[0] == VERTEX_SHADER_STATE else 'ps'
                    _, start, _ = c['state_resources'][(ti, pi, si)]
                    words = struct.unpack('<%dI' % prog[1], data[start:start + 4 * prog[1]])
                    pair[stage] = prog[0]
                    toks = body(words)
                    bodies[prog[0]] = (stage, prog[1], fnv32(toks), len(toks))
                key = (pair.get('vs'), pair.get('ps'))
                pairs.setdefault(key, set()).add(Path(path).stem)
    print('INSTANCE_BULLETS pairs (FL root + addon2):')
    for (vs, ps), stems in sorted(pairs.items(), key=str):
        print(f'  vs {vs} ps {ps} effects {",".join(sorted(stems))}')
    print('programs (stage, dwords, instruction-token FNV-1a 32, tokens):')
    for h, (stage, n, f, t) in sorted(bodies.items()):
        print(f'  {stage} {h} dwords {n} body {f:08x} tokens {t}')


def mesh_and_texture(lab):
    import numpy as np
    from PIL import Image
    text = (Path(lab) / 'assets.js').read_text()
    marker = 'window.LAB_ASSETS["fl"] = '
    data = json.JSONDecoder().raw_decode(text[text.index(marker) + len(marker):])[0]
    b = data['bodies']['objects/effects/weapons/bullet_ls_alpha']
    m = b['materials'][0]
    print(f'IRE body: {len(m["triangles"])} triangles, blend {m["blend"]}, diffuse {m["diffuse"]}, alpha {m["alpha"]}, '
          f'extents {b["extents"]}')
    img = np.asarray(Image.open(Path(lab) / 'tex' / 'fx_bullets_diff-ef82daa8.png').convert('RGBA')).astype(np.float64) / 255
    h, w = img.shape[:2]
    print(f'texture {w}x{h}')
    parts = {}
    for t in m['triangles']:
        parts.setdefault('body' if max(p[4] for p in t) < 0.5 else 'head', []).append(t)
    for k, ts in parts.items():
        us = [p[3] for t in ts for p in t]
        vs = [p[4] for t in ts for p in t]
        zs = [p[2] for t in ts for p in t]
        rad = [float(np.hypot(p[0], p[1])) for t in ts for p in t]
        r0, r1 = int(min(vs) * h), int(np.ceil(max(vs) * h))
        c0, c1 = int(min(us) * w), int(np.ceil(max(us) * w))
        reg = img[r0:r1, c0:c1]
        lum = reg[:, :, :3] @ np.array(W)
        print(f'{k}: {len(ts)} triangles, u {min(us):.4f}..{max(us):.4f}, v {min(vs):.4f}..{max(vs):.4f}, '
              f'z {min(zs):.4f}..{max(zs):.4f}, radius {min(rad):.5f}..{max(rad):.5f} (body units)')
        print(f'  texels rows {r0}..{r1} cols {c0}..{c1}: luminance max {lum.max():.3f} mean {lum.mean():.3f} '
              f'p50 {np.percentile(lum, 50):.3f} p90 {np.percentile(lum, 90):.3f} p99 {np.percentile(lum, 99):.3f}; '
              f'alpha max {reg[:, :, 3].max():.3f}; corr(alpha, luminance) {np.corrcoef(lum.ravel(), reg[:, :, 3].ravel())[0, 1]:.3f}')
        bright = reg[:, :, :3][lum > 0.3]
        if len(bright):
            mean = bright.mean(axis=0)
            print(f'  texels with luminance > 0.3: {len(bright)}, hue (rgb / max) {tuple(round(float(x), 3) for x in mean / mean.max())}')
        cross = lum[:, lum.shape[1] // 4: 3 * lum.shape[1] // 4].mean(axis=1)
        print('  across (per texel row, v rising; middle half of u):', ' '.join(f'{x:.2f}' for x in cross))
        along = lum.mean(axis=0)
        step = max(1, len(along) // 24)
        print(f'  along (column means, every {step} texels, u rising):', ' '.join(f'{x:.2f}' for x in along[::step]))
    # the body strip's rows just past its UV rectangle towards the axis edge (the bilinear and mip neighbours)
    lum = img[:, :, :3] @ np.array(W)
    rows = range(178, 190)
    print('body strip rows 178..189 (v 0.348..0.371), luminance mean over cols 128..384:',
          ' '.join(f'{lum[r, 128:384].mean():.3f}' for r in rows))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--game', default='/mnt/c/X3FL')
    parser.add_argument('--lab', default=str(ROOT / 'verification/results/weapon-effects-lab/fl'))
    args = parser.parse_args()
    programs(args.game)
    mesh_and_texture(args.lab)


if __name__ == '__main__':
    main()
