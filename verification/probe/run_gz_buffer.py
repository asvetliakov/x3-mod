#!/usr/bin/env python3
"""gz read-ahead buffer fixture against the bottle's real zlib1.dll (zlib 1.2.3).

Builds verification/probe/gz_buffer_fixture.cpp with src/proxy/gz_buffer.cpp,
copies the game's zlib1.dll from the selected bottle (verification/probe/bottle.py,
X3M_FIXTURE_BOTTLE) next to it, runs it under that bottle's Wine and records
gz-buffer-fixture.txt, gz-buffer-fixture-wine.log and gz-buffer-summary.json in
the bottle's results directory. Every case compares the buffered handle with an
unbuffered handle of the same DLL; the timing case reports 10 M 3-byte reads
unbuffered, inside the loading-trace hook envelope, and buffered. Run under verification/probe/wine_lock.py like every other runner.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('src/proxy/gz_buffer.h', 'src/proxy/gz_buffer.cpp', 'src/proxy/cpu_state.h', 'src/proxy/capture.h',
           'verification/probe/gz_buffer_fixture.cpp', 'verification/probe/build_gz_buffer.sh', 'verification/probe/run_gz_buffer.py')
CASE = re.compile(r'^GZ_CASE name=(\S+) capacity=(\d+) ops=(\d+) checks=(\d+) failures=(\d+)\r?$', re.M)
TIMING = re.compile(r'^GZ_TIMING mode=(\w+) calls=(\d+) bytes=(\d+) seconds=([0-9.]+) ns_per_call=([0-9.]+)(?: wall_ms=(\d+))?(?: row_count=\d+ row_bytes=\d+ nesting=\d)?\r?$', re.M)
ZLIB = re.compile(r'^GZ_ZLIB version=(\S+) exports=(\d) rewind=(\d)\r?$', re.M)
STATS = re.compile(r'^GZ_STATS (.*)$', re.M)
FILE_LINE = re.compile(r'^gz_buffer_file (.*)$', re.M)
RESULT = re.compile(r'^GZ BUFFER RESULT checks=(\d+) failures=(\d+)\r?$', re.M)
EXPECTED_CASES = ('big-cap4k', 'big-cap64k', 'big-cap256k-r', 'small', 'empty', 'truncated', 'crc', 'crc-cap64k', 'corrupt',
                  'corrupt-cap64k', 'multiple', 'multiple-crc', 'multiple-crc-bad', 'transparent', 'concat', 'eof-error-paths',
                  'sequential-3byte', 'mixed-handles', 'table-full', 'timing')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(text):
    return {k: int(v) if v.lstrip('-').isdigit() else v for k, v in re.findall(r'(\w+)=(\S+)', text)}


def parse_report(text):
    """Structured view of the fixture's stdout; raises AssertionError on an incomplete or failing report."""
    cases = {m.group(1): {'capacity': int(m.group(2)), 'ops': int(m.group(3)), 'checks': int(m.group(4)), 'failures': int(m.group(5))}
             for m in CASE.finditer(text)}
    timing = {m.group(1): {'calls': int(m.group(2)), 'bytes': int(m.group(3)), 'seconds': float(m.group(4)), 'ns_per_call': float(m.group(5)),
                           'wall_ms': int(m.group(6)) if m.group(6) else None}
              for m in TIMING.finditer(text)}
    zlib = ZLIB.search(text)
    stats = STATS.search(text)
    results = RESULT.findall(text)
    assert len(results) == 1, 'missing or repeated result line'
    checks, fails = (int(v) for v in results[0])
    report = {'zlib_version': zlib.group(1) if zlib else None, 'zlib_exports': bool(zlib and zlib.group(2) == '1'),
              'zlib_rewind': bool(zlib and zlib.group(3) == '1'), 'cases': cases, 'timing': timing,
              'statistics': fields(stats.group(1)) if stats else {}, 'file_lines': len(FILE_LINE.findall(text)),
              'checks': checks, 'failures': fails, 'fail_lines': [l for l in text.splitlines() if l.startswith('FAIL')]}
    missing = [c for c in EXPECTED_CASES if c not in cases]
    assert not missing, 'missing cases: ' + ', '.join(missing)
    assert text.rstrip().endswith(results and f'GZ BUFFER RESULT checks={checks} failures={fails}'), 'result line is not terminal'
    assert fails == 0 and not report['fail_lines'], 'fixture failures: ' + '; '.join(report['fail_lines'][:5])
    assert sum(c['checks'] for c in cases.values()) <= checks, 'case checks exceed the total'
    assert all(c['failures'] == 0 for c in cases.values()), 'case failures'
    assert {'unbuffered', 'hooked', 'buffered'} <= set(timing), 'timing lines missing'
    assert timing['unbuffered']['calls'] == timing['buffered']['calls'] == timing['hooked']['calls'] > 0, 'timing call counts differ'
    report['hook_envelope_ratio'] = timing['hooked']['seconds'] / timing['unbuffered']['seconds'] if timing['unbuffered']['seconds'] > 0 else None
    report['speedup'] = timing['unbuffered']['seconds'] / timing['buffered']['seconds'] if timing['buffered']['seconds'] > 0 else None
    # The light envelope (loading_trace_light.cpp) replaced the CpuCallBoundary one; recorded runs before it have no line.
    if 'light' in timing:
        assert timing['light']['calls'] == timing['unbuffered']['calls'], 'light timing call count differs'
        report['light_envelope_ratio'] = timing['light']['seconds'] / timing['unbuffered']['seconds'] if timing['unbuffered']['seconds'] > 0 else None
        report['light_envelope_ns'] = timing['light']['ns_per_call'] - timing['unbuffered']['ns_per_call']
        report['hooked_envelope_ns'] = timing['hooked']['ns_per_call'] - timing['unbuffered']['ns_per_call']
        assert timing['light']['seconds'] < timing['hooked']['seconds'], 'the light envelope is not cheaper than the CpuCallBoundary one'
    if 'qpc' in timing:
        report['qpc_envelope_ns'] = timing['qpc']['ns_per_call'] - timing['unbuffered']['ns_per_call']
    return report


def main():
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from game_guard import game_running  # real game processes only
    import bottle
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timing-calls', type=int, default=10_000_000, help='3-byte reads per timing pass (default 10 M)')
    parser.add_argument('--timeout', type=int, default=1800)
    args = parser.parse_args()
    results = bottle.results_dir(ROOT)
    build = ROOT / 'verification/probe/build/gz_buffer'
    native = bottle.game_dir() / 'zlib1.dll'
    report = {'passed': False, 'phase': 'building', 'game_launched': False, 'bottle': bottle.describe(),
              'timing_calls': args.timing_calls, 'zlib1_dll': str(native)}
    summary = results / 'gz-buffer-summary.json'
    summary.write_text(json.dumps(report, indent=2) + '\n')
    try:
        running = game_running()
        assert not running, 'X3AP is running: ' + '; '.join(running)
        report['sources'] = {s: sha(ROOT / s) for s in SOURCES}
        assert native.is_file(), f'missing {native}'
        report['zlib1_sha256_before'] = sha(native)
        subprocess.run(['sh', str(ROOT / 'verification/probe/build_gz_buffer.sh')], cwd=ROOT, check=True)
        shutil.copy(native, build / 'zlib1.dll')
        binary = build / 'gz_buffer_fixture.exe'
        report['executable_sha256'] = sha(binary)
        report['zlib1_sha256_copied'] = sha(build / 'zlib1.dll')
        override = 'zlib1=n,b'
        command = [bottle.WINE] + bottle.wine_args() + ['--dll', override, '--workdir', str(build), str(binary), str(args.timing_calls)]
        report['phase'] = 'running'
        with (results / 'gz-buffer-fixture.txt').open('w') as out, (results / 'gz-buffer-fixture-wine.log').open('w') as err:
            run = subprocess.run(command, env=dict(os.environ, WINEDLLOVERRIDES=override), stdout=out, stderr=err, timeout=args.timeout)
        text = (results / 'gz-buffer-fixture.txt').read_text(errors='replace')
        report['exit_code'] = run.returncode
        report['report'] = parse_report(text)
        assert run.returncode == 0, f'exit code {run.returncode}'
        assert report['report']['zlib_version'] == '1.2.3', 'the bundled zlib is expected to be 1.2.3'
        assert sha(native) == report['zlib1_sha256_before'] and sha(binary) == report['executable_sha256'], 'inputs changed during the run'
        assert {s: sha(ROOT / s) for s in SOURCES} == report['sources'], 'sources changed during the run'
        for name in build.glob('gzb_*'):
            name.unlink()
        report['passed'] = True
        report['phase'] = 'complete'
    except (Exception, KeyboardInterrupt) as error:
        report.update(passed=False, phase='failed', error=repr(error))
    finally:
        summary.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({k: v for k, v in report.items() if k != 'sources'}, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
