#!/usr/bin/env python3
"""The engine-side skip's fixture evidence (docs/verification/occlusion-cull.md, 2026-10-08 "Engine-side skip"):
the ENGINESUMMARY fields and the per-frame marks of part 1b from the two tracked records.

    python3 verification/results/occlusion-cull-batched/engine_summary.py
"""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
for backend in ('dxvk', 'wined3d'):
    record = json.loads((HERE / f'fixture-{backend}.json').read_text())
    engine = record['engine_summary'][0]
    print(f'{backend}: passed={record["passed"]} checks={record["result"]["checks"]} failed={record["result"]["failed"]} '
          f'elapsed_s={record["elapsed_s"]}')
    print('  ' + ' '.join(f'{k}={v}' for k, v in engine.items()))
    print('  marks per frame (hidden1 hidden2 hidden3 visible1 visible2 front mover teleport alpha; E engine, s proxy, D drawn):')
    print('  ' + ' '.join(f'{f["frame"]}:{f["marks"]}' for f in record['engine_frames']))
