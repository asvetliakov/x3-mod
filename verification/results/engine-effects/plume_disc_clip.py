#!/usr/bin/env python3
"""End-on disc clip on the fixture's resolved FP16 images (after flight G, Run 124 / run412: the own ship's disc in chase
view read as a clipped pink-white disc wider than the nozzle).

Reads the linear RGB (before the AgX tonemap) that `run_engine_plumes.py --dump-images DIR --dump-linear` leaves as
verification/probe/build/engine-plumes/dump/<name>.pfm (copy each run's .pfm files into a state directory first).
The end-on image 02b: two 150 px nozzles at s = 1 with the exhaust at the camera, red left (600, 540), blue right
(1320, 540); both capped and faded by the near-camera cap (drawn n 65.6 px: the own ship's chase regime, the disc
under chase_disc_floor). Per disc and state:
  centre (radius 0.3 n): the fraction with min(rgb) >= 1.0 (all three channels past 1: white on the display) and
  the peak per channel;
  disc (radius 4 n, the halo's reach): pixels with min(rgb) >= 1.0 (count and fraction of the first state's mask,
  luma709 >= 0.25 with a 5x5 minimum filter), pixels with R >= 1.0 (resp. B for blue: the tint channel past 1, the
  coloured clip), and the summed luma less the box's median background, as a ratio to the first state.
Every other image: the summed luma of the whole frame over the first state's and the largest per-pixel change of
any channel (side views, distance dots, hull and crowd frames: 0 when the change does not reach them); the
oblique 02a (45 degrees, disc weight 1) is reported, not held. The side bands' and far dots' radiance kept is
plume_mouth_whiteness.py's second table (run it on the same states).

Usage: python3 plume_disc_clip.py LABEL=DIR [LABEL=DIR ...]   (the first DIR is the reference)
plume_disc_clip_out.txt: before = 3ee84cf3 (chase_disc_floor 0.6 as a floor over the body's fade), after = its working
tree with the disc's own chase fade 1 -> chase_disc_floor 0.4 (engine_plumes_core.h); X3, 2026-10-04.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from plume_mouth_whiteness import find, load_pfm, luma, min_filter5  # noqa: E402

N = 65.6
DISCS = [("02b red", (600, 540), 0), ("02b blue", (1320, 540), 2)]
PREFIXES = ["01a", "01b", "02a", "03a", "03b", "03c", "04", "05", "06a", "06b", "07", "08", "09", "10"]


def disc_stats(rgb, centre, chan, mask_ref):
    x, y = centre
    yy, xx = np.mgrid[0:rgb.shape[0], 0:rgb.shape[1]]
    r2 = (xx - x) ** 2 + (yy - y) ** 2
    c = r2 <= (0.3 * N) ** 2
    disc = r2 <= (4.0 * N) ** 2
    mn = rgb.min(axis=2)
    box = rgb[int(y - 4 * N):int(y + 4 * N) + 1, int(x - 4 * N):int(x + 4 * N) + 1]
    bg = float(np.median(luma(box)))
    m = disc & mask_ref
    return dict(centre_clip=float(np.mean(mn[c] >= 1.0)), peak=rgb[disc].max(axis=0), clip_px=int(np.sum((mn >= 1.0) & disc)),
                clip_frac=float(np.mean(mn[m] >= 1.0)), tint_px=int(np.sum((rgb[..., chan] >= 1.0) & disc)),
                energy=float((luma(rgb[disc]) - bg).sum()), mask_px=int(m.sum()))


def main():
    states = [a.split("=", 1) for a in sys.argv[1:]]
    if not states:
        raise SystemExit(__doc__)
    ref = load_pfm(find(states[0][1], "02b"))
    mask_ref = min_filter5(luma(ref) >= 0.25)
    print(f"02b end-on, n {N} px drawn (capped and faded); centre radius 0.3 n, disc radius 4 n; mask from '{states[0][0]}'")
    print(f"{'disc':10s} {'state':>16s} {'centre>=1':>9s} {'peak R':>7s} {'peak G':>7s} {'peak B':>7s} {'disc>=1 px':>10s} "
          f"{'frac':>6s} {'tint>=1 px':>10s} {'energy':>9s} {'ratio':>6s}")
    for name, centre, chan in DISCS:
        base = None
        for label, d in states:
            s = disc_stats(load_pfm(find(d, "02b")), centre, chan, mask_ref)
            base = base if base is not None else s["energy"]
            print(f"{name:10s} {label:>16s} {s['centre_clip']:9.3f} {s['peak'][0]:7.3f} {s['peak'][1]:7.3f} {s['peak'][2]:7.3f} "
                  f"{s['clip_px']:10d} {s['clip_frac']:6.3f} {s['tint_px']:10d} {s['energy']:9.1f} {s['energy'] / base:6.3f}")
    print()
    print("other frames: summed luma over the first state's, largest per-pixel change of any channel")
    print(f"{'image':6s} " + " ".join(f"{lab:>24s}" for lab, _ in states[1:]))
    for p in PREFIXES:
        a = load_pfm(find(states[0][1], p))
        ta = float(luma(a).sum())
        cells = []
        for _, d in states[1:]:
            b = load_pfm(find(d, p))
            cells.append(f"{float(luma(b).sum()) / ta:8.4f} / {float(np.abs(b - a).max()):9.4f}")
        print(f"{p:6s} " + " ".join(f"{c:>24s}" for c in cells))


if __name__ == "__main__":
    main()
