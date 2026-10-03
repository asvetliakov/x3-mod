#!/usr/bin/env python3
"""The numeric model behind the end-on disc's constants (src/proxy/engine_plumes_core.h Look: disc_kappa, disc_halo,
disc_cap, the facing band 0.3..0.7, axial_floor 0.5; docs/architecture/engine-effects-modern.md "After flight C", and
since the revised look law docs/architecture/engine-exhaust-look-critique.md "Implemented"). The still look (no
turbulence, erosion, tongues or pulse), lengths in nozzle widths.

Three laws: "previous" (the slab law of flight C: kappa 1.8, halo 3, cap 1.5), "revised" (the peaked profile, the
carving cells, the tight halo exp(-d / 0.32 halo) exp(-4 u)) and "tuned" (the critique's section 6 tuning pass: the
side view's outer sheath 4 x 0.32 m (1 - m), m = smoothstep(0.25, 0.9, radial), not in the disc's samples, so kappa
rises; the end-on ring x 3 at twice the side's sigma). Per law:
- kappa: the side view's body energy over the 8-sample integrated end-on profile's, per nozzle width of length (I x L / n);
- halo: the end-on halo gain that keeps the previous law's ratio of the disc's halo energy to the side view's halo energy;
- the energy rows: the end-on disc's (soft-capped body + halo) against the side view's (body + halo) at s 0 / 0.5 / 1 and
  L / n 0.5..8, and the facing rows (the axial quad's foreshortened share approximated by sqrt(1 - f^2), no hand-over);
- the 02b radial profile: the end-on disc as the fixture's look image draws it (the 150 px fighter nozzle capped to
  88.6 px, faded 0.5, the disc at 0.6 / 0.5 of it, L / n 4, the ring widened to the pixel), through AgX at EV 0
  (tools/analysis/agx_reference.py), Rec. 709 luma of the gamma-2.2-decoded display every 4 px, and the ring and
  hot-centre measures of the fixture's disc gate (plume_look_metrics.py).
Usage: python3 plume_end_on_model.py; the output is plume_end_on_model_out.txt.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools", "analysis"))
import agx_reference as agx  # noqa: E402

bulge, taper, tail, shock, period, cfade, core, halo, hb, ring = 1.15, .45, .7, .5, .16, .6, .45, 1.1, .20, .3
SHEATH, RING_W, RING = .32, 2.0, 3.0  # the tuned law's Look::outer, disc_ring_width, disc_ring
c0, c1, c2, c3 = .5 * bulge, (.04 - .5 * bulge) * taper, .5 * bulge * taper, .6 * taper
tf, t12 = .75 + (.25 - .75) * tail, 1.2 * tail
LAWS = {
    "previous": dict(kappa=1.8, halo=3.0, cap=1.5, sigma=.5 * halo, fall=2.2, ring=1.0),
    "revised": dict(kappa=None, halo=None, cap=None, sigma=.32 * halo, fall=4.0, ring=3.0, sheath=0.0, ring_w=1.0),
    # The tuning pass (docs/architecture/engine-exhaust-look-critique.md section 6, "Tuning"): the side view's outer
    # sheath (4 Look::outer x m (1 - m), m = smoothstep(0.25, 0.9, radial); not in the disc's samples), the end-on ring
    # x 3 with its Gaussian twice as wide.
    "tuned": dict(kappa=None, halo=None, cap=None, sigma=.32 * halo, fall=4.0, ring=RING, sheath=SHEATH, ring_w=RING_W),
}


def ss(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def width(u):
    b = c0 * (1 - .55 * np.exp(-9 * u)) * (1 + .35 * ss(0, .25, u) * np.exp(-4 * u))
    return np.minimum(c0 + c1 * u, b + c2) * np.maximum(1 - c3 * ss(.6, 1, u), .05)


def tailf(u):
    return (1 - ss(tf, 1, u)) * np.exp(-u * t12) * (1 - .5 * (1 - ss(0, .3, u)))


def cell(law, u):
    """previous: the cosine's amplitude; revised: the carving depth a (1 - c) (engine_plumes_core.h law::cell)."""
    if law == "previous":
        return shock * np.cos(2 * np.pi * u / period) * np.exp(-u * 5 * cfade) * ss(0, period, u)
    c = (.5 + .5 * np.cos(2 * np.pi * u / period)) ** 3
    return min(1.0, 1.7 * shock) * np.exp(-u * 5 * cfade) * ss(0, .5 * period, u) * (1 - c)


def profile(law, radial, s, c, u=0.0, disc=False):
    if law == "previous":
        edge, cm = 1 - ss(.55, 1, radial), 1 - ss(0, 1, radial / (1.4 * core))
        return edge * (1 + c * (1 - ss(0, .8, radial)) * s) * (1 + .6 * cm)
    edge = 1 - ss(.45, 1, radial)
    peaked = .08 + .92 * np.exp(-(radial / .32) ** 2) + (0.0 if disc else 4 * LAWS[law]["sheath"] * ss(.25, .9, radial) * (1 - ss(.25, .9, radial)))
    hot = np.exp(-(radial / (.45 * core)) ** 2)
    return edge * (1 - c * s * (1 - ss(.3, .9, radial))) * peaked * (1 + .6 * hot * (1 - .5 * ss(.2, .8, u)))


def side(law, s, ln, dx=.01):
    p = LAWS[law]
    i, ih, sig = 1.2 + 2.8 * s, hb * (1.2 + 2.8 * s) / 4, p["sigma"]
    X, Y = np.meshgrid(np.arange(-3, ln + 3, dx), np.arange(-3, 3, dx))
    u, r = X / ln, np.abs(Y)
    uc = np.clip(u, 0, 1)
    body = i * tailf(u) * ((u >= 0) & (u <= 1)) * profile(law, r / width(uc), s, cell(law, uc), uc)
    d = np.hypot(X - np.clip(X, 0, ln), r) / sig
    h = ih * np.exp(-d) * np.exp(-p["fall"] * uc) * np.clip(2 * (2.25 - d), 0, 1)
    total = body + h
    mouth = total[:, X[0] <= .15 * ln].max()
    crest = total[:, (X[0] >= .1 * ln) & (X[0] <= .4 * ln)].max()
    return body.sum() * dx * dx, h.sum() * dx * dx, total.sum() * dx * dx, mouth, crest


uk = (np.arange(8) + .5) / 8
wk, tk = width(uk), tailf(uk)


def axis_peak(law, s):
    u = np.linspace(0, 1, 513)
    c = cell(law, u)
    if law == "previous":
        return 1.6 * (tailf(u) * (1 + s * c)).max()
    return ((1 + .6 * (1 - .5 * ss(.2, .8, u))) * tailf(u) * (1 - s * c)).max()


def mean_profile(law, rho, s):
    return sum(t * profile(law, rho / w, s, cell(law, u), u, True) for w, t, u in zip(wk, tk, uk)) / 8


def disc_halo_energy(sig, dx=.01):
    X, Y = np.meshgrid(np.arange(-3, 3, dx), np.arange(-3, 3, dx))
    rho = np.hypot(X, Y) / sig
    return (np.exp(-rho) * np.clip(2 * (2.25 - rho), 0, 1)).sum() * dx * dx


def disc(law, s, ln, f=1.0, dx=.01):
    p = LAWS[law]
    i, ih, sig = 1.2 + 2.8 * s, hb * (1.2 + 2.8 * s) / 4, p["sigma"]
    X, Y = np.meshgrid(np.arange(-3, 3, dx), np.arange(-3, 3, dx))
    rho = np.hypot(X, Y)
    mean = mean_profile(law, rho, s)
    cap = p["cap"] * i * axis_peak(law, s)
    x = i * p["kappa"] * ln * f * mean + ih * p["halo"] * ln * f * np.exp(-rho / sig) * np.clip(2 * (2.25 - rho / sig), 0, 1)
    shown = cap * (1 - np.exp(-x / cap))
    return shown.sum() * dx * dx, shown.max(), mean.sum() * dx * dx


def soft_max(a, b):
    return (a ** 4 + b ** 4) ** .25


def look_image_profile(law, tint, head, n_px=88.6, ln=4.0, near=.5):
    """The 02b disc as drawn: I_core 4 x the near fade, the disc's level 0.6 / near (chase_disc_floor), rho every 4 px;
    returns the display-decoded luma per radius (px 0, 4, .., 72)."""
    p = LAWS[law]
    i = 4.0 * near
    level = max(.6 / near, 1.0)
    ih = hb * near
    sig = p["sigma"]
    s2 = 1 / 240 + .25 / n_px ** 2
    out = []
    tint, head = np.array(tint), np.array(head)
    white = np.array([1.0, .97, .9])
    for px in range(0, 76, 4):
        rho = px / n_px
        # the body and its colour (the revised law's three stops; the previous law's two-tone + heat)
        B, rgb = 0.0, np.zeros(3)
        for w, t, u in zip(wk, tk, uk):
            radial = rho / w
            b = t * profile(law, radial, 1.0, cell(law, u), u, True)
            if law == "previous":
                cm = 1 - ss(0, 1, radial / (1.4 * core))
                h = .7 * (1 - ss(0, .55, u)) * cm
                col = head + (tint - head) * ss(.3, 1, u)
            else:
                h = .7 * (1 - ss(.05, .3, u)) * np.exp(-(radial / (.45 * core)) ** 2)
                hot = head + (tint * (1 - .4 * ss(.6, 1, u)) - head) * ss(.2, .5, u)
                col = hot + (.5 * tint - hot) * ss(.25, .9, radial)
            B += b
            rgb += b * ((1 - h) * col + h * white)
        colour = rgb / B if B > 0 else tint
        body = level * i * p["kappa"] * ln * B / 8
        halo_v = level * ih * p["halo"] * ln * np.exp(-rho / sig) * np.clip(2 * (2.25 - rho / sig), 0, 1)
        rd = rho - .46 * bulge
        rr = rd / p.get("ring_w", 1.0)
        ring_v = level * i * ring * p["ring"] / 4 * np.sqrt((1 / 240) / s2) * np.exp(-rr ** 2 / (2 * s2))
        ring_c = tint + (np.array([1.0, .95, .85]) - tint) * .6
        m = ring_v + halo_v if p.get("ring_sum") else soft_max(ring_v, halo_v)
        mouth_c = ((ring_c * ring_v + tint * halo_v) / max(ring_v + halo_v, 1e-30) if p.get("ring_sum") else
                   (ring_c * ring_v ** 4 + tint * halo_v ** 4) / max(ring_v ** 4 + halo_v ** 4, 1e-30))
        total = body + m
        cap = level * p["cap"] * i * axis_peak(law, 1.0)
        shown = cap * (1 - np.exp(-total / cap))
        lin = (colour * body + mouth_c * m) * (shown / total if total > 0 else 0)
        d = np.clip(np.array(agx.agx(tuple(float(v) for v in lin))), 0, 1) ** 2.2
        out.append(float(.2126 * d[0] + .7152 * d[1] + .0722 * d[2]))
    return np.array(out)


def ring_measures(prof, n_px=88.6):
    """The disc gate (plume_look_metrics.py): the ring = the profile's maximum in 0.4..0.7 n over its minimum between
    0.2 n and that maximum (a bump, not a shoulder); the hot centre = the centre over the profile's maximum."""
    r = np.arange(len(prof)) * 4 / n_px
    band = np.where((r >= .4) & (r <= .7))[0]
    j = band[np.argmax(prof[band])]
    inner = np.where((r >= .2) & (np.arange(len(prof)) <= j))[0]
    return prof[j] / max(prof[inner].min(), 1e-9), prof[0] / prof.max(), r[j]


if __name__ == '__main__':
    # Derive the revised law's constants: kappa from the energy ratio at the design throttle (s 1, L / n 4), the halo
    # gain from the previous law's disc / side halo energy ratio, the cap as before (1.5 x the side view's axis peak).
    _, h_prev, _, _, _ = side("previous", 1, 4)
    prev_ratio = LAWS["previous"]["halo"] * 4 * hb * disc_halo_energy(LAWS["previous"]["sigma"]) / h_prev
    for law in ("revised", "tuned"):
        LAWS[law].update(kappa=1.0, halo=1.0, cap=1.5)
        eb, h_rev, _, _, _ = side(law, 1, 4)
        kappa = eb / (4.0 * 4 * disc(law, 1, 4)[2])
        halo_gain = prev_ratio * h_rev / (4 * hb * disc_halo_energy(LAWS[law]["sigma"]))
        print(f"# {law} law: kappa = {kappa:.3f}, halo gain for the same halo ratio = {halo_gain:.3f}")
        LAWS[law].update(kappa=round(kappa, 2), halo=round(halo_gain, 2))
    print(f"# previous law: disc halo energy / side halo energy at s 1, L/n 4 = {prev_ratio:.3f} (halo gain 3)")
    for law in ("previous", "revised", "tuned"):
        p = LAWS[law]
        print(f"\n## {law}: kappa {p['kappa']}, halo {p['halo']}, cap {p['cap']}, end-on ring x{p['ring']}, halo sigma {p['sigma']:.3f} n, "
              f"fall {p['fall']}")
        for s, ln in ((1, 4), (.5, 2.25), (0, .5), (1, 8), (.5, 4.5), (1, 2)):
            eb, eh, e, mouth, crest = side(law, s, ln)
            k_here = eb / ((1.2 + 2.8 * s) * ln * disc(law, s, ln)[2])
            ed, pk, _ = disc(law, s, ln)
            row = [f's={s} L/n={ln} kappa={k_here:.3f} side_E={e:.3f} body_E={eb:.3f} mouth/crest={mouth / crest:.3f} '
                   f'end_on_E/side_E={ed / e:.3f} end_on_peak/crest={pk / crest:.3f} facing']
            for f in (1.0, .866, .7, .5, .3, 0.0):
                wd = float(ss(.3, .7, f))
                share = (1 - .5 * wd) * np.sqrt(1 - f * f) + (wd * disc(law, s, ln, f)[0] / e if wd > 0 else 0.0)
                row.append(f'{f}:{share:.2f}')
            print(' '.join(row))
    e_prev = side("previous", 1, 4)[0]
    e_rev, e_tun = side("revised", 1, 4)[0], side("tuned", 1, 4)[0]
    print(f"\n# side body energy revised / previous at s 1, L/n 4: {e_rev / e_prev:.3f}, tuned / previous {e_tun / e_prev:.3f}; axis peak (x I) "
          f"{axis_peak('revised', 1):.3f} / {axis_peak('previous', 1):.3f}")
    print("\n# 02b look image (the capped fighter disc, 88.6 px nozzle, faded 0.5): display-decoded luma every 4 px")
    tints = {"split-red": ((1.0, .15, .15), (1.0, .81, .81)), "argon-blue": ((.14, .71, 1.0), (.27, .90, 1.0))}
    for law in ("previous", "revised", "tuned"):
        for name, (mean, peak) in tints.items():
            lm, lp = agx.luma(mean), agx.luma(peak)
            k = min(1.0, lm / lp) if lp > lm else 1.0
            if law != "previous":
                k = max(k, .75)
            prof = look_image_profile(law, mean, np.array(peak) * k)
            ringv, hot, at = ring_measures(prof)
            print(f"  {law:>8} {name:>10}: " + " ".join(f"{v:.2f}" for v in prof) +
                  f" | ring {ringv:.3f} at {at:.2f} n, centre / max {hot:.3f}, max off-centre / centre {prof[1:].max() / prof[0]:.3f}")
