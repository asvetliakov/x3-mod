"""Find the sky glow centre (excluding the player hull below y=900) and print a radial luminance profile
for hdr (linear FP16, read at the first write-back = after the fog composite and resolve) and present (8-bit).
Usage: python3 sun_glow_profile.py <capture_dir> <frame> [cx cy]"""
import sys, numpy as np
d, f = sys.argv[1], sys.argv[2]; W, H = 5120, 1440
k = np.array([.2126, .7152, .0722], np.float32)
hdr = np.asarray(np.memmap(f"{d}/hdr_1_{f}.rgba16f", np.float16, 'r', shape=(H, W, 4))[..., :3], np.float32)
pre = np.asarray(np.memmap(f"{d}/present_1_{f}.bgra8", np.uint8, 'r', shape=(H, W, 4))[..., 2::-1], np.float32)
hl, pl = hdr @ k, pre @ k
b = 16; top = hl[:896].reshape(56, b, W // b, b).mean((1, 3))
if len(sys.argv) > 4: cx, cy = int(sys.argv[3]), int(sys.argv[4])
else:
    i = np.unravel_index(np.argmax(top), top.shape); cy, cx = i[0] * b + b // 2, i[1] * b + b // 2
print(f"glow centre ({cx},{cy}) block16 max hdr={top.max():.3f}; hdr full-frame median={np.median(hl):.3f}")
yy, xx = np.mgrid[0:H, 0:W]; r = np.hypot(xx - cx, yy - cy)
sky = yy < 900
for r0, r1 in ((0, 4), (4, 16), (16, 48), (48, 120), (120, 250), (250, 500), (500, 900), (900, 1500)):
    m = (r >= r0) & (r < r1) & sky
    if m.any():
        print(f"r {r0:4d}-{r1:4d}: hdr mean={hl[m].mean():.3f} p99={np.percentile(hl[m],99):.3f} max={hl[m].max():.3f} rgb={hdr[m].mean(0).round(3)} | present mean={pl[m].mean():.1f} max={pl[m].max():.1f} rgb={pre[m].mean(0).round(1)}")
