import math
r = 1105.0
a = math.radians(23.6)
h = (0, math.sin(a), math.cos(a))
A = (0, -0.8 * r, -1.0 * r)


def norm(v):
    return math.sqrt(sum(x * x for x in v))


for rho_f in (0.2, 0.25, 0.3, 0.35, 0.4, 0.5):
    rho = rho_f * r
    worst = 0
    worst_case = None
    for k in range(8):
        phi = 2 * math.pi * k / 8 + math.pi / 8
        o = (rho * math.cos(phi), rho * math.sin(phi), 0.0)
        X = tuple(A[i] - 0.75 * r * h[i] + o[i] for i in range(3))
        d = tuple(X[i] - A[i] for i in range(3))
        b = sum(d[i] * h[i] for i in range(3))
        c = sum(x * x for x in d) - (0.75 * r) ** 2
        disc = b * b - c
        if disc < 0:
            print('miss', rho_f, k)
            continue
        lam = -b - math.sqrt(disc)
        C = tuple(X[i] + lam * h[i] for i in range(3))
        for th_deg in range(-45, 46, 5):
            th = math.radians(th_deg)
            B = (0, -0.55 * math.cos(th) * r, 0.55 * math.sin(th) * r)
            v = tuple(B[i] - C[i] for i in range(3))
            cos = sum(v[i] * h[i] for i in range(3)) / norm(v)
            err = math.degrees(math.acos(max(-1, min(1, cos))))
            if err > worst:
                worst = err
                worst_case = (k, th_deg)
    print(f'rho={rho_f:.2f} r  max heading error {worst:.1f} deg at ring {worst_case}')
for rho_f in (0.25, 0.3):
    print('adjacent chord', rho_f, round(2 * rho_f * r * math.sin(math.pi / 8), 1), 'm')
