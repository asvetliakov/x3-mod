#!/usr/bin/env python3
"""Display (AgX, run412_engine_disc.tonemap) of the radial-bin mean engine RGB of run416_profiles.py outputs at the
frame's exposure, and the AgX grey white point (all channels >= 0.98) at that EV (run413_profiles.white_point's rule).
Usage: python3 run416_display_rows.py PROFILE_OUT EV HANDLE"""
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(HERE, "..", "run412-engine-disc"))
import run412_engine_disc as m
np.seterr(all="ignore")
p, ev, h = sys.argv[1], float(sys.argv[2]), sys.argv[3]
g = np.linspace(0.2, 12.0, 2361); t = m.tonemap(np.stack([g] * 3, -1), 2.0 ** ev)
print("EV %.4f: display white from grey engine %.2f; bins of h=%s (r/n, engine RGB -> display RGB):" % (ev, g[np.argmax(t.min(1) >= 0.98)], h))
on = False
for line in open(p):
    if line.startswith("## "):
        on = ("h=" + h) in line
    elif on and line.strip()[:1].isdigit():
        f = line.split(); rgb = np.array([float(x) for x in f[3:6]])
        d = m.tonemap(rgb[None, :], 2.0 ** ev)[0]
        print("  %s  %.3f %.3f %.3f -> %.2f %.2f %.2f" % (f[0], *rgb, *d))
