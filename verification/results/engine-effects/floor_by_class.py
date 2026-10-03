"""Per-class effect of the size-dependent plume floor (Mayhem tree): median R, median largest main nozzle before/after, median lift.

At the floor scale SCALE (ini engine_plume_floor; default 0.5 since flight E, 2026-10-03; first argument overrides):
value_eff = min(max(v, SCALE x k(R) x R), 4 v). Run from the repository root: python3 verification/results/engine-effects/floor_by_class.py [scale]."""
import sys, re, numpy as np
sys.path.insert(0, 'verification/results/engine-effects'); sys.path.insert(0, 'tools/effects'); sys.path.insert(0, 'tools/analysis')
import floor_ratio_effects as f
import engine_bodies as eb
SCALE = float(sys.argv[1]) if len(sys.argv) > 1 else 0.5


def effect(R, v):
    return min(max(v, SCALE * f.k_of(R * f.CONTEXT) * R), f.CAP * v)


t = eb.generate(eb.bob1.DEFAULT_GAME, stock_only=False)
assets, _ = eb.load_assets(eb.bob1.DEFAULT_GAME, False)
jets = {k.lower(): v for k, v in t['bodies'].items()}
scenes, rows = f.tships_scenes(assets)
cache, by = {}, {}
for token in scenes:
    s = f.ship(assets, token, jets, cache)
    if not s: continue
    m = re.search(r'_(m[1-8]|ts|tp|tl|tm|goner|ts\d|tp\d)[_a-z]*', token.lower().split('\\')[-1])
    cls = m.group(1).upper() if m else '?'
    R, n = s
    vmax = max(v for _, v in n); vmin = min(v for _, v in n)
    by.setdefault(cls, []).append((R * f.CONTEXT, vmax, effect(R, vmax), vmin, effect(R, vmin)))
print(f'floor scale {SCALE:g}')
print(f"{'class':6}{'ships':>6}{'R med':>9}{'main before':>13}{'main after':>12}{'lift':>6}{'min noz before':>16}{'min noz after':>15}")
for cls in sorted(by, key=lambda c: -np.median([r[0] for r in by[c]])):
    a = np.array(by[cls]); med = np.median(a, axis=0)
    print(f"{cls:6}{len(a):>6}{med[0]:>9.0f}{med[1]:>13.1f}{med[2]:>12.1f}{med[2]/med[1]:>6.2f}{med[3]:>16.1f}{med[4]:>15.1f}")
