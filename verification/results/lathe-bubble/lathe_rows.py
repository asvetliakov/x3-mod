"""The lathe bubble's ledger figures (docs/verification/weapon-effects.md "Lathe bubble from the offline profile",
2026-11-12) from the native record: the units check, the placement against the FL 67 walk ellipsoid, the routing, the
GPU rows and the stage / GPU cost of the lathe against the hull decal on the same hits. The frames beside this script are
the fixture's dump-shield/*.png of that run (the Titan's hull lane: its profile, uninflated).
Usage: python3 -I verification/results/lathe-bubble/lathe_rows.py"""
import json
from pathlib import Path

R = Path(__file__).resolve().parents[3]
d = json.loads((R / 'verification/results/native-windows/weapon-effects/summary.json').read_text())
m = d['metrics']
print('passed', d['passed'], 'checks', len(d['checks']), 'failed', d['failed_checks'])
for x in m['lathe']:
    print('LATHE', ' '.join(f'{k}={v}' for k, v in x.items()))
cpu = {x['case']: x for x in m['stage_cpu']}
for case in ('one_capital_64_hits', 'sixteen_capitals_16_hits', 'one_capital_12hps'):
    a, b = cpu[case], cpu[case + '_lathe']
    print(f'STAGE_CPU {case}: decal mean {a["mean_us"]} us ({a["patches"]} draws, {a["cells"]} cells, '
          f'{a["cells_dropped"]} dropped) / lathe mean {b["mean_us"]} us ({b["patches"]} draws, {b["cells"]} cells, '
          f'{b["cells_dropped"]} dropped)')
for x in m['timing']:
    if x['case'].startswith('titan_'):
        print(f'TIMING {x["width"]}x{x["height"]} {x["case"]} gpu_us={x["gpu_us"]} raster_share={x["raster_share"]}')
