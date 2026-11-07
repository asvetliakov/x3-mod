#!/usr/bin/env python3
"""size_derivation.py: the hull burst's size from data (docs/verification/weapon-effects.md "Burst body, glow, flash,
smoke", "Size derivation"). Inputs [M]: ship hull lengths (verification/results/engine-effects-census/ships.csv,
hull_len_z in engine units, 500 per metre: weapon_effects_core.h engine_units_per_metre), weapon ranges
(tools/effects/weapon_catalog/<view>.json range_m = speed x lifetime, by mounting class), the projection at the
launcher's default 58.7155 deg vertical (docs/verification/field-of-view.md; p11 1.7778, the same at 1080 and 1440 rows).
Assumed [A]: the engagement distance per tier (a fraction of the median range), the readability targets.
Prints per tier the median length, range, distance and the energy burst's R(120 ms) in metres and px at both heights,
for the burst law of src/proxy/weapon_effects_core.h (class_puff, r_scale, r_base, r_k, cell_m)."""
import csv, json, math, re, statistics, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
core = (ROOT / 'src/proxy/weapon_effects_core.h').read_text()
num = lambda name: float(re.search(r'\b%s = ([0-9.]+)f?' % name, core).group(1))
cell_m = [float(v.rstrip('f')) for v in re.search(r'cell_m\[patch_tier_count\] = \{([^}]*)\}', core).group(1).split(',')]
r_scale, r_base, r_k = num('r_scale'), num('r_base'), num('r_k')
puff = re.search(r'class_puff\[10\]\[10\] = \{\{([^}]*)\}', core).group(1).split(',')
rb_r, life, life_k, rise = float(puff[0].rstrip('f')), float(puff[3].rstrip('f')), float(puff[4].rstrip('f')), float(puff[7].rstrip('f'))

TIERS = {'fighter': ('M5', 'M4', 'M3'), 'corvette': ('M6', 'M8'), 'capital': ('M7', 'M2', 'M1', 'TL')}
lengths = {t: [] for t in TIERS}
with open(ROOT / 'verification/results/engine-effects-census/ships.csv') as f:
    for row in csv.DictReader(f):
        if row['view'] != 'stock' or not row['hull_len_z']:
            continue
        for t, classes in TIERS.items():
            if row['class'] in classes:
                lengths[t].append(float(row['hull_len_z']) / 500.0)
ranges = {t: [] for t in TIERS}
for view in ('stock_ap',):
    for w in json.loads((ROOT / f'tools/effects/weapon_catalog/{view}.json').read_text())['weapons']:
        if not w.get('range_m'):
            continue
        for t, classes in TIERS.items():
            if any(c in (w.get('used_by') or {}) for c in classes):
                ranges[t].append(float(w['range_m']))
P11 = 1.0 / math.tan(math.radians(58.7155) / 2)
ENGAGE = {'fighter': 0.35, 'corvette': 0.6, 'capital': 0.8}  # [A] the fraction of the median range
K = {'fighter': 0.33, 'corvette': 0.5, 'capital': 0.8}       # [A] the typical calibre per tier
print('tier ships median_len_m ranges median_range_m engage_m r_hit_m R_b_m R120_m R120_px_1440 R120_px_1080 '
      'R_b_over_len')
for i, t in enumerate(TIERS):
    L = statistics.median(lengths[t]) if lengths[t] else float('nan')
    rng = statistics.median(ranges[t]) if ranges[t] else float('nan')
    D = ENGAGE[t] * rng
    k = K[t]
    r_hit = r_scale * cell_m[min(i, 2)] * (r_base + r_k * k)
    R_b = rb_r * r_hit
    Lb = life + life_k * k
    R120 = R_b * (1 - math.exp(-120 / (rise * Lb)))
    px = lambda rows: R120 * 0.5 * rows * P11 / D
    print(f'{t} {len(lengths[t])} {L:.1f} {len(ranges[t])} {rng:.0f} {D:.0f} {r_hit:.2f} {R_b:.2f} {R120:.2f} '
          f'{px(1440):.1f} {px(1080):.1f} {R_b / L:.3f}')
