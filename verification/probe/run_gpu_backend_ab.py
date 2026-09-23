#!/usr/bin/env python3
"""GPU backend A/B fixture on the bottle's D3D9 (wined3d) and D3D11 (DXMT): one Wine run of
verification/probe/gpu_backend_ab_fixture.cpp, a compact summary.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_gpu_backend_ab.py
Without --exe the runner builds CMake target gpu_backend_ab_fixture into build/gpu-backend-ab (an
empty directory: no d3d9.dll beside the executable). The fixture runs with builtin d3d9 and the
bottle's default d3d11/dxgi. Writes <results>/gpu-backend-ab/summary.json (bottle, WineArch and
environment lines, source and executable hashes, per workload and size the medians and p90 of
both APIs, their ratios, the readback agreement) beside the raw fixture.txt (local, untracked).
docs/architecture/d3d9-to-d3d11-translation.md. Never launches the game.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import threading
import time
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory

ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = ROOT / 'build/gpu-backend-ab'
TARGET = 'gpu_backend_ab_fixture'
SOURCES = ('verification/probe/gpu_backend_ab_fixture.cpp', 'verification/probe/run_gpu_backend_ab.py')
DEFAULT_OVERRIDES = 'd3d9=b'
# (workload, width, height) the fixture measures on both APIs.
EXPECTED = (('fullscreen_pass', 1920, 1080), ('fullscreen_pass', 5120, 1440), ('scene_460', 1920, 1080), ('scene_460', 5120, 1440), ('scene_460_cpu', 1920, 1080))
FRAMES = 300
AGREEMENT = 0.05  # readback coverage and channel means, relative, between the two APIs
# Absolute readback floors per workload: (minimum coverage, minimum channel mean).
MIN_READBACK = {'fullscreen_pass': (0.999, 0.05), 'scene_460': (0.9, 0.05), 'scene_460_cpu': (0.002, 0.0)}
EXIT_GRACE_S = 30  # the bottle's fixtures sometimes hang at exit after a full PASS


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(line):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', line):
        if key == 'hr' or key.endswith('_hr'):
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
    report = {'modules': [], 'devices': [], 'formats': [], 'queries': [], 'shaders': [], 'measures': [], 'verifies': [], 'occlusions': [], 'notes': [], 'steps': {},
              'checks': [], 'renderer': None, 'adapter': None, 'scene': None, 'result': None}
    lists = {'MODULE': 'modules', 'DEVICE': 'devices', 'FORMAT': 'formats', 'QUERY': 'queries', 'SHADER': 'shaders', 'MEASURE': 'measures', 'VERIFY': 'verifies', 'OCCLUSION': 'occlusions', 'NOTE': 'notes'}
    singles = {'RENDERER': 'renderer', 'ADAPTER': 'adapter', 'SCENE': 'scene'}
    for line in text.splitlines():
        tag, _, rest = line.strip().partition(' ')
        if tag == 'CHECK':
            label, _, verdict = rest.rpartition(' ')
            report['checks'].append([label, verdict == 'PASS'])
        elif tag == 'STEP':
            name, _, rest = rest.partition(' ')
            report['steps'][name] = fields(rest).get('status')
        elif tag == 'RESULT':
            report['result'] = dict(fields(rest), verdict=line.split()[-1])
        elif tag in lists:
            report[lists[tag]].append(fields(rest))
        elif tag in singles:
            report[singles[tag]] = fields(rest)
    report['failed_checks'] = [label for label, passed in report['checks'] if not passed]
    return report


def ratio(a, b):
    return round(a / b, 3) if a and b else None


def relative(a, b):
    return abs(a - b) / max(abs(a), abs(b), 1e-9)


def summarise(report):
    """Per workload and size: both APIs side by side, the ratios, the D3D11 event/timestamp agreement."""
    measures = {(m['api'], m['workload'], m['width'], m['height']): m for m in report['measures']}
    verifies = {(v['api'], v['workload'], v['width'], v['height']): v for v in report['verifies']}
    occlusions = {(o['api'], o['workload'], o['width'], o['height']): o for o in report['occlusions']}
    empty = {api: measures.get((api, 'empty', 0, 0), {}).get('event_us') for api in ('d3d9', 'd3d11')}
    workloads = {}
    for workload, w, h in EXPECTED:
        m9, m11 = measures.get(('d3d9', workload, w, h)), measures.get(('d3d11', workload, w, h))
        if not m9 or not m11:
            continue
        ts11, ts9 = m11.get('timestamp_us') if m11.get('timestamp_samples') else None, m9.get('timestamp_us') if m9.get('timestamp_samples') else None
        net9 = m9['event_us'] - (empty['d3d9'] or 0)
        net11 = m11['event_us'] - (empty['d3d11'] or 0)
        v9, v11 = verifies.get(('d3d9', workload, w, h), {}), verifies.get(('d3d11', workload, w, h), {})
        agreement = {k: round(relative(v9[k], v11[k]), 4) for k in ('coverage', 'mean_r', 'mean_g', 'mean_b') if k in v9 and k in v11}
        workloads.setdefault(workload, {})[f'{w}x{h}'] = {
            'd3d9_event_us': m9['event_us'], 'd3d9_event_p90_us': m9['event_p90_us'],
            'd3d11_event_us': m11['event_us'], 'd3d11_event_p90_us': m11['event_p90_us'],
            'd3d11_timestamp_us': ts11, 'd3d11_timestamp_p90_us': m11.get('timestamp_p90_us') if ts11 else None,
            'd3d9_timestamp_us': ts9, 'd3d9_timestamp_p90_us': m9.get('timestamp_p90_us') if ts9 else None,
            'cpu_submit_us': {'d3d9': m9['submit_us'], 'd3d11': m11['submit_us']},
            'cpu_submit_p90_us': {'d3d9': m9['submit_p90_us'], 'd3d11': m11['submit_p90_us']},
            'pipelined_us': {'d3d9': m9['pipelined_us'], 'd3d11': m11['pipelined_us']},
            'pipelined_submit_us': {'d3d9': m9['pipelined_submit_us'], 'd3d11': m11['pipelined_submit_us']},
            'ratio_d3d9_over_d3d11': ratio(m9['event_us'], m11['event_us']),
            'ratio_pipelined': ratio(m9['pipelined_us'], m11['pipelined_us']),
            'ratio_net_of_empty_bracket': ratio(net9, net11),
            'ratio_cpu_submit': ratio(m9['submit_us'], m11['submit_us']),
            'd3d11_event_over_timestamp': ratio(m11['event_us'], ts11),
            'd3d11_event_net_over_timestamp': ratio(net11, ts11),
            'readback': {'d3d9': {k: v9.get(k) for k in ('coverage', 'mean_r', 'mean_g', 'mean_b')}, 'd3d11': {k: v11.get(k) for k in ('coverage', 'mean_r', 'mean_g', 'mean_b')}, 'relative_difference': agreement},
            'overdraw': {'raster_from_geometry': (report['scene'] or {}).get('raster_overdraw') if workload == 'scene_460' else None,
                         'depth_passing': {api: occlusions.get((api, workload, w, h), {}).get('depth_passing_overdraw') for api in ('d3d9', 'd3d11')}},
            'timeouts': {'d3d9': m9['timeouts'], 'd3d11': m11['timeouts']}}
    return {'empty_bracket_us': empty, 'workloads': workloads,
            'thread_times': 'not summarised: on Wine GetProcessTimes equals GetThreadTimes and ticks at 10 ms, so other-thread CPU (wined3d CS thread, DXMT encoder) is not measurable in-process; raw values stay in fixture.txt'}


def accept(report, summary):
    """Raises AssertionError naming the first violated acceptance term."""
    result = report['result']
    assert result and result['verdict'] == 'PASS' and not report['failed_checks'], (result, report['failed_checks'])
    assert report['steps'].get('d3d9') == 'ok' and report['steps'].get('d3d11') == 'ok', report['steps']
    for workload, w, h in EXPECTED:
        entry = summary['workloads'].get(workload, {}).get(f'{w}x{h}')
        assert entry, ('missing', workload, w, h)
        assert entry['timeouts'] == {'d3d9': 0, 'd3d11': 0}, (workload, w, h, entry['timeouts'])
        assert entry['d3d9_event_us'] > 0 and entry['d3d11_event_us'] > 0 and entry['ratio_pipelined'], (workload, w, h)
        floor_coverage, floor_mean = MIN_READBACK[workload]
        for api in ('d3d9', 'd3d11'):
            got = entry['readback'][api]
            assert (got['coverage'] or 0) >= floor_coverage and min(got['mean_r'] or 0, got['mean_g'] or 0, got['mean_b'] or 0) >= floor_mean, ('readback below its floor', api, workload, w, h, got)
        worst = max(entry['readback']['relative_difference'].values(), default=1.0)
        assert worst <= AGREEMENT, ('readback disagrees between the APIs', workload, w, h, entry['readback'])
    for m in report['measures']:
        assert m['frames'] == FRAMES, m


def run(command, env, timeout_s):
    """Runs the fixture; when it printed RESULT and then does not exit within EXIT_GRACE_S, kills it."""
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
    out, err = [], []
    readers = [threading.Thread(target=lambda s=s, b=b: b.extend(iter(s.readline, '')), daemon=True) for s, b in ((process.stdout, out), (process.stderr, err))]
    for r in readers:
        r.start()
    started, result_seen, killed = time.time(), None, None
    while process.poll() is None:
        time.sleep(0.5)
        if result_seen is None and any(line.startswith('RESULT ') for line in out):
            result_seen = time.time()
        if result_seen and time.time() - result_seen > EXIT_GRACE_S:
            killed = 'exit_hang_after_result'
        elif time.time() - started > timeout_s:
            killed = 'timeout'
        if killed:
            process.kill()
            process.wait()
            break
    for r in readers:
        r.join(10)
    return process.returncode, ''.join(out), ''.join(err), killed


def build():
    if not (BUILD_DIR / 'CMakeCache.txt').exists():
        subprocess.run(['cmake', '-S', str(ROOT), '-B', str(BUILD_DIR), '-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake', '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
                        '-DPython3_EXECUTABLE=/usr/bin/python3'], check=True, cwd=ROOT, capture_output=True, text=True)
    subprocess.run(['cmake', '--build', str(BUILD_DIR), '--target', TARGET], check=True, cwd=ROOT, capture_output=True, text=True)
    return BUILD_DIR / f'{TARGET}.exe'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe', type=Path, help=f'A built {TARGET}.exe (default: build it into build/gpu-backend-ab)')
    parser.add_argument('--dll-overrides', default=DEFAULT_OVERRIDES, help='WINEDLLOVERRIDES for the run (default d3d9=b)')
    args = parser.parse_args()
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3 and run through verification/probe/wine_lock.py')
    exe = (args.exe or build()).resolve()
    if not exe.is_file():
        sys.exit(f'fixture not built: {exe}')
    beside = [name for name in ('d3d9.dll', 'd3d11.dll', 'dxgi.dll', 'd3dcompiler_47.dll') if (exe.parent / name).exists()]
    if beside:
        sys.exit(f'{", ".join(beside)} beside the fixture would be loaded before the bottle\'s modules; build in an empty directory')
    results = bottle.results_dir(ROOT) / 'gpu-backend-ab'
    results.mkdir(parents=True, exist_ok=True)
    record = {'passed': False, 'game_launched': False, 'bottle': bottle.describe(), 'sources': {s: sha(ROOT / s) for s in SOURCES},
              'executable_sha256': sha(exe), 'dll_overrides': args.dll_overrides, 'frames': FRAMES, 'warmup': 30,
              'command_line': 'X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_gpu_backend_ab.py',
              'method': 'per iteration: CPU time, work (submit ends), event query (D3D11 End+Flush), spin GetData (D3D9 FLUSH); timestamp-disjoint pair around the same work; 30 warm-up + 300 measured; medians and p90. '
                        'pipelined: the same work 300 times back to back with a Present-like flush per iteration (D3D9 event Issue + one GetData FLUSH poll, D3D11 Flush) and one drain; total / 300'}
    out_path = results / 'summary.json'
    try:
        command = [bottle.WINE, *bottle.wine_args(), '--dll', args.dll_overrides, '--workdir', str(exe.parent), str(exe)]
        started = time.time()
        code, stdout, stderr, killed = run(command, dict(os.environ, WINEDLLOVERRIDES=args.dll_overrides), 1200)
        record['elapsed_s'] = round(time.time() - started, 1)
        record['exit_code'], record['killed'] = code, killed
        (results / 'fixture.txt').write_text(f'# {bottle.label()}\n' + stdout + ('\n--- stderr (tail) ---\n' + stderr[-6000:] if stderr else ''))
        report = parse(stdout)
        record.update({'renderer_registry': report['renderer'], 'modules': report['modules'], 'devices': report['devices'], 'adapter': report['adapter'],
                       'queries': report['queries'], 'formats': report['formats'], 'notes': report['notes'], 'scene': report['scene'],
                       'shaders': [{k: s.get(k) for k in ('name', 'target', 'hr', 'bytes')} for s in report['shaders']],
                       'checks': len(report['checks']), 'failed_checks': report['failed_checks'], 'result': report['result']})
        summary = summarise(report)
        record.update(summary)
        assert code == 0 or killed == 'exit_hang_after_result', (code, killed, stdout[-2000:], stderr[-2000:])
        accept(report, summary)
        assert record['sources'] == {s: sha(ROOT / s) for s in SOURCES} and sha(exe) == record['executable_sha256'], 'Provenance changed during run'
        record['passed'] = True
    finally:
        out_path.write_text(json.dumps(record, indent=1) + '\n')
        print(json.dumps({k: record.get(k) for k in ('passed', 'exit_code', 'killed', 'elapsed_s', 'failed_checks', 'empty_bracket_us')}, indent=1))
        for workload, sizes in record.get('workloads', {}).items():
            for size, e in sizes.items():
                print(f"{workload:14} {size:9} d3d9_event={e['d3d9_event_us']} d3d11_event={e['d3d11_event_us']} d3d11_ts={e['d3d11_timestamp_us']} ratio={e['ratio_d3d9_over_d3d11']} pipelined={e['pipelined_us']} ratio_pipelined={e['ratio_pipelined']} cpu_submit={e['cpu_submit_us']}")
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
