"""Crop a 5120x1440 rgba16f HDR capture to PNG (Reinhard x/(1+x), gamma 1/2.2), plus red-share of the crop.
Usage: python3 crop_hdr.py FILE x0 y0 x1 y1 OUT.png [scale]"""
import sys
import numpy as np
from PIL import Image
f, x0, y0, x1, y1, out = sys.argv[1], *map(int, sys.argv[2:6]), sys.argv[6]
k = float(sys.argv[7]) if len(sys.argv) > 7 else 1.0
a = np.fromfile(f, np.float16).reshape(1440, 5120, 4)[y0:y1, x0:x1, :3].astype(np.float32) * k
a = np.nan_to_num(a)
t = (a / (1 + a)) ** (1 / 2.2)
Image.fromarray((np.clip(t, 0, 1) * 255).astype(np.uint8)).resize(((x1 - x0) * 2, (y1 - y0) * 2), Image.NEAREST).save(out)
r, g, b = a[..., 0], a[..., 1], a[..., 2]
lit = (r + g + b) > 0.05
red = lit & (r > 1.5 * g) & (r > 1.5 * b)
print(out, 'lit_px', int(lit.sum()), 'red_px', int(red.sum()), 'red_share_of_lit %.4f' % (red.sum() / max(1, lit.sum())))
