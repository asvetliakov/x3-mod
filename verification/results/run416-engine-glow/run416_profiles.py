#!/usr/bin/env python3
"""Run 128 A (run416) end-on Ocelot nozzles: HDR radial profiles in units of the natural nozzle width n against the
fixture's 02b end-on disc at the same L / n (4: z 2, s 1).

Flight: F8 hdr_1_<frame>.rgba16f (engine-space FP16 RGBA, row 0 top, 5120x1440; the HDR target read back before the
frame's first write-back, i.e. before the AgX display transform and the bloom composite that runs inside it) and
depth_1_<frame>.rgba32f (channel 2 = view depth, -1 sky), memory-mapped, one window per nozzle. Nozzle centre,
facing f, n = 0.5 value_eff x ppu from run416_geometry.py (log rows via run416_log.py).
Fixture: 02b .pfm (run_engine_plumes.py --dump-images --dump-linear at 39f13132; red disc at (600, 540), n 150 px
natural, f 1, L / n 4; the disc's chase fade 0.4 applies there, so the fixture is compared by SHAPE, the cumulative
energy fraction by radius).

Per radius bin (n units): pixels, mean engine RGB, mean engine luma (the fixture's energy unit) and mean decoded luma
(e^2.2, the display/bloom domain), fraction over sky, fraction alpha >= 0.5 (the scene alpha = the game's authored
glow mask: hull emissive plates; bloom weights it x 0.375), fraction with depth nearer than the nozzle by > 0.25 value
(hull in front; excluded from the means), the mean decoded luma over sky / over hull with alpha < 0.5 / over
alpha >= 0.5 pixels separately (an additive plume term continues across the hull's silhouette; hull lighting stops
at it), median depth - vz in value units (the depth lane: the nozzle's recess and
rim). Then the energy split <= 0.5 n, 0.5-1, 1-2, 2-4 n (sum over bins of mean x annulus area, so a partly excluded
annulus is filled at its mean; background = the 3.5-4 n bin's median luma over sky, subtracted).
Usage: python3 run416_profiles.py RUN FRAME HANDLE [HANDLE ...] [--fixture PFM]
"""
import math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run416_geometry as g  # noqa: E402
W, H = 5120, 1440
BINS = np.arange(0, 4.0001, 0.25)
def luma(c):
    return 0.2126 * c[..., 0] + 0.7152 * c[..., 1] + 0.0722 * c[..., 2]
def profile(col, rr_n, keep, extra):
    rows = []
    for a, b in zip(BINS[:-1], BINS[1:]):
        m = (rr_n >= a) & (rr_n < b)
        k = m & keep
        if k.sum() == 0:
            rows.append((a, b, int(m.sum()), 0, None, 0.0, 0.0, {}))
            continue
        c = col[k]
        ex = {name: fn(m, k) for name, fn in extra.items()}
        rows.append((a, b, int(m.sum()), int(k.sum()), c.mean(0), float(luma(c).mean()),
                     float(luma(np.maximum(c, 0) ** 2.2).mean()), ex))
    return rows
def bands(rows, npx, bg_e, bg_d):
    out = {}
    for lo, hi in ((0, .5), (.5, 1), (1, 2), (2, 4)):
        e = d = 0.0
        for a, b, n_all, n_keep, rgb, le, ld, ex in rows:
            if a >= lo and b <= hi and n_keep:
                area = math.pi * (b * b - a * a) * npx * npx
                e += max(le - bg_e, 0) * area; d += max(ld - bg_d, 0) * area
        out[(lo, hi)] = (e, d)
    te = sum(v[0] for v in out.values()); td = sum(v[1] for v in out.values())
    return out, te, td
def show(label, rows, npx, extra_names):
    print("  r/n        px  kept  engine R G B         luma_e  luma_lin " + " ".join("%8s" % n for n in extra_names))
    for a, b, n_all, n_keep, rgb, le, ld, ex in rows:
        if rgb is None:
            print("  %.2f-%.2f %6d %5d  (all excluded)" % (a, b, n_all, n_keep)); continue
        print("  %.2f-%.2f %6d %5d  %5.3f %5.3f %5.3f  %7.4f %8.4f " % (a, b, n_all, n_keep, *rgb, le, ld) +
              " ".join("%8.3f" % ex[n] for n in extra_names))
    bg_e = rows[-1][5]; bg_d = rows[-1][6]
    out, te, td = bands(rows, npx, bg_e, bg_d)
    print("  energy (background %.4f / %.5f subtracted): " % (bg_e, bg_d) + "; ".join(
        "%.1f-%.1f n %.1f%% / %.1f%%" % (lo, hi, 100 * e / te if te else 0, 100 * d / td if td else 0) for (lo, hi), (e, d) in out.items()) +
          "  (engine luma / decoded luma); total %.0f / %.0f" % (te, td))
def flight(run, fr, handles):
    np.seterr(all="ignore")
    L = g.L
    noz = {n["handle"]: n for n in g.nozzles(fr)}
    sess = "/tmp/x3-bottleX3-" + run
    hdr = np.memmap(os.path.join(sess, "hdr_1_%d.rgba16f" % fr), dtype="<f2", mode="r", shape=(H, W, 4))
    dep = np.memmap(os.path.join(sess, "depth_1_%d.rgba32f" % fr), dtype="<f4", mode="r", shape=(H, W, 4))
    for h in handles:
        n = noz[h]; npx = n["npx"]; rad = int(4 * npx) + 2
        cx, cy = int(round(n["x"])), int(round(n["y"]))
        x0, y0, x1, y1 = max(0, cx - rad), max(0, cy - rad), min(W, cx + rad + 1), min(H, cy + rad + 1)
        c4 = np.array(hdr[y0:y1, x0:x1]).astype(float); col = c4[..., :3]; alpha = c4[..., 3]
        z = np.array(dep[y0:y1, x0:x1, 2]).astype(float)
        yy, xx = np.mgrid[y0:y1, x0:x1]; rr_n = np.hypot(xx - n["x"], yy - n["y"]) / npx
        vz = n["vz"]; val = n["value"]
        front = (z > 0) & (z < vz - 0.25 * val)
        keep = ~front
        others = [(o["name"], o["handle"], math.hypot(o["x"] - n["x"], o["y"] - n["y"]) / npx) for o in noz.values()
                  if o["handle"] != h and o["name"] in ("red_big3", "red_huge") and math.hypot(o["x"] - n["x"], o["y"] - n["y"]) < 4 * npx]
        print("## %s frame %d %s h=%s centre (%.1f,%.1f) dist %.0f vz %.0f f %+.3f value %.1f n %.1f px detail %.2f disc_w %.2f axial_w %.2f disc_far %.2f" % (
            run, fr, n["name"], h, n["x"], n["y"], n["dist"], vz, n["f"], val, npx, n["detail"], n["disc_w"], n["axial_w"], n["disc_far"]))
        print("   other Ocelot nozzles within 4 n (name handle distance/n): " + ", ".join("%s %s %.2f" % o for o in sorted(others, key=lambda o: o[2])))
        extra = {"sky": lambda m, k: float((z[m] <= 0).mean()),
                 "alpha>=.5": lambda m, k: float((alpha[k] >= 0.5).mean()),
                 "front": lambda m, k: float(front[m].mean()),
                 "lin_sky": lambda m, k: float(luma(np.maximum(col[k & (z <= 0)], 0) ** 2.2).mean()) if (k & (z <= 0)).any() else float("nan"),
                 "lin_hull": lambda m, k: float(luma(np.maximum(col[k & (z > 0) & (alpha < .5)], 0) ** 2.2).mean()) if (k & (z > 0) & (alpha < .5)).any() else float("nan"),
                 "lin_plate": lambda m, k: float(luma(np.maximum(col[k & (alpha >= .5)], 0) ** 2.2).mean()) if (k & (alpha >= .5)).any() else float("nan"),
                 "dz/value": lambda m, k: float(np.median(z[k][z[k] > 0]) - vz) / val if (z[k] > 0).any() else float("nan")}
        show("flight", profile(col, rr_n, keep, extra), npx, list(extra))
    del hdr, dep
def fixture(path, cx=600.0, cy=540.0, npx=150.0):
    sys.path.insert(0, os.path.join(HERE, "..", "engine-effects"))
    from plume_mouth_whiteness import load_pfm
    img = load_pfm(path)
    yy, xx = np.mgrid[0:img.shape[0], 0:img.shape[1]]; rr_n = np.hypot(xx - cx, yy - cy) / npx
    print("## fixture 02b red disc (%.0f,%.0f) n %.0f px natural, f 1, L/n 4: %s" % (cx, cy, npx, os.path.basename(path)))
    show("fixture", profile(img, rr_n, np.ones(rr_n.shape, bool), {}), npx, [])
if __name__ == "__main__":
    args = sys.argv[1:]
    if args[0] == "--fixture":
        fixture(args[1])
    else:
        flight(args[0], int(args[1]), args[2:])
