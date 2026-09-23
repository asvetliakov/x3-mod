#!/usr/bin/env python3
"""D3D11 post-chain feasibility probe on the bottle's D3D9 / D3D11 / DXGI: one Wine run of the
CMake-built fixture, a compact record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_d3d11_interop.py --exe <build>/d3d11_interop_fixture.exe
The fixture (verification/probe/d3d11_interop_fixture.cpp, target d3d11_interop_fixture) runs
with builtin d3d9 (what the proxy forwards to) and the bottle's default d3d11/dxgi; this runner
never builds it. It parses the MODULE / DEVICE / ADAPTER / SHADER / SHARE / KEYEDMUTEX / DILATE /
VERIFY / OUTPUT / SWAPCHAIN / COLORSPACE / HDR / CAPS / FORMAT / STEP / CHECK / RESULT lines and
writes <results>/d3d11-interop.json (bottle, WineArch and environment lines, source and executable
hashes, the parsed report) beside the raw <results>/d3d11-interop.txt.
docs/architecture/d3d11-post-chain-feasibility.md. Never launches the game.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT / 'build/d3d11_interop_fixture.exe'
SOURCES = ('verification/probe/d3d11_interop_fixture.cpp', 'verification/probe/run_d3d11_interop.py')
DEFAULT_OVERRIDES = 'd3d9=b'
LIST_TAGS = {'MODULE': 'modules', 'DEVICE': 'devices', 'ADAPTER': 'adapters', 'SHADER': 'shaders', 'SHARE': 'shares', 'DILATE': 'dilations', 'VERIFY': 'verifications',
             'OUTPUT': 'outputs', 'SWAPCHAIN': 'swapchain_attempts', 'COLORSPACE': 'colour_spaces', 'FORMAT': 'formats', 'STEP': 'steps', 'NOTE': 'notes'}
SINGLE_TAGS = {'COMPILER': 'compiler', 'PLAIN': 'plain', 'KEYEDMUTEX': 'keyed_mutex', 'HDR': 'hdr', 'CAPS': 'caps'}
REQUIRED_STEPS = ('device_d3d9', 'device_d3d9ex', 'device_d3d11', 'share_d3d11_to_d3d9', 'share_d3d9_to_d3d11', 'hdr_outputs', 'hdr_swapchain', 'caps')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


HEX_KEYS = ('hr', 'handle', 'flags', 'luid', 'first_bytes')  # printed as hex: kept as text even when every digit is decimal


def fields(line):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', line):
        if key in HEX_KEYS or key.endswith('_hr'):
            out[key] = value
        elif re.fullmatch(r'-?\d+', value):
            out[key] = int(value)
        elif re.fullmatch(r'-?\d+\.\d+', value):
            out[key] = float(value)
        else:
            out[key] = value
    return out


def parse(text):
    """The fixture's report as one dictionary (also the host test's subject)."""
    report = {key: [] for key in LIST_TAGS.values()}
    report.update({key: None for key in SINGLE_TAGS.values()})
    report.update({'checks': [], 'references': [], 'result': None})
    for line in text.splitlines():
        tag, _, rest = line.partition(' ')
        if tag == 'CHECK':
            label, _, verdict = rest.rpartition(' ')
            report['checks'].append([label, verdict.strip() == 'PASS'])
        elif tag == 'REFERENCE':
            report['references'].append(fields(rest))
        elif tag == 'RESULT':
            report['result'] = dict(fields(rest), verdict=line.split()[-1])
        elif tag == 'STEP':
            name, _, rest = rest.partition(' ')
            report['steps'].append(dict(fields(rest), name=name))
        elif tag in LIST_TAGS:
            report[LIST_TAGS[tag]].append(fields(rest))
        elif tag in SINGLE_TAGS:
            report[SINGLE_TAGS[tag]] = fields(rest)
    report['check_count'] = len(report['checks'])
    report['failed_checks'] = [label for label, passed in report['checks'] if not passed]
    report['step_status'] = {s['name']: s.get('status') for s in report['steps']}
    return report


def accept(report):
    """Raises AssertionError naming the first violated acceptance term. Unavailable steps are
    findings, not failures: the probe passes when it ran every step and every check held."""
    result = report['result']
    assert result and result['verdict'] == 'PASS' and not report['failed_checks'], (result, report['failed_checks'])
    assert result['checks'] == report['check_count'] >= 1, (result, report['check_count'])
    missing = [step for step in REQUIRED_STEPS if step not in report['step_status']]
    assert not missing, missing
    assert any(m.get('name') == 'd3d9.dll' and m.get('loaded') == 1 for m in report['modules']), report['modules']
    d3d9 = [d for d in report['dilations'] if d.get('api') == 'd3d9']
    assert {(d['width'], d['height']) for d in d3d9} == {(1920, 1080), (5120, 1440)}, d3d9


def summary_of(report):
    devices = {d['api']: {k: d[k] for k in d if k not in ('api',)} for d in report['devices']}
    shares = [{k: s.get(k) for k in ('dir', 'format', 'create_hr', 'handle_hr', 'open_hr', 'opened', 'exact', 'mismatches', 'roundtrip_us', 'sync_us')} for s in report['shares']]
    dilations = [{k: d.get(k) for k in ('api', 'variant', 'width', 'height', 'method', 'gpu_us', 'cpu_bracket_us', 'empty_bracket_us', 'mismatches', 'status', 'reason')} for d in report['dilations']]
    return {'checks': report['check_count'], 'failed_checks': report['failed_checks'], 'steps': report['step_status'],
            'modules': [{k: m.get(k) for k in ('name', 'loaded', 'image_size', 'version', 'product', 'builtin', 'markers')} for m in report['modules']],
            'devices': devices, 'compiler': report['compiler'], 'shares': shares, 'keyed_mutex': report['keyed_mutex'], 'dilations': dilations,
            'outputs': report['outputs'], 'swapchain_attempts': report['swapchain_attempts'], 'colour_spaces': report['colour_spaces'], 'hdr': report['hdr'],
            'caps': report['caps'], 'formats': report['formats'], 'result': report['result']}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE, help='The CMake-built d3d11_interop_fixture.exe (default build/d3d11_interop_fixture.exe)')
    parser.add_argument('--dll-overrides', default=DEFAULT_OVERRIDES, help='WINEDLLOVERRIDES for the run (default d3d9=b: builtin d3d9, the bottle default for d3d11/dxgi)')
    args = parser.parse_args()
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3 and run through verification/probe/wine_lock.py')
    exe = args.exe.resolve()
    if not exe.is_file():
        sys.exit(f'fixture not built: {exe} (cmake --build <dir> --target d3d11_interop_fixture)')
    beside = [name for name in ('d3d9.dll', 'd3d11.dll', 'dxgi.dll', 'd3dcompiler_47.dll') if (exe.parent / name).exists()]
    if beside:
        sys.exit(f'{", ".join(beside)} beside the fixture would be loaded before the bottle\'s modules (application directory first); build in an empty directory')
    results = bottle.results_dir(ROOT)
    record = {'passed': False, 'game_launched': False, 'bottle': bottle.describe(), 'sources': {s: sha(ROOT / s) for s in SOURCES},
              'executable': str(exe), 'executable_sha256': sha(exe), 'dll_overrides': args.dll_overrides}
    out_path = results / 'd3d11-interop.json'
    try:
        command = [bottle.WINE, *bottle.wine_args(), '--dll', args.dll_overrides, '--workdir', str(exe.parent), str(exe)]
        record['command'] = command
        started = time.time()
        run = subprocess.run(command, capture_output=True, text=True, timeout=1200, env=dict(os.environ, WINEDLLOVERRIDES=args.dll_overrides))
        record['elapsed_s'] = round(time.time() - started, 1)
        record['exit_code'] = run.returncode
        text_path = results / 'd3d11-interop.txt'
        text_path.write_text(f'# {bottle.label()}\n' + run.stdout + ('\n--- stderr (tail) ---\n' + run.stderr[-6000:] if run.stderr else ''))
        record['report_sha256'] = sha(text_path)
        record['report'] = parse(run.stdout)
        assert run.returncode == 0, run.stdout[-2000:] + run.stderr[-2000:]
        accept(record['report'])
        assert record['sources'] == {s: sha(ROOT / s) for s in SOURCES} and sha(exe) == record['executable_sha256'], 'Provenance changed during run'
        record['passed'] = True
    finally:
        out_path.write_text(json.dumps(record, indent=1) + '\n')
        summary = {k: record.get(k) for k in ('passed', 'exit_code', 'elapsed_s', 'executable_sha256')}
        if 'report' in record:
            summary.update(summary_of(record['report']))
        print(json.dumps(summary, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
