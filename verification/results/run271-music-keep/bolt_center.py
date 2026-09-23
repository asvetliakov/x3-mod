"""Run 72 B: bright small components in a screen window (bolt candidates ahead of the ship).
Usage: bolt_center.py CAPDIR FRAME X0 Y0 X1 Y1 [PNG]  (1920x1080 hdr readback, luminance > 0.5)
Prints per-frame component count, area histogram and peaks; optional PNG crop (x8 nearest) of the window."""
import sys, numpy as np
cap, f = sys.argv[1], int(sys.argv[2]); x0, y0, x1, y1 = map(int, sys.argv[3:7])
W, H = 1920, 1080
a = np.fromfile(f'{cap}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)[y0:y1, x0:x1]
y = 0.2126*a[..., 0] + 0.7152*a[..., 1] + 0.0722*a[..., 2]
m = y > 0.5
pts = set(zip(*np.nonzero(m))); areas = []; peaks = []; dims = []
while pts:
    st = [pts.pop()]; comp = []
    while st:
        p = st.pop(); comp.append(p)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                q = (p[0]+dy, p[1]+dx)
                if q in pts: pts.remove(q); st.append(q)
    ys = np.array([c[0] for c in comp]); xs = np.array([c[1] for c in comp])
    areas.append(len(comp)); peaks.append(float(y[ys, xs].max())); dims.append((int(np.ptp(xs))+1, int(np.ptp(ys))+1))
areas = np.array(areas); peaks = np.array(peaks); n = len(areas)
print(f'frame {f} window {x0},{y0}-{x1},{y1} components {n} area_px p50 {np.median(areas) if n else 0} max {areas.max() if n else 0} '
      f'area<=2 {int((areas<=2).sum()) if n else 0} peak_lum p50 {np.median(peaks) if n else 0:.2f} max {peaks.max() if n else 0:.2f} '
      f'max_extent_px {max((max(d) for d in dims), default=0)}')
if len(sys.argv) > 7:
    import zlib, struct
    t = np.clip(y / 4.0, 0, 1) ** (1/2.2); img = (np.repeat(np.repeat(t, 8, 0), 8, 1) * 255).astype(np.uint8)
    raw = b''.join(b'\x00' + r.tobytes() for r in img)
    def chunk(k, d): return struct.pack('>I', len(d)) + k + d + struct.pack('>I', zlib.crc32(k + d))
    open(sys.argv[7], 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', img.shape[1], img.shape[0], 8, 0, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
