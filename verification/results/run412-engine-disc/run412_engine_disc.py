#!/usr/bin/env python3
"""End-on engine disc in the Run124 flight (run412, flight G): own ship (Split Scorpion, chase view) and the far nozzles.

Reads, one capture at a time (memory-mapped, only a window per nozzle), the F8 captures of
/tmp/x3-bottleX3-run412 (read-only): hdr_1_<frame>.rgba16f (the FP16 HDR colour buffer in ENGINE space, the
tonemapper's input: rgba16f_row_major 5120x1440, row 0 = top) and depth_1_<frame>.rgba32f (channel 2 = the
view depth in camera units, -1 = sky). The session log gives per frame the camera (camera_state: rows r and t,
row-vector view = world . R + t; p00, p11), the exposure (hdr_frame ev of the frame before, the tonemapper's
exp2(EV_adapted) of frame n-1) and the nozzle origins (engine_draw name/origin/size).

Per window (own ship: radius 72 px about the mean of its two projected nozzles, nor + tiny, which lie 41 px apart;
far nozzles: radius 20 px about each group within 24 px): peak per channel (engine space), pixels with
min(rgb) >= 1.0, pixels the AgX write-back (tools/analysis/agx_reference.py: gamma 2.2 decode, look none, the
exposure of the frame before) shows white (all three display channels >= 0.98), pixels whose display R channel
is saturated (>= 0.98: the pink/red clipped area), pixels with engine R >= 1.0 (the red halo; and how many of
those lie over sky, depth -1, so they are the plume's and not hull lighting), each with its equivalent diameter
sqrt(4 A / pi); per nozzle the extents of those masks along the pixel row through its centre, its centre RGB, and
(own ship) the nozzle opening read from the depth capture: the flood-filled pixels within 0.15 depth units of the
depth under the projected centre (the recessed plateau of the nozzle; far nozzles are too small to read).

Usage: python3 run412_engine_disc.py > run412_engine_disc_out.txt
"""
import math
import os
import sys

import numpy as np

SESSION = "/tmp/x3-bottleX3-run412"
LOG = os.path.join(SESSION, "session-20261004-124734-212.log")
W, H = 5120, 1440
FRAMES = list(range(4480, 4488)) + list(range(5437, 5445))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools", "analysis"))
import agx_reference as agx  # noqa: E402

M_IN = np.array(agx.M_IN)
M_OUT = np.array(agx.M_OUT)
COEF = agx.CONTRAST_COEFFICIENTS


def tonemap(e, exposure):
    """Vectorised agx_reference.tonemap_engine (decode gamma 2.2, look none). e: (..., 3) engine space."""
    with np.errstate(all="ignore"):  # numpy's BLAS matmul raises spurious FP flags on this host; the values are finite
        v = np.maximum(e, 0.0) ** 2.2 * exposure
        v = v @ M_IN.T
        t = (np.clip(np.log2(np.maximum(v, agx.LOG_FLOOR)), agx.MIN_EV, agx.MAX_EV) - agx.MIN_EV) / (agx.MAX_EV - agx.MIN_EV)
        c = np.polyval(COEF, t)
        return np.clip(c @ M_OUT.T, 0.0, 1.0)


def kv(line):
    out = {}
    for tok in line.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out.setdefault(k, v)
    return out


def read_log():
    want = set(FRAMES) | {f - 1 for f in FRAMES}
    cams, evs, draws = {}, {}, {}
    with open(LOG, "rb") as f:
        for raw in f:
            if not (raw.startswith(b"camera_state device=1 ") or raw.startswith(b"hdr_frame device=1 ")
                    or raw.startswith(b"engine_draw device=1 ")):
                continue
            line = raw.decode("ascii", "replace")
            d = kv(line)
            fr = int(d.get("frame", -1))
            if fr not in want:
                continue
            if line.startswith("camera_state"):
                r = np.array([[float(d["r%d%d" % (i, j)]) for j in range(3)] for i in range(3)])
                cams[fr] = (r, np.array([float(x) for x in d["t"].split(",")]), float(d["p00"]), float(d["p11"]))
            elif line.startswith("hdr_frame"):
                evs[fr] = float(d["ev_adapted"])
            else:
                draws.setdefault(fr, []).append((d["name"].split("\\")[-1], float(d["size"]),
                                                 np.array([float(x) for x in d["origin"].split(",")])))
    return cams, evs, draws


def project(cam, p):
    r, t, p00, p11 = cam
    v = p @ r + t
    return (v[0] / v[2] * p00 * 0.5 + 0.5) * W, (0.5 - v[1] / v[2] * p11 * 0.5) * H, v[2]


def extent(mask_line):
    idx = np.nonzero(mask_line)[0]
    return int(idx[-1] - idx[0] + 1) if len(idx) else 0


def flood(m, y, x):
    seen = np.zeros_like(m)
    if not m[y, x]:
        return seen
    stack = [(y, x)]
    while stack:
        cy, cx = stack.pop()
        if cy < 0 or cx < 0 or cy >= m.shape[0] or cx >= m.shape[1] or seen[cy, cx] or not m[cy, cx]:
            continue
        seen[cy, cx] = True
        stack += [(cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)]
    return seen


def eqd(n):
    return 2.0 * math.sqrt(n / math.pi)


def measure(fr, centres, rad, exposure, own):
    """One window about the group's mean centre; per-nozzle opening and row extents through each centre."""
    x = sum(c[0] for c in centres) / len(centres)
    y = sum(c[1] for c in centres) / len(centres)
    x0, y0 = max(0, int(round(x)) - rad), max(0, int(round(y)) - rad)
    x1, y1 = min(W, int(round(x)) + rad + 1), min(H, int(round(y)) + rad + 1)
    if x1 <= x0 or y1 <= y0:
        return None
    hdr = np.memmap(os.path.join(SESSION, "hdr_1_%d.rgba16f" % fr), dtype="<f2", mode="r", shape=(H, W, 4))
    e = np.array(hdr[y0:y1, x0:x1, :3]).astype(np.float64)
    del hdr
    dep = np.memmap(os.path.join(SESSION, "depth_1_%d.rgba32f" % fr), dtype="<f4", mode="r", shape=(H, W, 4))
    z = np.array(dep[y0:y1, x0:x1, 2]).astype(np.float64)
    del dep
    mn = e.min(axis=2)
    disp = tonemap(e, exposure)
    white = disp.min(axis=2) >= 0.98
    red_sat = disp[..., 0] >= 0.98
    clip1 = mn >= 1.0
    red1 = e[..., 0] >= 1.0
    sky = z <= 0
    out = dict(peak=e.reshape(-1, 3).max(0), min1=int(clip1.sum()), white=int(white.sum()), red_sat=int(red_sat.sum()),
               red1=int(red1.sum()), red1_sky=int((red1 & sky).sum()), nozzles=[])
    for cx, cy in centres:
        cx, cy = int(round(cx)) - x0, int(round(cy)) - y0
        if not (0 <= cx < e.shape[1] and 0 <= cy < e.shape[0]):
            continue
        n = dict(min1_row=extent(clip1[cy]), white_row=extent(white[cy]), red1_row=extent(red1[cy]),
                 redsat_row=extent(red_sat[cy]), centre=e[cy, cx])
        if own:
            d0 = z[cy, cx]
            hole = flood((np.abs(z - d0) < 0.15) & (z > 0), cy, cx) if d0 > 0 else np.zeros_like(z, bool)
            n.update(hole_w=extent(hole.any(axis=0)), hole_h=extent(hole.any(axis=1)))
        out["nozzles"].append(n)
    return out


def main():
    cams, evs, draws = read_log()
    grey = np.linspace(0.5, 8.0, 1501)
    print("# AgX display white (>= 0.98) for a grey engine value from: " + ", ".join(
        "ev %.2f -> %.2f" % (ev, grey[np.argmax(tonemap(np.stack([grey] * 3, -1), 2.0 ** ev)[:, 0] >= 0.98)])
        for ev in (evs[4479], 0.0, evs[5436])))
    print("# window: own = radius 72 px about the mean of the two own nozzles; far = radius 20 px about a group of nozzles within 24 px")
    print("# columns: frame kind names z centre ev | peak R G B (engine) | min(rgb)>=1 px (eqd) | AgX white px (eqd) | AgX R saturated px (eqd)"
          " | R>=1 px (eqd; of which over sky) || per nozzle: row extents through its centre min>=1 / white / R sat / R>=1, centre RGB, depth opening w x h")
    for fr in FRAMES:
        exposure = 2.0 ** evs.get(fr - 1, evs[fr])
        groups = []
        for name, size, p in draws.get(fr, []):
            own = name.endswith(("_nor", "_tiny"))
            x, y, zz = project(cams[fr], p)
            if zz <= 0 or not (-16 <= x < W + 16 and -16 <= y < H + 16):
                continue
            for g in groups:
                if g["own"] == own and (own or math.hypot(g["c"][0][0] - x, g["c"][0][1] - y) < 24):
                    g["c"].append((x, y)); g["n"].append(name.replace("fx_engine_xtc_", "")); break
            else:
                groups.append(dict(own=own, c=[(x, y)], n=[name.replace("fx_engine_xtc_", "")], z=zz))
        for g in groups:
            m = measure(fr, g["c"], 72 if g["own"] else 20, exposure, g["own"])
            if m is None or (not g["own"] and m["peak"].max() < 1.0):
                continue
            mx = sum(c[0] for c in g["c"]) / len(g["c"]); my = sum(c[1] for c in g["c"]) / len(g["c"])
            row = "%d %s %s z=%.0f (%.0f,%.0f) ev=%.2f | %.2f %.2f %.2f | %d (%.1f) | %d (%.1f) | %d (%.1f) | %d (%.1f; sky %d) ||" % (
                fr, "own" if g["own"] else "far", "+".join(g["n"]), g["z"], mx, my, math.log2(exposure), *m["peak"],
                m["min1"], eqd(m["min1"]), m["white"], eqd(m["white"]), m["red_sat"], eqd(m["red_sat"]),
                m["red1"], eqd(m["red1"]), m["red1_sky"])
            for n in m["nozzles"]:
                row += " %d/%d/%d/%d c=%.1f,%.1f,%.1f" % (n["min1_row"], n["white_row"], n["redsat_row"], n["red1_row"], *n["centre"])
                if g["own"]:
                    row += " open %dx%d;" % (n["hole_w"], n["hole_h"])
            print(row)


if __name__ == "__main__":
    main()
