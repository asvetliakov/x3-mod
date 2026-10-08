#!/usr/bin/env python3
"""Print the figures docs/architecture/d3d9-to-d3d11-translation.md quotes from the smoke records
beside this script (written by verification/probe/run_d3d9_backend_smoke.py): python3 summary.py"""
import json
from pathlib import Path

for path in sorted(Path(__file__).parent.glob('*.json')):
    record = json.loads(path.read_text())
    report = record.get('report', {})
    adapter = report.get('adapter') or {}
    sweep = report.get('sweepsummary') or {}
    draws = {d['name']: d for d in report.get('draws', [])}
    quad = draws.get('quad_vs30_ps30', {})
    print(f"{record['name']}: exit={record.get('exit_code')} killed={record.get('killed')} elapsed_s={record.get('elapsed_s')} "
          f"moltenvk_sha256={(record.get('moltenvk') or {}).get('sha256', '-')[:16]} d3d9_sha256={(record.get('d3d9_sha256') or '-')[:16]}")
    print(f"  adapter={adapter.get('description')} driver={adapter.get('driver')} vendor={adapter.get('vendor')} "
          f"device={adapter.get('device')} device_hr={(report.get('device') or {}).get('hr')} "
          f"create_ms={(report.get('device') or {}).get('create_ms')}")
    print(f"  checks={report.get('result')} failed={report.get('failed_checks')}")
    print(f"  quad means={quad.get('mean_r')},{quad.get('mean_g')},{quad.get('mean_b')} coverage={quad.get('coverage')}")
    for name in ('alpha_test', 'two_samplers_ps11', 'two_samplers_ps30_2d_cube', 'two_stage_fixed_function', 'managed_relock',
                 'stretchrect_rt_scaled', 'stretchrect_backbuffer', 'd3dx_effect'):
        d = draws.get(name)
        if d:
            print(f"  {name}: {d['mean_r']},{d['mean_g']},{d['mean_b']} cov={d['coverage']}")
    print(f"  sweep created={sweep.get('created')}/{sweep.get('programs')} create_failed={sweep.get('create_failed')} "
          f"programs_with_backend_errors={report.get('sweep_error_programs')} "
          f"(vs={sum(e['kind'] == 'vs' for e in report.get('sweep_errors', []))}, "
          f"ps={sum(e['kind'] == 'ps' for e in report.get('sweep_errors', []))})")
    print(f"  first sweep error={report['sweep_errors'][0]['first'] if report.get('sweep_errors') else None}")
    print(f"  queries={[(q.get('type'), q.get('ok', q.get('hr', q.get('supported_hr'))), q.get('pixels')) for q in report.get('queries', [])]}")
    print(f"  resz={[(r.get('format'), r.get('reference'), r.get('mean_r')) for r in report.get('resz', [])]}")
    print(f"  effect={report.get('effect')} present={report.get('present')} stretch_depth={report.get('stretchdepth')}")

# --pipeline-cost records: per step the median and max of draw + event wait (us) over the measured pixel
# shaders, the median ratio to step a, and the median excess over step f (the cached-pipeline baseline)
# relative to step a's excess.
from statistics import median
for path in sorted(Path(__file__).parent.glob('pipeline-cost-*.json')):
    record = json.loads(path.read_text())
    rows = record.get('report', {}).get('pipeline_cost', [])
    if not rows:
        continue
    by_step = {}
    for row in rows:
        by_step.setdefault(row['step'], []).append(row['us'])
    a, f = median(by_step['a']), median(by_step['f'])
    stderr = record['report'].get('pipeline_cost_stderr') or {}
    print(f"{record['name']}: shaders={len(by_step['a'])} wine_env={record.get('wine_env')} elapsed_s={record.get('elapsed_s')} "
          f"stderr_lines_in_steps={stderr.get('lines')} distinct={sorted({l for ls in (stderr.get('first') or {}).values() for l in ls})[:3]}")
    for step in 'abcdefg':
        values = by_step.get(step, [])
        m = median(values)
        print(f"  step={step} median_us={m:.0f} max_us={max(values)} ratio_to_a={m / a:.2f} "
              f"excess_over_f_vs_a={(m - f) / (a - f) if a != f else float('nan'):.2f}")
