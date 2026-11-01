"""Locate the brightest region in an FL capture and compare hdr / color / present there.
Usage: python3 sun_locate.py <capture_dir> <frame> [x y]  (prints only summary numbers)."""
import sys, numpy as np
d, f = sys.argv[1], sys.argv[2]
W, H = 5120, 1440
def bgra(kind):
    a = np.memmap(f"{d}/{kind}_1_{f}.bgra8", np.uint8, 'r', shape=(H, W, 4)); return a[..., 2::-1].astype(np.float32)  # rgb
hdr = np.memmap(f"{d}/hdr_1_{f}.rgba16f", np.float16, 'r', shape=(H, W, 4))
pres, col = bgra("present"), bgra("color")
lum = lambda rgb: rgb @ np.array([.2126, .7152, .0722], np.float32)
hl = lum(np.asarray(hdr[..., :3], np.float32)); pl = lum(pres); cl = lum(col)
def blocks(img, b=32):
    return img[:H//b*b, :W//b*b].reshape(H//b, b, W//b, b).mean((1, 3))
for name, img in (("hdr", hl), ("color", cl), ("present", pl)):
    bm = blocks(img); i = np.unravel_index(np.argmax(bm), bm.shape)
    y, x = np.unravel_index(np.argmax(img), img.shape)
    print(f"{name}: max={img.max():.3f} at ({x},{y}) mean={img.mean():.3f} brightest32block=({i[1]*32+16},{i[0]*32+16}) blockmean={bm[i]:.3f}")
if len(sys.argv) > 4:
    x, y = int(sys.argv[3]), int(sys.argv[4])
    for r in (4, 16, 64, 160):
        s = np.s_[max(y-r,0):y+r+1, max(x-r,0):x+r+1]
        print(f"r={r}: hdr mean={hl[s].mean():.3f} max={hl[s].max():.3f} | color mean={cl[s].mean():.1f} max={cl[s].max():.1f} | present mean={pl[s].mean():.1f} max={pl[s].max():.1f} sat255={(pres[s].min(-1)>=250).mean():.3f}")
