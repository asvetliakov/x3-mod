#!/usr/bin/env python3
"""Run 125 A (run413): per Ocelot nozzle, the radial profile of the F8 HDR capture and the depth capture about the
nozzle's projected origin (engine_draw origin through camera_state), frames 6044 (Q1, engines4 pose) and 11208 (Q2,
engines5 pose). Captures read-only, one window per nozzle (memory-mapped). Uses run413_log.json (run413_log.py) and
run412_engine_disc.py's tonemap (AgX, gamma 2.2 decode, the frame-before exposure).

Per nozzle: view z, facing f = axis . to_camera, projected value px (value x ppu) and nozzle width n = 0.5 value px;
the depth under the origin and the minimum depth within 0.6 n (the rim nearest the camera) and their gap in record
units against the plume's guards (bias 0.5 value f, spill min(2 value, 300), soft 0.15 value); then per annulus
(4 px, out to 1.6 n): pixel count, median engine RGB, median display RGB, median depth, fraction min(rgb)>=1.
Usage: python3 run413_profiles.py > run413_profiles_out.txt
"""
import json, math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "run412-engine-disc"))
import run412_engine_disc as m  # noqa: E402
SESSION = "/tmp/x3-bottleX3-run413"
W, H = 5120, 1440
L = json.load(open(os.path.join(HERE, "run413_log.json")))


def cam(d):
    r = np.array([[float(d["r%d%d" % (i, j)]) for j in range(3)] for i in range(3)])
    return r, np.array([float(x) for x in d["t"].split(",")]), float(d["p00"]), float(d["p11"])


def white_point(ev):
    g = np.linspace(0.2, 12.0, 2361)
    t = m.tonemap(np.stack([g] * 3, -1), 2.0 ** ev)
    return g[np.argmax(t.min(1) >= 0.98)]


def main(frames):
    np.seterr(all="ignore")
    for fr in frames:
        s = L[str(fr)]
        r, t, p00, p11 = cam(s["camera_state"])
        ev = float(L[str(fr - 1)]["hdr_frame"]["ev_adapted"])
        print("## frame %d ev(frame-1) %.3f exposure %.3f; display white (all channels >= 0.98) from grey %.2f; red-only "
              "pixel R saturates (display R >= 0.98) from engine R ~ see rows" % (fr, ev, 2 ** ev, white_point(ev)))
        hdr = np.memmap(os.path.join(SESSION, "hdr_1_%d.rgba16f" % fr), dtype="<f2", mode="r", shape=(H, W, 4))
        dep = np.memmap(os.path.join(SESSION, "depth_1_%d.rgba32f" % fr), dtype="<f4", mode="r", shape=(H, W, 4))
        for d in s["engine_draw"]:
            name = d["name"].split("\\")[-1].replace("fx_engine_xtc_", "")
            if name in ("red_nor", "red_tiny"):
                continue
            o = np.array([float(x) for x in d["origin"].split(",")]); a = np.array([float(x) for x in d["axis"].split(",")])
            v = o @ r + t; av = a @ r
            e = -v / np.linalg.norm(v); f = float(av @ e)
            x = (v[0] / v[2] * p00 * 0.5 + 0.5) * W; y = (0.5 - v[1] / v[2] * p11 * 0.5) * H
            value = float(d["value_eff"]); ppu = p00 * 0.5 * W / v[2]
            npx = 0.5 * value * ppu
            rad = int(min(1.6 * npx, 400)) + 2
            cx, cy = int(round(x)), int(round(y))
            if not (0 <= cx < W and 0 <= cy < H):
                print("%s h=%s offscreen (%.0f,%.0f)" % (name, d["handle"], x, y)); continue
            x0, y0, x1, y1 = max(0, cx - rad), max(0, cy - rad), min(W, cx + rad + 1), min(H, cy + rad + 1)
            col = np.array(hdr[y0:y1, x0:x1, :3]).astype(float)
            z = np.array(dep[y0:y1, x0:x1, 2]).astype(float)
            yy, xx = np.mgrid[y0:y1, x0:x1]
            rr = np.hypot(xx - x, yy - y)
            disp = m.tonemap(col, 2.0 ** ev)
            z0 = z[cy - y0, cx - x0]
            inner = (rr <= 0.6 * npx) & (z > 0)
            zmin = z[inner].min() if inner.any() else float("nan")
            bias = 0.5 * value * max(0.0, f)
            print("# %s h=%s idx=%s z=%s s=%s value %.1f vz %.0f facing %.3f origin px (%.1f,%.1f) value %.0f px n %.0f px | "
                  "depth at origin %.1f (record units; vz %.1f) min depth within 0.6n %.1f -> origin behind nearest rim by %.1f; "
                  "bias %.1f spill guard %.1f soft 0.15v %.1f" % (name, d["handle"], d["index"], d["z"], d["s"], value, v[2], f, x, y,
                  value * ppu, npx, z0, v[2], zmin, z0 - zmin, bias, min(2 * value, 300.0), 0.15 * value))
            print("   r_px  n_frac   px  engine_RGB(median)      display_RGB(median)  depth(median) frac_min>=1 frac_sky")
            for k in range(0, rad, 4):
                mk = (rr >= k) & (rr < k + 4)
                if mk.sum() == 0:
                    continue
                ce = np.median(col[mk], 0); cd = np.median(disp[mk], 0); zm = np.median(z[mk])
                print("   %4d  %.2f  %5d  %5.2f %5.2f %5.2f  %4.2f %4.2f %4.2f  %8.1f  %.2f %.2f" % (
                    k, (k + 2) / npx, mk.sum(), *ce, *cd, zm, (col[mk].min(1) >= 1).mean(), (z[mk] <= 0).mean()))
        del hdr, dep


if __name__ == "__main__":
    main([int(a) for a in sys.argv[1:]] or [6044, 11208])
