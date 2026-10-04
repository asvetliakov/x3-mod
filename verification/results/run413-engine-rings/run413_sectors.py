#!/usr/bin/env python3
"""Run 125 A (run413): the Ocelot red_huge nozzles' rim by angle. Pixels at plate-plane radius 0.145..0.22 value_eff
(run413_rings.py's ray-plane radius: the white material-12 annulus and the protruding lip) in 8 sectors of the
screen angle about the projected origin; per sector the pixel count, median engine RGB, its AgX display and the median
depth minus the plate's (the depth under the origin). Also the display of reference engine colours at the frame's EV.
Usage: python3 run413_sectors.py 6051 11208 > run413_sectors_out.txt
"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "run412-engine-disc"))
sys.path.insert(0, HERE)
import run412_engine_disc as m  # noqa: E402
from run413_profiles import cam, L, SESSION, W, H  # noqa: E402
REF = {"ExL plate 1.00/0.88/0.66": (1.0, 0.88, 0.66), "mat10 lightmap x1 1.00/0.94/0.35": (1.0, 0.94, 0.35),
       "mat10 lightmap x4": (4.0, 3.76, 1.40), "mat12 annulus x1 1/1/1": (1.0, 1.0, 1.0), "mat12 annulus x4": (4.0, 4.0, 4.0),
       "mat12 wall x4 1.2/0.8/0.2": (1.2, 0.8, 0.2)}


def main(frames):
    np.seterr(all="ignore")
    for fr in frames:
        s = L[str(fr)]; r, t, p00, p11 = cam(s["camera_state"])
        ev = float(L[str(fr - 1)]["hdr_frame"]["ev_adapted"])
        print("## frame %d ev %.3f; reference display: %s" % (fr, ev, "; ".join(
            "%s -> %s" % (k, "/".join("%.2f" % c for c in m.tonemap(np.array([v]), 2.0 ** ev)[0])) for k, v in REF.items())))
        hdr = np.memmap(os.path.join(SESSION, "hdr_1_%d.rgba16f" % fr), dtype="<f2", mode="r", shape=(H, W, 4))
        dep = np.memmap(os.path.join(SESSION, "depth_1_%d.rgba32f" % fr), dtype="<f4", mode="r", shape=(H, W, 4))
        huge = sorted([d for d in s["engine_draw"] if "red_huge" in d["name"]], key=lambda d: int(d["handle"], 16))
        for d in huge:
            o = np.array([float(x) for x in d["origin"].split(",")]) @ r + t
            a = np.array([float(x) for x in d["axis"].split(",")]) @ r
            value = float(d["value_eff"])
            x = (o[0] / o[2] * p00 * 0.5 + 0.5) * W; y = (0.5 - o[1] / o[2] * p11 * 0.5) * H
            vpx = value * p00 * 0.5 * W / o[2]; rad = int(0.3 * vpx) + 2
            cx, cy = int(round(x)), int(round(y))
            x0, y0, x1, y1 = max(0, cx - rad), max(0, cy - rad), min(W, cx + rad + 1), min(H, cy + rad + 1)
            col = np.array(hdr[y0:y1, x0:x1, :3]).astype(float); z = np.array(dep[y0:y1, x0:x1, 2]).astype(float)
            yy, xx = np.mgrid[y0:y1, x0:x1] + 0.5
            dvec = np.stack([((xx / W) - 0.5) * 2 / p00, -((yy / H) - 0.5) * 2 / p11, np.ones(xx.shape)], -1)
            sc = (o @ a) / (dvec @ a); p = dvec * sc[..., None] - o
            rr = np.linalg.norm(p - (p @ a)[..., None] * a, axis=-1) / value
            ang = (np.degrees(np.arctan2(-(yy - y), xx - x)) + 360) % 360
            z0 = z[cy - y0, cx - x0]
            print("# %s h=%s %s vz %.0f plate depth %.1f" % (d["name"].split("_")[-1], d["handle"], "LIGHT" if d is huge[0] else "-", o[2], z0))
            for k in range(8):
                mk = (rr >= 0.145) & (rr < 0.22) & (ang >= 45 * k) & (ang < 45 * k + 45) & (sc > 0)
                if mk.sum() < 3:
                    continue
                med = np.median(col[mk], 0); disp = m.tonemap(med[None], 2.0 ** ev)[0]
                print("   sector %3d-%3d deg px %5d med %5.2f %5.2f %5.2f disp %.2f %.2f %.2f dz %+7.1f" % (
                    45 * k, 45 * k + 45, mk.sum(), *med, *disp, np.median(z[mk]) - z0))
        del hdr, dep


if __name__ == "__main__":
    main([int(v) for v in sys.argv[1:]])
