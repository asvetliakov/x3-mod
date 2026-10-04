#!/usr/bin/env python3
"""Run 125 A (run413): which radius peaks are hull and which are plume. For the two Ocelot red_huge nozzles over the
8 frames of an F8 set, the median engine RGB per plate-plane radius bin (0.01 value_eff; run413_rings.py's ray-plane
radius), one row per frame. Hull features (the white material-12 annulus at 0.148..0.159 v) stay at a fixed radius;
the plume disc's terms scale with the near-cap factor k (value x k, L x k; the length pulse changes k per frame), so
its ring (0.529 nozzle widths = 0.2645 k v) moves. Also prints the cap estimate per frame: q and k from
engine_plumes_core.h (half = 0.7226 x 0.5 value; f = p11 H / 2; cap 0.12 H; near depth o_z - toward L) for the pulse
extremes L = 2 value x (0.75, 1.25).
Usage: python3 run413_ring_frames.py 6044 > run413_ring_frames_6044_out.txt
"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from run413_profiles import cam, L, SESSION, W, H  # noqa: E402


def cap(o, a, value, z, p11, pulse):
    half = 0.7226 * 0.5 * value; f = p11 * H * 0.5; capv = 0.12 * H
    Ln = max(z, 0.5) * value * pulse
    toward = -a[2] if a[2] < 0 else 0.0
    nd = max(o[2] - toward * Ln, 1.0)
    q = 2 * half * f / nd / capv
    k = 1.0
    if q > 1:
        den = 2 * half * f + capv * toward * Ln
        k = capv * o[2] / den if den > 0 else 1 / q
        if not (0 < k <= 1):
            k = 1 / q
    t = min(1.0, max(0.0, (q - 0.8) / 0.2))
    return q, k, 1 - 0.6 * t


def main(first):
    np.seterr(all="ignore")
    bins = np.arange(0.0, 0.36, 0.01)
    for fr in range(first, first + 8):
        s = L[str(fr)]; r, t, p00, p11 = cam(s["camera_state"])
        hdr = np.memmap(os.path.join(SESSION, "hdr_1_%d.rgba16f" % fr), dtype="<f2", mode="r", shape=(H, W, 4))
        for d in sorted([d for d in s["engine_draw"] if "red_huge" in d["name"]], key=lambda d: d["handle"]):
            o = np.array([float(x) for x in d["origin"].split(",")]) @ r + t
            a = np.array([float(x) for x in d["axis"].split(",")]) @ r
            value = float(d["value_eff"]); z = float(d["z"])
            x = (o[0] / o[2] * p00 * 0.5 + 0.5) * W; y = (0.5 - o[1] / o[2] * p11 * 0.5) * H
            vpx = value * p00 * 0.5 * W / o[2]
            rad = int(0.4 * vpx) + 2
            cx, cy = int(round(x)), int(round(y))
            x0, y0, x1, y1 = max(0, cx - rad), max(0, cy - rad), min(W, cx + rad + 1), min(H, cy + rad + 1)
            col = np.array(hdr[y0:y1, x0:x1, :3]).astype(float)
            yy, xx = np.mgrid[y0:y1, x0:x1] + 0.5
            dvec = np.stack([((xx / W) - 0.5) * 2 / p00, -((yy / H) - 0.5) * 2 / p11, np.ones(xx.shape)], -1)
            sc = (o @ a) / (dvec @ a)
            p = dvec * sc[..., None] - o
            rr = np.linalg.norm(p - (p @ a)[..., None] * a, axis=-1) / value
            caps = [cap(o, a, value, z, p11, pu) for pu in (0.75, 1.0, 1.25)]
            print("%d h=%s vz %.0f value %.0f px | cap q/k/disc_fade at pulse 0.75,1,1.25: %s -> plume ring at r/v %s" % (
                fr, d["handle"][-3:], o[2], vpx, " ".join("%.2f/%.2f/%.2f" % c for c in caps),
                "/".join("%.3f" % (0.2645 * c[1]) for c in caps)))
            rows = []
            for b in bins:
                mk = (rr >= b) & (rr < b + 0.01) & (sc > 0)
                rows.append(np.median(col[mk], 0) if mk.sum() > 2 else np.full(3, np.nan))
            rows = np.array(rows)
            for ch, nm in enumerate("RGB"):
                print("   %s " % nm + " ".join("%5.2f" % v for v in rows[:, ch]))
        del hdr
    print("bins r/v from: " + " ".join("%5.2f" % b for b in bins))


if __name__ == "__main__":
    main(int(sys.argv[1]))
