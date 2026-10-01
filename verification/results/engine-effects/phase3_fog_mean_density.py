#!/usr/bin/env python3
"""Mean shaped fog density of the stored-density look, for the engine plumes' per-nozzle fog transmittance (phase 3).

Bakes the shared field exactly as tools/build/bake_fog_fields.py does, takes family_density at each occupancy the
recipe uses (0.12, 0.24), applies the look's remap rho' = saturate((rho - c - dc) / (1 - c))^p (c .35, p 2; the
coverage waves' dc swings +-.12 about 0 and is sampled at -.12 / 0 / +.12) and prints the volume mean of rho' (the
factor in front of sigma x density_scale x ready_far x sigma_scale that gives the mean extinction per render unit).
Voxel-centre values (no trilinear blend). Output: phase3_fog_mean_density_out.txt beside this script.
"""
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools/build'))
import bake_fog_fields as bake  # noqa: E402

carrier, mask, eligible, t1, _ = bake.bake_fields()
lines = []
for occupancy in (0.12, 0.24):
    rho = bake.family_density(carrier, mask, eligible, t1, occupancy)
    means = []
    for dc in (-.12, 0., .12):
        shaped = np.clip((rho - .35 - dc) / .65, 0., 1.) ** 2
        means.append(float(shaped.mean()))
    lines.append(f'occupancy={occupancy} rho_mean={float(rho.mean()):.5f} shaped_mean_dc-0.12={means[0]:.5f} '
                 f'shaped_mean_dc0={means[1]:.5f} shaped_mean_dc+0.12={means[2]:.5f} '
                 f'shaped_mean_avg={sum(means) / 3:.5f}')
text = '\n'.join(lines) + '\n'
print(text, end='')
Path(__file__).with_name('phase3_fog_mean_density_out.txt').write_text(text)
