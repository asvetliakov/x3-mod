#!/usr/bin/env python3
"""Structure metrics of the engine plume look images (docs/architecture/engine-exhaust-look-critique.md).

Reads the fixture's --dump-images PNGs (verification/results/engine-effects/look-images/, 1920x1080, AgX EV 0 display
RGB) and prints, per side-view band: the measured plume length, the axial luma profile's shock-cell modulation depth
in u in [0.05, 0.4], the radial core/edge contrast at u = 0.2 and 0.5, the head/body whiteness (1 - min/max channel) and
the streak anisotropy (axial / radial autocorrelation half-length of the high-passed body). Luma is Rec. 709 of the
display values decoded with gamma 2.2 ("lin" below): AgX compresses the top, so linear contrasts near the peak are
underestimated, never overestimated. Also: the end-on disc's radial profile (02b) and the uncapped peaks (05, 09, 10).
Since the revised look law (docs/architecture/engine-exhaust-look-critique.md section 5, "Implemented"): the body lane's
cell depth and dark gaps (the row 0.35 w(u) off the axis, w the law's width at the band's drawn nozzle width), the
display gates per side band (radial >= 2.0 at u 0.2, lane cells >= 0.2), the end-on disc's ring (its maximum in
0.4..0.7 n over the minimum between 0.2 n and it, >= 1.05) and hot centre (the centre over the maximum, >= 0.9) on the
azimuthal mean, and the far dots of 05 (the summed luma of the 40 / 12 / 6 / 2 px plumes; with --before DIR their ratio
to the same sums in DIR, the gate 0.7..1.3 at 2 and 6 px).

Usage: python3 plume_look_metrics.py [look-images dir] [--crops DIR] [--before DIR]  (crops: 3x upscaled band crops for
viewing; before: an earlier dump, e.g. the images of e51872be checked out into a scratch directory).
"""
import os
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.abspath(__file__))
DIR = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else os.path.join(ROOT, "look-images")
CROPS = None
if "--crops" in sys.argv:
    CROPS = sys.argv[sys.argv.index("--crops") + 1]
    os.makedirs(CROPS, exist_ok=True)
BEFORE = sys.argv[sys.argv.index("--before") + 1] if "--before" in sys.argv else None
# The drawn nozzle width (px) of the bands: the 150 px fighter and capital are held by the near-camera cap to 88.5 px at
# 1080 (the fixture's STRUCTURE rows), the 40 px ones are uncapped; the end-on 150 px disc 65.6 px.
NOZZLE_PX = {"05": 40.0, "09": 40.0}
CAPPED_NOZZLE_PX, DISC_NOZZLE_PX = 88.5, 65.6

# Side-view bands: file, label, nozzle (x, y), exhaust direction (-1: leftwards), throttle.
SIDE = [
    ("01a_side_fighter_v500_n150_s0-0.5-1_split-red_default_agx-ev0.png", "01a red s=0", (1350, 180), 0.0),
    ("01a_side_fighter_v500_n150_s0-0.5-1_split-red_default_agx-ev0.png", "01a red s=0.5", (1350, 540), 0.5),
    ("01a_side_fighter_v500_n150_s0-0.5-1_split-red_default_agx-ev0.png", "01a red s=1", (1350, 900), 1.0),
    ("01b_side_fighter_v500_n150_s0-0.5-1_argon-blue_default_agx-ev0.png", "01b blue s=0", (1350, 180), 0.0),
    ("01b_side_fighter_v500_n150_s0-0.5-1_argon-blue_default_agx-ev0.png", "01b blue s=0.5", (1350, 540), 0.5),
    ("01b_side_fighter_v500_n150_s0-0.5-1_argon-blue_default_agx-ev0.png", "01b blue s=1", (1350, 900), 1.0),
    ("03a_side_capital_v10000_n150_s1_argon-blue_t0-top_t100ms-bottom_agx-ev0.png", "03a capital t0", (1350, 270), 1.0),
    ("03a_side_capital_v10000_n150_s1_argon-blue_t0-top_t100ms-bottom_agx-ev0.png", "03a capital t+100ms", (1350, 810), 1.0),
    ("03c_side_fighter_v500_n150_s1_argon-blue_t0-top_t100ms-bottom_agx-ev0.png", "03c fighter t0", (1350, 270), 1.0),
    ("03c_side_fighter_v500_n150_s1_argon-blue_t0-top_t100ms-bottom_agx-ev0.png", "03c fighter t+100ms", (1350, 810), 1.0),
    ("04_presets_side_fighter_v500_n150_s1_argon-blue_restrained-default-strong_agx-ev0.png", "04 restrained", (1350, 180), 1.0),
    ("04_presets_side_fighter_v500_n150_s1_argon-blue_restrained-default-strong_agx-ev0.png", "04 default", (1350, 540), 1.0),
    ("04_presets_side_fighter_v500_n150_s1_argon-blue_restrained-default-strong_agx-ev0.png", "04 strong", (1350, 900), 1.0),
    ("05_distance_fighter_v500_s1_argon-blue_n40-12-6-2px_agx-ev0.png", "05 n40 uncapped", (560, 540), 1.0),
    ("08_seta_warp6_side_fighter_v500_n150_s1_argon-blue_normal-top_travel-bottom_agx-ev0.png", "08 normal", (1700, 270), 1.0),
    ("08_seta_warp6_side_fighter_v500_n150_s1_argon-blue_normal-top_travel-bottom_agx-ev0.png", "08 warp6", (1700, 810), 1.0),
    ("09_ribbon_moving8px_fighter_v500_n40_s1_argon-blue_agx-ev0.png", "09 ribbon n40 uncapped", (1300, 540), 1.0),
]


def load(name):
    return np.asarray(Image.open(os.path.join(DIR, name)).convert("RGB")).astype(np.float64) / 255.0


def luma_lin(rgb):
    lin = rgb ** 2.2
    return 0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]


def band(img, noz, half_h=170, reach=700):
    x0, y0 = noz
    ys = slice(max(y0 - half_h, 0), min(y0 + half_h, img.shape[0]))
    xs = slice(max(x0 - reach, 0), min(x0 + 40, img.shape[1]))
    return img[ys, xs], (xs.start, ys.start)


def axis_profile(Y, noz_local, thresh=0.02):
    """Per column left of the nozzle: luma-weighted centroid row and axis luma (the max over a 5-row window)."""
    xn, yn = noz_local
    cols = np.arange(xn - 1, 0, -1)
    cen, val = [], []
    for c in cols:
        col = Y[:, c]
        m = col.max()
        if m < thresh:
            break
        w = np.clip(col - 0.25 * m, 0, None)
        r = int(round((w * np.arange(len(col))).sum() / max(w.sum(), 1e-9)))
        lo, hi = max(r - 2, 0), min(r + 3, len(col))
        cen.append(r)
        val.append(col[lo:hi].max())
    return cols[: len(cen)], np.array(cen), np.array(val)


def modulation(vals, length, lo=0.05, hi=0.4, period_frac=0.16):
    """Shock-cell depth: the axial profile divided by its moving mean over 1.5 periods; max/min of the ratio in [lo, hi]."""
    n = len(vals)
    if n < 20:
        return float("nan"), float("nan")
    win = max(int(1.5 * period_frac * length), 5)
    k = np.ones(win) / win
    pad = np.pad(vals, (win // 2, win - win // 2 - 1), mode="edge")
    mean = np.convolve(pad, k, mode="valid")[:n]
    ratio = vals / np.maximum(mean, 1e-9)
    a, b = int(lo * length), int(hi * length)
    seg = ratio[a:b]
    if len(seg) < 5:
        return float("nan"), float("nan")
    s = np.convolve(np.pad(seg, 2, mode="edge"), np.ones(5) / 5, mode="valid")
    depth = (s.max() - s.min()) / (s.max() + s.min())
    return depth, len(seg)


def radial(Y, cols, cen, vals, length, u, frac=0.6):
    """Core / edge contrast at u: axis luma over the luma at frac x the half-width (half-width: 10 % of the axis luma)."""
    i = int(u * length)
    if i >= len(cols):
        return float("nan"), float("nan"), float("nan")
    c, r = cols[i], cen[i]
    col = Y[:, c]
    peak = col[max(r - 2, 0): r + 3].max()
    hw = 0
    for d in range(1, 200):
        if r + d >= len(col) or r - d < 0:
            break
        if max(col[r + d], col[r - d]) < 0.1 * peak:
            hw = d
            break
    if hw == 0:
        return peak, float("nan"), float("nan")
    e = int(round(frac * hw))
    edge = 0.5 * (col[min(r + e, len(col) - 1)] + col[max(r - e, 0)])
    return peak, hw, peak / max(edge, 1e-9)


def whiteness(rgb_band, cols, cen, length, u):
    i = int(u * length)
    if i >= len(cols):
        return float("nan")
    px = rgb_band[cen[i], cols[i]]
    return 1.0 - px.min() / max(px.max(), 1e-9)


def anisotropy(Y, cols, cen, length, sigma=10):
    """High-passed body luma: autocorrelation half-length along x against y (window: u 0.1..0.8, |dy| <= 0.6 half-width)."""
    from PIL import ImageFilter
    a, b = int(0.1 * length), int(0.8 * length)
    if b - a < 40:
        return float("nan"), float("nan"), float("nan")
    xs = cols[a:b]
    rows = cen[a:b]
    xmin, xmax = xs.min(), xs.max() + 1
    ymid = int(np.median(rows))
    hw = 0
    for d in range(1, 170):
        if ymid + d >= Y.shape[0] or ymid - d < 0:
            break
        if max(Y[ymid + d, xs].max(), Y[ymid - d, xs].max()) < 0.1 * Y[ymid, xs].max():
            hw = d
            break
    hw = max(int(0.6 * hw), 6)
    sub = Y[ymid - hw: ymid + hw + 1, xmin:xmax]
    img = Image.fromarray((np.clip(sub, 0, 1) * 255).astype(np.uint8))
    low = np.asarray(img.filter(ImageFilter.GaussianBlur(sigma))).astype(np.float64) / 255.0
    hp = sub - low
    hp -= hp.mean()
    var = (hp * hp).mean()
    if var < 1e-10:
        return float("nan"), float("nan"), float("nan")

    def half_len(axis):
        for lag in range(1, 60):
            if axis == 1:
                if lag >= hp.shape[1] // 2:
                    return lag
                c = (hp[:, lag:] * hp[:, :-lag]).mean() / var
            else:
                if lag >= hp.shape[0] // 2:
                    return lag
                c = (hp[lag:, :] * hp[:-lag, :]).mean() / var
            if c < 0.5:
                return lag
        return 60

    lx, ly = half_len(1), half_len(0)
    return lx, ly, lx / ly


def width_law(u, bulge=1.15, taper=0.45):
    """The law's width w(u) in nozzle widths (engine_plumes_core.h law::width)."""
    def ss(a, b, x):
        t = min(max((x - a) / (b - a), 0.0), 1.0)
        return t * t * (3 - 2 * t)
    b = 0.5 * bulge * (1 - 0.55 * np.exp(-9 * u)) * (1 + 0.35 * ss(0, 0.25, u) * np.exp(-4 * u)) + 0.5 * bulge * taper
    line = 0.5 * bulge + (0.04 - 0.5 * bulge) * taper * u
    return min(line, b) * max(1 - 0.6 * taper * ss(0.6, 1, u), 0.05)


def lane(Y, cols, cen, length, n_px, period_frac=0.16):
    """The body lane 0.35 w(u) off the axis (both sides' mean), u 0..0.6: the cell depth (max - min) / (max + min) of
    its ratio to the running mean of 1.5 periods in u 0.05..0.4, and the dark gaps, its minimum over its mean in u
    0.1..0.3 (the fixture's structure case on the FP16 readback; here display-decoded)."""
    n = min(int(0.6 * length), len(cols))
    if n < 20:
        return float("nan"), float("nan")
    vals, us = [], []
    for i in range(n):
        u = i / length
        off = int(round(0.35 * width_law(min(u, 1.0)) * n_px))
        r, c = cen[i], cols[i]
        vals.append(0.5 * (Y[min(r + off, Y.shape[0] - 1), c] + Y[max(r - off, 0), c]))
        us.append(u)
    vals, us = np.array(vals), np.array(us)
    win = max(int(1.5 * period_frac * length), 5)
    pad = np.pad(vals, (win // 2, win - win // 2 - 1), mode="edge")
    mean = np.convolve(pad, np.ones(win) / win, mode="valid")[:n]
    ratio = vals / np.maximum(mean, 1e-9)
    sel = (us >= 0.05) & (us <= 0.4)
    gap = (us >= 0.1) & (us <= 0.3)
    depth = (ratio[sel].max() - ratio[sel].min()) / (ratio[sel].max() + ratio[sel].min())
    return depth, vals[gap].min() / max(vals[gap].mean(), 1e-9)


def far_dots(directory):
    """The 05 distance series: the summed display-decoded luma of the 40 / 12 / 6 / 2 px plumes (boxes about each
    nozzle, the plume to the left; the background is the dark static starfield, the same in every dump)."""
    img = load_from(directory, "05_distance_fighter_v500_s1_argon-blue_n40-12-6-2px_agx-ev0.png")
    Y = luma_lin(img)
    out = {}
    for px, (cx, back, half) in ((40, (560, 230, 60)), (12, (960, 80, 30)), (6, (1300, 45, 18)), (2, (1600, 20, 10))):
        out[px] = float(Y[540 - half: 540 + half + 1, cx - back: cx + 21].sum())
    return out


def load_from(directory, name):
    return np.asarray(Image.open(os.path.join(directory, name)).convert("RGB")).astype(np.float64) / 255.0


def crop(img, noz, name, half_h=120, reach=520, scale=3):
    if not CROPS:
        return
    x0, y0 = noz
    box = (max(x0 - reach, 0), max(y0 - half_h, 0), min(x0 + 60, img.shape[1]), min(y0 + half_h, img.shape[0]))
    im = Image.fromarray((img * 255).astype(np.uint8)).crop(box)
    im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    im.save(os.path.join(CROPS, name + ".png"))


print("# side views (luma: Rec.709 of gamma-2.2 decoded display; 'disp' = 8-bit display luma of the axis peak)")
print("band | L_px | peak lin | peak disp | cell depth u.05-.4 | core/edge u.2 (hw px) | core/edge u.5 | white head u.1 / body u.5 | aniso lx/ly | lane cells / gaps")
GATES = []
for fn, label, noz, s in SIDE:
    img = load(fn)
    sub, (ox, oy) = band(img, noz)
    Y = luma_lin(sub)
    nl = (noz[0] - ox, noz[1] - oy)
    cols, cen, vals = axis_profile(Y, nl)
    length = len(cols)
    if length < 5:
        print(f"{label} | no plume found")
        continue
    peak = vals.max()
    pk_i = int(np.argmax(vals))
    disp = sub[cen[pk_i], cols[pk_i]].max() * 255
    depth, _ = modulation(vals, length)
    p2, hw2, cr2 = radial(Y, cols, cen, vals, length, 0.2)
    p5, hw5, cr5 = radial(Y, cols, cen, vals, length, 0.5)
    wh1 = whiteness(sub, cols, cen, length, 0.1)
    wh5 = whiteness(sub, cols, cen, length, 0.5)
    lx, ly, an = anisotropy(Y, cols, cen, length)
    ld, lg = lane(Y, cols, cen, length, NOZZLE_PX.get(fn[:2], CAPPED_NOZZLE_PX))
    print(f"{label} | {length} | {peak:.3f} | {disp:.0f} | {depth:.2f} | {cr2:.1f} ({hw2}) | {cr5:.1f} | {wh1:.2f} / {wh5:.2f} | {lx}/{ly} = {an:.1f} | {ld:.2f} / {lg:.2f}")
    if s == 1.0:
        GATES.append((label, cr2, ld, an))
    crop(img, noz, label.replace(" ", "_").replace("=", ""))

# Axial profile of one band, printed coarsely, to see the cells.
img = load(SIDE[5][0])
sub, (ox, oy) = band(img, SIDE[5][2])
Y = luma_lin(sub)
cols, cen, vals = axis_profile(Y, (SIDE[5][2][0] - ox, SIDE[5][2][1] - oy))
print("\n# 01b s=1 axial luma (lin) every 10 px from the nozzle:")
print(" ".join(f"{v:.2f}" for v in vals[::10]))
print("# 01b s=1 radial luma (lin) at u=0.2, every 4 px from the axis outwards (one side):")
i = int(0.2 * len(cols))
col = Y[:, cols[i]]
r = cen[i]
print(" ".join(f"{col[r + d]:.2f}" for d in range(0, 80, 4)))
print("# 01b s=1 axis RGB (display 0..255) at u = 0.05, 0.1, 0.2, 0.3, 0.5, 0.7, 0.9:")
for u in (0.05, 0.1, 0.2, 0.3, 0.5, 0.7, 0.9):
    i = min(int(u * len(cols)), len(cols) - 1)
    print(f"  u={u}: {np.round(sub[cen[i], cols[i]] * 255).astype(int)}")

# End-on disc (02b): the radial profile about the nozzle.
img = load("02b_end-on_fighter_v500_n150_s1_split-red-left_argon-blue-right_agx-ev0.png")
Y = luma_lin(img)
print("\n# 02b end-on discs: radial luma (lin) at 0, 4, 8, .. 72 px from the centre (mean over 4 directions)")
for label, (cx, cy) in (("red", (600, 540)), ("blue", (1320, 540))):
    prof = []
    for d in range(0, 76, 4):
        prof.append(np.mean([Y[cy, cx + d], Y[cy, cx - d], Y[cy + d, cx], Y[cy - d, cx]]))
    prof = np.array(prof)
    ring = prof[1:].max() / max(prof[0], 1e-9)
    print(f"  {label}: " + " ".join(f"{v:.2f}" for v in prof) + f"  | max off-centre / centre = {ring:.2f}; centre RGB {np.round(img[cy, cx] * 255).astype(int)}")
    if CROPS:
        im = Image.fromarray((img * 255).astype(np.uint8)).crop((cx - 130, cy - 130, cx + 130, cy + 130))
        im.resize((780, 780), Image.NEAREST).save(os.path.join(CROPS, f"02b_{label}.png"))

# The disc gate on the azimuthal mean (64 directions, bilinear, every px to 0.9 n).
print(f"# 02b disc gate (azimuthal mean, display-decoded luma, n {DISC_NOZZLE_PX} px): ring = max in 0.4..0.7 n / min in 0.2 n..it "
      "(gate >= 1.05), hot centre = centre / max (gate >= 0.9)")
for label, (cx, cy) in (("red", (600, 540)), ("blue", (1320, 540))):
    prof = []
    for rr in range(0, int(0.9 * DISC_NOZZLE_PX) + 1):
        acc = 0.0
        for a in range(64):
            x, y = cx + rr * np.cos(2 * np.pi * a / 64), cy + rr * np.sin(2 * np.pi * a / 64)
            x0, y0 = int(np.floor(x)), int(np.floor(y))
            fx, fy = x - x0, y - y0
            acc += ((1 - fy) * ((1 - fx) * Y[y0, x0] + fx * Y[y0, x0 + 1]) + fy * ((1 - fx) * Y[y0 + 1, x0] + fx * Y[y0 + 1, x0 + 1]))
        prof.append(acc / 64)
    prof = np.array(prof)
    rn = np.arange(len(prof)) / DISC_NOZZLE_PX
    band = np.where((rn >= 0.4) & (rn <= 0.7))[0]
    j = band[np.argmax(prof[band])]
    inner = np.where((rn >= 0.2) & (np.arange(len(prof)) <= j))[0]
    ring = prof[j] / max(prof[inner].min(), 1e-9)
    print(f"  {label}: ring {ring:.2f} at {rn[j]:.2f} n, hot centre {prof[0] / prof.max():.2f} -> {'PASS' if ring >= 1.05 and prof[0] / prof.max() >= 0.9 else 'FAIL'}")

# The display gates of the side bands at s = 1 (section 5: radial contrast >= 2.0 at u 0.2, body-lane cells >= 0.2).
print("\n# display gates at s = 1 (radial >= 2.0 at u 0.2, lane cells >= 0.2; anisotropy >= 3 reported)")
for label, cr2, ld, an in GATES:
    print(f"  {label}: radial {cr2:.2f} {'PASS' if cr2 >= 2.0 else 'FAIL'}, lane {ld:.2f} {'PASS' if ld >= 0.2 else 'FAIL'}, aniso {an:.1f}")

# The far dots (05).
dots = far_dots(DIR)
print("\n# 05 far dots: summed display-decoded luma per plume (nozzle px: sum)" + ("; after / before (gate 0.7..1.3 at 2 and 6 px)" if BEFORE else ""))
if BEFORE:
    before = far_dots(BEFORE)
    for px in (40, 12, 6, 2):
        ratio = dots[px] / max(before[px], 1e-9)
        verdict = ("PASS" if 0.7 <= ratio <= 1.3 else "FAIL") if px in (2, 6) else "reported"
        print(f"  {px} px: {dots[px]:.3f} (before {before[px]:.3f}) ratio {ratio:.2f} {verdict}")
else:
    print("  " + ", ".join(f"{px} px: {v:.3f}" for px, v in dots.items()))

# Uncapped peaks: 05 (40 px), 09 (40 px), 10 (crowd), and the capped 01b for reference.
print("\n# peak display RGB (max over the image region) of the uncapped images")
for fn, box in (("05_distance_fighter_v500_s1_argon-blue_n40-12-6-2px_agx-ev0.png", (300, 440, 700, 640)),
                ("09_ribbon_moving8px_fighter_v500_n40_s1_argon-blue_agx-ev0.png", (900, 440, 1400, 640)),
                ("10_crowd_30_mixed_agx-ev0.png", (0, 0, 1920, 1080)),
                ("01b_side_fighter_v500_n150_s0-0.5-1_argon-blue_default_agx-ev0.png", (900, 800, 1400, 1000))):
    img = load(fn)
    sub = img[box[1]:box[3], box[0]:box[2]]
    Y = luma_lin(sub)
    iy, ix = np.unravel_index(np.argmax(Y), Y.shape)
    px = np.round(sub[iy, ix] * 255).astype(int)
    print(f"  {fn[:24]}: peak lin {Y.max():.3f} display {px} at ({box[0] + ix}, {box[1] + iy}); pixels >= 250 on any channel: {(sub.max(axis=2) >= 250 / 255).sum()}")

if CROPS:
    for fn, noz, name in (("02a_45deg_fighter_v500_n150_s1_split-red-top_argon-blue-bottom_agx-ev0.png", (1250, 300), "02a_red_45"),
                          ("02a_45deg_fighter_v500_n150_s1_split-red-top_argon-blue-bottom_agx-ev0.png", (1250, 780), "02a_blue_45"),
                          ("03b_30deg_capital_v10000_n150_s1_argon-blue_t0-top_t100ms-bottom_agx-ev0.png", (1100, 270), "03b_30deg"),
                          ("06b_hull_20deg_fighter_v500_n150_s1_argon-blue_agx-ev0.png", (1060, 540), "06b_hull20"),
                          ("06a_hull_head-on_fighter_v500_n150_s1_argon-blue_agx-ev0.png", (960, 540), "06a_headon"),
                          ("07_rcs_steering_v100_n60_argon-blue_steady-left_puff-peak+1-right_agx-ev0.png", (800, 540), "07_rcs_steady"),
                          ("07_rcs_steering_v100_n60_argon-blue_steady-left_puff-peak+1-right_agx-ev0.png", (1500, 540), "07_rcs_puff"),
                          ("05_distance_fighter_v500_s1_argon-blue_n40-12-6-2px_agx-ev0.png", (960, 540), "05_n12"),
                          ("10_crowd_30_mixed_agx-ev0.png", (1300, 1000), "10_crowd_br")):
        img = load(fn)
        crop(img, noz, name, half_h=140, reach=300 if "06" in name or "02a" in name or "03b" in name else 200, scale=3)
