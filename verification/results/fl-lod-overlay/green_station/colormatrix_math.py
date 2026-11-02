"""PS b2d8f40c albedo path (measured from the D3DX disassembly): overlay(diffuse, c13) -> rows c1..c3 (w = offset).
Prints the preshader parameters that reproduce the captured rows and what grey texels become.
Usage: python3 colormatrix_math.py"""
import math
def preshader(B, C, S, H):
    th = (H / 360.0 + 0.5) % 1.0 * 2 * math.pi - math.pi; c, s = math.cos(th), math.sin(th)
    k = (1 - c) / 3; r1 = (k + s / math.sqrt(3), k - s / math.sqrt(3), c + k)
    lum = (0.3086, 0.6094, 0.082); bc = B * C
    cols = [[bc * ((1 - S) * lum[j] + (S if i == j else 0)) for i in range(3)] for j in range(3)]  # cols[j][i]: in j -> out i
    rows = [[0.0] * 4 for _ in range(3)]
    for j in range(3):
        v = cols[j]
        rows[0][j] = v[1] * r1[0] + v[2] * r1[1] + v[0] * r1[2]
        rows[1][j] = v[2] * r1[0] + v[0] * r1[1] + v[1] * r1[2]
        rows[2][j] = v[0] * r1[0] + v[1] * r1[1] + v[2] * r1[2]
    off = 0.5 - 0.5 * C
    for i in range(3): rows[i][3] = off
    return rows
CAP = [[-3.413, -2.105, 6.55, -0.0009995], [5.098, -5.341, 1.275, -0.0009995], [-0.1769, 3.17, -1.961, -0.0009995]]
best = None
for S10 in range(0, 200):
    S = S10 / 10
    for H in range(-180, 180):
        C = 1 - 2 * CAP[0][3]; B = sum(CAP[0][:3]) / C
        m = preshader(B, C, S, H); e = sum((m[i][j] - CAP[i][j]) ** 2 for i in range(3) for j in range(3))
        if best is None or e < best[0]: best = (e, B, C, S, H)
print('fit: err=%.4g B=%.4f C=%.4f S=%.1f hue=%d deg' % best)
print('row sums', [round(sum(r[:3]), 4) for r in CAP])
def overlay(t, Q):
    luma = 0.299 * t[0] + 0.587 * t[1] + 0.114 * t[2]; w = min(max((luma - 0.45) * 10, 0), 1)
    mult = [2 * t[i] * Q[i] for i in range(3)]; scr = [1 - 2 * (1 - Q[i]) * (1 - t[i]) for i in range(3)]
    return [mult[i] + w * (scr[i] - mult[i]) for i in range(3)]
def apply(M, x): return [sum(M[i][j] * x[j] for j in range(3)) + M[i][3] for i in range(3)]
ID = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0]]
for label, Q in (('c13 draws 16/48/50', (0.955, 0.637, 0.538)), ('c13 draw 14', (0.972, 0.96, 0.915)), ('c13 neutral 0.5', (0.5, 0.5, 0.5))):
    for g in (0.2, 0.35, 0.5, 0.7):
        o = overlay((g, g, g), Q)
        print('%-20s grey %.2f -> overlay %s -> identity %s -> captured rows %s' % (label, g, [round(v, 3) for v in o],
              [round(v, 3) for v in apply(ID, o)], [round(v, 3) for v in apply(CAP, o)]))
