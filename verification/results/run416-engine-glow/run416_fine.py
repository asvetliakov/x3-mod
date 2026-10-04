#!/usr/bin/env python3
"""Fine radial profile (0.05 n bins to 1.5 n) of an end-on disc: engine R, G and decoded luma (mean over pixels that
are not hull in front), flight nozzle vs the fixture's 02b red disc; locates the disc's ring (law: radius
0.46 bulge = 0.53 n, sigma 0.065 n x disc_ring_width; src/proxy/engine_plumes_core.h:1348) and the edge.
Optional SECTOR (degrees, screen angle of the half-width-60-degree wedge kept; 90 = up): a nozzle whose neighbours
lie on one side is read on the other (the huge 568 at run414 4832: neighbours below, wedge up).
Usage: python3 run416_fine.py RUN FRAME HANDLE [SECTOR] | python3 run416_fine.py --fixture PFM
"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
W, H = 5120, 1440
LW = np.array([0.2126, 0.7152, 0.0722])
def rows(col, rr, keep, extra=None):
    out = []
    for a in np.arange(0, 1.5, 0.05):
        k = keep & (rr >= a) & (rr < a + 0.05)
        if not k.any():
            continue
        c = col[k]
        out.append((a, int(k.sum()), c[:, 0].mean(), c[:, 1].mean(), (np.maximum(c, 0) ** 2.2 @ LW).mean(),
                    extra[k].mean() if extra is not None else float("nan")))
    return out
def show(t, rs):
    print(t); print("  r/n    px   eng_R  eng_G  lin_luma  alpha>=.5")
    for r in rs:
        print("  %.2f %5d  %6.3f %6.3f %8.4f  %.2f" % r)
if sys.argv[1] == "--fixture":
    sys.path.insert(0, os.path.join(HERE, "..", "engine-effects"))
    from plume_mouth_whiteness import load_pfm
    img = load_pfm(sys.argv[2]); yy, xx = np.mgrid[0:img.shape[0], 0:img.shape[1]]
    show("## fixture 02b red (600,540) n 150", rows(img, np.hypot(xx - 600, yy - 540) / 150.0, np.ones(xx.shape, bool)))
else:
    run, fr, h = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    SECTOR = float(sys.argv[4]) if len(sys.argv) > 4 else None
    sys.argv = [sys.argv[0], run]
    import run416_geometry as g
    n = [o for o in g.nozzles(fr) if o["handle"] == h][0]; npx = n["npx"]; rad = int(1.6 * npx) + 2
    cx, cy = int(round(n["x"])), int(round(n["y"]))
    sess = "/tmp/x3-bottleX3-" + run
    hdr = np.memmap(os.path.join(sess, "hdr_1_%d.rgba16f" % fr), dtype="<f2", mode="r", shape=(H, W, 4))
    dep = np.memmap(os.path.join(sess, "depth_1_%d.rgba32f" % fr), dtype="<f4", mode="r", shape=(H, W, 4))
    c4 = np.array(hdr[cy - rad:cy + rad + 1, cx - rad:cx + rad + 1]).astype(float)
    z = np.array(dep[cy - rad:cy + rad + 1, cx - rad:cx + rad + 1, 2]).astype(float)
    yy, xx = np.mgrid[cy - rad:cy + rad + 1, cx - rad:cx + rad + 1]
    keep = ~((z > 0) & (z < n["vz"] - 0.25 * n["value"]))
    if len(sys.argv) > 4 or SECTOR is not None:
        ang = np.degrees(np.arctan2(-(yy - n["y"]), xx - n["x"]))
        keep &= np.abs((ang - SECTOR + 180) % 360 - 180) <= 60
    show("## %s %d %s h=%s n %.1f px f %.3f detail %.2f sector %s" % (run, fr, n["name"], h, npx, n["f"], n["detail"], SECTOR),
         rows(c4[..., :3], np.hypot(xx - n["x"], yy - n["y"]) / npx, keep, (c4[..., 3] >= .5).astype(float)))
