#!/usr/bin/env python3
"""Crop of the FP16 scene, written as x/(1+x) (Reinhard, display gamma 1/2.2) PNG; also a >1 / >4 mask PNG."""
import sys, numpy as np
from PIL import Image
d, f, x0, y0, x1, y1, out = sys.argv[1], int(sys.argv[2]), *map(int, sys.argv[3:7]), sys.argv[7]
W, H = 5120, 1440
c = np.fromfile(f'{d}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4)[y0:y1, x0:x1, :3].astype(np.float32)
Image.fromarray((np.clip(c / (1 + c), 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)).save(out)
mx = c.max(-1); m = np.zeros(mx.shape + (3,), np.uint8); m[mx > 1] = (255, 255, 0); m[mx > 2.5] = (255, 0, 0); m[mx > 4] = (255, 255, 255)
Image.fromarray(m).save(out.replace('.png', '_mask.png'))
