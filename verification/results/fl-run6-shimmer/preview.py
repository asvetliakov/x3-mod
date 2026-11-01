#!/usr/bin/env python3
"""Downsampled previews of an F8 capture: mean HDR (AgX-free log tonemap) and per-pixel
temporal coefficient of variation of pre-TAA luma over the 8 frames. Output PNGs go to a
local directory (not tracked). Usage: preview.py CAPDIR FIRST_FRAME OUTDIR [step]"""
import sys, numpy as np
from PIL import Image
W, H = 5120, 1440
def hdr(cap, f):
    return np.memmap('%s/hdr_1_%d.rgba16f' % (cap, f), dtype='<f2', mode='r', shape=(H, W, 4))
def luma(a):
    a = a.astype(np.float32)
    return 0.2126*a[..., 0] + 0.7152*a[..., 1] + 0.0722*a[..., 2]
cap, f0, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
s = int(sys.argv[4]) if len(sys.argv) > 4 else 4
L = np.stack([luma(np.asarray(hdr(cap, f0+i)[::s, ::s])) for i in range(8)])
m = L.mean(0)
img = np.clip(np.log2(np.maximum(m, 1e-5)/m[m > 0].mean()*0.18)/8+0.75, 0, 1)
Image.fromarray((img*255).astype(np.uint8)).save('%s/mean_%d.png' % (out, f0))
cv = L.std(0)/np.maximum(m, 1e-4)
Image.fromarray((np.clip(cv, 0, 1)*255).astype(np.uint8)).save('%s/cv_%d.png' % (out, f0))
print(f0, 'mean luma', float(m.mean()), 'p99', float(np.percentile(m, 99)), 'cv>0.3 frac', float((cv > 0.3).mean()))
