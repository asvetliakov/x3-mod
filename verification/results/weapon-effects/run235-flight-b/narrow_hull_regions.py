"""The axis-region rule (core::hit_region: the owner axis nearest to the contact point's radial direction) on a narrow
hull: an ellipsoid of half-extents 1:1:6 (z the long axis). Prints the share of its surface (area-weighted) whose
region is the long axis +-z (the lattice plane perpendicular to the hull: seen side on, the patch is edge-on) and the
mean angle between that plane's normal and the hull normal there, and the same for the +-x/+-y regions.
Usage: python3 narrow_hull_regions.py [a c]   (default 1 6)
"""
import math
import sys

a, c = (float(sys.argv[1]), float(sys.argv[2])) if len(sys.argv) > 2 else (1.0, 6.0)
N = 600
area = {'long': 0.0, 'side': 0.0}
angle = {'long': 0.0, 'side': 0.0}
for i in range(N):
    th = math.pi * (i + 0.5) / N
    for j in range(N):
        ph = 2 * math.pi * (j + 0.5) / N
        p = (a * math.sin(th) * math.cos(ph), a * math.sin(th) * math.sin(ph), c * math.cos(th))
        # |r_theta x r_phi| for (a sin th cos ph, a sin th sin ph, c cos th)
        rt = (a * math.cos(th) * math.cos(ph), a * math.cos(th) * math.sin(ph), -c * math.sin(th))
        rp = (-a * math.sin(th) * math.sin(ph), a * math.sin(th) * math.cos(ph), 0.0)
        cr = (rt[1] * rp[2] - rt[2] * rp[1], rt[2] * rp[0] - rt[0] * rp[2], rt[0] * rp[1] - rt[1] * rp[0])
        dA = math.sqrt(sum(v * v for v in cr)) * (math.pi / N) * (2 * math.pi / N)
        comps = [p[2], -p[2], p[0], -p[0], p[1], -p[1]]
        k = max(range(6), key=lambda m: (comps[m], -m))
        axis = [(0, 0, 1), (0, 0, -1), (1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0)][k]
        n = (p[0] / a ** 2, p[1] / a ** 2, p[2] / c ** 2)
        nn = math.sqrt(sum(v * v for v in n))
        cosang = abs(sum(axis[m] * n[m] for m in range(3))) / nn
        key = 'long' if k < 2 else 'side'
        area[key] += dA
        angle[key] += dA * math.degrees(math.acos(min(1.0, cosang)))
total = sum(area.values())
for key in ('long', 'side'):
    print(f'{key}: area_share={area[key] / total:.3f} mean_angle_plane_normal_to_hull_normal_deg={angle[key] / area[key]:.1f}')
