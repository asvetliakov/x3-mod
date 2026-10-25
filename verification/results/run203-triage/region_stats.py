#!/usr/bin/env python3
"""Read-only: luminance stats of freighter_base regions, Run 203 A F8 pair (far 2725..2732 overlay lod 1, close
6029..6036 lod 0). Boxes picked by eye from tonemapped crops of 2732 / 6036 (same parts in both). Pixels with depth<1.
detail = mean |Laplacian| / mean (texture/edge detail). Usage: python3 region_stats.py CAPTURE_DIR"""
import sys, numpy as np
d = sys.argv[1]; W, H = 5120, 1440
boxes = {'far': {'dome_top': (2487, 2567, 625, 640), 'dome_band': (2485, 2568, 642, 662),
                 'ring': (2477, 2593, 677, 730), 'lower_cyl': (2515, 2563, 737, 770)},
         'close': {'dome_top': (2130, 2390, 500, 550), 'dome_band': (2120, 2400, 555, 615),
                   'ring': (2090, 2480, 660, 830), 'lower_cyl': (2230, 2380, 850, 960)}}
frames = {'far': range(2725, 2733), 'close': range(6029, 6037)}
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
