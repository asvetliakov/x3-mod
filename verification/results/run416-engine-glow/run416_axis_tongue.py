#!/usr/bin/env python3
"""Energy of the end-on axial quad's tongue: from run416_fine.py wedge outputs (120-degree wedges) of one nozzle,
the decoded-luma energy at r >= R0 in the axis wedge less the control wedge (up, away from neighbours), over the
disc's energy at r < R0 (the control wedge x 3, i.e. the disc taken as round). Area per bin = pi ((r+0.05)^2 - r^2) / 3.
Usage: python3 run416_axis_tongue.py AXIS_OUT CONTROL_OUT [R0]"""
import sys, math
def read(p):
    out = {}
    for line in open(p):
        f = line.split()
        if len(f) >= 6 and f[0][0].isdigit():
            out[round(float(f[0]), 2)] = (float(f[2]), float(f[4]))
    return out
a, c = read(sys.argv[1]), read(sys.argv[2]); r0 = float(sys.argv[3]) if len(sys.argv) > 3 else 0.65
area = lambda r: math.pi * ((r + .05) ** 2 - r * r) / 3
tongue = sum(max(a[r][1] - c[r][1], 0) * area(r) for r in a if r >= r0 and r in c)
tongue_e = sum(max(a[r][0] - c[r][0], 0) * area(r) for r in a if r >= r0 and r in c)
disc = 3 * sum(c[r][1] * area(r) for r in c if r < r0); disc_e = 3 * sum(c[r][0] * area(r) for r in c if r < r0)
print("r0 %.2f n: tongue (axis wedge - control, r >= r0) decoded %.4f, engine R %.4f; disc (r < r0, round) decoded %.4f, engine R %.4f; "
      "tongue / disc decoded %.3f, engine R %.3f   (units: n^2 x value)" % (r0, tongue, tongue_e, disc, disc_e, tongue / disc, tongue_e / disc_e))
