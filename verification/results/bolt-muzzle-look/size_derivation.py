"""Size derivation for docs/architecture/bolt-muzzle-look.md (2026-11-07).
Inputs [M]: tools/effects/weapon_catalog/{stock_ap,mayhem3}.json (speed, lifetime, range, damage, shots/min, mounts);
the projection law of docs/reverse-engineering/field-of-view.md (vertical FOV 73.74 deg vanilla, 58.72 deg under the
90-deg-on-16:9 remap; px per metre at 1 m = (rows/2)/tan(vfov/2)).
Chosen laws (note section 1): mount tier = the smallest ship class that mounts the weapon (fighter M5/M4/M3, capital
when only M1/M2/M7/TL, else corvette); d_eng = min(600 / 1500 / 4000 m, 0.75 range); w = (3 px d_eng / f(1440, 73.74)) (1 + 0.6 k);
core L tracer 12 w s_v, slug 4 w s_v, orb diameter 1.6 w, wave thickness 0.5 w; s_v = 0.75 + 0.5 min(v/2000, 1);
tail min(v tau, Tmax w), heavy (k >= 0.91) tau 40 ms, Tmax 20 w.
Second mode (--mode reach, the parallel Fable note's law, kept as the fallback of the note's section 8):
w = 1.2 m (1 + 2 k) clamp(reach / 2000 m, 1, range_cap 8), engagement 0.3 reach (fighter and corvette mounts) and
0.7 reach (capital mounts), tracer length 60 ms of flight clamped to 12-60 w.
Prints: f per case; per view the range quantiles per mount tier; per weapon w, L, tail and px at d_eng, at range,
past a chase camera at 60 m; the stream ratio (core + half tail) / (v * refire) for one gun."""
import json, math, statistics as st, sys
def k_of(d): return min(max(math.log10(max(d, 1) / 300) / 2, 0), 1)
def f_px(rows, vfov): return (rows / 2) / math.tan(math.radians(vfov) / 2)
F = f_px(1440, 73.74)
D_ENG = {'fighter': 600.0, 'corvette': 1500.0, 'capital': 4000.0}
def d_eng(tier, rng): return min(D_ENG[tier], 0.75 * rng)
def w_m(tier, k, rng): return 3.0 * d_eng(tier, rng) / F * (1 + 0.6 * k)
def s_v(v): return 0.75 + 0.5 * min(v / 2000.0, 1.0)
REST = {'tracer': 12, 'slug': 4, 'orb': 1.6, 'wave': 0.5}
TAIL = {'tracer': (0.015, 8), 'slug': (0.030, 12), 'orb': (0.010, 4), 'wave': (0, 0)}
FIGHT, CAP = {'M5', 'M4', 'M3'}, {'M1', 'M2', 'M7', 'TL'}
print('f px per m at 1 m: 1440 rows', round(f_px(1440, 73.74)), '/', round(f_px(1440, 58.72)),
      '; 1080 rows', round(f_px(1080, 73.74)), '/', round(f_px(1080, 58.72)), '(vfov 73.74 / 58.72)')
MODE = 'reach' if '--mode' in sys.argv and sys.argv[sys.argv.index('--mode') + 1] == 'reach' else 'mount'
ARGS = [a for i, a in enumerate(sys.argv[1:], 1) if a != '--mode' and sys.argv[i - 1] != '--mode']
if MODE == 'reach':
    def d_eng(tier, rng): return rng * (0.7 if tier == 'capital' else 0.3)
    def w_m(tier, k, rng): return 1.2 * (1 + 2 * k) * min(max(rng / 2000.0, 1.0), 8.0)
print('mode', MODE)
for view in ARGS:
    d = json.load(open(view)); rows = []
    for e in d['weapons']:
        style = e['style'].split('+')[0]
        if style == 'beam' or not e.get('lasers'): continue
        u = set()
        for l in e['lasers']: u |= set((l.get('used_by') or {}).keys())
        tier = 'fighter' if (u & FIGHT or not u) else ('capital' if u <= CAP else 'corvette')
        k = k_of(max(e['damage_shield'] or 0, e['damage_hull'] or 0))
        spm = max(l.get('shots_per_min') or 0 for l in e['lasers'])
        rows.append((e['id'][10:], style, tier, k, e['range_m'], e['speed_mps'], spm / 60))
    print('#', d['view'], len(rows), 'bolt weapons with a laser')
    for tier in D_ENG:
        rg = sorted(r[4] for r in rows if r[2] == tier)
        if rg: print(f'  {tier:8s} n={len(rg):2d} range p10/med/p90 m {rg[len(rg)//10]:.0f} / {st.median(rg):.0f} / {rg[(9*len(rg))//10]:.0f}; d_eng {D_ENG[tier]:.0f} = {D_ENG[tier]/st.median(rg):.2f} of the median')
    ratios = []
    for name, style, tier, k, rng, v, f in rows:
        w = w_m(tier, k, rng); heavy = k >= 0.91
        L = REST[style] * w * (s_v(v) if style in ('tracer', 'slug') else 1)
        if MODE == 'reach' and style == 'tracer': L = min(max(0.06 * v, 12 * w), 60 * w)
        tau, tmax = (0.040, 20) if heavy and style in ('tracer', 'slug') else TAIL[style]
        T = min(v * tau, tmax * w); de = d_eng(tier, rng)
        if f > 0: ratios.append(((L + 0.5 * T) / (v / f), name))
        print(f'   {name:14s} {style:6s} {tier:8s} k={k:.2f} w={w:5.2f} L={L:6.1f} T={T:6.1f} m | d_eng {w*F/de:3.1f} x {L*F/de:5.1f} px'
              f' | range {rng:6.0f} m {w*F/rng:3.1f} x {L*F/rng:5.1f} px | 60 m {w*F/60:5.1f} x {L*F/60:6.1f} px')
    ratios.sort()
    print('  stream ratio top 5:', ', '.join(f'{n} {r:.3f}' for r, n in ratios[-5:][::-1]))
    print(f'  stream ratio (core + half tail) / spacing, one gun: median {st.median(r for r, _ in ratios):.3f}, max {ratios[-1][0]:.3f} ({ratios[-1][1]}), n={len(ratios)}')
