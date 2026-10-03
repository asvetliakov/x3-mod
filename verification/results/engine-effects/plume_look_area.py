#!/usr/bin/env python3
"""Axial-quad area of the plume look port (2026-10-03, review fixes the same day) against the first look, per nozzle, at
value 100 px.

First look (engine_plumes_core.h before the port): sigma 0.5 value, core radius 0.15 value, the trapezoid through
(-(2.25 sigma + 1 px), w_back) and (L + 1.125 sigma + 1 px, 1.125 sigma + 1 px), w(0) = max(2.25 sigma, r0) + 1 px.
New look (Look defaults, pulse off; review fixes: bulge 1.15, the halo at the nozzle's sigma along the whole plume):
nozzle width n = value / 4, the body's spread = max(1 + 0.48 erode, ring / line0) over the width line
0.5 bulge + (0.04 - 0.5 bulge) taper u, the halo's reach R = 2.25 sigma0 n constant; back = n max(2.25 sigma0, 0.05) +
1 px, front = L + R + 1 px; each end's half-width the wider of the body's line (extrapolated) and R, + 1 px. The same
formulas as build_nozzle(); the host test (test_engine_plumes AREA rows) measures them on the built vertices. Output:
plume_look_area_out.txt beside this script.
"""
from pathlib import Path

VALUE_PX, PX = 100.0, 1.0
BULGE, TAPER, ERODE, HALO, NOZZLE = 1.15, 0.45, 0.57, 1.1, 0.25


def first(z):
    sigma, L = 0.5 * VALUE_PX, z * VALUE_PX
    back, front = 2.25 * sigma + PX, L + 1.125 * sigma + PX
    w0, wf = max(2.25 * sigma, 0.15 * VALUE_PX) + PX, 1.125 * sigma + PX
    wb = w0 + (w0 - wf) * back / front
    return (wb + wf) * (back + front)


def new(z):
    n, L = NOZZLE * VALUE_PX, z * VALUE_PX
    hb = 0.5 * BULGE
    line0, slope = hb, (0.04 - hb) * TAPER
    sigma0 = 0.5 * HALO
    spread = max(1 + 0.48 * ERODE, (0.46 * BULGE + 3 * 0.0645497) / line0)
    reach = 2.25 * sigma0 * n
    back = n * max(2.25 * sigma0, 0.05) + PX
    front = L + reach + PX
    bb = bf = spread * n * line0
    if back < L:
        bb = spread * n * (line0 - slope * back / L)
        bf = spread * n * (line0 + slope * front / L)
    wb, wf = max(bb, reach) + PX, max(bf, reach) + PX
    return (wb + wf) * (back + front)


lines = []
for z in (0.25, 1.125, 2.0):
    a, b = first(z), new(z)
    lines.append(f'z={z:.3f} first_px2={a:.0f} new_px2={b:.0f} ratio={b / a:.3f}')
text = '\n'.join(lines) + '\n'
(Path(__file__).with_name('plume_look_area_out.txt')).write_text(text)
print(text, end='')
