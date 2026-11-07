#!/usr/bin/env python3
"""review_rows.py [summary.json]: the burst review's rows (2026-11-07) from the native record: the octave blend and its
hysteresis, the cluster fade, the ring annulus, the ember lobes, the CPU rows and the alone / every-pixel GPU rows."""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'verification/results/native-windows/weapon-effects/summary.json'
r = json.loads(path.read_text())
m = r['metrics']
checks = dict(r['checks'])
print('passed', r['passed'], 'checks', sum(1 for _, ok in r['checks'] if ok), '/', len(r['checks']))
for g in ('hull_burst_octave_blend_no_jump', 'hull_budget_octave_hysteresis', 'hull_cluster_neighbour_fades',
          'hull_ring_class_no_dark_annulus', 'hull_ember_lobed_remnant', 'hull_20_hits_no_white_sun',
          'hull_glow_follows_burst', 'hull_cpu_64_hits_mixed', 'hull_cpu_ring_1024_visible_64', 'hull_cpu_ring_1024_visible_256'):
    print(g, checks.get(g))
for x in m['class']:
    c = x.get('case')
    if c in ('octave_blend', 'octave_hysteresis', 'cluster_neighbour_fade', 'ember_lobes_median', 'twenty_hits_peak'):
        print(json.dumps(x))
    if c == 'ring_annulus':
        print('ring_annulus', x['class'], 'to_ring', x['glow_to_ring'], 'age', x['age_ms'], 'min/rim', x['min_over_rim'])
    if c == 'glow_follows_burst_median':
        print(json.dumps(x))
for x in m['class_cpu'] + m['hull_cpu']:
    print('cpu', x['case'], x.get('best_us'), x.get('mean_us'), x.get('target_us'))
for x in m['hull_timing'] + m['class_timing']:
    if x.get('width') == 5120 and ('alone' in x.get('case', '') or x.get('case') == 'fighter_200m_20_hits'):
        print('gpu5120', x['case'], x['gpu_us'], round(x['gpu_us'] / 177.5, 3))
