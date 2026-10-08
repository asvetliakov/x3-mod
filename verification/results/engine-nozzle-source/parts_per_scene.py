#!/usr/bin/env python3
"""Direct parts per ship scene (the root's child list a node-sourced nozzle walk would traverse) and main jets per scene,
installed and stock views, one entry per TShips column-16 text scene. Reuses the parser of
verification/results/engine-effects/floor_ratio_effects.py (read-only on the game tree; about 90 s per view).

  python3 verification/results/engine-nozzle-source/parts_per_scene.py > verification/results/engine-nozzle-source/parts_per_scene_out.txt
"""
import importlib.util
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('floor_ratio_effects', ROOT / 'verification/results/engine-effects/floor_ratio_effects.py')
fr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fr)
eb = fr.eb


def main():
    for view, stock in (('installed', False), ('stock', True)):
        t = eb.generate(eb.bob1.DEFAULT_GAME, stock_only=stock)
        assets, _ = eb.load_assets(eb.bob1.DEFAULT_GAME, stock)
        jets = {k.lower(): v for k, v in t['bodies'].items()}
        scenes, rows = fr.tships_scenes(assets)
        parts, mains, allj, top = [], [], [], []
        for token in scenes:
            try:
                data, _ = fr.scene_member(assets, token)
            except FileNotFoundError:
                continue
            ps = fr.parts_of(data)
            n_main = n_jet = 0
            for body, mode, _pos in ps:
                key, _ = eb.body_key(body)
                if key.lower() in jets:
                    n_jet += 1
                    if mode & fr.MAIN_MODE == fr.MAIN_MODE:
                        n_main += 1
            parts.append(len(ps))
            mains.append(n_main)
            allj.append(n_jet)
            top.append((len(ps), n_jet, n_main, token))
        p = np.array(parts)
        q = np.percentile(p, [50, 90, 99])
        print(f'{view}: tships_rows={rows} scenes={len(scenes)} text_scenes={len(parts)} parts_per_scene p50={q[0]:.0f} '
              f'p90={q[1]:.0f} p99={q[2]:.0f} max={p.max()} jets_max={max(allj)} main_jets_max={max(mains)} '
              f'scenes_over_256_parts={int((p > 256).sum())} scenes_over_512_parts={int((p > 512).sum())}')
        for n, j, m, token in sorted(top, reverse=True)[:5]:
            print(f'  parts={n} jets={j} main={m} {token}')


if __name__ == '__main__':
    main()
