"""Run235 Flight B (weapon_effects=shields): the stage's cost and pressure per weapon_effects_frame row (300 frames).

Prints the distribution of stage_us (the period mean over the frames that drew) and stage_us_max, and the spill,
cells_dropped, hits_dropped and patches figures of the rows whose period drew. Usage: python3 stage_cost.py x3m.log
"""
import re
import sys

rows = []
for line in open(sys.argv[1], errors='replace'):
    if line.startswith('weapon_effects_frame'):
        rows.append({k: float(v) for k, v in re.findall(r'(\w+)=(-?[0-9.]+)(?=\s|$)', line)})
drew = [r for r in rows if r.get('drew', 0) > 0]


def dist(name, rs):
    v = sorted(r.get(name, 0.0) for r in rs)
    if not v:
        return f'{name}: none'
    q = lambda f: v[min(len(v) - 1, int(f * len(v)))]
    return f'{name}: n={len(v)} min={v[0]:.1f} median={q(0.5):.1f} p90={q(0.9):.1f} max={v[-1]:.1f} sum={sum(v):.0f}'


print(f'rows={len(rows)} rows_with_draws={len(drew)}')
for name in ('stage_us', 'stage_us_max', 'patches', 'spills', 'cells_dropped', 'hits_dropped', 'dropped', 'drew', 'hits'):
    print(dist(name, drew))
per_frame = sorted(r['patches'] / r['drew'] for r in drew if r['drew'])
if per_frame:
    print(f'patches_per_drawn_frame: median={per_frame[len(per_frame) // 2]:.2f} max={per_frame[-1]:.2f}')
spill_frame = sorted(r['spills'] / r['drew'] for r in drew if r['drew'])
if spill_frame:
    print(f'spills_per_drawn_frame: median={spill_frame[len(spill_frame) // 2]:.2f} max={spill_frame[-1]:.2f}')
print(f'rows_with_stage_us_max_over_200us={sum(r["stage_us_max"] > 200 for r in drew)}')
