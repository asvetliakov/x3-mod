#!/usr/bin/env python3
"""Prints the ledger figures of d3d11-interop.json (docs/architecture/d3d11-post-chain-feasibility.md):
modules, devices, sharing per direction and format, dilation timings, HDR colour spaces, outputs, caps.
Usage: python3 verification/results/bottle-X3/d3d11-interop-summary.py [path]"""
import json
import sys
from pathlib import Path

path = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).with_name('d3d11-interop.json'))
record = json.loads(path.read_text())
report = record['report']
print(f"passed={record['passed']} exit={record['exit_code']} elapsed_s={record['elapsed_s']} bottle={record['bottle']['name']} arch={record['bottle']['wine_arch']} env={record['bottle']['environment']} overrides={record['dll_overrides']}")
print(f"result={report['result']} failed_checks={report['failed_checks']}")
print('steps: ' + ' '.join(f"{k}={v}" for k, v in report['step_status'].items()))
for m in report['modules']:
    print(f"module {m.get('name')} loaded={m.get('loaded')} image={m.get('image_size')} file={m.get('file_size')} version={m.get('version')} product={m.get('product')} builtin={m.get('builtin')} markers={m.get('markers')}")
for d in report['devices']:
    print('device ' + ' '.join(f"{k}={v}" for k, v in d.items()))
for a in report['adapters']:
    print('adapter ' + ' '.join(f"{k}={v}" for k, v in a.items()))
print(f"compiler={report['compiler']} plain={report['plain']} keyed_mutex={report['keyed_mutex']}")
for s in report['shaders']:
    print(f"shader {s.get('name')} {s.get('target')} hr={s.get('hr')} bytes={s.get('bytes')} message={s.get('message')}")
for s in report['shares']:
    print(f"share {s.get('dir'):16} {s.get('format'):14} create={s.get('create_hr')} handle={s.get('handle_hr', '-')}/{s.get('handle')} open={s.get('open_hr')} opened={s.get('opened')} exact={s.get('exact')} mismatches={s.get('mismatches')} roundtrip_us={s.get('roundtrip_us')} sync_us={s.get('sync_us')}")
for d in report['dilations']:
    print(f"dilate {d.get('api'):7} {d.get('variant'):17} {d.get('width')}x{d.get('height')} method={d.get('method')} gpu_us={d.get('gpu_us')} cpu_bracket_us={d.get('cpu_bracket_us')} empty_bracket_us={d.get('empty_bracket_us')} mismatches={d.get('mismatches')} status={d.get('status', 'ok')} reason={d.get('reason')}")
for v in report['verifications']:
    print('verify ' + ' '.join(f"{k}={v[k]}" for k in v))
for o in report['outputs']:
    print('output ' + ' '.join(f"{k}={v}" for k, v in o.items()))
for a in report['swapchain_attempts']:
    print(f"swapchain attempt={a.get('attempt')} hr={a.get('hr')}")
for c in report['colour_spaces']:
    print(f"colourspace {c.get('space')} hr={c.get('hr')} support={c.get('support')} present={c.get('present')}")
print(f"hdr={report['hdr']}")
print(f"caps={report['caps']}")
for f in report['formats']:
    print('format ' + ' '.join(f"{k}={v}" for k, v in f.items()))
for n in report['notes']:
    print('note ' + ' '.join(f"{k}={v}" for k, v in n.items()))
