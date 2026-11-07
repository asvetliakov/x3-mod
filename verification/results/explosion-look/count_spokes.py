"""count_spokes.py <rays-only png> <full frame png (centre)> <frame png>...: the lab's rays-only frame gives the centre and the rim (the dark disc);
counts angular peaks (> 0.1 of the circle's max over its median, local maxima over +-2 deg) on circles of k rim radii."""
import sys, math
from PIL import Image
def lum(im):
    w, h = im.size; p = im.convert('RGB').load()
    return w, h, lambda x, y: max(p[int(x), int(y)]) if 0 <= x < w and 0 <= y < h else 0
w, h, L = lum(Image.open(sys.argv[1]))
# centre: the dark disc nearest the frame's middle-left region: scan for the darkest 9x9 block within the lit area
best = None
for y in range(h // 2 - 60, h // 2 + 60, 2):
    for x in range(w // 2 - 120, w // 2 + 60, 2):
        if L(x, y) == 0 and L(x + 25, y) == 0 and L(x - 25, y) == 0 and L(x, y + 25) == 0 and L(x, y - 25) == 0:
            pass
# simpler: rays-only lit pixels' centroid weighted ring: centre = mean of lit pixels
_, _, B = lum(Image.open(sys.argv[2]))  # the full frame's white core gives the centre
sx = sy = n = 0
for y in range(0, h):
    for x in range(0, w):
        if B(x, y) >= 250: sx += x; sy += y; n += 1
cx, cy = sx / n, sy / n
# rim: the smallest radius where the mean over angles is lit
def ring(f, r, N=720):
    return [f(cx + r * math.cos(2 * math.pi * i / N), cy + r * math.sin(2 * math.pi * i / N)) for i in range(N)]
rim = next(r for r in range(2, 200) if sorted(ring(L, r))[-20] > 40)  # the rays' start: the rim
print(f'centre {cx:.1f},{cy:.1f} rim {rim} px')
def peaks(s, thr):
    N = len(s); c = 0
    for i in range(N):
        if s[i] > thr and all(s[i] >= s[(i + d) % N] for d in range(-4, 5)) and s[i] > s[(i + 1) % N] - 1e-9 and s[i] > min(s[(i - 4) % N], s[(i + 4) % N]):
            c += 1
    return c
for path in sys.argv[1:]:
    _, _, F = lum(Image.open(path))
    out = []
    for k in (1.2, 1.5, 2.0, 2.5):
        s = ring(F, k * rim); base = sorted(s)[len(s) // 2]; mx = max(s)
        out.append(f'{k}R: {peaks(s, base + 0.1 * (mx - base))}')
    print(path, '; '.join(out))
