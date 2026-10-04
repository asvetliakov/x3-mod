#!/usr/bin/env python3
"""Screen direction (degrees, 90 = up) and projected length (in n) of a nozzle's plume axis: origin -> origin + L axis,
L = z value (the game's length law; 4 n at z 2), for the sector reads of run416_fine.py (the axial quad lies along it).
Usage: python3 run416_axis_dir.py RUN FRAME HANDLE [...]"""
import math, sys
import numpy as np
run = sys.argv[1]; fr = int(sys.argv[2]); hs = sys.argv[3:]
sys.argv = [sys.argv[0], run]
import run416_geometry as g
s = g.L[str(fr)]; r, t, p00, p11 = g.cam(s["camera_state"])
for d in s["engine_draw"]:
    if d["handle"] not in hs:
        continue
    o = np.array([float(x) for x in d["origin"].split(",")]); a = np.array([float(x) for x in d["axis"].split(",")])
    val = float(d["value_eff"]); Lu = float(d["z"]) * val
    def px(p):
        v = p @ r + t
        return (v[0] / v[2] * p00 * 0.5 + 0.5) * g.W, (0.5 - v[1] / v[2] * p11 * 0.5) * g.H, v[2]
    x0, y0, z0 = px(o); x1, y1, _ = px(o + a * Lu)
    n = 0.5 * val * p00 * 0.5 * g.W / z0
    print("%s %s h=%s axis on screen: %.0f deg, tip at %.2f n from the nozzle (L = %.0f units = %.1f n)" % (
        run, fr, d["handle"], math.degrees(math.atan2(-(y1 - y0), x1 - x0)), math.hypot(x1 - x0, y1 - y0) / n, Lu, Lu / (0.5 * val)))
