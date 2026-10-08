#!/usr/bin/env python3
"""Compact before/after summary of the batched occlusion cull records in this directory (no Wine, no build).

Reads fixture-<backend>.json (the batched pass) and legacy-<backend>.json (the Run137 pass, legacy_cost.exe) and writes
summary.json: per backend the realistic-state functional summary and the DERIVED cost rows (medians over the repeats).
    python3 verification/results/occlusion-cull-batched/summary.py
"""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    out = {}
    for backend in ('wined3d', 'dxvk'):
        entry = {}
        for impl in ('fixture', 'legacy'):
            path = HERE / f'{impl}-{backend}.json'
            if not path.exists():
                continue
            r = json.loads(path.read_text())
            entry[impl] = {
                'passed': r['passed'], 'result': r['result'], 'executable_sha256': r['executable_sha256'],
                'sources': r['sources'], 'derived': r.get('derived', []),
                'real_summary': (r.get('real_summary') or [None])[0],
                'command': r['command'][-2:],
            }
        out[backend] = entry
    (HERE / 'summary.json').write_text(json.dumps(out, indent=1) + '\n')
    for backend, entry in out.items():
        for impl, e in entry.items():
            for d in e['derived']:
                print(f"{backend:8} {impl:8} buffer={d.get('buffer')} per_test_us={d.get('per_test_us')} "
                      f"per_block_us={d.get('per_block_us')} all_test_us={d.get('all_test_us')} "
                      f"all_frame_overhead_us={d.get('all_frame_overhead_us')} cull_test_us={d.get('cull_test_us')} "
                      f"cull_tests={d.get('cull_tests')} cull_frame_delta_us={d.get('cull_frame_delta_us')} "
                      f"all_cpu_overhead_us={d.get('all_cpu_overhead_us')} cull_cpu_delta_us={d.get('cull_cpu_delta_us')}")
            if e['real_summary']:
                s = e['real_summary']
                print(f"{backend:8} {impl:8} realistic: hidden {s['hidden_skips']}/{s['hidden_expected']} visible_skips "
                      f"{s['visible_skips']} exactly3 {s['visible_exactly_3']} per_frame {s['visible_tested_per_frame']} "
                      f"reveal_at {s['reveal_drawn_at']} diff_frames {s['diff_frames']} outside {s['diff_outside']}")
            print(f"{backend:8} {impl:8} passed={e['passed']} {e['result']}")


if __name__ == '__main__':
    main()
