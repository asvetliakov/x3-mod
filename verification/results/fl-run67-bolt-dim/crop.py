#!/usr/bin/env python3
# Viewing aid: full-res crop of the pre-resolve hdr lane (Reinhard), optional 2x upscale. args: dir out frame x0 y0 x1 y1
import numpy as np, sys
from PIL import Image
D, OUT, fr, x0, y0, x1, y1 = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:8])
hdr = np.memmap(f'{D}/hdr_1_{fr}.rgba16f', np.float16, 'r', shape=(1440, 5120, 4))
c = np.asarray(hdr[y0:y1, x0:x1, :3], np.float32); c = c / (1 + c)
im = Image.fromarray((np.clip(c, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8))
im.save(f'{OUT}/crop_{fr}_{x0}_{y0}.png')
