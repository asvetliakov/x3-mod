#!/usr/bin/env python3
"""Build a frozen lease CPU benchmark, retain timings and exact build provenance."""
import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
ROOT = Path(__file__).resolve().parents[2]
FILES = ['src/ownership/d3d9_ownership.h', 'src/ownership/d3d9_ownership.cpp',
         'src/ownership/d3d9_classes_inc.h', 'src/ownership/d3d9_forwarders_inc.h',
         'src/ownership/execution_state.h', 'src/ownership/execution_state.cpp',
         'src/ownership/finite_buffer_evidence.h', 'src/ownership/finite_buffer_evidence.cpp',
         'src/ownership/portable_managed_upload.h', 'src/ownership/portable_managed_upload.cpp',
         'tools/ownership/generate_d3d9_forwarders.py',
         'verification/probe/geometry_lease_benchmark.cpp',
         'verification/probe/build_geometry_lease_benchmark.sh',
         'verification/probe/run_geometry_lease_benchmark.py']
NATIVE_ROOT = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib/wine/i386-windows')
# Runtime identity is recorded for reproducibility, never an admission allowlist.
NATIVE = ('d3d9.dll', 'wined3d.dll')

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def hashes():
    return {name: digest(ROOT / name) for name in FILES} | {'native/' + name: digest(NATIVE_ROOT / name) for name in NATIVE}

def no_game():
    run = subprocess.run(['pgrep', '-ifl', 'X3AP.exe'], capture_output=True, text=True)
    if run.returncode not in (0, 1) or run.stdout.strip():
        raise RuntimeError('Game present or inventory failed: ' + run.stdout)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--label', required=True, choices=['baseline', 'qualification', 'optimized', 'portable', 'sidecar-index'])
    args = parser.parse_args()
    prefix = ROOT / ('verification/results/geometry-lease-performance-' + args.label)
    meta = dict(passed=False, phase='building', label=args.label, game_launched=False, draws=0,
                started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    def save():
        Path(str(prefix) + '.json').write_text(json.dumps(meta, indent=2) + '\n')
    save()
    try:
        no_game()
        before = hashes()
        meta['source_hashes_before_build'] = before
        meta['git_head'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
        save()
        with Path(str(prefix) + '-build.txt').open('wb') as out:
            subprocess.run(['sh', 'verification/probe/build_geometry_lease_benchmark.sh'], cwd=ROOT,
                           stdout=out, stderr=subprocess.STDOUT, check=True, timeout=90)
        meta['source_hashes_after_build'] = hashes()
        if before != meta['source_hashes_after_build']:
            raise RuntimeError('Source changed during compilation')
        built = ROOT / 'verification/probe/build/geometry_lease_benchmark.exe'
        frozen = built.with_name('geometry_lease_benchmark_' + args.label + '.exe')
        shutil.copyfile(built, frozen)
        meta.update(executable_sha256=digest(frozen), executable=str(frozen.relative_to(ROOT)),
                    fresh_build=True, phase='running')
        command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
                   '--bottle', 'Steam', '--no-update', '--dll', 'd3d9=b', '--workdir', str(frozen.parent), str(frozen)]
        meta['command'] = command
        save()
        print('BUILD_FROZEN ' + args.label + ' ' + meta['executable_sha256'], flush=True)
        no_game()
        report, wine = Path(str(prefix) + '.txt'), Path(str(prefix) + '-wine.log')
        with report.open('wb') as out, wine.open('wb') as err:
            run = subprocess.run(command, cwd=ROOT, stdout=out, stderr=err, timeout=240,
                                 env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'))
        text = report.read_text()
        terminal = [line for line in text.splitlines() if line.startswith('RESULT ')]
        match = re.fullmatch(r'RESULT PASS checks=(\d+) samples=84 draws=0', terminal[0]) if len(terminal) == 1 else None
        if not match or int(match[1]) != 831397 or not text.rstrip().endswith(terminal[0]) or run.returncode != 0:
            raise RuntimeError('Benchmark failed or incomplete')
        frequency = re.findall(r'^BENCHMARK qpc_frequency=(\d+) draws=0 trials=7 counts=1,100,700,4096$', text, re.M)
        if len(frequency) != 1 or int(frequency[0]) <= 0:
            raise RuntimeError('Invalid QPC frequency')
        meta['qpc_frequency'] = int(frequency[0])
        samples = []
        for line in text.splitlines():
            if line.startswith('SAMPLE '):
                fields = dict(part.split('=', 1) for part in line.split()[1:])
                sample = {key: value if key == 'profile' else float(value) if key.endswith('_us') else int(value)
                          for key, value in fields.items()}
                if any(not math.isfinite(value) or value < 0 for key, value in sample.items() if key.endswith('_us')):
                    raise RuntimeError('Invalid timing value')
                samples.append(sample)
        expected = {(profile, count, iteration) for profile in ['shared_small', 'shared_varying_range', 'many_small']
                    for count in [1, 100, 700, 4096] for iteration in range(7)}
        if len(samples) != 84 or {(s['profile'], s['leases'], s['iteration']) for s in samples} != expected:
            raise RuntimeError('Wrong sample inventory')
        aggregate = []
        for profile, count in sorted({(s['profile'], s['leases']) for s in samples}):
            group = [s for s in samples if (s['profile'], s['leases']) == (profile, count)]
            row = dict(profile=profile, leases=count, distinct_pairs=group[0]['distinct_pairs'], reserved_bytes=group[0]['reserved_bytes'])
            for key in group[0]:
                if key.endswith('_us'):
                    values = [s[key] for s in group]
                    row[key] = dict(median=statistics.median(values), minimum=min(values), maximum=max(values))
            row['counters'] = [{key: s[key] for key in ['acquire_components', 'inspect_components', 'acquire_cache_hits', 'inspect_cache_hits']} for s in group]
            aggregate.append(row)
        after = hashes()
        meta.update(passed=True, phase='complete', exit_code=run.returncode, checks=int(match[1]),
                    samples=84, aggregates=aggregate, source_hashes_after_run=after,
                    source_unchanged_during_run=before == after,
                    executable_unchanged=digest(frozen) == meta['executable_sha256'],
                    report_sha256=digest(report), wine_log_sha256=digest(wine),
                    limits=['CPU wall time for synthetic indexed lease operations, seven trials after one warmup; no GPU draws.',
                            'A frozen executable represents its before/after-build source map even if separate source work proceeds during execution.',
                            'Qualifier ticks overlap acquire/inspect times and must not be added to those phase totals.',
                            'Native reference and public-evidence controls are different operations, not an uninstrumented game baseline.'])
        if not meta['executable_unchanged'] or any(after['native/' + name] != before['native/' + name] for name in NATIVE):
            raise RuntimeError('Frozen executable or native runtime changed')
    except (Exception, KeyboardInterrupt) as error:
        meta.update(passed=False, phase='failed', error=repr(error))
    save()
    print(json.dumps({k: meta.get(k) for k in ['passed', 'phase', 'samples', 'checks', 'error']}))
    return 0 if meta['passed'] else 1
if __name__ == '__main__':
    raise SystemExit(main())
