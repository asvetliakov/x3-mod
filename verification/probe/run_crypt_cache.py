#!/usr/bin/env python3
"""CryptoAPI context/key cache fixture against the bottle's real ADVAPI32/rsaenh.

Builds verification/probe/crypt_cache_fixture.cpp with src/proxy/crypt_cache.cpp,
runs it under the selected bottle's Wine (verification/probe/bottle.py,
X3M_FIXTURE_BOTTLE) and records crypt-cache-fixture.txt, crypt-cache-fixture-wine.log
and crypt-cache-summary.json in the bottle's results directory. The fixture runs
the game's script-signature CryptoAPI sequence N times with the cache off and N
times with it on and requires call-by-call identical results and last errors, no
handle leak and the container left absent; the runner adds the timing ratio.
Run under verification/probe/wine_lock.py like every other runner.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import math
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('src/proxy/crypt_cache.h', 'src/proxy/crypt_cache.cpp',
           'verification/probe/crypt_cache_fixture.cpp', 'verification/probe/crypt_cache_controls.cpp',
           'verification/probe/crypt_cache_hook_fixture.cpp', 'verification/probe/build_crypt_cache.sh',
           'verification/probe/build_crypt_cache_hook.sh', 'verification/probe/build_loading_light.sh',
           'verification/probe/build_admission_dependencies.sh', 'verification/probe/loading_admission_witness.h',
           'verification/probe/run_crypt_cache.py', 'verification/probe/bottle.py', 'verification/probe/game_guard.py')
# Installer fixture links the production loading/ownership graph. Bind all its
# checked-in implementation/header inputs, not just the five core-cache files.
SOURCES = tuple(sorted(set(SOURCES) | {str(p.relative_to(ROOT)) for folder in ('src/proxy', 'src/ownership')
                                     for p in (ROOT / folder).iterdir() if p.suffix in ('.cpp', '.h')}))
HOOK_MODES = {'complete': 36, 'bytes': 26, **{f'missing{i}': 26 for i in range(4)},
              **{f'patch{i}': 24 + 2*i for i in range(1, 5)}}
KEY = re.compile(r'^CRYPT_KEY bits=(\d+) blob_bytes=(\d+) signature_bytes=(\d+) expected_verified=(\d+) provider="([^"]*)" container=(\S+)\r?$', re.M)
MODE = re.compile(r'^CRYPT_MODE mode=(\w+) (.*)$', re.M)
STATS = re.compile(r'^CRYPT_STATS (.*)$', re.M)
COMPARE = re.compile(r'^CRYPT_COMPARE (.*)$', re.M)
SHUTDOWN = re.compile(r'^CRYPT_SHUTDOWN (.*)$', re.M)
LEAK = re.compile(r'^CRYPT_LEAK live_providers=(-?\d+) live_keys=(-?\d+)\r?$', re.M)
RESULT = re.compile(r'^CRYPT CACHE RESULT checks=(\d+) failures=(\d+)\r?$', re.M)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(text):
    out = {}
    for k, v in re.findall(r'(\w+)=(\S+)', text):
        if re.fullmatch(r'-?\d+', v):
            out[k] = int(v)
        elif re.fullmatch(r'-?\d+\.\d+', v):
            out[k] = float(v)
        elif re.fullmatch(r'0x[0-9a-fA-F]+', v):
            out[k] = int(v, 16)
        else:
            out[k] = v
    return out


def parse_report(text):
    """Structured view of the fixture's stdout; raises AssertionError on an incomplete or failing report."""
    results = RESULT.findall(text)
    assert len(results) == 1, 'missing or repeated result line'
    checks, fails = (int(v) for v in results[0])
    for expression, count in ((KEY, 1), (MODE, 2), (STATS, 1), (COMPARE, 1), (SHUTDOWN, 1), (LEAK, 1)):
        assert len(expression.findall(text)) == count, 'duplicate or missing report rows'
    key = KEY.search(text)
    assert key, 'CRYPT_KEY line missing'
    modes = {m.group(1): fields(m.group(2)) for m in MODE.finditer(text)}
    stats, compare, shutdown, leak = STATS.search(text), COMPARE.search(text), SHUTDOWN.search(text), LEAK.search(text)
    assert stats and compare and shutdown and leak, 'CRYPT_STATS/CRYPT_COMPARE/CRYPT_SHUTDOWN/CRYPT_LEAK line missing'
    report = {'key': {'bits': int(key.group(1)), 'blob_bytes': int(key.group(2)), 'signature_bytes': int(key.group(3)),
                      'expected_verified': int(key.group(4)), 'provider': key.group(5), 'container': key.group(6)},
              'modes': modes, 'statistics': fields(stats.group(1)), 'compare': fields(compare.group(1)), 'shutdown': fields(shutdown.group(1)),
              'leak': {'live_providers': int(leak.group(1)), 'live_keys': int(leak.group(2))},
              'checks': checks, 'failures': fails, 'fail_lines': [l for l in text.splitlines() if l.startswith('FAIL')]}
    assert text.rstrip().endswith(f'CRYPT CACHE RESULT checks={checks} failures={fails}'), 'result line is not terminal'
    assert fails == 0 and not report['fail_lines'], 'fixture failures: ' + '; '.join(report['fail_lines'][:5])
    assert set(modes) == {'off', 'on'}, 'mode lines missing or duplicated'
    off, on = modes['off'], modes['on']
    for mode in (off, on):
        for field in ('total_ms', 'mean_us', 'median_us', 'min_us', 'max_us', 'context_mean_us'):
            value = mode[field]
            assert isinstance(value, (int, float)) and math.isfinite(value) and value >= 0, 'invalid timing'
        assert mode['mean_us'] > 0, 'zero timing denominator'
    assert off['iterations'] == on['iterations'] > 0, 'iteration counts differ'
    assert report['key']['blob_bytes'] == 0x114, 'the public key blob is not the game\'s 276 bytes'
    assert report['compare']['mismatches'] == 0 and report['compare']['iterations'] == off['iterations'], 'call-by-call comparison failed'
    assert off['verified'] == on['verified'] == report['key']['expected_verified'], 'verification outcomes differ from the expectation'
    assert off['real_acquires'] == 3 * off['iterations'] and off['real_releases'] == off['iterations'], 'uncached pass: three acquires and one release per check'
    assert on['real_acquires'] == 2 and on['real_releases'] == 0 and on['real_imports'] == 1, 'cached pass: exactly one delete probe, one create and one import'
    assert report['leak'] == {'live_providers': 0, 'live_keys': 0}, 'handle leak'
    assert report['shutdown']['container_absent'] == 1 and report['shutdown']['released'] == 1, 'shutdown left the container or the handles'
    assert checks == 37 + off['iterations'], 'fixture check inventory differs'
    assert report['compare']['steps'] == 12, 'comparison step inventory differs'
    assert report['statistics']['imports'] == off['iterations'] and report['statistics']['import_hits'] == off['iterations'] - 1, 'key hit accounting differs'
    modules = re.findall(r'^CRYPT_MODULE name=(advapi32[.]dll|rsaenh[.]dll) path=(.+)\r?$', text, re.M)
    assert len(modules) in (0, 2) and len(dict(modules)) == len(modules), 'module provenance inventory'
    report['modules'] = dict(modules)
    assert len(text.splitlines()) == 8 + len(modules), 'unexpected report text'
    # Timing is diagnostic; scheduler noise is not a semantic failure.
    report['speedup'] = off['mean_us'] / on['mean_us'] if on['mean_us'] > 0 else None
    report['saved_us_per_check'] = off['mean_us'] - on['mean_us']
    report['context_saved_us_per_check'] = off['context_mean_us'] - on['context_mean_us']
    return report


def parse_controls(text):
    lines = text.splitlines()
    assert lines and lines[-1] == 'CRYPT CONTROLS RESULT checks=53 failures=0', 'control terminal inventory'
    assert len(lines) == 54 and all(x.startswith('CHECK ') and x.endswith(' PASS') for x in lines[:-1]), 'control witnesses'
    return {'checks': 53}


def parse_hook(mode, text):
    count = HOOK_MODES[mode]
    lines = text.splitlines()
    assert lines and lines[-1] == f'CRYPT HOOK RESULT mode={mode} checks={count} failures=0', 'hook terminal inventory'
    assert len(lines) == count + 1 and all(x.startswith('CHECK ') and x.endswith(' PASS') for x in lines[:-1]), 'hook witnesses'
    return {'mode': mode, 'checks': count}


def main():
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from game_guard import game_running  # real game processes only
    import bottle
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iterations', type=int, default=200, help='signature checks per pass (default 200)')
    parser.add_argument('--timeout', type=int, default=1800)
    args = parser.parse_args()
    results = bottle.results_dir(ROOT)
    build = ROOT / 'verification/probe/build/crypt_cache'
    report = {'passed': False, 'phase': 'building', 'game_launched': False, 'bottle': bottle.describe(), 'iterations': args.iterations}
    summary = results / 'crypt-cache-summary.json'
    summary.write_text(json.dumps(report, indent=2) + '\n')
    try:
        running = game_running()
        assert not running, 'X3AP is running: ' + '; '.join(running)
        report['sources'] = {s: sha(ROOT / s) for s in SOURCES}
        subprocess.run(['sh', str(ROOT / 'verification/probe/build_crypt_cache.sh')], cwd=ROOT, check=True)
        subprocess.run(['sh', str(ROOT / 'verification/probe/build_crypt_cache_hook.sh')], cwd=ROOT, check=True)
        report['sources_after_build'] = {s: sha(ROOT / s) for s in SOURCES}
        assert report['sources_after_build'] == report['sources'], 'sources changed during build'
        report['wine_sha256_before'] = sha(bottle.WINE)
        report['compiler'] = subprocess.run(['i686-w64-mingw32-g++', '--version'], check=True, capture_output=True, text=True).stdout
        runtime_root = Path.home() / 'Library/Application Support/CrossOver/Bottles' / bottle.BOTTLE / 'drive_c/windows/syswow64'
        native_inputs = [runtime_root / name for name in ('advapi32.dll', 'rsaenh.dll')]
        report['native_inputs_before'] = {str(path): sha(path) for path in native_inputs}
        binary = build / 'crypt_cache_fixture.exe'
        report['executable_sha256'] = sha(binary)
        command = [bottle.WINE] + bottle.wine_args() + ['--workdir', str(build), str(binary), str(args.iterations)]
        report['phase'] = 'running'
        with (results / 'crypt-cache-fixture.txt').open('w') as out, (results / 'crypt-cache-fixture-wine.log').open('w') as err:
            run = subprocess.run(command, env=dict(os.environ), stdout=out, stderr=err, timeout=args.timeout)
        text = (results / 'crypt-cache-fixture.txt').read_text(errors='replace')
        report['report_sha256'] = sha(results / 'crypt-cache-fixture.txt')
        report['stderr_sha256'] = sha(results / 'crypt-cache-fixture-wine.log')
        report['exit_code'] = run.returncode
        report['report'] = parse_report(text)
        assert set(report['report']['modules']) == {'advapi32.dll', 'rsaenh.dll'}, 'loaded native modules not reported'
        assert run.returncode == 0, f'exit code {run.returncode}'
        assert sha(binary) == report['executable_sha256'], 'the executable changed during the run'
        assert {s: sha(ROOT / s) for s in SOURCES} == report['sources'], 'sources changed during the run'
        report['controls'] = []
        for mode in ('controls', *HOOK_MODES):
            assert not game_running(), 'game started; defer remaining controls'
            executable = build / ('crypt_cache_controls.exe' if mode == 'controls' else 'crypt_cache_hook_fixture.exe')
            executable_hash = sha(executable)
            args_tail = [] if mode == 'controls' else [mode]
            command = [bottle.WINE] + bottle.wine_args() + ['--workdir', str(build), str(executable), *args_tail]
            control = subprocess.run(command, env=dict(os.environ), capture_output=True, text=True, timeout=60)
            raw = results / f'crypt-cache-{mode}.txt'
            errors = results / f'crypt-cache-{mode}-wine.log'
            raw.write_text(control.stdout)
            errors.write_text(control.stderr)
            assert control.returncode == 0, f'{mode}: exit {control.returncode}'
            parsed = parse_controls(control.stdout) if mode == 'controls' else parse_hook(mode, control.stdout)
            assert sha(executable) == executable_hash, 'control executable changed'
            report['controls'].append({**parsed, 'mode': mode, 'executable_sha256': executable_hash,
                                       'report_sha256': sha(raw), 'stderr_sha256': sha(errors)})
        report['sources_after_run'] = {s: sha(ROOT / s) for s in SOURCES}
        report['wine_sha256_after'] = sha(bottle.WINE)
        report['native_inputs_after'] = {str(path): sha(path) for path in native_inputs}
        assert report['native_inputs_before'] == report['native_inputs_after'], 'native inputs changed during run'
        assert report['sources_after_run'] == report['sources'], 'sources changed during controls'
        assert report['wine_sha256_after'] == report['wine_sha256_before'], 'Wine launcher changed during run'
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
