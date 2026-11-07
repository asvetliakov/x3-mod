#!/usr/bin/env python3
"""size_derivation.py: the death explosion's sizes, pixel footprints, ember speeds, pre-roll cadence and the
close-capital budget for docs/architecture/explosion-look.md (2026-11-07). Standalone: every input is a constant
below with its mark. [M] radius medians per class from docs/reverse-engineering/game-scale-and-ship-classes.md
section 4 (stock AP; the collision radius [obj+0xa4] / 500) and section 6 (installed station families); hull
lengths from verification/results/weapon-effects-burst/size_derivation_out.txt (stock, hull_len_z / 500);
f = 960 px per metre at 1 m at 1440 rows (vanilla vfov 73.74 deg, docs/verification/field-of-view.md), 720 at
1080 rows; budget weights from docs/verification/weapon-effects.md "Burst body, glow, flash, smoke" (5120x1440,
EVENT-fenced, over the decal's 177.5 us). [A] the viewing distance per tier and the missile radius. [C] the laws.
Prints the per-tier table, the capital and station cadences and the close-capital charge."""
import math

F1440, F1080 = 960.0, 720.0
DECAL_US = 177.5
W_FIREBALL, W_FIREBALL_COARSE, W_SMOKE, W_FLASH, W_GLOW = 1.18, 1.03, 1.20, 0.40, 0.40   # [M]
W_SHOCK = 0.40                                                                             # [I] bound, gate pins it

# tier: (typical R_obj m [M], hull length m [M], viewing distance m [A], f [C], clamp m [C], life L s [C], pre-roll s [C])
TIERS = {
    'fighter':  (14.0,  21.6,   600.0, 2.0, (12.0, 50.0),    2.0, 0.0),   # M4 14 (M5 12, M3 19) stock
    'corvette': (87.0, 130.3,  1500.0, 1.5, (30.0, 160.0),   3.0, 0.0),   # M6 87 stock (TS 50, TP 74, TM 92, M8 36)
    'capital':  (1288.0, 1849.0, 1500.0, 0.9, (80.0, 600.0), 4.0, 1.5),   # M2 1288 (M7 618, M1 1064, TL 1403)
    'station':  (1800.0, 3600.0, 3000.0, 0.6, (200.0, 1800.0), 6.0, 3.0), # FAC_TECH installed median 1800 [M]; length [A] 2 R
    'missile':  (3.0,     6.0,   300.0, 3.5, (6.0, 25.0),    0.8, 0.0),   # [A] radius; clamp carries it
}
SECOND = {'fighter': 0, 'corvette': 0, 'capital': 8, 'station': 12, 'missile': 0}     # [C]
EMBERS = {'fighter': 32, 'corvette': 64, 'capital': 128, 'station': 160, 'missile': 12}  # [C]

def clamp(v, lo, hi): return max(lo, min(hi, v))

print('tier R_obj_m hull_m dist_m R_f_m 2R_f/hull px1440 px1080 hull_px1440 flash_arm_px core_px sec_px v_e_mps '
      'v_e_px_s reach_m reach_over_R_f L_s preroll_s')
for tier, (r_obj, hull, dist, f, (lo, hi), life, pre) in TIERS.items():
    r_f = clamp(f * r_obj, lo, hi)
    px = F1440 * r_f / dist
    arm_px = min(F1440 * 0.4 * r_f / dist, 0.25 * 1440)
    core_px = max(F1440 * 0.08 * r_f / dist, 3.0)
    sec_px = F1440 * 0.35 * r_f / dist if SECOND[tier] else 0.0
    v_e = clamp(1.5 * r_f, 40.0, 300.0)
    reach = v_e * 1.2 * life
    print(f'{tier:8s} {r_obj:7.0f} {hull:6.0f} {dist:6.0f} {r_f:6.0f} {2 * r_f / hull:5.2f} {px:6.0f} {F1080 * r_f / dist:6.0f} '
          f'{F1440 * hull / dist:7.0f} {arm_px:6.0f} {core_px:4.1f} {sec_px:5.0f} {v_e:5.0f} {F1440 * v_e / dist:6.0f} '
          f'{reach:6.0f} {reach / r_f:4.1f} {life:3.1f} {pre:3.1f}')

print('\nfloors (1440 rows): fighter R_f at 2 km', round(F1440 * 28 / 2000, 1), 'px (floor 12); at 1.2 km',
      round(F1440 * 28 / 1200, 1), 'px; missile at 800 m', round(F1440 * 10.5 / 800, 1), 'px')

def cadence(n, total, ratio):
    g0 = total * (1 - ratio) / (1 - ratio ** (n - 1))
    gaps = [g0 * ratio ** i for i in range(n - 1)]
    t, times = 0.0, [0.0]
    for g in gaps:
        t += g; times.append(t)
    return gaps, times

for name, n, total, ratio in (('capital', 8, 1.5, 0.7), ('station', 12, 3.0, 0.8)):
    gaps, times = cadence(n, total, ratio)
    print(f'\n{name} pre-roll: {n} secondaries over {total} s, gap ratio {ratio}: gaps',
          ' '.join(f'{g:.2f}' for g in gaps), '| times', ' '.join(f'{t:.2f}' for t in times))

# close capital: eye at 1.2 R_f from the centre; every box that contains the screen is one screen
r_f = clamp(0.9 * 1288, 80, 600)
eye = 1.2 * r_f
body_px = F1440 * r_f / eye
print(f'\nclose capital: R_f {r_f:.0f} m, eye at {eye:.0f} m, body radius {body_px:.0f} px (> 720: the screen)')
sec_r = 0.5 * r_f                       # the last two secondaries
sec_box = min(1.0, (2 * F1440 * sec_r / (eye + 0.6 * r_f)) ** 2 / (5120 * 1440))
flash_box = (2 * 0.25 * 1440) ** 2 / (5120 * 1440)
charges = {
    'body 3 oct': 1.0 * W_FIREBALL, 'body 2 oct': 1.0 * W_FIREBALL_COARSE, 'shock (first 0.1 s)': 1.0 * W_SHOCK,
    'glow on hulls': 1.0 * W_GLOW, 'two secondaries': 2 * sec_box * W_FIREBALL, 'flash (40 ms)': flash_box * W_FLASH,
    'smoke (from 0.5 L)': 1.0 * W_SMOKE,
}
for k, v in charges.items():
    print(f'  {k:22s} {v:5.2f} screens  {v * DECAL_US:6.0f} us')
full = charges['body 3 oct'] + charges['shock (first 0.1 s)'] + charges['glow on hulls'] + charges['two secondaries'] + charges['flash (40 ms)']
shed1 = full - charges['body 3 oct'] + charges['body 2 oct']
shed3 = charges['body 2 oct'] + charges['glow on hulls'] + charges['flash (40 ms)']
print(f'  sum at t_b + 0.1 s, no shed (smoke not yet): {full * DECAL_US:.0f} us; third octave shed: {shed1 * DECAL_US:.0f} us; '
      f'secondaries and shock shed: {shed3 * DECAL_US:.0f} us; fence 450 us; the 3-screen ceiling {3 * DECAL_US:.0f} us')
print(f'  smoke at 0.5 L adds {charges["smoke (from 0.5 L)"] * DECAL_US:.0f} us: shed first on a close capital')
print(f'  capital at 1.5 km: body box {(2 * F1440 * r_f / 1500) ** 2 / (5120 * 1440):.3f} screens = '
      f'{(2 * F1440 * r_f / 1500) ** 2 / (5120 * 1440) * W_FIREBALL * DECAL_US:.0f} us at three octaves')

# ---------------------------------------------------------------------------------------------------------------
# Section 13 (2026-11-07): the fire ring (the planar heat wave) of capital and station deaths and the composed body.
# [C] the laws; [M] R_obj, hull lengths, f, the weights; [A] the viewing distances, the far case 5 km; [I] the
# estimates marked below (the wave's weights and the composed program's per-pixel factors are pinned by their
# *_alone_fills_frame rows, not by this script).
W_RING, W_SHIMMER = 0.50, 2.85                      # [M] the hull ring and the hull shimmer rect (ledger "Shock front")
W_WAVE_MISS = 0.10                                   # [I] the wave box's early-out pixels; pinned by wave_miss_alone
WAVE_RMAX_X, WAVE_TAU_X, WAVE_W_X, WAVE_H_X, WAVE_L_X = 7.0, 0.25, 0.3, 0.3, 0.4   # [C] r_max / R_f, tau / L, band, slab, life
SCREENS = {'5120x1440': (5120, 1440, F1440), '1920x1080': (1920, 1080, F1080)}

def wave_r(t, r0, rmax, tau):
    x = min(1.0, t / tau)
    return r0 + (rmax - r0) * (1.0 - (1.0 - x) ** 3) + (rmax - r0) * 0.05 * max(0.0, t / tau - 1.0)

def wave_t_at(r_target, r0, rmax, tau):
    if r_target <= r0: return 0.0
    if r_target >= rmax: return None
    return tau * (1.0 - (1.0 - (r_target - r0) / (rmax - r0)) ** (1.0 / 3.0))

print('\n== 13. the fire ring (capital, station): r_0 = the hull half length, r_max = %.0f R_f over tau = %.2f L (cubic ease-out),'
      ' band %.1f R_f, slab %.1f R_f, life %.1f L ==' % (WAVE_RMAX_X, WAVE_TAU_X, WAVE_W_X, WAVE_H_X, WAVE_L_X))
for tier, dists in (('capital', (1500.0, 5000.0)), ('station', (3000.0,))):
    r_obj, hull, _, f, (lo, hi), life, pre = TIERS[tier]
    r_f = clamp(f * r_obj, lo, hi)
    r0, rmax, tau = 0.5 * hull, WAVE_RMAX_X * r_f, WAVE_TAU_X * life
    band, slab, l_w = WAVE_W_X * r_f, WAVE_H_X * r_f, WAVE_L_X * life
    print(f'{tier}: R_f {r_f:.0f} m, r_0 {r0:.0f} m, r_max {rmax:.0f} m, tau {tau:.2f} s, band {band:.0f} m, slab {slab:.0f} m, '
          f'fade by {l_w:.1f} s; speed at birth {3 * (rmax - r0) / tau / 1000:.1f} km/s, mean {(rmax - r0) / tau / 1000:.1f} km/s')
    for dist in dists:
        for name, (w_px, h_px, fpx) in SCREENS.items():
            k = fpx / dist
            t_half_h = wave_t_at(0.5 * h_px / k, r0, rmax, tau)
            t_half_w = wave_t_at(0.5 * w_px / k, r0, rmax, tau)
            # the refraction rect: the ring's box (2 (r + band))^2 against the cap 0.25 of the target
            r_cap = (0.25 * w_px * h_px) ** 0.5 / 2 / k - band
            t_cap = wave_t_at(r_cap, r0, rmax, tau)
            fmt = lambda t: 'never' if t is None else f'{1000 * t:.0f} ms'
            print(f'  at {dist:.0f} m, {name}: band {band * k:.0f} px, slab (edge-on bar) {slab * k:.0f} px, r_0 {r0 * k:.0f} px, '
                  f'r_max {rmax * k:.0f} px; face-on the front passes the half height at {fmt(t_half_h)}, the half width at '
                  f'{fmt(t_half_w)}; the box refraction rect passes the 0.25 cap at r {r_cap:.0f} m, {fmt(t_cap)}')
    # the octagon strips' draw area against the box at a face-on ring of r_px: 8 strips of chord 2 r sin 22.5 deg,
    # depth the sagitta r (1 - cos 22.5 deg) + the band's projection b (the band ahead of the edge, 2 w_e = 0.2 R_f)
    for r_px, b_px in ((1000.0, 128.0), (679.0, 128.0)):
        strips = 8 * (2 * r_px * math.sin(math.pi / 8)) * (r_px * (1 - math.cos(math.pi / 8)) + b_px)
        print(f'  refraction strips at r {r_px:.0f} px, band {b_px:.0f} px: 8 strips {strips / 1e6:.2f} Mpx = '
              f'{strips / (5120 * 1440):.3f} screens ({strips * W_SHIMMER * DECAL_US / (5120 * 1440):.0f} us at 2.85) '
              f'against the box {4 * r_px * r_px / 1e6:.2f} Mpx = {4 * r_px * r_px / (5120 * 1440):.3f} screens; ratio {strips / (4 * r_px * r_px):.2f}')

# the close capital: the eye 720 m from the centre inside the hull's half length (925 m): the ring is born outside the eye
r_obj, hull, _, f, (lo, hi), life, pre = TIERS['capital']
r_f = clamp(f * r_obj, lo, hi); r0, rmax, tau = 0.5 * hull, WAVE_RMAX_X * r_f, WAVE_TAU_X * life
slab, band = WAVE_H_X * r_f, WAVE_W_X * r_f
print('\nclose capital (eye 720 m, in the wave plane: the worst case, the slab seen edge-on from inside):')
for t in (0.0, 0.05, 0.12, 0.3, 0.7, 1.0):
    r = wave_r(t, r0, rmax, tau)
    d = r - 720.0
    bar_px = min(1440.0, F1440 * slab / d)
    share = bar_px / 1440.0
    us = (share * W_RING + 1.0 * W_WAVE_MISS) * DECAL_US
    print(f'  t {t:.2f} s: r {r:.0f} m, the wall {d:.0f} m ahead, {bar_px:.0f} px tall across the width: {share:.2f} screens '
          f'of band x 0.50 + 1 screen of box x 0.10 = {us:.0f} us')
print(f'  the measured worst close-capital frame (P + 700 ms, shed 9) 361.4 us + the wave at 0.7 s {((min(1440, F1440 * slab / (wave_r(0.7, r0, rmax, tau) - 720)) / 1440) * W_RING + W_WAVE_MISS) * DECAL_US:.0f} us'
      f' = {361.4 + ((min(1440, F1440 * slab / (wave_r(0.7, r0, rmax, tau) - 720)) / 1440) * W_RING + W_WAVE_MISS) * DECAL_US:.0f} us [I]; '
      f'the design frame at t_b + 0.1 s without the shock (D14) 373 - 71 = 302 us + the wave {((min(1440, F1440 * slab / (wave_r(0.1, r0, rmax, tau) - 720)) / 1440) * W_RING + W_WAVE_MISS) * DECAL_US:.0f} us')
print(f'  capital at 1.5 km face-on at r 2 R_f: the annulus 2 pi r w / screen = '
      f'{2 * math.pi * (2 * r_f * F1440 / 1500) * (band * F1440 / 1500) / (5120 * 1440):.3f} screens x 0.50 + the clipped box 1 x 0.10: '
      f'{(2 * math.pi * (2 * r_f * F1440 / 1500) * (band * F1440 / 1500) / (5120 * 1440) * W_RING + W_WAVE_MISS) * DECAL_US:.0f} us')

print('\n== 13. the composed body: N sub-bursts of radius 0.45-0.8 R_f, offsets to 0.5 R_f, in one quad (N spheres in the program) ==')
N_SUB = {'fighter': 3, 'corvette': 3, 'capital': 4, 'station': 5, 'missile': 3}   # [C] the typical N (hashed 3-5; big tiers + 1-2 jets)
JETS = {'fighter': 0, 'corvette': 0, 'capital': 1, 'station': 2, 'missile': 0}
W_SUB_HIT, W_SUB_MISS = 1.0, 0.07          # [I] a sphere's noise sample / its miss, in units of the single body's 1.18 weight
AVG_HITS = 1.6                             # [I] spheres covering a lit pixel on average at the tier's distance (offsets 0.5, radii 0.45-0.8)
for tier, (r_obj, hull, dist, f, (lo, hi), life, pre) in TIERS.items():
    r_f = clamp(f * r_obj, lo, hi); k = F1440 / dist
    n, j = N_SUB[tier], JETS[tier]
    extent = (0.5 + 0.8) * r_f
    box_single = (2 * 1.21 * r_f * k) ** 2 / (5120 * 1440) if r_f * k > 320 else (2 * 1.33 * r_f * k) ** 2 / (5120 * 1440)
    box_cluster = min(1.0, (2 * 1.1 * extent * k) ** 2 / (5120 * 1440))
    per_px = AVG_HITS * W_SUB_HIT + (n + j - AVG_HITS) * W_SUB_MISS
    us_single = min(1.0, box_single) * W_FIREBALL * DECAL_US
    us_cluster = box_cluster * per_px * W_FIREBALL * DECAL_US
    print(f'{tier:8s}: sub-burst radii {0.45 * r_f * k:.0f}-{0.8 * r_f * k:.0f} px, offsets to {0.5 * r_f * k:.0f} px, cluster extent {extent * k:.0f} px '
          f'(the single body {r_f * k:.0f} px); N {n} + jets {j} (2:1, {0.8 * r_f * k:.0f} x {0.4 * r_f * k:.0f} px); '
          f'box {box_cluster:.3f} screens (single {min(1.0, box_single):.3f}); per-pixel factor {per_px:.2f}: {us_cluster:.0f} us against {us_single:.0f} us [I]')
print('eye inside the cluster (own death, the close capital), the shared-noise mode forced (one noise sample at the sphere holding the eye,'
      ' the others by chord and lobes only, ~0.15 each [I]):')
for n in (2, 3, 4, 5, 6):
    print(f'  N {n}: 1 screen x (1.18 + {n - 1} x 0.15) x 177.5 = {(1.18 + (n - 1) * 0.15) * DECAL_US:.0f} us; every sphere sampled instead: {n * 1.18 * DECAL_US:.0f} us')
print('slots [I]: the fireball 356 + the sphere table derivation (~40) + the ellipsoid transform (~12) + the union composition and loop (~15) = ~420 rolled;'
      ' unrolled 5 x ~300 = ~1,500 (the plan caps 32,768; the native validator decides)')
