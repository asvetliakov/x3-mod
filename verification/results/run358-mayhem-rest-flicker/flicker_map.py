#!/usr/bin/env python3
"""Run 358: per-pixel rest flicker map of present_ (display luma codes, unwarped f-1 -> f rms over the rest frames),
summarised as the 16 worst 32x32 blocks with their depth range (view z), plus a PNG heat map (scratch, untracked).
Usage: flicker_map.py <dir> first last out.png"""
import sys, numpy as np
from PIL import Image
W, H = 5120, 1440; P22, P32 = 1.00000298, -6.00001812; LUMA = np.array([0.2126, 0.7152, 0.0722])
d, a, b, png = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
def pres(f): return (np.array(np.memmap(f'{d}/present_1_{f}.bgra8', np.uint8, 'r', shape=(H, W, 4))[:, :, :3])[..., ::-1] / 255.) @ LUMA * 255
acc = np.zeros((H, W)); prev = pres(a)
for f in range(a + 1, b + 1):
    c = pres(f); acc += (c - prev) ** 2; prev = c
rms = np.sqrt(acc / (b - a))
z = np.array(np.memmap(f'{d}/depth_1_{a}.rgba32f', np.float32, 'r', shape=(H, W, 4))[:, :, 0])
with np.errstate(divide='ignore', invalid='ignore'): vz = np.where(z >= 0, P32 / (z - P22), -1)
bl = rms.reshape(H // 32, 32, W // 32, 32).mean((1, 3))
idx = np.argsort(bl.ravel())[::-1][:16]
print(f'frames {a}-{b}: frame rms mean {rms.mean():.3f}; share of px > 4 codes rms {100*(rms>4).mean():.3f} %')
for i in idx:
    by, bx = divmod(i, W // 32); sl = (slice(by*32, by*32+32), slice(bx*32, bx*32+32)); zz = vz[sl]; g = zz[zz > 0]
    print(f'block x {bx*32}-{bx*32+32} y {by*32}-{by*32+32}: rms mean {bl[by,bx]:.2f} max {rms[sl].max():.1f} geom px {g.size} z p10/p50/p90 ' + (f'{np.percentile(g,10):.0f}/{np.percentile(g,50):.0f}/{np.percentile(g,90):.0f}' if g.size else '-'))
Image.fromarray(np.clip(rms[::2, ::2] * 16, 0, 255).astype(np.uint8)).save(png)
