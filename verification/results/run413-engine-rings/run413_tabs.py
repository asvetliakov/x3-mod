#!/usr/bin/env python3
"""Run 125 A (run413): the yellow rim around each Ocelot nozzle. Pixels at plate-plane radius 0.16..0.45 value_eff
(run413_rings.py's ray-plane radius) whose colour is yellow (B < 0.5 R, G > 0.6 R, R > 0.3: the light-mapped tabs,
material 10 metal_gastanksA_01_light 1.0/0.94/0.35 and the lip walls, material 12, 0.3/0.2/0.05): count, median and
p90 engine RGB, so the ratio to the texture gives the light-map gain in effect (inferred: lit diffuse adds a little).
Usage: python3 run413_tabs.py 6044 11208 > run413_tabs_out.txt
"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from run413_profiles import cam, L, SESSION, W, H  # noqa: E402


def main(frames):
    np.seterr(all="ignore")
    for fr in frames:
        s = L[str(fr)]; r, t, p00, p11 = cam(s["camera_state"])
        hdr = np.memmap(os.path.join(SESSION, "hdr_1_%d.rgba16f" % fr), dtype="<f2", mode="r", shape=(H, W, 4))
        huge = sorted([d for d in s["engine_draw"] if "red_huge" in d["name"]], key=lambda d: int(d["handle"], 16))
        print("## frame %d" % fr)
        for d in s["engine_draw"]:
            if not ("red_huge" in d["name"] or "red_big3" in d["name"]):
                continue
            o = np.array([float(x) for x in d["origin"].split(",")]) @ r + t
            a = np.array([float(x) for x in d["axis"].split(",")]) @ r
            value = float(d["value_eff"])
            x = (o[0] / o[2] * p00 * 0.5 + 0.5) * W; y = (0.5 - o[1] / o[2] * p11 * 0.5) * H
            vpx = value * p00 * 0.5 * W / o[2]; rad = int(0.5 * vpx) + 2
            cx, cy = int(round(x)), int(round(y))
            x0, y0, x1, y1 = max(0, cx - rad), max(0, cy - rad), min(W, cx + rad + 1), min(H, cy + rad + 1)
            if x1 <= x0 or y1 <= y0:
                continue
            col = np.array(hdr[y0:y1, x0:x1, :3]).astype(float)
            yy, xx = np.mgrid[y0:y1, x0:x1] + 0.5
            dvec = np.stack([((xx / W) - 0.5) * 2 / p00, -((yy / H) - 0.5) * 2 / p11, np.ones(xx.shape)], -1)
            sc = (o @ a) / (dvec @ a); p = dvec * sc[..., None] - o
            rr = np.linalg.norm(p - (p @ a)[..., None] * a, axis=-1) / value
            R, G, B = col[..., 0], col[..., 1], col[..., 2]
            mk = (rr >= 0.16) & (rr < 0.45) & (sc > 0) & (B < 0.5 * R) & (G > 0.6 * R) & (R > 0.3)
            lit = "LIGHT" if d is huge[0] else "-"
            if mk.sum() < 5:
                print("%s h=%s %s yellow px %d" % (d["name"].split("_")[-1], d["handle"][-3:], lit, mk.sum())); continue
            print("%s h=%s %s vz %.0f yellow px %5d med %5.2f %5.2f %5.2f p90 %5.2f %5.2f %5.2f" % (
                d["name"].split("_")[-1], d["handle"][-3:], lit, o[2], mk.sum(), *np.median(col[mk], 0), *np.percentile(col[mk], 90, axis=0)))
        del hdr


if __name__ == "__main__":
    main([int(v) for v in sys.argv[1:]])
