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
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('src/proxy/crypt_cache.h', 'src/proxy/crypt_cache.cpp',
           'verification/probe/crypt_cache_fixture.cpp', 'verification/probe/build_crypt_cache.sh', 'verification/probe/run_crypt_cache.py')
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
    assert {'off', 'on'} <= set(modes), 'mode lines missing'
    off, on = modes['off'], modes['on']
    assert off['iterations'] == on['iterations'] > 0, 'iteration counts differ'
    assert report['key']['blob_bytes'] == 0x114, 'the public key blob is not the game\'s 276 bytes'
    assert report['compare']['mismatches'] == 0 and report['compare']['iterations'] == off['iterations'], 'call-by-call comparison failed'
    assert off['verified'] == on['verified'] == report['key']['expected_verified'], 'verification outcomes differ from the expectation'
    assert off['real_acquires'] == 3 * off['iterations'] and off['real_releases'] == off['iterations'], 'uncached pass: three acquires and one release per check'
    assert on['real_acquires'] == 2 and on['real_releases'] == 0 and on['real_imports'] == 1, 'cached pass: exactly one delete probe, one create and one import'
    assert report['leak'] == {'live_providers': 0, 'live_keys': 0}, 'handle leak'
    assert report['shutdown']['container_absent'] == 1 and report['shutdown']['released'] == 1, 'shutdown left the container or the handles'
    assert on['mean_us'] < off['mean_us'], 'the cached pass is not faster'
    report['speedup'] = off['mean_us'] / on['mean_us'] if on['mean_us'] > 0 else None
    report['saved_us_per_check'] = off['mean_us'] - on['mean_us']
    report['context_saved_us_per_check'] = off['context_mean_us'] - on['context_mean_us']
    return report


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
        binary = build / 'crypt_cache_fixture.exe'
        report['executable_sha256'] = sha(binary)
        command = [bottle.WINE] + bottle.wine_args() + ['--workdir', str(build), str(binary), str(args.iterations)]
        report['phase'] = 'running'
        with (results / 'crypt-cache-fixture.txt').open('w') as out, (results / 'crypt-cache-fixture-wine.log').open('w') as err:
            run = subprocess.run(command, env=dict(os.environ), stdout=out, stderr=err, timeout=args.timeout)
        text = (results / 'crypt-cache-fixture.txt').read_text(errors='replace')
        report['exit_code'] = run.returncode
        report['report'] = parse_report(text)
        assert run.returncode == 0, f'exit code {run.returncode}'
        assert sha(binary) == report['executable_sha256'], 'the executable changed during the run'
        assert {s: sha(ROOT / s) for s in SOURCES} == report['sources'], 'sources changed during the run'
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
