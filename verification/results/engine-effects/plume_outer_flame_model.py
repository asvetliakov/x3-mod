#!/usr/bin/env python3
"""The outer-flame energy compensation (docs/architecture/engine-exhaust-look-critique.md section 6, "Review fixes",
the coordinator's decision of 2026-10-04: keep the hue gate, carry the energy in the outer flame).

The side law of src/effects/engine_plume_ps.hlsl at the detail level 1 (still: S2 = 0) with the compensation split by
radius: the core term (the peaked profile, its hot boost) x G(radial) = lerp(gc, g, smoothstep(ga, gb, radial)) (the
axis at gc, the mid-radius layers at g), and the outer sheath, A4 m (1 - m) with m = smoothstep(w0, w1, radial), x g
inside its own edge 1 - smoothstep(e0, e1, radial), coloured by the darker tint (0.5 tint, darkening along u as the
rim's stop) instead of the rim's head-to-darker-tint mix. Prints, per candidate: the side energy over the slab law's
at s 1 / 0.5 (the frame total of every pixel's largest channel on a white tint, body + halo; the fixture's ENERGY case
measured 0.92 of this model's ratio on the previous two laws), and on the structure case's two tints at the 40 px
uncapped nozzle (I 4) and the 150 px capped one (88.5 px, faded to I 2): the axis whiteness at u 0.5 (gate 0.15), the
rim whiteness at radial 0.6, u 0.2 (gate 0.5), the FP16 radial contrast at u 0.2 (Rec. 709 luma, axis over 0.6 of the
10 % half-width; gate 3.0) and that half-width over the slab law's (target 0.8..1.0). Whiteness is 1 - min / max of the
8-bit display through the write-back's path at EV 0 (tools/analysis/agx_reference.py tonemap_engine: gamma 2.2 decode,
then AgX; the fixture's dump::agx). Calibration rows: "tuned" (8eb0514b) and "uniform"
(e117e960, g on the whole revised body), which the fixture measured.
Usage: python3 plume_outer_flame_model.py; the output is plume_outer_flame_model_out.txt.
"""
import os
import sys

import numpy as np

import plume_end_on_model as m

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools", "analysis"))
import agx_reference as agx  # noqa: E402

shock, period, cfade, core, halo, hb = .5, .16, .6, .45, 1.1, .20
C13 = 1 / (.45 * core)
TINTS = {"argon-blue": ((.14, .71, 1.0), (.27, .90, 1.0)), "split-red": ((1.0, .15, .15), (1.0, .81, .81))}
WHITE = np.array([1.0, .97, .9])
G1, G0 = 2.36, 1.91  # e117e960's g(s)


def ss(a, b, x):
    return m.ss(a, b, x)


def head_of(mean, peak):
    lm, lp = agx.luma(mean), agx.luma(peak)
    k = lm / lp if lp > lm and lp > 0 else 1.0
    return np.array(peak) * max(k, .75)


def terms(p, u, radial, s):
    """The body's radiance per I (white tint and head), split: (slab, core term, outer term) and the colours' weights."""
    uc = np.clip(u, 0, 1)
    crest = .5 + .5 * np.cos(2 * np.pi * u / period)
    env = np.exp(-u * 5 * cfade) * ss(0, 1, u * 2 / period) * (1 - ss(.3, .9, radial)) * s
    cells = 1 - min(1, 1.7 * shock) * env * (1 - crest ** 3)
    hot = np.exp(-(radial * C13) ** 2)
    boost = 1 + .6 * hot * (1 - .5 * ss(.2, .8, uc))
    edge = 1 - ss(.45, 1, radial)
    # The mixing layer grows downstream: the gain's excess over 1 rises from lo at the mouth to all of it over
    # smoothstep(ua, ub, u) (lo 1: uniform along the plume).
    gain = 1 + (p["gain"] - 1) * (p.get("lo", 1.0) + (1 - p.get("lo", 1.0)) * ss(p.get("ua", 0.0), p.get("ub", 1e-3), uc))
    # The axis gain gc near the mouth only (u under 0.3, back to 1 by 0.45: the axis at u 0.5 keeps its hue).
    gc = 1 + (p["gc"] - 1) * (1 - ss(.3, .45, uc)) if p.get("gc_mouth") else p["gc"]
    G = gc + (gain - gc) * ss(p["ga"], p["gb"], radial)
    core_t = edge * cells * (.08 + .92 * np.exp(-(radial / .32) ** 2)) * boost * G
    # The mid-radius body layer: Bm smoothstep(ga, gb, radial) (1 - smoothstep(b0, b1, radial)) x the gain, coloured as
    # the body (a flat band from the core's flank to b0; Bm 0: none).
    if p.get("Bm", 0.0) > 0:
        core_t = core_t + edge * cells * p["Bm"] * ss(p["ga"], p["gb"], radial) * (1 - ss(p["b0"], p["b1"], radial)) * gain
    mw = ss(p["w0"], p["w1"], radial)
    if p["split"]:
        outer_t = (1 - ss(p["e0"], p["e1"], radial)) * cells * p["A4"] * mw * (1 - mw) * gain
        if p.get("outer_grow"):  # the single law: the sheath itself grows along the plume, lo at the mouth
            outer_t = outer_t * (p["lo"] + (1 - p["lo"]) * ss(p["ua"], p["ub"], uc))
    else:  # the tuning pass: the sheath inside the edge, x the boost and G, coloured as the core
        core_t = core_t + edge * cells * p["A4"] * mw * (1 - mw) * boost * G
        outer_t = 0 * radial
    slab = (1 - ss(.55, 1, radial)) * (1 + .6 * (1 - ss(0, 1, radial * C13 * .3214286))) * (1 + shock * env * (2 * crest - 1))
    return slab, core_t, outer_t


def colours(u, radial, tint, head, sat=1.0):
    """The core's colour (tone -> darker tint across, white by the heat) and the outer layer's (darker tint; `sat` > 1
    raises its saturation: tint^sat over its largest channel)."""
    uc = np.clip(u, 0, 1)
    tint = np.asarray(tint)
    dark = 1 - .4 * ss(.6, 1, uc)
    tone = head + (tint * dark - head) * ss(.2, .5, uc)
    rim = ss(.25, .9, radial)
    heat = .7 * np.exp(-(radial * C13) ** 2) * (1 - ss(.05, .3, u))
    across = tone + (.5 * tint - tone) * rim
    ts = tint ** sat / max((tint ** sat).max(), 1e-6) * tint.max()
    return across + (WHITE - across) * heat, .5 * ts * dark


def energy(p, s, ln, dx=.01):
    """The side frame's total (largest channel, white tint): detail 1 (the candidate) and 0 (the slab law), body + halo."""
    i, ih = 1.2 + 2.8 * s, hb * (1.2 + 2.8 * s) / 4
    X, Y = np.meshgrid(np.arange(-3, ln + 3, dx), np.arange(-3, 3, dx))
    u, r = X / ln, np.abs(Y)
    uc = np.clip(u, 0, 1)
    inside = (u >= 0) & (u <= 1)
    radial = r / m.width(uc)
    tl = m.tailf(u) * inside
    slab, core_t, outer_t = terms(p, u, radial, s)
    # The white tint's largest channel: the tone 1 -> the darker stop along, -> 0.5 across, -> 1 by the heat; the outer 0.5.
    dark = 1 - .4 * ss(.6, 1, uc)
    tone = 1 + (dark - 1) * ss(.2, .5, uc)
    across = tone + (.5 - tone) * ss(.25, .9, radial)
    heat = .7 * np.exp(-(radial * C13) ** 2) * (1 - ss(.05, .3, u))
    col = across + (1 - across) * heat
    d0 = np.hypot(X - np.clip(X, 0, ln), r)
    h1 = ih * np.exp(-d0 / (.32 * halo)) * np.exp(-4 * uc) * np.clip(2 * (2.25 - d0 / (.32 * halo)), 0, 1)
    h0 = ih * np.exp(-d0 / (.5 * halo)) * np.exp(-2.2 * uc) * np.clip(2 * (2.25 - d0 / (.5 * halo)), 0, 1)
    e1 = (i * tl * (core_t * col + outer_t * .5 * dark) + h1).sum()
    e0 = (i * tl * slab + h0).sum()  # detail 0: the tone on white is 1, the heat to white 1
    return e1 / e0


def pixel(p, u, r_n, s, ln, i, tint, head):
    """The linear RGB at (u, r in nozzle widths) on the side view, I given (the near fade folded in), halo included."""
    uc = min(max(u, 0.0), 1.0)
    radial = r_n / float(m.width(uc))
    slab, core_t, outer_t = terms(p, np.float64(u), np.float64(radial), s)
    col, dark = colours(np.float64(u), np.float64(radial), tint, head, p.get("sat", 1.0))
    tl = float(m.tailf(u))
    x = u * ln
    d0 = np.hypot(x - min(max(x, 0), ln), r_n) / (.32 * halo)
    h = hb * i / 4 * np.exp(-d0) * np.exp(-4 * uc) * min(max(2 * (2.25 - d0), 0), 1)
    return i * tl * (core_t * col + outer_t * dark) + h * np.asarray(tint)


def whiteness(rgb):
    # The write-back's path as the fixture's port (dump::agx): the scene value decoded with gamma 2.2, then AgX.
    d = np.array(agx.tonemap_engine(tuple(float(v) for v in rgb)))
    c = np.round(np.clip(d, 0, 1) * 255)
    return 1 - c.min() / c.max() if c.max() > 0 else 0.0


def luma709(rgb):
    return .2126 * rgb[0] + .7152 * rgb[1] + .0722 * rgb[2]


def radial_gate(p, s, ln, i, tint, head, n_px, u=.2, full=False):
    """The structure case at u 0.2: axis peak (rows |dy| <= 2) over the luma at round(0.6 hw), hw the first row under 10 %.
    full: also the contrast at 0.5 hw and the largest rise outward (lum[k] / lum[k - 1] - 1 over k; 0: monotone)."""
    lum = [luma709(pixel(p, u, dy / n_px, s, ln, i, tint, head)) for dy in range(0, 400)]
    pk = max(lum[:3])
    hw = next((k for k in range(1, 400) if lum[k] < .1 * pk), 399)
    e = int(np.floor(.6 * hw + .5))
    if not full:
        return pk / max(lum[e], 1e-9), hw / n_px
    e5 = int(np.floor(.5 * hw + .5))
    rise = max((lum[k] - lum[k - 1]) / max(pk, 1e-9) for k in range(1, hw + 1))
    return pk / max(lum[e], 1e-9), hw / n_px, pk / max(lum[e5], 1e-9), rise


def slab_hw(s, ln, n_px):
    lum = []
    for dy in range(0, 400):
        radial = dy / n_px / float(m.width(.2))
        slab, _, _ = terms(dict(gain=1, gc=1, ga=.25, gb=.45, w0=.25, w1=.9, A4=0, split=True, e0=.45, e1=1), np.float64(.2),
                           np.float64(radial), s)
        lum.append(float(slab))
    pk = max(lum[:3])
    return next((k for k in range(1, 400) if lum[k] < .1 * pk), 399) / n_px


def report(name, p):
    out = [f"{name}: e(s1) {energy(p, 1.0, 4.0):.3f} e(s.5) {energy(p, .5, 2.25):.3f}"]
    hw0 = slab_hw(1.0, 4.0, 40.0)
    for tname, (mean, peak) in TINTS.items():
        head = head_of(mean, peak)
        for n_px, i in ((40.0, 4.0), (88.5, 2.0)):
            ax = whiteness(pixel(p, .5, 0.0, 1.0, 4.0, i, mean, head))
            rim = whiteness(pixel(p, .2, .6 * float(m.width(.2)), 1.0, 4.0, i, mean, head))
            rc, hw = radial_gate(p, 1.0, 4.0, i, mean, head, n_px)
            out.append(f"  {tname[:4]} {n_px:.0f}px: axis {ax:.3f} rim {rim:.3f} radial {rc:.2f} hw/slab {hw / hw0:.2f}")
    print("\n".join(out))


BASE = dict(gain=1.0, gc=1.0, ga=.25, gb=.45, w0=.25, w1=.9, A4=1.28, split=False, e0=.45, e1=1.0)


def evaluate(p):
    """Energy at s 1 / 0.5 and the worst gate values over both tints and both sizes."""
    hw0 = slab_hw(1.0, 4.0, 40.0)
    ax, rim, rc, hws, c5s, rises = [], [], [], [], [], []
    for mean, peak in TINTS.values():
        head = head_of(mean, peak)
        for n_px, i in ((40.0, 4.0), (88.5, 2.0)):
            ax.append(whiteness(pixel(p, .5, 0.0, 1.0, 4.0, i, mean, head)))
            rim.append(whiteness(pixel(p, .2, .6 * float(m.width(.2)), 1.0, 4.0, i, mean, head)))
            c, hw, c5, rise = radial_gate(p, 1.0, 4.0, i, mean, head, n_px, full=True)
            _, _, c5_u5, rise_u5 = radial_gate(p, 1.0, 4.0, i, mean, head, n_px, u=.5, full=True)
            rc.append(c)
            hws.append(hw / hw0)
            c5s.append(c5)
            rises.append(max(rise, rise_u5))
    return dict(e1=energy(p, 1.0, 4.0), e05=energy(p, .5, 2.25), axis=min(ax), rim=min(rim), radial=min(rc), hw_lo=min(hws),
                hw_hi=max(hws), radial_half=min(c5s), rise=max(rises))


# The search's limits: the gates at the model's calibration (axis 0.155, rim 0.5, radial 3.1; the model reads the
# fixture's 0.157 axis as 0.161 and its 0.528 red rim as 0.49) and the half-width 0.8..1.0 of the slab law at u 0.2;
# overridable as LIMITS_<name>=value in the environment.
LIMITS = {k: float(os.environ.get(f"LIMITS_{k}", v)) for k, v in dict(axis=.155, rim=.5, radial=3.1, hw_lo=.8, hw_hi=1.0).items()}


def search(count, seed=1, grow=False, mid=False):
    """Random search: the largest min(e(s 1), e(s 0.5)) within LIMITS."""
    rng = np.random.default_rng(seed)
    best = None
    for _ in range(count):
        ga = rng.uniform(.15, .45)
        w0 = rng.uniform(.1, .5)
        e0 = rng.uniform(.45, .9)
        p = dict(BASE, split=True, gc_mouth=True, gain=rng.uniform(1, 3.5), gc=rng.uniform(1, 1.3), ga=ga,
                 gb=ga + rng.uniform(.05, .4), A4=rng.uniform(0, 6), w0=w0, w1=w0 + rng.uniform(.3, 1.0), e0=e0,
                 e1=e0 + rng.uniform(.15, .6), sat=rng.uniform(1, 2.5))
        if mid:
            b0 = rng.uniform(.3, .7)
            p.update(Bm=rng.uniform(0, 1.2), b0=b0, b1=b0 + rng.uniform(.05, .4))
        if grow:
            ua = rng.uniform(.1, .4)
            p.update(lo=rng.uniform(0, .6), ua=ua, ub=ua + rng.uniform(.1, .4))
        if os.environ.get("SMOOTH"):  # growth from the mouth: at least 0.3 of the gain there, the ramp from u <= 0.2
            ua = rng.uniform(0., .2)
            p.update(lo=rng.uniform(.3, .8), ua=ua, ub=ua + rng.uniform(.3, .6))
        if os.environ.get("SAT2"):
            p["sat"] = 2.0
        v = evaluate(p)
        ok = feasible(v)
        score = min(v["e1"], v["e05"])
        if ok and (best is None or score > best[0]):
            best = (score, p, v)
    return best


BOUNDS = dict(gain=(1, 4.5), gc=(1, 1.3), ga=(.25, .5), gb=(.3, .8), w0=(.1, .6), w1=(.6, 1.6), A4=(0, 8), e0=(.45, 1.0),
              e1=(.6, 1.4), Bm=(0, 1.5), b0=(.25, .8), b1=(.3, 1.0), lo=(.3, 1.0), ua=(0, .2), ub=(.2, .8))
if os.environ.get("GROWFREE"):  # the growth may start late (no gain at the mouth): lo 0..1, ua up to 0.4
    BOUNDS.update(lo=(0, 1.0), ua=(0, .4))
if os.environ.get("NOGAIN"):  # the single law of the 2026-10-04 design change: no gain, the outer sheath alone (x its growth)
    BOUNDS.update(gain=(1, 1), gc=(1, 1), Bm=(0, 0))


def feasible(v):
    """The gates with margins, and a thin hot core: the radial profile at u 0.2 and 0.5 never rises outward by more than
    2 % of the axis (no bright band or hollow tube) and the contrast holds at half the half-width too (>= 2.5)."""
    return (v["axis"] >= LIMITS["axis"] and v["rim"] >= LIMITS["rim"] and v["radial"] >= LIMITS["radial"] and
            LIMITS["hw_lo"] <= v["hw_lo"] and v["hw_hi"] <= LIMITS["hw_hi"] and v.get("rise", 0) <= .02 and
            v.get("radial_half", 9) >= 2.5)


def refine(p, steps, seed=1):
    """Local search from p: one or two parameters perturbed per step (bounds above, gb > ga, b1 > b0, e1 > e0, w1 > w0,
    ub > ua), kept when feasible and min(e(s 1), e(s 0.5)) rises."""
    rng = np.random.default_rng(seed)
    p = dict(p, **{k: float(np.clip(p[k], *BOUNDS[k])) for k in BOUNDS if k in p})
    v = evaluate(p)
    best = (min(v["e1"], v["e05"]) if feasible(v) else -1, dict(p), v)
    for _ in range(steps):
        q = dict(best[1])
        for k in rng.choice(list(BOUNDS), size=rng.integers(1, 3), replace=False):
            lo, hi = BOUNDS[k]
            q[k] = float(np.clip(q[k] + rng.normal(0, .08 * (hi - lo)), lo, hi))
        if not (q["gb"] > q["ga"] + .03 and q["b1"] > q["b0"] + .03 and q["e1"] > q["e0"] + .05 and q["w1"] > q["w0"] + .2 and
                q["ub"] > q["ua"] + .1):
            continue
        w = evaluate(q)
        score = min(w["e1"], w["e05"])
        if feasible(w) and score > best[0]:
            best = (score, q, w)
    return best


if __name__ == "__main__":
    if len(sys.argv) > 3 and sys.argv[1] == "--refine":
        import ast
        best = refine(ast.literal_eval(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]) if len(sys.argv) > 4 else 1)
        print("best:", (round(best[0], 3), {k: round(v, 3) if isinstance(v, float) else v for k, v in best[1].items()},
                        {k: round(float(v), 3) for k, v in best[2].items()}))
        sys.exit(0)
    if len(sys.argv) > 2 and sys.argv[1] == "--search":
        best = search(int(sys.argv[2]), int(sys.argv[3]) if len(sys.argv) > 3 else 1, "--grow" in sys.argv, "--mid" in sys.argv)
        print("best:", None if best is None else (round(best[0], 3), {k: round(v, 3) if isinstance(v, float) else v for k, v in best[1].items()},
                                                   {k: round(v, 3) for k, v in best[2].items()}))
        sys.exit(0)
    report("tuned (8eb0514b, calibration)", BASE)
    report("uniform g 2.36 (e117e960, calibration)", dict(BASE, gain=G1, gc=G1, ga=0, gb=1e-3))
    for args in sys.argv[1:]:
        p = dict(BASE, split=True)
        for kv in args.split(","):
            k, v = kv.split("=")
            p[k] = float(v)
        report(args, p)
