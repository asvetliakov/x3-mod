#!/usr/bin/env python3
# FL run 67 bolt dimming: per capture frame, RT2 (depth lane, rgba32f: r=z/w, g=share or bolt flag base+32*add, b=a=clip w)
# flag pixel counts (g>1) and the bright-hdr pixel count; memmaps only.
import numpy as np, sys, os, glob, re
D = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3FL/x3-modern-captures'
W, H = 5120, 1440
frames = sorted(int(re.search(r'_(\d+)\.', f).group(1)) for f in glob.glob(D + '/hdr_1_*.rgba16f'))
for fr in frames:
    hdr = np.memmap(f'{D}/hdr_1_{fr}.rgba16f', np.float16, 'r', shape=(H, W, 4))
    dep = np.memmap(f'{D}/depth_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))
    g = np.asarray(dep[..., 1]); r = np.asarray(dep[..., 0]); w = np.asarray(dep[..., 2])
    flag = g > 1
    lum = np.asarray(hdr[..., :3], np.float32).max(-1)
    print(fr, 'flag', int(flag.sum()), 'gmax %.2f' % g.max(), 'r>=0 %d' % int((r >= 0).sum()),
          'w_med %.0f' % (np.median(w[r >= 0]) if (r >= 0).any() else -1), 'hdr>4', int((lum > 4).sum()), 'hdrmax %.1f' % lum.max())
