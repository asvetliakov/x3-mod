#!/usr/bin/env python3
"""Decode FL 71 A F8 captures to small PNGs (hdr thumbnails, crops, shadow-map views).
usage: decode.py hdr FRAME OUT [scale]          -> tonemapped hdr thumbnail
       decode.py hdrcrop FRAME OUT x0 y0 x1 y1 [up] -> full-res crop (optionally upscaled)
       decode.py smap CASCADE FRAME OUT [x0 y0 x1 y1] -> shadow-map depth view (normalised in window)
"""
import sys, numpy as np
from PIL import Image
CAP = '/mnt/c/X3FL/x3-modern-captures/'
W, H = 5120, 1440
SIZES = {0: 2048, 1: 4096, 2: 4096, 3: 4096, 4: 2048}

def hdr(frame):
    a = np.memmap(CAP + f'hdr_1_{frame}.rgba16f', dtype='<f2', mode='r', shape=(H, W, 4))
    return a

def tonemap(rgb):
    rgb = np.nan_to_num(rgb.astype(np.float32), nan=0, posinf=0, neginf=0).clip(0, None)
    out = rgb / (1.0 + rgb)
    return (np.power(out, 1 / 2.2) * 255).clip(0, 255).astype(np.uint8)

def smap(c, frame):
    n = SIZES[c]
    return np.memmap(CAP + f'shadow_map{c}_1_{frame}.r32f', dtype='<f4', mode='r', shape=(n, n))

def main():
    k = sys.argv[1]
    if k == 'hdr':
        f, out = sys.argv[2], sys.argv[3]
        s = int(sys.argv[4]) if len(sys.argv) > 4 else 8
        a = hdr(f)[::s, ::s, :3]
        Image.fromarray(tonemap(a)).save(out)
    elif k == 'hdrcrop':
        f, out = sys.argv[2], sys.argv[3]
        x0, y0, x1, y1 = map(int, sys.argv[4:8])
        up = int(sys.argv[8]) if len(sys.argv) > 8 else 1
        a = np.array(hdr(f)[y0:y1, x0:x1, :3])
        im = Image.fromarray(tonemap(a))
        if up > 1:
            im = im.resize((im.width * up, im.height * up), Image.NEAREST)
        im.save(out)
    elif k == 'smap':
        c, f, out = int(sys.argv[2]), sys.argv[3], sys.argv[4]
        m = smap(c, f)
        n = m.shape[0]
        if len(sys.argv) > 8:
            x0, y0, x1, y1 = map(int, sys.argv[5:9])
        else:
            x0, y0, x1, y1 = 0, 0, n, n
        a = np.array(m[y0:y1, x0:x1], dtype=np.float32)
        step = max(1, max(a.shape) // 1024)
        a = a[::step, ::step]
        valid = np.isfinite(a) & (a < 0.99999)
        v = np.zeros(a.shape, np.uint8)
        if valid.any():
            lo, hi = np.percentile(a[valid], [1, 99])
            v[valid] = (255 - ((a[valid] - lo) / max(hi - lo, 1e-9) * 200).clip(0, 200)).astype(np.uint8)
        Image.fromarray(v).save(out)
        print('cascade', c, 'window', x0, y0, x1, y1, 'step', step, 'covered', int(valid.sum()), 'of', valid.size,
              'min', float(a.min()), 'max', float(a.max()))

if __name__ == '__main__':
    main()
