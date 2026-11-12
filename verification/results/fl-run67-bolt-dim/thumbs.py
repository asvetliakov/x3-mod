#!/usr/bin/env python3
# Viewing aid only: 4x-downsampled Reinhard thumbnails of the pre-resolve hdr lane (and an RT2 flag overlay in red).
import numpy as np, sys
from PIL import Image
D, OUT = sys.argv[1], sys.argv[2]; frames = [int(x) for x in sys.argv[3:]]
W, H = 5120, 1440
for fr in frames:
    hdr = np.memmap(f'{D}/hdr_1_{fr}.rgba16f', np.float16, 'r', shape=(H, W, 4))
    dep = np.memmap(f'{D}/depth_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))
    c = np.asarray(hdr[::2, ::2, :3], np.float32); c = c / (1 + c)
    im = (np.clip(c, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)
    flag = np.asarray(dep[::2, ::2, 1]) > 1
    Image.fromarray(im).save(f'{OUT}/hdr_{fr}.png')
    im2 = im.copy(); im2[flag] = [255, 0, 255]
    Image.fromarray(im2).save(f'{OUT}/flag_{fr}.png')
