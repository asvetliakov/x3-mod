# Copy of run340-run91a-pan-replay/station_blobs.py with the frame size set to 1920x1080 for run351/352.
#!/usr/bin/env python3
"""Connected components (8-connected, 4-px downsampled) of routed pixels beyond 2,000 view units on a capture frame:
bbox, pixel count, median view depth, median motion. Usage: station_blobs.py <dir> <frame> [min_px]"""
import sys, numpy as np
W, H = 1920, 1080  # run351/352 captures are 1920x1080 (create_device rows)
P22, P32 = 1.00000298, -6.00001812
d, fr = sys.argv[1], int(sys.argv[2]); minpx = int(sys.argv[3]) if len(sys.argv) > 3 else 5000
z = np.array(np.memmap(f'{d}/depth_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))[:, :, 0])
m = np.memmap(f'{d}/motion_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))
ys, xs = np.mgrid[0:H, 0:W]
mag = np.hypot(m[:, :, 0] * W - (xs + 0.5), m[:, :, 1] * H - (ys + 0.5))
with np.errstate(divide='ignore', invalid='ignore'):
    vz = np.where(z >= 0, P32 / (z - P22), 0)
occ = vz > 2000
G = 8
small = occ.reshape(H // G, G, W // G, G).any((1, 3))
lab = np.zeros(small.shape, int); n = 0
for (r, c) in zip(*np.nonzero(small)):  # flood fill, 8-connected
    if lab[r, c]: continue
    n += 1; stack = [(r, c)]; lab[r, c] = n
    while stack:
        a, b = stack.pop()
        for da in (-1, 0, 1):
            for db in (-1, 0, 1):
                u, v = a + da, b + db
                if 0 <= u < small.shape[0] and 0 <= v < small.shape[1] and small[u, v] and not lab[u, v]:
                    lab[u, v] = n; stack.append((u, v))
full = np.kron(lab, np.ones((G, G), int)) * occ
for i in range(1, n + 1):
    s = full == i; c = int(s.sum())
    if c < minpx: continue
    yy, xx = np.nonzero(s)
    print(f'blob {i}: x {xx.min()}-{xx.max()+1} y {yy.min()}-{yy.max()+1} px {c} z p50 {np.median(vz[s]):.0f} (fp {np.median(vz[s])/1280:.1f} u/px) |d| p50 {np.median(mag[s]):.2f}')
