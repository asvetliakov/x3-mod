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
import math
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
           'verification/probe/build_loading_light.sh', 'verification/probe/run_resource_reader.py',
           'src/proxy/capture.h', 'src/proxy/cpu_state.h', 'src/proxy/object_trace.h',
           'verification/probe/bottle.py', 'verification/probe/game_guard.py', 'verification/probe/wine_lock.py',
           'verification/analysis/test_resource_reader.py')
CASE = re.compile(r'^RR_CASE name=(\S+)(.*)$', re.M)
FALLBACK = re.compile(r'^RR_FALLBACK case=(\S+) reason=(\S+)\r?$', re.M)
STATS = re.compile(r'^RR_STATS (.*)$', re.M)
TIMING = re.compile(r'^RR_TIMING name=(\w+) bytes=(\d+) rounds=(\d+) fast_us=([0-9.]+) reference_us=([0-9.]+) ratio=([0-9.]+)\r?$', re.M)
PHASES = re.compile(r'^RR_PHASES name=(\w+) (.*)$', re.M)
CURSOR_STATS = re.compile(r'^RR_STATS_CURSOR (.*)$', re.M)
ZLIB = re.compile(r'^RR_ZLIB version=(\S+)\r?$', re.M)
RESULT = re.compile(r'^RESOURCE READER RESULT checks=(\d+) failures=(\d+)\r?$', re.M)
EXPECTED_CASES = ('fast', 'cursor', 'fallbacks', 'probes', 'pool', 'rewind_failure', 'error_abi')
EXPECTED_FALLBACKS = {'gz_handle': 'gz_handle', 'progress': 'progress', 'transparent': 'not_gzip', 'state_position': 'state',
                      'state_cursor': 'state', 'method': 'method', 'reserved': 'reserved', 'length': 'length',
                      'inflate': 'inflate', 'size': 'size', 'truncated': 'inflate', 'empty': 'empty', 'header': 'header', 'alloc': 'alloc'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(text):
    result = {}
    for token in text.split():
        match = re.fullmatch(r'(\w+)=(\S+)', token)
        assert match, f'malformed scalar: {token}'
        key, value = match.groups()
        assert key not in result, f'duplicate scalar: {key}'
        if re.fullmatch(r'-?\d+', value):
            value = int(value)
        elif re.fullmatch(r'-?\d+\.\d+', value):
            value = float(value)
            assert math.isfinite(value), f'nonfinite scalar: {key}'
        result[key] = value
    return result


def parse_report(text, expected_checks=None):
    """Strict inventory for this fixture revision, including the authored mismatch."""
    records = {}
    for line in text.splitlines():
        prefix = next((p for p in ('RR_CASE ', 'RR_FALLBACK ', 'RR_STATS ', 'RR_STATS_CURSOR ',
                      'RR_TIMING ', 'RR_PHASES ', 'RR_ZLIB ', 'resource_reader verify ') if line.startswith(p)), None)
        if not prefix:
            continue
        row = fields(line[len(prefix):])
        name = row.get('name') if prefix in ('RR_CASE ', 'RR_TIMING ', 'RR_PHASES ') else row.get('case') if prefix == 'RR_FALLBACK ' else ''
        assert name is not None, f'missing record identity: {line}'
        key = (prefix, name)
        assert key not in records, f'duplicate record: {key}'
        records[key] = row
    def rows(prefix):
        return {name: row for (kind, name), row in records.items() if kind == prefix}
    cases = rows('RR_CASE ')
    fallbacks = {name: row.get('reason') for name, row in rows('RR_FALLBACK ').items()}
    timing, phases = rows('RR_TIMING '), rows('RR_PHASES ')
    stats = records.get(('RR_STATS ', ''), {})
    cursor_stats = records.get(('RR_STATS_CURSOR ', ''), {})
    zlib = records.get(('RR_ZLIB ', ''), {}).get('version')
    results = RESULT.findall(text)
    assert len(results) == 1, 'missing or repeated result line'
    checks, fails = (int(v) for v in results[0])
    if expected_checks is not None:
        assert checks == expected_checks, f'check inventory: expected {expected_checks}, got {checks}'
    assert text.rstrip().endswith(f'RESOURCE READER RESULT checks={checks} failures={fails}'), 'result line is not terminal'
    fail_lines = [line for line in text.splitlines() if line.startswith('FAIL')]
    assert checks > 0 and fails == 0 and not fail_lines, 'fixture failure or empty check inventory'
    assert set(cases) == set(EXPECTED_CASES), 'case inventory mismatch'
    assert fallbacks == EXPECTED_FALLBACKS, 'fallback inventory mismatch'
    assert zlib == '1.2.3', 'fixture runtime version mismatch'
    assert cases['fast'] == dict(name='fast', loose_handled=10, record_handled=20, sources=11), 'fast source inventory'
    assert cases['probes'] == dict(name='probes', installed=4), 'probe install inventory'
    assert stats.get('mode') == 'verify', 'verify mode witness'
    assert cases['cursor'] == dict(name='cursor', sources=20, class_records=16, loose_and_record_handled=20, last_record_class=1), 'cursor source inventory'
    assert cursor_stats == dict(verify_files=40, cursor_short=32, verify_equal=60, verify_mismatched=1), 'cursor verify inventory'
    for key, value in dict(calls=22, handled=20, fallbacks=2, verify_files=20, verify_equal=20, verify_mismatched=0).items():
        assert stats.get(key) == value, f'verify inventory: {key}'
    assert cases['rewind_failure'] == dict(name='rewind_failure', modes=2, allocations_freed=2, entry_restored=2, bookkeeping_unchanged=2, original_retries=2), 'rewind fault inventory'
    assert cases['error_abi'] == dict(name='error_abi', incoming=0x31415926, original_input=0x31415926, outgoing=0x16180339, returned=0x16180339), 'LastError witness'
    mismatch = records.get(('resource_reader verify ', ''), {})
    expected = dict(equal=0, size=31000, original_size=31000, mismatches=1, first=0, original_null=0,
                    globals_ok=1, counters_ok=1, cursor_ok=1, position_ok=1, catalogue=1, scrambled=1)
    for key, value in expected.items():
        assert mismatch.get(key) == value, f'authored diagnostic: {key}'
    assert set(mismatch) == set(expected) | {'cursor', 'expected_cursor', 'position', 'expected_position', 'our_us', 'original_us'}, 'diagnostic scalar inventory'
    for key in ('cursor', 'position'):
        assert isinstance(mismatch[key], int) and mismatch[key] > 1 and mismatch[key] == mismatch['expected_' + key], f'diagnostic {key}'
    for key in ('our_us', 'original_us'):
        assert isinstance(mismatch[key], (int, float)) and math.isfinite(mismatch[key]) and mismatch[key] >= 0, f'diagnostic {key}'
    assert set(timing) == set(phases) == {'large', 'medium', 'large_record'}, 'timing/phase inventory'
    for name in timing:
        for key in ('bytes', 'rounds', 'fast_us', 'reference_us', 'ratio'):
            assert isinstance(timing[name].get(key), (int, float)) and timing[name][key] > 0, f'timing {name}/{key}'
        for key in ('extent', 'read_us', 'scan_us', 'alloc_us', 'inflate_us', 'total_us'):
            assert isinstance(phases[name].get(key), (int, float)) and math.isfinite(phases[name][key]) and phases[name][key] >= 0, f'phase {name}/{key}'
    return dict(zlib_version=zlib, cases=cases, fallbacks=fallbacks, statistics=stats, timing=timing,
                phases=phases, cursor_statistics=cursor_stats, mismatch=mismatch,
                checks=checks, failures=fails, fail_lines=fail_lines)


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
              'zlib1_dll': str(native), 'verification_schema': 2,
              'expected_checks': None if args.quick else 4721,
              'terminal_inventory': 'quick-structural-only' if args.quick else 'exact-full'}
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
        summary.write_text(json.dumps(report, indent=2) + '\n')
        with (results / 'resource-reader-fixture.txt').open('w') as out, (results / 'resource-reader-fixture-wine.log').open('w') as err:
            run = subprocess.run(command, env=dict(os.environ, X3M_CAMERA='vanilla', X3M_CHASE_SCENE_FIX='0', X3M_CHASE_COMBAT_TIGHTNESS='0', WINEDLLOVERRIDES=override), stdout=out, stderr=err, timeout=args.timeout)
        report['stdout_sha256'] = sha(results / 'resource-reader-fixture.txt')
        report['wine_log_sha256'] = sha(results / 'resource-reader-fixture-wine.log')
        text = (results / 'resource-reader-fixture.txt').read_text(errors='replace')
        report['exit_code'] = run.returncode
        report['report'] = parse_report(text, expected_checks=report['expected_checks'])
        assert run.returncode == 0, f'exit code {run.returncode}'
        assert report['report']['zlib_version'] == '1.2.3', 'the bundled zlib is expected to be 1.2.3'
        assert sha(native) == report['zlib1_sha256_before'] and sha(binary) == report['executable_sha256'], 'inputs changed during the run'
        report['sources_after'] = {s: sha(ROOT / s) for s in SOURCES}
        report['zlib1_sha256_copied_after'] = sha(build / 'zlib1.dll')
        report['executable_sha256_after'] = sha(binary)
        assert report['sources_after'] == report['sources'], 'sources changed during the run'
        assert report['zlib1_sha256_copied_after'] == report['zlib1_sha256_copied'] == report['zlib1_sha256_before'], 'copied runtime changed'
        assert sha(results / 'resource-reader-fixture.txt') == report['stdout_sha256'], 'stdout changed during parsing'
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
