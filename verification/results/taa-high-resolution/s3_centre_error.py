#!/usr/bin/env python3
"""S3: float32 texel-centre error of the resolve's fetch positions, without and with the +1/1024 texel bias.

Emulates resolve.hlsl's arithmetic in IEEE float32 (round to nearest): tap = (i + 0.5) * (1 / W), then the filter unit's
texel coordinate u * W - 0.5. The fraction the unit sees is that minus i; a negative value under a truncating 8-bit
sub-texel unit would weight the left neighbour 1/256. The GPU's own arithmetic may differ (inferred model, not a readback;
the fixture's FILTER_PROBE is the readback on this backend).
"""
import numpy as np

for width in (1280, 1920, 2560, 3440, 3840, 5120):
    inv = np.float32(1) / np.float32(width)
    i = np.arange(width, dtype=np.float32)
    u = (i + np.float32(0.5)) * inv
    error = (u * np.float32(width) - np.float32(0.5)) - i
    biased = ((u + inv * np.float32(1 / 1024)) * np.float32(width) - np.float32(0.5)) - i
    print(f'W={width} error min={error.min():.3g} max={error.max():.3g} negative_columns={int((error < 0).sum())} '
          f'biased min={biased.min():.3g} max={biased.max():.3g} below_1_512={bool(biased.max() < 1 / 512)} positive={bool(biased.min() > 0)}')
