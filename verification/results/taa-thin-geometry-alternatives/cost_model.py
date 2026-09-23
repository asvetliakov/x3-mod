#!/usr/bin/env python3
"""Expected saving per thin-geometry option (docs/architecture/taa-thin-geometry-alternatives.md).

Measured inputs: Run 280 medians net of the 0.264 ms floor (gpu-sync-timing.md "Run 280"; busy still:
mask 1.87, box 0.92, resolve 1.82 ms; fogged still: 0.97 / 0.57 / 1.01), the mask bench per draw
(engine-frame-time.md "Mask draws": tests 0.27 sky / 0.35 hull, x 0.135, y 0.11 after the cuts, fixed cost
per full-screen draw 0.035-0.04, 16-byte full-screen read 0.1 ms per 2 Mpx), taa_copy 0.19 net, and the
high-resolution note's S4 / S5 figures (taa-high-resolution.md section 2). Everything printed is inferred
arithmetic on those inputs; nothing here was flown.
"""
PX_1080 = 1920 * 1080
SCALE_HI = 5120 * 1440 / PX_1080   # 3.556 linear
SCALE_LO = 1138 / 414              # 2.75 measured dilation-chain ratio (d3d11-post-chain-feasibility.md)
FIXED = 0.04
BENCH = {'tests': 0.35, 'x': 0.135, 'y': 0.11}          # ms per draw, hull scene, after the shipped cuts
bench_chain = sum(BENCH.values()) + 3 * FIXED           # 0.715 (the bench's own chain figure is 0.84 hull)
mask_game = {'busy': 1.87, 'fogged': 0.97}
copy_net = 0.19
resolve_game = {'busy': 1.82, 'fogged': 1.01}
box_game = {'busy': 0.92, 'fogged': 0.57}

def span(lo, hi):
    return f"{lo:.2f}-{hi:.2f}"

def at_5k(lo, hi):
    return f"{lo * SCALE_LO:.1f}-{hi * SCALE_HI:.1f}"

print("bench chain (tests + x + y + 3 fixed):", f"{bench_chain:.2f} ms; in-game busy mask {mask_game['busy']:.2f} -> unattributed excess ratio {mask_game['busy'] / 0.84:.2f}x over the bench chain 0.84")
rows = []
# A': drop the x and y draws, propagate region and closure through the age target inside the resolve.
xy_bench = BENCH['x'] + BENCH['y'] + 2 * FIXED
xy_scaled = xy_bench * (mask_game['busy'] / 0.84)      # if the busy excess is proportional over the three draws
resolve_delta = 0.05                                    # +8 B/px age lane (0.05 ms) and ~15 slots of ALU
a_lo, a_hi = xy_bench - resolve_delta, xy_scaled - resolve_delta
rows.append(("A' temporal propagation (drop x, y)", a_lo, a_hi))
# C: S4 half-res box and S5 half-res dilations (taa-high-resolution.md); S5 is moot beside A'.
rows.append(("C  S4 half-res box", 0.40, 0.50))
rows.append(("C  S5 half-res dilations (alone)", 0.10, 0.10))
# E: fold the box columns into the resolve after S3 (note: net about zero; here the columns' write + fixed).
rows.append(("E  fold box columns into resolve", 0.05, 0.12))
# B: per-draw tag replaces only the emissive vote's taps; the tests draw stays for modelled struts.
rows.append(("B  per-draw tag (vote only)", 0.00, 0.04))
# G: mask chain every other frame (copy draw returns on skipped frames).
g_lo, g_hi = (0.84 - copy_net) / 2, (mask_game['busy'] - copy_net) / 2
rows.append(("G  mask cadence 1/2", g_lo, g_hi))
# D: re-render tagged draws: adds draws; CPU per draw on wined3d about 4.8 us (gpu-backend A/B).
rows.append(("D  coverage re-render", -0.30, -0.10))
print(f"\n{'option':40s} {'1080p ms':>12s} {'5120x1440 ms':>14s}")
for name, lo, hi in rows:
    print(f"{name:40s} {span(lo, hi):>12s} {at_5k(lo, hi):>14s}")
print("\nA' + S4 combined at 1080p busy:", span(a_lo + 0.40, a_hi + 0.50), "of the stage's 4.78 net;",
      "at 5120x1440", at_5k(a_lo + 0.40, a_hi + 0.50))
print("D per-draw CPU: 448 draws x 20 % tagged x 4.8 us =", f"{448 * 0.2 * 4.8e-3:.2f} ms per frame on the submitting path")
