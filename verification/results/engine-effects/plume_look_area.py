#!/usr/bin/env python3
"""Axial-quad area of the plume look port (2026-10-03) against the first look, per nozzle, at value 100 px.

First look (engine_plumes_core.h before the port): sigma 0.5 value, core radius 0.15 value, the trapezoid through
(-(2.25 sigma + 1 px), w_back) and (L + 1.125 sigma + 1 px, 1.125 sigma + 1 px), w(0) = max(2.25 sigma, r0) + 1 px.
New look (Look defaults, pulse off): nozzle width n = value / 4, spread = max(1 + 0.48 erode, 2.25 sigma0 / w0),
the width line 0.5 bulge + (0.04 - 0.5 bulge) taper u, back = n max(2.25 sigma0, 0.05) + 1 px, front = L + the tip's
halo reach + 1 px. The same formulas as build_nozzle(); the host test (test_engine_plumes AREA rows) measures them on
the built vertices. Output: plume_look_area_out.txt beside this script.
"""
from pathlib import Path

VALUE_PX, PX = 100.0, 1.0
BULGE, TAPER, ERODE, HALO, NARROW, NOZZLE = 1.2, 0.45, 0.57, 1.1, 1.6, 0.25


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
    w0 = min(hb, hb * 0.45 + hb * TAPER)
    w_tip = (hb + slope) * max(1 - NARROW * TAPER, 0.05)
    sigma0 = 0.5 * HALO
    spread = max(1 + 0.48 * ERODE, 2.25 * sigma0 / w0)
    spread = max(spread, (0.46 * BULGE + 3 * 0.0645497) / line0)
    back = n * max(2.25 * sigma0, 0.05) + PX
    reach_tip = 2.25 * sigma0 * w_tip / w0 * n
    front = L + reach_tip + PX
    wb = wf = spread * n * line0 + PX
    if back < L:
        wb = spread * n * (line0 - slope * back / L) + PX
        wf = spread * n * (line0 + slope * front / L) + PX
    wf = max(wf, reach_tip + PX)
    return (wb + wf) * (back + front)


lines = []
for z in (0.25, 1.125, 2.0):
    a, b = first(z), new(z)
    lines.append(f'z={z:.3f} first_px2={a:.0f} new_px2={b:.0f} ratio={b / a:.3f}')
text = '\n'.join(lines) + '\n'
(Path(__file__).with_name('plume_look_area_out.txt')).write_text(text)
print(text, end='')
