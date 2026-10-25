#!/usr/bin/env python3
"""Read-only: luminance stats of freighter_base regions, Run 204 A F8 pair (far 1496..1503 overlay lod 1, close
4357..4364 lod 0). Boxes picked by eye from tonemapped crops of 1503 / 4364 (crop.py) (same parts in both). Pixels with depth<1.
detail = mean |Laplacian| / mean (texture/edge detail). Usage: python3 region_stats.py CAPTURE_DIR"""
import sys, numpy as np
d = sys.argv[1]; W, H = 5120, 1440
boxes = {'far': {'dome_top': (2520, 2592, 561, 575), 'dome_band': (2516, 2597, 576, 591),
                 'ring': (2515, 2610, 615, 645), 'lower_cyl': (2548, 2590, 670, 692)},
         'close': {'dome_top': (2205, 2410, 510, 540), 'dome_band': (2195, 2425, 547, 590),
                   'ring': (2190, 2470, 650, 740), 'lower_cyl': (2285, 2405, 820, 880)}}
frames = {'far': range(1496, 1504), 'close': range(4357, 4365)}
Y = np.array([0.2126, 0.7152, 0.0722], np.float32)
for k, fr in frames.items():
    for r, (x0, x1, y0, y1) in boxes[k].items():
        acc = []
        for f in fr:
            h = np.memmap(f'{d}/hdr_1_{f}.rgba16f', dtype=np.float16, mode='r').reshape(H, W, 4)[y0:y1, x0:x1, :3].astype(np.float32)
            z = np.memmap(f'{d}/depth_1_{f}.rgba32f', dtype=np.float32, mode='r').reshape(H, W, 4)[y0:y1, x0:x1, 0]
            Lf = h @ Y; m = z < 1; L = Lf[m]
            lap = np.abs(4 * Lf[1:-1, 1:-1] - Lf[:-2, 1:-1] - Lf[2:, 1:-1] - Lf[1:-1, :-2] - Lf[1:-1, 2:])[m[1:-1, 1:-1]]
            acc.append((L.mean(), np.percentile(L, 95), L.std() / L.mean(), lap.mean() / L.mean(), m.mean()))
        a = np.array(acc)
        print(f'{k:5s} {r:9s} mean {a[:,0].mean():.3f} p95 {a[:,1].mean():.3f} cv {a[:,2].mean():.2f} detail {a[:,3].mean():.2f} '
              f'cover {a[:,4].mean():.2f} mean_range_over_frames {a[:,0].min():.3f}-{a[:,0].max():.3f}')
