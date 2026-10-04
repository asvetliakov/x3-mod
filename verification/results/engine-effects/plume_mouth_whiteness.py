#!/usr/bin/env python3
"""Mouth whiteness of the engine plume on the fixture's resolved FP16 images (after flight F, Run 123 A / run409).

Reads the linear RGB (before the AgX tonemap) that `run_engine_plumes.py --dump-images DIR --dump-linear [--preset P]`
leaves as verification/probe/build/engine-plumes/dump/<name>.pfm (copy each run's .pfm files into a state directory:
the next dump deletes them). For each band: the body pixels within the first 0.3 L of the nozzle (side and oblique
views: columns from the nozzle 0.15 n behind it to 0.3 L along the exhaust, which runs leftwards; rows within
n + 0.15 L of the nozzle) and, for the end-on image 02b, the disc's centre (radius 0.3 n). "Body" is the mask of the
FIRST state given (luma709 >= 0.25 linear and a 5x5 minimum filter of that test, which drops the starfield's single
pixels), applied unchanged to every state, so all states are compared on the same pixels. Per state and band:
(a) the mean of min(rgb) / max(rgb) (1 = white, 0 = saturated), (b) the fractions of the mask with min(rgb) >= 1.0
(all three channels past the tonemapper's white point: clipped white on the display) and >= 0.5, and the mean luma
of the mask (the radiance kept).
Radiance kept (the second table, the summed linear luma less the region's median background, each state over the
first): the side bands' body past the mouth (0.4 L .. 1.15 L from the nozzle, rows within n + 0.3 L) and the whole
band, and the far dots of 05 (the 12 / 6 / 2 px plumes, boxes 5 L x 3 n about them).

The drawn nozzle n: 88.5 px for the 150 px bands (the near-camera cap), 40 px uncapped, 65.6 px for the end-on disc;
L = 2 z n with z = 2 at s = 1 (1.25 at s = 0.5), x 2 at warp 6, x the axis's projected length (README axes).

Usage: python3 plume_mouth_whiteness.py LABEL=DIR [LABEL=DIR ...]   (the first DIR defines the mask)
"""
import os
import sys

import numpy as np

# file prefix, label, nozzle (x, y), drawn n px, throttle, length factor (projection x travel), kind
BANDS = [
    ("01a", "01a red s=1", (1350, 900), 88.5, 1.0, 0.94, "side"),
    ("01b", "01b blue s=1", (1350, 900), 88.5, 1.0, 0.94, "side"),
    ("03a", "03a capital t0", (1350, 270), 88.5, 1.0, 0.93, "side"),
    ("03a", "03a capital t+100ms", (1350, 810), 88.5, 1.0, 0.93, "side"),
    ("03c", "03c fighter t0", (1350, 270), 88.5, 1.0, 0.93, "side"),
    ("03c", "03c fighter t+100ms", (1350, 810), 88.5, 1.0, 0.93, "side"),
    ("04", "04 default band", (1350, 540), 88.5, 1.0, 0.92, "side"),
    ("05", "05 n40 uncapped", (560, 540), 40.0, 1.0, 0.92, "side"),
    ("08", "08 normal", (1700, 270), 88.5, 1.0, 0.81, "side"),
    ("08", "08 warp6", (1700, 810), 88.5, 1.0, 1.62, "side"),
    ("09", "09 ribbon n40", (1300, 540), 40.0, 1.0, 0.94, "side"),
    ("01a", "01a red s=0.5", (1350, 540), 88.5, 0.5, 0.92, "side"),
    ("01b", "01b blue s=0.5", (1350, 540), 88.5, 0.5, 0.92, "side"),
    ("02a", "02a 45deg red", (1250, 300), 88.5, 1.0, 0.89, "side"),
    ("02a", "02a 45deg blue", (1250, 780), 88.5, 1.0, 0.89, "side"),
    ("02b", "02b end-on red centre", (600, 540), 65.6, 1.0, 0.0, "disc"),
    ("02b", "02b end-on blue centre", (1320, 540), 65.6, 1.0, 0.0, "disc"),
]
LUMA_MIN = 0.25


def load_pfm(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"PF"
        w, h = map(int, f.readline().split())
        scale = float(f.readline())
        data = np.frombuffer(f.read(), dtype="<f4" if scale < 0 else ">f4").reshape(h, w, 3)
    return np.flipud(data).astype(np.float64)


def find(d, prefix):
    names = [n for n in os.listdir(d) if n.startswith(prefix + "_") and n.endswith(".pfm")]
    assert len(names) == 1, (d, prefix, names)
    return os.path.join(d, names[0])


def luma(rgb):
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def min_filter5(m):
    p = np.pad(m, 2, constant_values=False)
    out = np.ones_like(m)
    for dy in range(5):
        for dx in range(5):
            out &= p[dy:dy + m.shape[0], dx:dx + m.shape[1]]
    return out


def region(rgb, band):
    _, _, (x, y), n, s, k, kind = band
    if kind == "disc":
        r = 0.3 * n
        yy, xx = np.mgrid[0:rgb.shape[0], 0:rgb.shape[1]]
        return (xx - x) ** 2 + (yy - y) ** 2 <= r * r
    z = 0.25 + 1.75 * s
    reach = 0.3 * 2.0 * z * n * k
    sel = np.zeros(rgb.shape[:2], bool)
    half = int(n + 0.15 * reach / max(k, 1e-3) * k)
    sel[max(y - half, 0):y + half + 1, int(x - reach):int(x + 0.15 * n) + 1] = True
    return sel


def main():
    states = [a.split("=", 1) for a in sys.argv[1:]]
    if not states:
        raise SystemExit(__doc__)
    cache = {}
    def img(d, prefix):
        key = (d, prefix)
        if key not in cache:
            cache[key] = load_pfm(find(d, prefix))
        return cache[key]
    print(f"mask: luma709 >= {LUMA_MIN} linear, 5x5 min filter, from state '{states[0][0]}'; region: side 0.3 L from the "
          "nozzle (+0.15 n behind), end-on radius 0.3 n")
    print(f"{'band':26s} {'px':>6s} " + " ".join(f"{lab:>34s}" for lab, _ in states))
    print(f"{'':26s} {'':>6s} " + " ".join(f"{'min/max  >=1.0  >=0.5  luma':>34s}" for _ in states))
    for band in BANDS:
        ref = img(states[0][1], band[0])
        sel = region(ref, band)
        mask = sel & min_filter5(luma(ref) >= LUMA_MIN) if band[6] == "side" else sel
        cells = []
        for _, d in states:
            px = img(d, band[0])[mask]
            mx, mn = px.max(axis=1), px.min(axis=1)
            ratio = (mn / np.maximum(mx, 1e-9)).mean()
            cells.append(f"{ratio:7.3f} {np.mean(mn >= 1.0):6.3f} {np.mean(mn >= 0.5):6.3f} {luma(px).mean():6.3f}")
        print(f"{band[1]:26s} {int(mask.sum()):6d} " + " ".join(f"{c:>34s}" for c in cells))
    kept(states, img)


def kept_sum(rgb, box):
    x0, x1, y0, y1 = (int(v) for v in box)
    Y = luma(rgb[max(y0, 0):y1, max(x0, 0):x1])
    return float((Y - np.median(Y)).sum())


def kept_boxes(band):
    _, _, (x, y), n, s, k, kind = band
    L = 2.0 * (0.25 + 1.75 * s) * n * k
    half = n + 0.3 * L
    return {"past mouth": (x - 1.15 * L, x - 0.4 * L, y - half, y + half),
            "whole": (x - 1.3 * L, x + 0.5 * n, y - half, y + half)}


FAR = [("05 far 12 px", (960, 540), 12.0), ("05 far 6 px", (1300, 540), 6.0), ("05 far 2 px", (1600, 540), 2.0)]


def kept(states, img):
    print()
    print("radiance kept: summed luma less the background, over the first state")
    print(f"{'band':26s} " + " ".join(f"{lab + ' past/whole':>30s}" for lab, _ in states[1:]))
    for band in BANDS:
        if band[6] != "side":
            continue
        boxes = kept_boxes(band)
        ref = {kname: kept_sum(img(states[0][1], band[0]), b) for kname, b in boxes.items()}
        cells = []
        for _, d in states[1:]:
            a = img(d, band[0])
            cells.append(f"{kept_sum(a, boxes['past mouth']) / ref['past mouth']:6.3f} / {kept_sum(a, boxes['whole']) / ref['whole']:6.3f}")
        print(f"{band[1]:26s} " + " ".join(f"{c:>30s}" for c in cells))
    for label, (x, y), n in FAR:
        L = 4.0 * n
        box = (x - 1.25 * L, x + 0.5 * n + 2, y - 1.5 * n - 3, y + 1.5 * n + 4)
        ref = kept_sum(img(states[0][1], "05"), box)
        cells = [f"{kept_sum(img(d, '05'), box) / ref:6.3f}" for _, d in states[1:]]
        print(f"{label:26s} " + " ".join(f"{c:>30s}" for c in cells))


if __name__ == "__main__":
    main()
