#!/usr/bin/env python3
"""Detached --gpu-sync-timing owner on the bottle's D3D9 device: one Wine run of the
CMake-built fixture, a compact record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_gpu_sync_timing.py --exe <build>/gpu_sync_timing_fixture.exe
The fixture (verification/probe/gpu_sync_timing_fixture.cpp with the production
src/renderer/gpu_sync_timing.cpp, target gpu_sync_timing_fixture) runs with builtin D3D9;
this runner never builds it. It parses the CHECK / SOFTFAIL / SUPPORT / WINDOW / PASS /
STATS / SESSION / SYNC_COST / RESET / RESULT lines and writes <results>/gpu-sync-timing.json
(bottle, source and executable hashes, check counts, the per-window pass medians) beside the
raw <results>/gpu-sync-timing.txt. docs/architecture/engine-frame-time.md, "GPU sync timing";
ledger docs/verification/gpu-sync-timing.md. Never launches the game.
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
DEFAULT_EXE = ROOT / 'build/gpu_sync_timing_fixture.exe'
SOURCES = ('src/renderer/gpu_sync_timing_core.h', 'src/renderer/gpu_sync_timing.h', 'src/renderer/gpu_sync_timing.cpp', 'src/proxy/cpu_state.h',
           'verification/probe/gpu_sync_timing_fixture.cpp', 'verification/probe/run_gpu_sync_timing.py')
EXPECTED_CHECKS = 32  # soft fail 4, attach 2, CPU state 2, 2 phases x 8 (with the repair census), syncs/failures 2, session 1, Reset 2, failed Reset/detach 3
EXPECTED_WINDOWS = {'first': 3, 'after_reset': 1}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(line):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', line):
        try:
            out[key] = int(value) if re.fullmatch(r'-?\d+', value) else float(value)
        except ValueError:
            out[key] = value
    return out


def parse(text):
    """The fixture's report as one dictionary (also the host test's subject)."""
    report = {'checks': [], 'softfail': [], 'windows': [], 'passes': [], 'census': [], 'cpu_state': None, 'support': None, 'stats': None, 'session': None, 'sync_cost': None,
              'reset': None, 'device': None, 'probe': None, 'result': None}
    for line in text.splitlines():
        if line.startswith('CHECK '):
            _, label, verdict = line.split(' ', 2)
            report['checks'].append([label, verdict.strip() == 'PASS'])
        elif line.startswith('SOFTFAIL '):
            report['softfail'].append(fields(line))
        elif line.startswith('WINDOW '):
            report['windows'].append(fields(line))
        elif line.startswith('PASS '):
            report['passes'].append(fields(line))
        elif line.startswith('CENSUS '):
            report['census'].append(fields(line))
        elif line.startswith('DEVICE '):
            report['device'] = line[len('DEVICE '):]
        else:
            for tag, key in (('PROBE ', 'probe'), ('CPUSTATE ', 'cpu_state'), ('SUPPORT ', 'support'), ('STATS ', 'stats'), ('SESSION ', 'session'), ('SYNC_COST ', 'sync_cost'), ('RESET ', 'reset')):
                if line.startswith(tag):
                    report[key] = fields(line)
            if line.startswith('RESULT '):
                report['result'] = dict(fields(line), verdict=line.split()[-1])
    report['check_count'] = len(report['checks'])
    report['failed_checks'] = [label for label, passed in report['checks'] if not passed]
    return report


def accept(report):
    """Raises AssertionError naming the first violated acceptance term."""
    result = report['result']
    assert result and result['verdict'] == 'PASS' and not report['failed_checks'], report['failed_checks']
    assert result['checks'] == report['check_count'] == EXPECTED_CHECKS, (result, report['check_count'])
    phases = {}
    for window in report['windows']:
        phases[window['phase']] = phases.get(window['phase'], 0) + 1
    assert phases == EXPECTED_WINDOWS, phases
    assert len(report['softfail']) == 2 and report['support'] and report['support']['available'] == 1, (report['softfail'], report['support'])


def summary_of(report):
    medians = {}
    for entry in report['passes']:
        medians.setdefault(entry['pass'], []).append(entry['median_us'])
    return {'checks': report['check_count'], 'failed_checks': report['failed_checks'], 'device': report['device'], 'probe': report['probe'],
            'support': report['support'], 'softfail': [(s['kind'], s['reason'], s['available']) for s in report['softfail']],
            'windows': [(w['phase'], w['window'], w['n_frames'], w['dt_median_us']) for w in report['windows']],
            'pass_median_us_per_window': medians, 'census': [(c['phase'], c['window'], c['n'], c['median_ppm'], c['last_pixels'], c['unread']) for c in report['census']], 'stats': report['stats'], 'session': report['session'], 'sync_cost': report['sync_cost'], 'reset': report['reset']}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE, help='The CMake-built gpu_sync_timing_fixture.exe (default build/gpu_sync_timing_fixture.exe)')
    args = parser.parse_args()
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3 and run through verification/probe/wine_lock.py')
    exe = args.exe.resolve()
    if not exe.is_file():
        sys.exit(f'fixture not built: {exe} (cmake --build <dir> --target gpu_sync_timing_fixture)')
    results = bottle.results_dir(ROOT)
    record = {'passed': False, 'game_launched': False, 'bottle': bottle.describe(), 'sources': {s: sha(ROOT / s) for s in SOURCES},
              'executable': str(exe), 'executable_sha256': sha(exe), 'expected_checks': EXPECTED_CHECKS}
    out_path = results / 'gpu-sync-timing.json'
    try:
        command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', '--workdir', str(exe.parent), str(exe)]
        record['command'] = command
        started = time.time()
        run = subprocess.run(command, capture_output=True, text=True, timeout=900, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'))
        record['elapsed_s'] = round(time.time() - started, 1)
        record['exit_code'] = run.returncode
        text_path = results / 'gpu-sync-timing.txt'
        text_path.write_text(run.stdout + ('\n--- stderr (tail) ---\n' + run.stderr[-4000:] if run.stderr else ''))
        record['report_sha256'] = sha(text_path)
        record['report'] = parse(run.stdout)
        assert run.returncode == 0, run.stdout[-2000:]
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
