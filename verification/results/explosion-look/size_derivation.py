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
