#!/usr/bin/env python3
"""Step 0 gate of the occlusion cull: per-part cost of an occlusion-query box test, wined3d and DXVK (bottle X3).

Reads the two backend smoke records (`run_d3d9_backend_smoke.py --occlusion-cost`) and writes gate.json beside this
script. Rows (fixture `occlusion_cost`): sets=3 rows (unpaced, n 128 / 512) carry the CPU breakdown of every variant
(B: 12-triangle box with a per-test dynamic-buffer Lock, D: the production shape); sets=1 rows (n 128..512, paced
13 / 20 ms) issue the production shape alone (screen rectangle at nearest depth in c252/c253, static strip, z write
off, colour mask untouched with blend ZERO/ONE) and carry its CPU cost, lag-1 readiness and the pipeline time per test
(event query to completion, with vs without the tests; `gpu_mask_us_per_test` is the same test with COLORWRITEENABLE 0).

Gate: production CPU cost per test (issue + GetData, game thread) under 5 us on DXVK. Policy (user 2026-10-08): no tuned
budget; every candidate is tested every frame, limited only by the fixed pool (1,024 queries, 512 per frame slot). The
record states the cost per candidate the policy rests on and the per-frame cost at N = 128 and 512, against the saving of
the close views (26 us per hidden draw, 67-91 hidden among 130-190 sub-part draws). "mrt": the protection of the route's
lazy RT1/RT2 (flush = unbind and rebind, mask = COLORWRITEENABLE1/2 0, zeros = every output written 0 under the
ZERO/ONE blend, the production choice).
"""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[3]
SMOKE = ROOT / 'verification/results/bottle-X3/d3d9-backend-smoke'
GATE_US, SAVED_US, HIDDEN = 5.0, 26.0, (67, 91)
KEYS = ('n', 'pace_ms', 'sets', 'part_draw_us_p50', 'test_delta_us_p50', 'rect_delta_us_p50', 'getdata_us_p50',
        'rect_per_part_us', 'frame_us_p50', 'frame_us_p95', 'ready', 'ready_lag2', 'not_ready', 'read', 'wrong',
        'drain_noflush_ms', 'gpu_without_ms', 'gpu_with_ms', 'gpu_us_per_test', 'gpu_query_us_per_test',
        'gpu_alt_us_per_test', 'gpu_keep_color_us_per_test', 'gpu_mask_us_per_test')

out = {'gate_us': GATE_US, 'saved_us_per_hidden_draw': SAVED_US, 'hidden_close_views': HIDDEN, 'backends': {}}
for backend in ('wined3d', 'dxvk'):
    record = json.loads((SMOKE / f'occlusion-cost-{backend}.json').read_text())
    rows = record['report']['occlusion_cost']
    if backend == 'dxvk':
        dxvk_rows_all = rows
    production = [r for r in rows if r['sets'] == 1]
    breakdown = [r for r in rows if r['sets'] == 3]
    out['backends'][backend] = {
        'record': f'verification/results/bottle-X3/d3d9-backend-smoke/occlusion-cost-{backend}.json',
        'passed': record['passed'], 'fixture_sources_sha256': record['sources'], 'd3d9_sha256': record['d3d9_sha256'],
        'rows': [{k: r.get(k) for k in KEYS} for r in rows],
        'lock_variant_cpu_us_per_test_max': max(r['test_delta_us_p50'] + r['getdata_us_p50'] for r in breakdown),
        'production_cpu_us_per_test_max': max(r['frame_us_p50'] / r['n'] for r in production),
        'production_pipeline_us_per_test_max': max(r['gpu_us_per_test'] for r in production),
        'mask_variant_pipeline_us_per_test_max': max(r['gpu_mask_us_per_test'] for r in production),
        'production_lag1_ready_fraction_min': min(r['ready'] / r['read'] for r in production),
        'wrong': sum(r['wrong'] for r in rows),
        # The route's lazy RT1 (A32B32G32R32F) / RT2 (R32F) bound: per protection, CPU and pipeline us per test over 60
        # frames at N = 128 and the RT1/RT2 texels changed against no test (byte compare).
        'mrt_caps': (record['report'].get('occlusion_mrt_caps') or [None])[0],
        'mrt': {r['variant']: {k: r[k] for k in ('cpu_us_per_test', 'pipeline_us_per_test', 'rt1_changed', 'rt2_changed')}
                for r in record['report'].get('occlusion_mrt') or []}}
dxvk = out['backends']['dxvk']
cpu = dxvk['production_cpu_us_per_test_max']
out['gate_passed'] = cpu < GATE_US and dxvk['wrong'] == 0
prod = [r for r in dxvk_rows_all if r['sets'] == 1]
cpu_p50 = sorted(r['frame_us_p50'] / r['n'] for r in prod)[len(prod) // 2]
pipe_p50 = sorted(r['gpu_us_per_test'] for r in prod)[len(prod) // 2]
out['policy'] = {'pool_per_frame': 512, 'budget_setting': None,
                 'cost_per_candidate_us': {'cpu_p50': round(cpu_p50, 2), 'cpu_max': round(cpu, 2), 'pipeline_p50': round(pipe_p50, 2),
                                           'pipeline_max': dxvk['production_pipeline_us_per_test_max']},
                 'per_frame_us': {str(n): {'cpu_p50': max(r['frame_us_p50'] for r in prod if r['n'] == n),
                                           'pipeline': round(n * max(r['gpu_us_per_test'] for r in prod if r['n'] == n), 1)}
                                  for n in (128, 512)},
                 'close_view': {'candidates': (130, 190), 'hidden': HIDDEN,
                                'saving_us': [h * SAVED_US for h in HIDDEN],
                                'cpu_cost_us': [round(c * cpu, 1) for c in (130, 190)]}}
(Path(__file__).with_name('gate.json')).write_text(json.dumps(out, indent=1) + '\n')
print(json.dumps({'gate_passed': out['gate_passed'], 'policy': out['policy'],
                  **{b: {k: v for k, v in out['backends'][b].items() if k.endswith('_max') or k.endswith('_min') or k == 'wrong'}
                     for b in out['backends']},
                  'mrt': {b: out['backends'][b]['mrt'] for b in out['backends']}}, indent=1))
