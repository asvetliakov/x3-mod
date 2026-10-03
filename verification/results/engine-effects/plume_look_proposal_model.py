#!/usr/bin/env python3
"""CPU model of the side-view plume law, current (engine_plume_ps.hlsl, Look defaults) against the proposal of
docs/architecture/engine-exhaust-look-critique.md, with the structure metrics the critique gates on, before and after the
write-back's AgX at EV 0 (tools/analysis/agx_reference.py). Analytic noise ported from the shader (vnoise / fbm).

Prints per law and tint: the axis peak (x I_core), the radial core/edge ratio at u = 0.2 and 0.5 (axis over 0.6 of the
10 % half-width; linear FP16 and display-decoded), the shock-cell depth in u in [0.05, 0.4] on the axis (linear and
display), the head/body whiteness, the streak anisotropy of the high-passed body and the body energy relative to the
current law (the far-dot and the end-on kappa follow it). No bloom, no TAA. Usage: python3 plume_look_proposal_model.py
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools", "analysis"))
import agx_reference as agx  # noqa: E402

I_CORE = 4.0  # I(s = 1)
N_PX = 60     # pixels per nozzle width in the model grid
# The law as implemented (docs/architecture/engine-exhaust-look-critique.md "Implemented"): the proposal with the cells'
# gap depth 0.85 (min(1, 1.7 shock)) ramped in over half a period, and the hot core's boost cooling to half over
# smoothstep(0.2, 0.8, u). The proposal: gap 0.8 over a whole period, no cooling.
IMPLEMENTED = dict(gap=0.85, ramp=0.08, cool=0.5)
TINTS = {
    "argon-blue": ((0.14, 0.71, 1.0), (0.27, 0.90, 1.0)),
    "split-red": ((1.0, 0.15, 0.15), (1.0, 0.81, 0.81)),
}
WHITE = np.array([1.0, 0.97, 0.9])


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def vnoise(p):
    f = p - np.floor(p)
    i = p - f
    base = i * 0.3183099 + np.array([0.1, 0.2, 0.3])
    a = base - np.floor(base)
    b = (base + 0.3183099) - np.floor(base + 0.3183099)
    f = f * f * (3.0 - 2.0 * f)

    def h(x, y, z):
        v = x * y * z * 83521.0 * (x + y + z)
        return v - np.floor(v)

    def lx(z):
        return ((h(a[..., 0], a[..., 1], z) * (1 - f[..., 0]) + h(b[..., 0], a[..., 1], z) * f[..., 0]) * (1 - f[..., 1]) +
                (h(a[..., 0], b[..., 1], z) * (1 - f[..., 0]) + h(b[..., 0], b[..., 1], z) * f[..., 0]) * f[..., 1])

    return lx(a[..., 2]) * (1 - f[..., 2]) + lx(b[..., 2]) * f[..., 2]


def fbm(p):
    return 0.5 * vnoise(p) + 0.25 * vnoise(p * 2.03 + 1.7) + 0.125 * vnoise(p * 4.1 + 3.1)


def width(u):
    bulge, taper, tail_n = 1.15, 0.45, 0.6
    b = 0.5 * bulge * (1 - 0.55 * np.exp(-9 * u)) * (1 + 0.35 * smoothstep(0, 0.25, u) * np.exp(-4 * u))
    w = np.minimum(0.5 * bulge + (0.04 - 0.5 * bulge) * taper * u, b + 0.5 * bulge * taper)
    return w * np.maximum(1 - tail_n * taper * smoothstep(0.6, 1.0, u), 0.05)


def head_colour(mean, peak, k_min=0.0):
    lm = agx.luma(mean)
    lp = agx.luma(peak)
    k = min(1.0, lm / lp) if lp > lm else 1.0
    k = max(k, k_min)
    return np.array(peak) * k


def law(kind, X, Y, tint_name, phase=7.3, t=0.5, seed=0.37, s=1.0):
    """kind: 'current' | 'proposed' | 'implemented'. X along the axis (nozzle widths), Y across. Returns (rgb radiance,
    scalar body)."""
    mean, peak = TINTS[tint_name]
    mean = np.array(mean)
    L = 4.0
    u = X / L
    uc = np.clip(u, 0, 1)
    w = width(uc)
    r = np.abs(Y)
    radial = r / w
    inside = ((u >= 0) & (u <= 1)).astype(float)
    tail = (1 - smoothstep(0.4, 1.0, u)) * np.exp(-u * 0.84) * (1 - 0.5 * (1 - smoothstep(0, 0.3, u)))
    if kind == "current":
        p = np.stack([(X - phase) * 1.6, Y * 3.0, np.full_like(X, seed * 1861.5 + t * 0.7)], -1)
        n1 = fbm(p)
        n2 = fbm(p * 2.2 + np.array([5.0, 2.0, 1.0]))
        erosion = 0.57 * 0.96 * (n1 - 0.5)
        turb = 1 + 0.6 * 2.2 * (n2 - 0.5)
        edge = 1 - smoothstep(0.55, 1.0, radial + erosion)
        cells = 1 + 0.5 * np.cos(2 * np.pi * u / 0.16) * np.exp(-u * 3.0) * smoothstep(0, 1, u / 0.16) * (1 - smoothstep(0, 0.8, radial)) * s
        core_mask = 1 - smoothstep(0, 1, radial / (1.4 * 0.45))
        heat = 0.7 * core_mask * (1 - smoothstep(0, 0.55, u))
        head = head_colour(mean, peak)
        tone = head[None, None, :] + (mean - head)[None, None, :] * smoothstep(0.3, 1, u)[..., None]
        colour = tone + (WHITE[None, None, :] - tone) * heat[..., None]
        body = I_CORE * edge * tail * inside * cells * turb * (1 + 0.6 * core_mask)
        return colour * body[..., None], body
    # proposed / implemented
    gap, ramp, cool = (IMPLEMENTED["gap"], IMPLEMENTED["ramp"], IMPLEMENTED["cool"]) if kind == "implemented" else (0.8, 0.16, 0.0)
    p = np.stack([(X - phase) * 1.0, Y * 4.5, np.full_like(X, seed * 1861.5 + t * 0.7)], -1)  # the streak field, 4.5:1
    n1 = fbm(p)
    n2 = fbm(p * 2.2 + np.array([5.0, 2.0, 1.0]))
    S = n1 - 0.4375  # zero-mean streak (fbm's mean is about 0.44)
    S2 = n2 - 0.4375
    erosion = 0.57 * 1.6 * S2 * (0.6 + 0.8 * uc)  # from the finer field, growing along the plume: tongues
    edge = 1 - smoothstep(0.45, 1.0, radial + erosion)
    tail = (1 - smoothstep(0.4, 1.0, u + 0.35 * S2)) * np.exp(-u * 0.84) * (1 - 0.5 * (1 - smoothstep(0, 0.3, u)))
    profile = 0.08 + 0.92 * np.exp(-(radial / 0.32) ** 2)  # peaked body
    core = np.exp(-(radial / 0.2) ** 2)  # the thin hot core
    c = (0.5 + 0.5 * np.cos(2 * np.pi * u / 0.16)) ** 3  # sharp crests, dark gaps; mean 0.3125
    # the cells carve the body (crest 1, gap 1 - a): a = shock_gap 0.8 at the mouth, fading 5 cfade along, ramped in
    # over the first period, across the core to 0.9 w
    a_cells = gap * np.exp(-u * 3.0) * smoothstep(0, ramp, u) * (1 - smoothstep(0.3, 0.9, radial)) * s
    cells = 1 - a_cells * (1 - c)
    turb = 1 + 0.6 * 2.2 * S2 * (0.4 + 0.6 * np.clip(radial, 0, 1))  # steady core, boiling sheath
    heat = 0.7 * core * (1 - smoothstep(0.05, 0.3, u))
    head = head_colour(mean, peak, k_min=0.75)
    tint_dark = mean * 0.5
    tint_tail = mean * 0.6
    body_tint = mean[None, None, :] + (tint_tail - mean)[None, None, :] * smoothstep(0.6, 1.0, uc)[..., None]
    hot = head[None, None, :] + (body_tint - head[None, None, :]) * smoothstep(0.2, 0.5, uc)[..., None]
    colour = hot + (tint_dark[None, None, :] - hot) * smoothstep(0.25, 0.9, radial)[..., None]
    colour = colour + (WHITE[None, None, :] - colour) * heat[..., None]
    body = I_CORE * edge * tail * inside * cells * turb * profile * (1 + 0.6 * core * (1 - cool * smoothstep(0.2, 0.8, uc)))
    return colour * body[..., None], body


def display(rgb):
    out = np.zeros_like(rgb)
    flat = rgb.reshape(-1, 3)
    o = out.reshape(-1, 3)
    for i in range(flat.shape[0]):
        o[i] = agx.agx(tuple(float(v) for v in flat[i]))
    return out


def luma_lin(rgb):
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def metrics(label, rgb, body, X, Y):
    n_px = N_PX
    Yl = luma_lin(rgb)
    disp = display(rgb)
    Yd = luma_lin(np.clip(disp, 0, 1) ** 2.2)
    mid = Y.shape[0] // 2
    axis = Yl[mid]
    axis_d = Yd[mid]
    L = 4.0
    out = {"peak/I": axis.max() / I_CORE}
    # cells
    for name, prof in (("lin", axis), ("disp", axis_d)):
        a, b = int((0.05 * L + 0.5) * n_px), int((0.4 * L + 0.5) * n_px)
        win = int(1.5 * 0.16 * L * n_px)
        k = np.ones(win) / win
        mean = np.convolve(np.pad(prof, (win // 2, win - win // 2 - 1), mode="edge"), k, mode="valid")[: len(prof)]
        ratio = (prof / np.maximum(mean, 1e-9))[a:b]
        out[f"cells_{name}"] = (ratio.max() - ratio.min()) / (ratio.max() + ratio.min())
    # the body lane: the cells at radial 0.35 w (the row 0.35 x w(u) off the axis per column)
    for name, img in (("lin", Yl), ("disp", Yd)):
        cols = np.arange(int((0.05 * L + 0.5) * n_px), int((0.4 * L + 0.5) * n_px))
        rows = mid + np.round(0.35 * width(np.clip((cols / n_px - 0.5) / L, 0, 1)) * n_px).astype(int)
        lane = img[rows, cols]
        win = int(1.5 * 0.16 * L * n_px)
        k = np.ones(win) / win
        mean = np.convolve(np.pad(lane, (win // 2, win - win // 2 - 1), mode="edge"), k, mode="valid")[: len(lane)]
        ratio = lane / np.maximum(mean, 1e-9)
        out[f"lane_cells_{name}"] = (ratio.max() - ratio.min()) / (ratio.max() + ratio.min())
        out[f"lane_mean_{name}"] = float(lane.mean())
    # radial
    for u in (0.2, 0.5):
        col = int((u * L + 0.5) * n_px)
        for name, img in (("lin", Yl), ("disp", Yd)):
            prof = img[:, col]
            pk = prof[mid]
            hw = next((d for d in range(1, mid) if max(prof[mid + d], prof[mid - d]) < 0.1 * pk), mid - 1)
            e = int(round(0.6 * hw))
            edge = 0.5 * (prof[mid + e] + prof[mid - e])
            out[f"core/edge_u{u}_{name}"] = pk / max(edge, 1e-9)
        out[f"hw_u{u}"] = hw / n_px
    # whiteness on the axis
    for u in (0.1, 0.5):
        px = disp[mid, int((u * L + 0.5) * n_px)]
        out[f"white_u{u}"] = 1 - px.min() / max(px.max(), 1e-9)
    # anisotropy of the high-passed linear body (u 0.1..0.8, |y| < 0.6 x the half-width)
    from PIL import Image, ImageFilter
    a, b = int((0.1 * L + 0.5) * n_px), int((0.8 * L + 0.5) * n_px)
    hw = int(0.6 * 0.6 * n_px)
    sub = Yl[mid - hw: mid + hw + 1, a:b]
    im = Image.fromarray((np.clip(sub / max(sub.max(), 1e-9), 0, 1) * 255).astype(np.uint8))
    low = np.asarray(im.filter(ImageFilter.GaussianBlur(10))).astype(float) / 255.0 * sub.max()
    hp = sub - low
    hp -= hp.mean()
    var = (hp * hp).mean()

    def half(axis):
        for lag in range(1, 80):
            c = (hp[:, lag:] * hp[:, :-lag]).mean() / var if axis else (hp[lag:, :] * hp[:-lag, :]).mean() / var
            if c < 0.5:
                return lag
        return 80

    lx, ly = half(1), half(0)
    out["aniso"] = lx / ly
    out["energy"] = float(body.sum()) / (n_px * n_px)
    out["energy_rgb"] = float(Yl.sum()) / (n_px * n_px)
    print(f"{label}: " + ", ".join(f"{k} {v:.2f}" for k, v in out.items()))
    return out


def save_png(rgb, path):
    from PIL import Image
    d = display(rgb)
    Image.fromarray((np.clip(d, 0, 1) * 255).astype(np.uint8)).resize((rgb.shape[1] * 2, rgb.shape[0] * 2), Image.NEAREST).save(path)


def main():
    n_px = N_PX
    png_dir = sys.argv[sys.argv.index("--png") + 1] if "--png" in sys.argv else None
    xs = np.arange(-0.5, 4.6, 1.0 / n_px)
    ys = np.arange(-1.2, 1.2 + 1e-9, 1.0 / n_px)
    X, Y = np.meshgrid(xs, ys)
    print("# AgX EV0 display luma (gamma-2.2 decoded) of grey linear 0.25 .. 16:")
    for v in (0.25, 0.5, 1, 2, 4, 8, 16):
        d = agx.agx((v, v, v))
        print(f"  {v:>5}: display {d[1]:.3f} (8-bit {d[1] * 255:.0f}), decoded {d[1] ** 2.2:.3f}")
    print("# head colours (peak scaled to the mean's luminance; proposal clamps k >= 0.75):")
    for name, (mean, peak) in TINTS.items():
        print(f"  {name}: current {np.round(head_colour(mean, peak), 2)}, proposed {np.round(head_colour(mean, peak, 0.75), 2)}, mean {mean}")
    res = {}
    for tint in TINTS:
        for kind in ("current", "proposed", "implemented"):
            rgb, body = law(kind, X, Y, tint)
            res[(tint, kind)] = metrics(f"{tint:>10} {kind:>8}", rgb, body, X, Y)
            if png_dir:
                save_png(rgb, os.path.join(png_dir, f"model_{tint}_{kind}.png"))
    for tint in TINTS:
        c = res[(tint, "current")]
        for kind in ("proposed", "implemented"):
            p = res[(tint, kind)]
            print(f"# {tint}: {kind} / current body energy {p['energy'] / c['energy']:.2f}, luma energy {p['energy_rgb'] / c['energy_rgb']:.2f}, "
                  f"peak {p['peak/I'] / c['peak/I']:.2f}")


if __name__ == "__main__":
    main()
