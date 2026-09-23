#!/usr/bin/env python3
"""Prints the ledger table (docs/verification/gpu-sync-timing.md, "GPU backend A/B fixture") from
summary.json, written by
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_gpu_backend_ab.py
Usage: python3 verification/results/bottle-X3/gpu-backend-ab/table.py [summary.json]"""
import json
import sys
from pathlib import Path

r = json.loads(Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).with_name('summary.json')).read_text())
b = r['bottle']
print(f"passed={r['passed']} exit={r['exit_code']} killed={r.get('killed')} elapsed_s={r.get('elapsed_s')} bottle={b['name']} arch={b['wine_arch']} env={b['environment']} exe={r['executable_sha256'][:12]}")
print(f"empty bracket us: {r['empty_bracket_us']}  renderer_registry={r.get('renderer_registry')}")
print('| workload | size | D3D9 event | D3D11 event | D3D11 timestamp | D3D9 timestamp | ratio D3D9/D3D11 | ratio net of empty | D3D9 submit | D3D11 submit | CPU ratio | pipelined D3D9 / D3D11 | pipelined ratio | D3D11 event/ts |')
print('| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |')
for workload, sizes in r['workloads'].items():
    for size, e in sizes.items():
        c, o = e['cpu_submit_us'], e['pipelined_us']
        print(f"| {workload} | {size} | {e['d3d9_event_us']} (p90 {e['d3d9_event_p90_us']}) | {e['d3d11_event_us']} (p90 {e['d3d11_event_p90_us']}) | {e['d3d11_timestamp_us']} | {e['d3d9_timestamp_us']} | "
              f"{e['ratio_d3d9_over_d3d11']} | {e['ratio_net_of_empty_bracket']} | {c['d3d9']} (p90 {e['cpu_submit_p90_us']['d3d9']}) | {c['d3d11']} (p90 {e['cpu_submit_p90_us']['d3d11']}) | {e['ratio_cpu_submit']} | {o['d3d9']} / {o['d3d11']} | {e['ratio_pipelined']} | {e['d3d11_event_over_timestamp']} |")
print('| workload | size | D3D9 coverage | D3D11 coverage | D3D9 mean r/g/b | D3D11 mean r/g/b | max relative difference | raster overdraw | depth-passing overdraw D3D9 / D3D11 |')
print('| --- | --- | ---: | ---: | --- | --- | ---: | ---: | ---: |')
for workload, sizes in r['workloads'].items():
    for size, e in sizes.items():
        a, b, o = e['readback']['d3d9'], e['readback']['d3d11'], e.get('overdraw', {})
        m = lambda x: '/'.join(f"{x[k]:.3f}" for k in ('mean_r', 'mean_g', 'mean_b'))
        dp = o.get('depth_passing', {})
        print(f"| {workload} | {size} | {a['coverage']:.4f} | {b['coverage']:.4f} | {m(a)} | {m(b)} | {max(e['readback']['relative_difference'].values()):.4f} | {o.get('raster_from_geometry')} | {dp.get('d3d9')} / {dp.get('d3d11')} |")
print('notes:', r.get('notes'))
print('queries:', r.get('queries'))
