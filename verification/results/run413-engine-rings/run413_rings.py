#!/usr/bin/env python3
"""Run 125 A (run413): per Ocelot nozzle, the F8 HDR capture binned by the radius in the nozzle's plate plane.

Each pixel's view ray is intersected with the plane through the engine_draw origin normal to its axis (view space from
camera_state), giving the plate-plane radius r in value_eff units (the Ocelot hull: hexagonal ExL plate, material 11,
r < 0.148 v; the flat white annulus, material 12 light map 1.0, 0.148..0.159 v; the protruding lip, material 12 walls,
0.148..0.211 v, 0.165 v long; run413_ocelot_faces.py). Zones: plate 0..0.13, annulus 0.145..0.165, lip/outer
0.165..0.22, tabs 0.22..0.30 (the plume disc's own ring sits at 0.529 n = 0.265 v), out 0.30..0.40. Per zone: pixels,
median and p95 engine RGB, display RGB of the median (AgX, the frame-before EV), median depth minus the origin's view z,
and the fraction with min(rgb) >= the display white grey. Usage: python3 run413_rings.py 4197 6044 11208 > out
"""
import json, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "run412-engine-disc"))
sys.path.insert(0, HERE)
import run412_engine_disc as m  # noqa: E402
from run413_profiles import cam, white_point, L, SESSION, W, H  # noqa: E402
ZONES = [("plate", 0.0, 0.13), ("annulus", 0.145, 0.165), ("lip", 0.165, 0.22), ("tabs", 0.22, 0.30), ("out", 0.30, 0.40)]


def main(frames):
    np.seterr(all="ignore")
    for fr in frames:
        s = L[str(fr)]; r, t, p00, p11 = cam(s["camera_state"])
        ev = float(L[str(fr - 1)]["hdr_frame"]["ev_adapted"]); wp = white_point(ev)
        print("## frame %d ev %.3f display white from grey %.2f; engine_stage capped=%s faded=%s discs=%s" % (
            fr, ev, wp, s["engine_stage"]["capped"], s["engine_stage"]["faded"], s["engine_stage"]["discs"]))
        hdr = np.memmap(os.path.join(SESSION, "hdr_1_%d.rgba16f" % fr), dtype="<f2", mode="r", shape=(H, W, 4))
        dep = np.memmap(os.path.join(SESSION, "depth_1_%d.rgba32f" % fr), dtype="<f4", mode="r", shape=(H, W, 4))
        huge = sorted([d for d in s["engine_draw"] if "red_huge" in d["name"]], key=lambda d: int(d["handle"], 16))
        for d in s["engine_draw"]:
            if not ("red_huge" in d["name"] or "red_big3" in d["name"]):
                continue
            o = np.array([float(x) for x in d["origin"].split(",")]) @ r + t
            a = np.array([float(x) for x in d["axis"].split(",")]) @ r
            value = float(d["value_eff"]); f = float(a @ (-o / np.linalg.norm(o)))
            x = (o[0] / o[2] * p00 * 0.5 + 0.5) * W; y = (0.5 - o[1] / o[2] * p11 * 0.5) * H
            vpx = value * p00 * 0.5 * W / o[2]
            rad = int(min(0.42 * vpx / max(f, 0.3), 700)) + 2
            cx, cy = int(round(x)), int(round(y))
            x0, y0, x1, y1 = max(0, cx - rad), max(0, cy - rad), min(W, cx + rad + 1), min(H, cy + rad + 1)
            if x1 <= x0 or y1 <= y0:
                continue
            col = np.array(hdr[y0:y1, x0:x1, :3]).astype(float)
            z = np.array(dep[y0:y1, x0:x1, 2]).astype(float)
            yy, xx = np.mgrid[y0:y1, x0:x1] + 0.5
            dx = ((xx / W) - 0.5) * 2 / p00; dy = -((yy / H) - 0.5) * 2 / p11
            dvec = np.stack([dx, dy, np.ones_like(dx)], -1)
            sc = (o @ a) / (dvec @ a)
            p = dvec * sc[..., None] - o
            rr = np.linalg.norm(p - (p @ a)[..., None] * a, axis=-1) / value
            lit = "LIGHT" if d is huge[0] else "-"
            print("# %s h=%s idx=%s %s facing %.3f vz %.0f value %.0f px origin (%.0f,%.0f)" % (
                d["name"].split("_")[-1], d["handle"], d["index"], lit, f, o[2], vpx, x, y))
            for zn, lo, hi in ZONES:
                mk = (rr >= lo) & (rr < hi) & (sc > 0)
                if mk.sum() < 3:
                    continue
                med = np.median(col[mk], 0); p95 = np.percentile(col[mk], 95, axis=0)
                disp = m.tonemap(med[None], 2.0 ** ev)[0]
                print("   %-7s px %6d med %5.2f %5.2f %5.2f p95 %5.2f %5.2f %5.2f disp %.2f %.2f %.2f dz %+7.1f white %.2f" % (
                    zn, mk.sum(), *med, *p95, *disp, np.median(z[mk]) - o[2], (col[mk].min(1) >= wp).mean()))
        del hdr, dep


if __name__ == "__main__":
    main([int(v) for v in sys.argv[1:]])
