#!/usr/bin/env python3
"""Read-only: tonemapped (x/(1+x), gamma 2.2) crop of an F8 hdr frame. Usage: crop.py CAPDIR FRAME x0 x1 y0 y1 SCALE OUT.png"""
import sys, numpy as np
from PIL import Image
d, f, x0, x1, y0, y1, s, out = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:8]), sys.argv[8]
h = np.memmap(f'{d}/hdr_1_{f}.rgba16f', dtype=np.float16, mode='r').reshape(1440, 5120, 4)[y0:y1, x0:x1, :3].astype(np.float32)
t = (np.clip(h / (1 + h), 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)
Image.fromarray(t).resize(((x1 - x0) * s, (y1 - y0) * s), Image.NEAREST).save(out)
