"""The heading-error bound of the gate approach spread (design amendment 8, re-derived for amendment 10).

The pass point B lies on the gate DISC (Run154, 91 transitions; the listing 0x004609d5..0x00460a01: x = -y0 sin theta,
y = y0 cos theta, z = z0): B = G + R (0.55 sin theta, -0.55 cos theta, -0.01) r, |theta| <= 45 deg. The lead-in line
from A = G + R (0, -0.8, -1) r aims at the arc's centre point (0, -0.55, -0.01) r: direction (0, 0.25, 0.99) /
|(0, 0.25, 0.99)| = (0, 0.24484, 0.96956), 14.17 deg above the axis. A ship's line is shifted by its ring offset in the
gate's x-y plane; it crosses the 0.75 r completion sphere around A and heads for the pass point the KC draws next.
The table gives the worst heading error to any B at that crossing per ring radius, and the on-axis (no spread) error.
"""
import math

r = 1105.0
lead = (0.0, 0.25, 0.99)
n = math.sqrt(sum(c * c for c in lead))
h = tuple(c / n for c in lead)
A = (0.0, -0.8 * r, -1.0 * r)


def norm(v):
    return math.sqrt(sum(x * x for x in v))


def worst(rho, ring=8):
    worst_err, case = 0.0, None
    offsets = [(0.0, 0.0)] if rho == 0.0 else [(rho * math.cos(2 * math.pi * k / ring + math.pi / ring), rho * math.sin(2 * math.pi * k / ring + math.pi / ring))
                                                for k in range(ring)]
    for k, (ox, oy) in enumerate(offsets):
        X = (A[0] - 0.75 * r * h[0] + ox, A[1] - 0.75 * r * h[1] + oy, A[2] - 0.75 * r * h[2])
        d = tuple(X[i] - A[i] for i in range(3))
        b = sum(d[i] * h[i] for i in range(3))
        c = sum(x * x for x in d) - (0.75 * r) ** 2
        disc = b * b - c
        if disc < 0:
            return None, (k, 'miss')
        lam = -b - math.sqrt(disc)
        C = tuple(X[i] + lam * h[i] for i in range(3))
        for th_deg in range(-45, 46, 5):
            th = math.radians(th_deg)
            B = (0.55 * math.sin(th) * r, -0.55 * math.cos(th) * r, -0.01 * r)
            v = tuple(B[i] - C[i] for i in range(3))
            cos = sum(v[i] * h[i] for i in range(3)) / norm(v)
            err = math.degrees(math.acos(max(-1.0, min(1.0, cos))))
            if err > worst_err:
                worst_err, case = err, (k, th_deg)
    return worst_err, case


print(f'lead-in direction h = ({h[0]:.5f}, {h[1]:.5f}, {h[2]:.5f}), {math.degrees(math.atan2(h[1], h[2])):.2f} deg above the axis')
for rho_f in (0.0, 0.15, 0.2, 0.25, 0.3):
    w, case = worst(rho_f * r)
    print(f'rho={rho_f:.2f} r  max heading error to any pass point {w:.1f} deg at (ring, theta) {case}')
for rho_f in (0.2, 0.25):
    print('adjacent chord', rho_f, round(2 * rho_f * r * math.sin(math.pi / 8), 1), 'm')
