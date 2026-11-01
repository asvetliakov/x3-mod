#!/usr/bin/env python3
"""Full-resolution crop of one HDR capture frame (log tonemap, x SCALE nearest), PNG to a local path.
Usage: crop.py CAPDIR FRAME x0 y0 x1 y1 OUT.png [scale] [ev]"""
import sys, numpy as np
from PIL import Image
W, H = 5120, 1440
cap, f = sys.argv[1], int(sys.argv[2]); x0, y0, x1, y1 = map(int, sys.argv[3:7]); out = sys.argv[7]
s = int(sys.argv[8]) if len(sys.argv) > 8 else 2; ev = float(sys.argv[9]) if len(sys.argv) > 9 else 1.2
a = np.memmap('%s/hdr_1_%d.rgba16f' % (cap, f), dtype='<f2', mode='r', shape=(H, W, 4))[y0:y1, x0:x1, :3].astype(np.float32)
a = a * 2**ev; a = a/(1+a); a = np.clip(a, 0, 1)**(1/2.2)
img = (a*255).astype(np.uint8).repeat(s, 0).repeat(s, 1)
Image.fromarray(img).save(out)
