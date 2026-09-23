#!/usr/bin/env python3
"""Run 75 B (run279) task 2: blue-dominant components (B > 1.35 R, luminance > 0.4, peak > 1.5, area >= 3) in the
pre-resolve FP16 scene of the F8 burst 6136-6143, over the whole frame above the exhaust band (y < 1000), with the
depth-lane .r (z/w, -1 = no routed depth row) at the component and the min covered z/w in a +-12 px box, and the
same components' presence in the neighbour frames (static = within 2 px in a neighbour frame)."""
import numpy as np
D = '/tmp/x3-bottleX3-run279'; W, H = 1920, 1080
pair = {6137, 6139, 6141}
hdr = lambda f: np.fromfile(f'{D}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)
dep = lambda f: np.fromfile(f'{D}/depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)[..., 0]
def comps(f):
    a = hdr(f)[:1000]; L = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]; z = dep(f)
    m = (L > 0.4) & (a[..., 2] > 1.35 * a[..., 0])
    pts = set(zip(*np.nonzero(m))); out = []
    while pts:
        st = [pts.pop()]; c = []
        while st:
            p = st.pop(); c.append(p)
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    q = (p[0] + dy, p[1] + dx)
                    if q in pts: pts.remove(q); st.append(q)
        ys = np.array([p[0] for p in c]); xs = np.array([p[1] for p in c]); pk = float(L[ys, xs].max())
        if pk <= 1.5 or len(c) < 3: continue
        cy, cx = int(ys.mean()), int(xs.mean()); box = z[max(cy - 12, 0):cy + 13, max(cx - 12, 0):cx + 13]; cov = box[box >= 0]
        out.append((cx, cy, int(np.ptp(xs)) + 1, int(np.ptp(ys)) + 1, len(c), pk, float(z[ys, xs].max()), float(cov.min()) if cov.size else -1.0))
    return out
C = {f: comps(f) for f in range(6136, 6144)}
for f in range(6136, 6144):
    nb = [c for g in (f - 1, f + 1) if g in C for c in C[g]]
    moving = [c for c in C[f] if not any(abs(c[0] - n[0]) <= 2 and abs(c[1] - n[1]) <= 2 for n in nb)]
    print(f'frame {f} pair={"yes" if f in pair else "no"} components {len(C[f])} not_in_neighbour {len(moving)} '
          + ' '.join('(%d,%d %dx%d area %d peak %.2f z_at %.4f zbox %.4f)' % c for c in moving))
