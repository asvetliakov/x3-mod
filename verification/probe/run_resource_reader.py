#!/usr/bin/env python3
"""Fast resource reader, .dat handle pool and engine-probe fixture against the bottle's real zlib1.dll.

Builds verification/probe/resource_reader_fixture.cpp with the production
core (src/proxy/resource_reader_core.cpp, compiled without SSE as in
CMakeLists.txt), the hook module, the patch arena and the probes, copies the
game's zlib1.dll from the selected bottle (verification/probe/bottle.py,
X3M_FIXTURE_BOTTLE) next to it, runs it under that bottle's Wine and records
resource-reader-fixture.txt, resource-reader-fixture-wine.log and
resource-reader-summary.json in the bottle's results directory. Run under
verification/probe/wine_lock.py like every other runner.
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
SOURCES = ('src/proxy/resource_reader.h', 'src/proxy/resource_reader.cpp', 'src/proxy/resource_reader_core.cpp',
           'src/proxy/engine_patch.h', 'src/proxy/engine_patch.cpp', 'src/proxy/loading_probes.h', 'src/proxy/loading_probes.cpp',
           'src/proxy/loading_trace_light.h', 'src/proxy/loading_trace_light.cpp', 'src/proxy/loading_trace.h',
           'verification/probe/resource_reader_fixture.cpp', 'verification/probe/build_resource_reader.sh',
           'verification/probe/build_loading_light.sh', 'verification/probe/run_resource_reader.py')
CASE = re.compile(r'^RR_CASE name=(\S+)(.*)$', re.M)
FALLBACK = re.compile(r'^RR_FALLBACK case=(\S+) reason=(\S+)\r?$', re.M)
STATS = re.compile(r'^RR_STATS (.*)$', re.M)
TIMING = re.compile(r'^RR_TIMING name=(\w+) bytes=(\d+) rounds=(\d+) fast_us=([0-9.]+) reference_us=([0-9.]+) ratio=([0-9.]+)\r?$', re.M)
ZLIB = re.compile(r'^RR_ZLIB version=(\S+)\r?$', re.M)
RESULT = re.compile(r'^RESOURCE READER RESULT checks=(\d+) failures=(\d+)\r?$', re.M)
EXPECTED_CASES = ('fast', 'fallbacks', 'probes', 'pool')
EXPECTED_FALLBACKS = {'gz_handle': 'gz_handle', 'progress': 'progress', 'transparent': 'not_gzip', 'state_position': 'state',
                      'state_cursor': 'state', 'method': 'method', 'reserved': 'reserved', 'length': 'length',
                      'inflate': 'inflate', 'size': 'size', 'truncated': 'inflate', 'empty': 'empty', 'header': 'header', 'alloc': 'alloc'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(text):
    return {k: int(v) if v.lstrip('-').isdigit() else (float(v) if re.fullmatch(r'-?\d+\.\d+', v) else v)
            for k, v in re.findall(r'(\w+)=(\S+)', text)}


def parse_report(text):
    """Structured view of the fixture's stdout; raises AssertionError on an incomplete or failing report."""
    cases = {m.group(1): fields(m.group(2)) for m in CASE.finditer(text)}
    fallbacks = {m.group(1): m.group(2) for m in FALLBACK.finditer(text)}
    timing = {m.group(1): {'bytes': int(m.group(2)), 'rounds': int(m.group(3)), 'fast_us': float(m.group(4)),
                           'reference_us': float(m.group(5)), 'ratio': float(m.group(6))} for m in TIMING.finditer(text)}
    zlib = ZLIB.search(text)
    stats = STATS.search(text)
    results = RESULT.findall(text)
    assert len(results) == 1, 'missing or repeated result line'
    checks, fails = (int(v) for v in results[0])
    report = {'zlib_version': zlib.group(1) if zlib else None, 'cases': cases, 'fallbacks': fallbacks,
              'statistics': fields(stats.group(1)) if stats else {},
              'timing': timing,
              'checks': checks, 'failures': fails, 'fail_lines': [l for l in text.splitlines() if l.startswith('FAIL')]}
    missing = [c for c in EXPECTED_CASES if c not in cases]
    assert not missing, 'missing cases: ' + ', '.join(missing)
    assert text.rstrip().endswith(f'RESOURCE READER RESULT checks={checks} failures={fails}'), 'result line is not terminal'
    assert fails == 0 and not report['fail_lines'], 'fixture failures: ' + '; '.join(report['fail_lines'][:5])
    for case, reason in EXPECTED_FALLBACKS.items():
        assert fallbacks.get(case) == reason, f'fallback {case}: expected {reason}, got {fallbacks.get(case)}'
    assert {'large', 'medium'} <= set(timing), 'timing lines missing'
    assert report['statistics'].get('verify_mismatched') == 0, 'verify mismatches'
    return report


def main():
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from game_guard import game_running  # real game processes only
    import bottle
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--quick', action='store_true', help='300 KB large file and 2 timing rounds instead of 3 MB and 5')
    parser.add_argument('--timeout', type=int, default=900)
    args = parser.parse_args()
    results = bottle.results_dir(ROOT)
    build = ROOT / 'verification/probe/build/resource_reader'
    native = bottle.game_dir() / 'zlib1.dll'
    report = {'passed': False, 'phase': 'building', 'game_launched': False, 'bottle': bottle.describe(), 'quick': args.quick,
              'zlib1_dll': str(native)}
    summary = results / 'resource-reader-summary.json'
    summary.write_text(json.dumps(report, indent=2) + '\n')
    try:
        running = game_running()
        assert not running, 'X3AP is running: ' + '; '.join(running)
        report['sources'] = {s: sha(ROOT / s) for s in SOURCES}
        assert native.is_file(), f'missing {native}'
        report['zlib1_sha256_before'] = sha(native)
        subprocess.run(['sh', str(ROOT / 'verification/probe/build_resource_reader.sh')], cwd=ROOT, check=True)
        shutil.copy(native, build / 'zlib1.dll')
        binary = build / 'resource_reader_fixture.exe'
        report['executable_sha256'] = sha(binary)
        report['zlib1_sha256_copied'] = sha(build / 'zlib1.dll')
        override = 'zlib1=n,b'
        command = [bottle.WINE] + bottle.wine_args() + ['--dll', override, '--workdir', str(build), str(binary)] + (['quick'] if args.quick else [])
        report['phase'] = 'running'
        with (results / 'resource-reader-fixture.txt').open('w') as out, (results / 'resource-reader-fixture-wine.log').open('w') as err:
            run = subprocess.run(command, env=dict(os.environ, WINEDLLOVERRIDES=override), stdout=out, stderr=err, timeout=args.timeout)
        text = (results / 'resource-reader-fixture.txt').read_text(errors='replace')
        report['exit_code'] = run.returncode
        report['report'] = parse_report(text)
        assert run.returncode == 0, f'exit code {run.returncode}'
        assert report['report']['zlib_version'] == '1.2.3', 'the bundled zlib is expected to be 1.2.3'
        assert sha(native) == report['zlib1_sha256_before'] and sha(binary) == report['executable_sha256'], 'inputs changed during the run'
        assert {s: sha(ROOT / s) for s in SOURCES} == report['sources'], 'sources changed during the run'
        for name in build.glob('rr_*'):
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
