#!/usr/bin/env python3
"""The revised law's energy compensation across the detail level (docs/architecture/engine-exhaust-look-critique.md
section 6, "One law", item 1; the uniform gain Look::revised_gain of the intermediate build e117e960, superseded by the
single law and kept as the record of why: it met the energy gates and whitened the 40 px axis past the hue gate).

Across the detail blend (16 -> 40 px) the body goes from the slab law to the revised law, whose side-view energy is 0.40
of the slab law's at s 1: a plume dimmed per pixel while it grew. The revised body is multiplied by g(s), linear in the
throttle between the two anchors below, so the side view's total energy (body + halo) at detail 1 is TARGET x the slab
law's at the same throttle and L / n (the ring is the same at both levels); the end-on disc follows: its revised body
x g(s) and its soft cap on the compensated side peak (x g(s)), its ring x disc_ring x g(s) (the ring's contrast against
the brighter body), and kappa raised by BOOST so the end-on total (body, halo, ring; soft-capped) at detail 1 is 0.9..1.2
of the slab law's end-on total at the same size. Still look (no turbulence, erosion or pulse), lengths in nozzle widths,
the shader's terms without pixel footprint (plume_end_on_model.py's laws "previous" and "tuned").
Usage: python3 plume_energy_compensation.py; the output is plume_energy_compensation_out.txt.
"""
import numpy as np

import plume_end_on_model as m

TARGET = 0.9          # the side view's total at detail 1 over the slab law's (the fixture gate: 0.8..1.0 per px^2)
ANCHORS = ((0.0, 1.0), (1.0, 4.0))  # (s, L / n): idle (z held to 0.5 for a main jet), full throttle
CHECKS = ((0.5, 2.25), (1.0, 2.0), (1.0, 8.0))
KAPPA_TUNED, HALO_TUNED, RING_TUNED, RING_W = 4.13, 2.82, 3.0, 2.0


def side_parts(law, s, ln):
    eb, eh, e, _, _ = m.side(law, s, ln)
    return eb, eh, e


def gain_at(s, g):
    return g[0] + (g[1] - g[0]) * s


def disc_total(law, s, ln, g=1.0, boost=1.0, ring_gain=1.0, dx=.01):
    """The end-on disc's soft-capped total (the shader's disc branch at detail 0 = "previous" or 1 = "tuned")."""
    i, ih = 1.2 + 2.8 * s, m.hb * (1.2 + 2.8 * s) / 4
    X, Y = np.meshgrid(np.arange(-3, 3, dx), np.arange(-3, 3, dx))
    rho = np.hypot(X, Y)
    mean = m.mean_profile(law, rho, s)
    if law == "previous":
        kappa, halo, sig, ring_mult, ring_w, peak = 1.8, 3.0, .5 * m.halo, 1.0, 1.0, m.axis_peak("previous", s)
    else:
        kappa, halo, sig = KAPPA_TUNED * boost * g, HALO_TUNED, .32 * m.halo
        ring_mult, ring_w, peak = RING_TUNED * ring_gain, RING_W, g * m.axis_peak("tuned", s)
    body = i * kappa * ln * mean
    dn = rho / sig
    hl = ih * halo * ln * np.exp(-dn) * np.clip(2 * (2.25 - dn), 0, 1)
    rr = (rho - .46 * m.bulge) / ring_w
    ring = i * m.ring / 4 * ring_mult * np.exp(-rr * rr / (2 / 240))
    total = body + (ring ** 4 + hl ** 4) ** .25
    cap = 1.5 * i * peak
    shown = cap * (1 - np.exp(-total / cap))
    return shown.sum() * dx * dx, (body.sum() * dx * dx, ring.sum() * dx * dx, hl.sum() * dx * dx)


def disc_profile(tint, head, g, boost, ring_gain, n_px=88.6, ln=4.0, near=.5, linear=True):
    """The fixture's STRUCTURE_DISC / 02b disc (the capped fighter: 88.6 px nozzle, faded 0.5, the disc's level 0.6 / 0.5)
    at detail 1 with the compensation: Rec. 709 luma per radius every px (linear FP16, or AgX display-decoded)."""
    import agx_reference as agx
    i, level, ih, sig = 4.0 * near, max(.6 / near, 1.0), m.hb * near, .32 * m.halo
    s2 = 1 / 240 + .25 / n_px ** 2
    tint, head, white = np.array(tint), np.array(head), np.array([1.0, .97, .9])
    out = []
    for px in range(0, int(.9 * n_px) + 1):
        rho = px / n_px
        B, rgb = 0.0, np.zeros(3)
        for w, t, u in zip(m.wk, m.tk, m.uk):
            radial = rho / w
            b = t * m.profile("tuned", radial, 1.0, m.cell("tuned", u), u, True)
            h = .7 * (1 - m.ss(.05, .3, u)) * np.exp(-(radial / (.45 * m.core)) ** 2)
            hot = head + (tint * (1 - .4 * m.ss(.6, 1, u)) - head) * m.ss(.2, .5, u)
            col = hot + (.5 * tint - hot) * m.ss(.25, .9, radial)
            B += b
            rgb += b * ((1 - h) * col + h * white)
        colour = rgb / B if B > 0 else tint
        body = level * i * KAPPA_TUNED * boost * g * ln * B / 8
        halo_v = level * ih * HALO_TUNED * ln * np.exp(-rho / sig) * np.clip(2 * (2.25 - rho / sig), 0, 1)
        rr = (rho - .46 * m.bulge) / RING_W
        ring_v = level * i * m.ring * RING_TUNED * ring_gain / 4 * np.sqrt((1 / 240) / s2) * np.exp(-rr ** 2 / (2 * s2))
        ring_c = tint + (np.array([1.0, .95, .85]) - tint) * .6
        mm = (ring_v ** 4 + halo_v ** 4) ** .25
        mouth_c = (ring_c * ring_v ** 4 + tint * halo_v ** 4) / max(ring_v ** 4 + halo_v ** 4, 1e-30)
        total = body + mm
        cap = level * 1.5 * i * g * m.axis_peak("tuned", 1.0)
        shown = cap * (1 - np.exp(-total / cap))
        lin = (colour * body + mouth_c * mm) * (shown / total if total > 0 else 0)
        if not linear:
            lin = np.clip(np.array(agx.agx(tuple(float(v) for v in lin))), 0, 1) ** 2.2
        out.append(float(.2126 * lin[0] + .7152 * lin[1] + .0722 * lin[2]))
    return np.array(out)


def ring_gate(prof, n_px=88.6):
    """structure::disc_measures: the maximum in 0.4..0.7 n over the minimum between 0.2 n and it; the centre over the maximum."""
    r = np.arange(len(prof)) / n_px
    band = np.where((r >= .4) & (r <= .7))[0]
    j = band[np.argmax(prof[band])]
    inner = np.where((r >= .2) & (np.arange(len(prof)) <= j))[0]
    return prof[j] / max(prof[inner].min(), 1e-9), prof[0] / prof.max()


if __name__ == '__main__':
    for law in ("tuned",):
        m.LAWS[law].update(kappa=KAPPA_TUNED, halo=HALO_TUNED, cap=1.5)
    print(f"# g(s): the revised body's gain so the side total at detail 1 = {TARGET} x the slab law's (body + halo)")
    g = []
    for s, ln in ANCHORS:
        bp, hp, ep_ = side_parts("previous", s, ln)
        bt, ht, et = side_parts("tuned", s, ln)
        gs = (TARGET * ep_ - ht) / bt
        g.append(gs)
        print(f"s {s} L/n {ln}: slab body {bp:.3f} halo {hp:.3f} total {ep_:.3f}; revised body {bt:.3f} halo {ht:.3f} total {et:.3f} "
              f"(ratio {et / ep_:.3f}); g = {gs:.3f}")
    g = [round(v, 2) for v in g]
    print(f"# g(s) = {g[0]} + ({g[1]} - {g[0]}) s")
    for s, ln in ANCHORS + CHECKS:
        bp, hp, ep_ = side_parts("previous", s, ln)
        bt, ht, et = side_parts("tuned", s, ln)
        gs = gain_at(s, g)
        print(f"  check s {s} L/n {ln}: g {gs:.3f}, compensated side total / slab {(gs * bt + ht) / ep_:.3f} (uncompensated {et / ep_:.3f})")
    print("\n# the end-on disc: soft-capped total at detail 1 over the slab law's (body, ring, halo before the cap); kappa x boost, "
          "the ring x 3 x g (ring_gain g) or x 3 (ring_gain 1)")
    for ring_scaled in (False, True):
        for boost in (1.0, 1.1, 1.2, 1.3, 1.4):
            row = []
            for s, ln in ((1.0, 4.0), (1.0, 2.0), (0.5, 2.25), (0.0, 1.0), (1.0, 8.0)):
                gs = gain_at(s, g)
                prev, pp = disc_total("previous", s, ln)
                comp, cp = disc_total("tuned", s, ln, gs, boost, gs if ring_scaled else 1.0)
                row.append(f"s{s}/Ln{ln} {comp / prev:.3f}")
            print(f"ring x g {ring_scaled!s:>5}, boost {boost:.1f}: " + "  ".join(row))
    s, ln = 1.0, 4.0
    gs = gain_at(s, g)
    prev, pp = disc_total("previous", s, ln)
    unc, up = disc_total("tuned", s, ln)
    print(f"\n# s 1 L/n 4 before the compensation: end-on total tuned / slab {unc / prev:.3f} (body {up[0]:.2f} ring {up[1]:.2f} halo {up[2]:.2f} "
          f"against {pp[0]:.2f} / {pp[1]:.2f} / {pp[2]:.2f})")
    eb, eh, e, _, _ = m.side("tuned", s, 2.0)
    d2, p2 = disc_total("tuned", s, 2.0)
    print(f"# L/n 2 (nozzle 1.0) before: end-on total / side total {d2 / e:.3f}; end-on ring energy / end-on body energy {p2[1] / p2[0]:.3f} "
          f"(the slab law's {disc_total('previous', s, 2.0)[1][1] / disc_total('previous', s, 2.0)[1][0]:.3f})")

    print("\n# the ring gate on the capped fighter disc (detail 1, s 1, L/n 4; FP16 linear / display): ring bump, centre / max")
    tints = {"argon-blue": ((.14, .71, 1.0), (.27, .90, 1.0)), "split-red": ((1.0, .15, .15), (1.0, .81, .81))}
    for ring_gain, boost in ((1.0, 1.0), (1.0, 1.3), (gs, 1.0), (gs, 1.3), (1.0, 0.0)):
        cells = []
        for name, (mean, peak) in tints.items():
            import agx_reference as agx
            lm, lp = agx.luma(mean), agx.luma(peak)
            k = max(min(1.0, lm / lp) if lp > lm else 1.0, .75)
            head = np.array(peak) * k
            gg = gs if boost > 0 else 1.0
            lin = disc_profile(mean, head, gg, boost if boost > 0 else 1.0, ring_gain)
            dsp = disc_profile(mean, head, gg, boost if boost > 0 else 1.0, ring_gain, linear=False)
            (rl, hl), (rd, hd) = ring_gate(lin), ring_gate(dsp)
            cells.append(f"{name} {rl:.3f} / {rd:.3f} (centre {hl:.2f} / {hd:.2f})")
        label = "uncompensated" if boost == 0 else f"ring x 3 x {ring_gain:.2f}, boost {boost:.1f}"
        print(f"  {label}: " + "; ".join(cells))
