#!/usr/bin/env python3
"""fire_pink.py: the fire ramp (src/proxy/weapon_effects_core.h look::fire_ramp, as fire_model.RAMP) through the AgX
display (tools/analysis/agx_reference.py's minimal transform as fire_model.agx: decode 2.2, exposure 0) at intensities
0.02-2.2 x the stop, blended towards the top stop by the core's weight w (0-1, the radiance x (1 + w)): the share of
(T, w, k) samples whose display colour is pink (B > 0.5 G and G < 0.85 R) and the first few. The old ramp for comparison."""
import sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import fire_model as M
OLD = [[0, 0, 0], [0.45, 0.05, 0.015], [1.0, 0.33, 0.05], [1.0, 0.80, 0.30], [1.0, 0.97, 0.85]]
for name, R in (('old', np.array(OLD, float)), ('shipped', M.RAMP.copy())):
    M.RAMP[:] = R
    bad = tot = 0
    first = []
    for t in np.linspace(0.05, 1, 40):
        base = M.ramp(np.array([t]))[0]
        for w in np.linspace(0, 1, 11):
            c = base + (R[4] - base) * w
            for k in np.linspace(0.02, 2.2, 45):
                d = M.agx(c * k * (1 + w))
                tot += 1
                if d[2] > 0.5 * d[1] and d[1] < 0.85 * d[0]:
                    bad += 1
                    if len(first) < 4: first.append((round(float(t), 2), round(float(w), 2), round(float(k), 2)))
    print('%s ramp: pink share %.3f of %d samples; first (T, w, k) %s' % (name, bad / tot, tot, first))
