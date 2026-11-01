"""Where do present and color differ (8-bit, per-pixel max channel |diff|)? Prints counts and bounding boxes
of differing regions, plus the difference in a disc around the projected sun.
Usage: python3 present_minus_color.py <capture_dir> <frame> <sun_x> <sun_y>"""
import sys, numpy as np
d, f, sx, sy = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]); W, H = 5120, 1440
ld = lambda k: np.asarray(np.memmap(f"{d}/{k}_1_{f}.bgra8", np.uint8, 'r', shape=(H, W, 4))[..., :3], np.int16)
p, c = ld("present"), ld("color"); diff = np.abs(p - c).max(-1)
for t in (1, 4, 16, 64):
    m = diff >= t; ys, xs = np.nonzero(m)
    print(f"|diff|>={t}: pixels={m.sum()}" + (f" bbox x {xs.min()}-{xs.max()} y {ys.min()}-{ys.max()}" if m.any() else ""))
s = (p.astype(np.int32).sum(-1) - c.astype(np.int32).sum(-1))
print(f"signed sum diff present-color: mean={s.mean():.3f} positive_px={(s>0).sum()} negative_px={(s<0).sum()}")
for r in (32, 128, 400):
    sl = np.s_[max(sy-r,0):sy+r, max(sx-r,0):sx+r]
    print(f"sun box r={r}: pixels |diff|>=4: {(diff[sl]>=4).sum()} max diff {diff[sl].max()} present-color mean {s[sl].mean():.2f}")
b = 64; bm = diff[:H//b*b, :W//b*b].reshape(H//b, b, W//b, b).astype(np.float32).mean((1, 3))
top = np.argsort(bm.ravel())[::-1][:8]
print("top diff 64-blocks (x,y,mean):", [(int(i % (W//b))*b+32, int(i // (W//b))*b+32, round(float(bm.ravel()[i]),1)) for i in top])
