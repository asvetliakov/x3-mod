#!/usr/bin/env python3
"""FL run-18 capture frame 15972: AP merged-LOD overlay rule applied to the five nodes whose per-draw rows survive
(capital_draws_summary.txt, hull_fingerprint_out.txt; the session log itself was rotated away). Inferred estimate.
Inputs (measured, preserved): node draws, bbox px, body identity, drawn LOD. Ladders and overlay coarse records:
lod_census_out.txt (FL ladders byte-identical to stock AP; overlay = installed AP addon/13..15 record 1).
Metric: s = r*640/D (lod-selection.md); projected radius px = s * m00 * width / 1280 with m00 = 0.375, width 5120
(camera_state p00 at 5120x1440 in run 19 D; assumed for the capture session) -> px_radius = 1.5 s.
s estimate = max(bbox w, h) / 2 / 1.5 (bbox half-extent ~ projected radius; checked against the drawn stock LOD band)."""
M00, W = 0.375, 5120
K = M00 * W / 1280
nodes = [  # name, draws, bbox w, h, stock ladder thresholds, drawn LOD (fingerprint), overlay T_pad, overlay coarse draws
    ('argon_m1 (M1)', 32, 801, 392, [30, 15, 5], 0, 120, 3),
    ('argon_tl (TL)', 31, 103, 108, [30, 15, 5], 0, 120, 8),
    ('argon_tech_s_factory_a (station part)', 15, 135, 116, [250, 150, 80, 30], 2, 163, 3),
    ('argon_m7 (M7)', 16, 76, 46, [30, 15, 5], 0, 120, 2),
    ('boron_ts_liquidstorage_l (TS part)', 21, 20, 20, [30, 15, 5], 1, 120, 2),
]


def stock_band(thr, s):  # Very High: sel = highest i with s < T_i, drawn = clamp(sel - 1, 0, n - 1)
    sel = next((i for i in range(len(thr), 0, -1) if s < thr[i - 1]), 0)
    return min(max(sel - 1, 0), len(thr))


before = after = 0
print(f'px_radius per s unit = {K:.2f}')
for name, d, w, h, thr, lod, tpad, coarse in nodes:
    s = max(w, h) / 2 / K
    pred = stock_band(thr, s)
    a = coarse if s < tpad else d
    before += d; after += a
    print(f'{name}: draws={d} bbox={w}x{h} s~{s:.0f} stock band LOD{pred} (fingerprint LOD{lod}) '
          f'overlay T_pad={tpad} (radius {tpad * K:.0f} px) -> {a} draws')
print(f'five nodes: before={before} after={after} saved={before - after}')
