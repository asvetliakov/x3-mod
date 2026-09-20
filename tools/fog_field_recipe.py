"""Qualified spatial-fog field recipe; no preview or captured-data inputs."""

import numpy as np

REQUIRED_NUMPY = "2.0.2"
PRNG = "numpy.random.default_rng/PCG64"
PERIOD = 32768.0
GRID = 128
HORIZON = 12000.0
RECIPE_ID = 1
ATLAS_WIDTH = 1560
ATLAS_HEIGHT = 1430
TEXEL_BYTES = 8
WARP_SEEDS = ((186, 189), (187, 190), (188, 191))
WARP_PERIODS = (8192.0, 4096.0)
WARP_WEIGHTS = (0.7, 0.3)
WARP_AMPLITUDE = 2048.0
CARRIER = ((192, 8192.0, 0.55), (193, 4096.0, 0.30), (194, 2048.0, 0.15))
CAVITY = ((195, 4096.0, 0.70), (196, 2048.0, 0.30))
CHROMA = (197, 4096.0)
CAVITY_QUANTILES = (70, 85)
CHROMA_QUANTILES = (10, 90)

PROFILES = {
    "bluewell": {
        "id": 1,
        "resource_id": 21101,
        "occupancy": 0.12,
        "base_sigma": 2.5e-6,
        "atlas_sha256": "4529497a5e1feda3276b334d6aaef3ba741460e4e4b539aaaf7df74cbfa7261d",
        "colours": np.array([
            [0.0, 0.21271182472443387, 1.0],
            [0.07572026794060213, 0.19668585055821633, 1.0],
            [0.051671565049836686, 0.3052151957413023, 1.0],
            [0.07417807774861103, 0.33525483945827456, 1.0],
        ], dtype=np.float32),
    },
    "foggreenoutlands": {
        "id": 2,
        "resource_id": 21102,
        "occupancy": 0.24,
        "base_sigma": 6.25e-6,
        "atlas_sha256": "d0342a05fe0e0421c980bb46bec72a765617f9bd043d84d134928a65d3dc0b0c",
        "colours": np.array([
            [0.12417480110175053, 1.0, 0.01784274291816572],
            [0.10174731107512709, 1.0, 0.03052097990922685],
            [0.1553095588422853, 1.0, 0.1038202739029337],
            [0.37638036178691936, 1.0, 0.2881417758433784],
        ], dtype=np.float32),
    },
}
