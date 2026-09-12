#!/usr/bin/env python3
"""Measure immutable actual DLLs with one original workload and retained provenance."""
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
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_guard import game_running  # noqa: E402
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory
ROOT = Path(__file__).resolve().parents[2]
RESULTS = bottle.results_dir(ROOT)
BUILD = ROOT / 'verification/probe/build'
WINE = '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'
NATIVE = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib/wine/i386-windows')
DOSDEVICES = bottle.bottle_dir() / 'dosdevices'

def drive_mappings():
    return {p.name.lower(): str(p.resolve()) for p in DOSDEVICES.iterdir()
            if len(p.name) == 2 and p.name[0].isalpha() and p.name[1] == ':' and p.is_symlink()}

FIXTURE_INPUTS = ['verification/probe/hook_admission_benchmark.cpp',
                  'verification/probe/build_hook_admission_benchmark.sh',
                  'verification/probe/run_hook_admission_benchmark.py']
ROUTES = ('get_render_state', 'set_render_state', 'invalid_set_rt', 'invalid_clear')
CASES = [('native', None, 0, 0), ('previous_proxy', 'previous', 0, 0),
         ('previous_ownership', 'previous', 1, 0), ('proxy_off', 'current', 0, 0),
         ('proxy_on', 'current', 0, 1), ('ownership_off', 'current', 1, 0),
         ('ownership_on', 'current', 1, 1)]

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def no_game():
    p = game_running()
    if p:
        raise RuntimeError('Game active or inventory failed: ' + '\n'.join(p))

def source_hashes(paths):
    return {name: sha(ROOT / name) for name in paths}

def parse_report(text):
    final = [line for line in text.splitlines() if line.startswith('RESULT ')]
    expected = 'RESULT PASS checks=640115 samples=28 calls_per_sample=20000 draws=0 presents=0'
    if final != [expected] or not text.rstrip().endswith(expected):
        raise RuntimeError('Incomplete or failed exact fixture inventory')
    metadata = re.findall(r'^BENCHMARK frequency=(\d+) iterations=20000 trials=7 routes=4 draws=0 presents=0$', text, re.M)
    if len(metadata) != 1 or int(metadata[0]) <= 0:
        raise RuntimeError('Invalid benchmark frequency')
    frequency = int(metadata[0])
    samples = []
    for line in text.splitlines():
        if not line.startswith('SAMPLE '):
            continue
        fields = dict(part.split('=', 1) for part in line.split()[1:])
        row = {k: v if k == 'route' else float(v) if k in ('total_us', 'ns_per_call') else int(v)
               for k, v in fields.items()}
        if row['calls'] != 20000 or row['elapsed_ticks'] < 0 or any(not math.isfinite(row[k]) or row[k] < 0 for k in ('total_us', 'ns_per_call')):
            raise RuntimeError('Invalid timing value')
        for key, expected_value in [('total_us', row['elapsed_ticks'] * 1e6 / frequency),
                                    ('ns_per_call', row['elapsed_ticks'] * 1e9 / frequency / 20000)]:
            if abs(row[key] - expected_value) > 0.000002:
                raise RuntimeError('Timing conversion mismatch')
        samples.append(row)
    if len(samples) != 28 or {(s['route'], s['iteration']) for s in samples} != {(r, i) for r in ROUTES for i in range(7)}:
        raise RuntimeError('Wrong sample inventory')
    return {'frequency': frequency, 'checks': 640115, 'samples': samples,
            'ns_per_call': {r: {'median': statistics.median(s['ns_per_call'] for s in samples if s['route'] == r),
                                'minimum': min(s['ns_per_call'] for s in samples if s['route'] == r),
                                'maximum': max(s['ns_per_call'] for s in samples if s['route'] == r)} for r in ROUTES}}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dll', type=Path, default=ROOT / 'build-ownership/d3d9.dll')
    parser.add_argument('--provenance', type=Path, default=RESULTS / 'ownership-integration-build.json')
    args = parser.parse_args()
    summary = RESULTS / 'hook-admission-performance.json'
    meta = {'passed': False, 'phase': 'building', 'game_launched': False, 'bottle': bottle.describe(),
            'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'cases': {}}
    def save():
        summary.write_text(json.dumps(meta, indent=2) + '\n')
    save()
    try:
        no_game()
        current = json.loads(args.provenance.read_bytes())
        if current.get('result') != 'PASS' or current['sources_before_build'] != current['sources_at_start']:
            raise RuntimeError('Current DLL requires completed fresh build provenance')
        if current['dll_sha256'] != sha(args.dll):
            raise RuntimeError('Current DLL does not match build manifest')
        if source_hashes(current['sources_at_start']) != current['sources_at_start']:
            raise RuntimeError('Current sources differ from DLL provenance')
        previous_path = RESULTS / 'hook-admission-baseline-provenance.json'
        previous = json.loads(previous_path.read_bytes())
        previous_dll = ROOT / previous['frozen_dll']
        if sha(previous_dll) != previous['dll_sha256']:
            raise RuntimeError('Retained previous DLL changed')
        native_before = {name: sha(NATIVE / name) for name in ('d3d9.dll', 'wined3d.dll')}
        fixtures_before = source_hashes(FIXTURE_INPUTS)
        drives_before = drive_mappings()
        meta.update(current_dll_provenance={'manifest_sha256': sha(args.provenance),
                    'dll_sha256': current['dll_sha256'], 'sources_before_build': current['sources_before_build'],
                    'sources_after_build': current['sources_at_start'], 'sources_after_run': current['sources']},
                    previous_dll_provenance={'manifest_sha256': sha(previous_path), **previous},
                    native_hashes_before=native_before, fixture_hashes_before=fixtures_before, drive_mappings_before=drives_before)
        save()
        with (RESULTS / 'hook-admission-performance-build.txt').open('wb') as out:
            subprocess.run(['sh', 'verification/probe/build_hook_admission_benchmark.sh'], cwd=ROOT,
                           stdout=out, stderr=subprocess.STDOUT, timeout=90, check=True)
        if source_hashes(FIXTURE_INPUTS) != fixtures_before:
            raise RuntimeError('Fixture changed during build')
        exe = BUILD / 'hook_admission_benchmark.exe'
        meta['fixture_sha256'] = sha(exe)
        frozen = BUILD / ('hook-admission-frozen-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
        frozen.mkdir()
        shutil.copyfile(args.dll, frozen / 'current.dll')
        shutil.copyfile(previous_dll, frozen / 'previous.dll')
        shutil.copyfile(exe, frozen / exe.name)
        meta.update(phase='running', frozen_directory=str(frozen.relative_to(ROOT)))
        save()
        for label, variant, ownership, admission in CASES:
            no_game()
            directory = frozen / label
            directory.mkdir()
            run_exe = directory / exe.name
            shutil.copyfile(frozen / exe.name, run_exe)
            if variant:
                shutil.copyfile(frozen / (variant + '.dll'), directory / 'd3d9.dll')
            override = 'd3d9=n,b' if variant else 'd3d9=b'
            env = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
            env.update(X3M_CAMERA='vanilla', X3M_CHASE_SCENE_FIX='0', X3M_CHASE_COMBAT_TIGHTNESS='0')
            env.update(WINEDLLOVERRIDES=override, X3M_OWNERSHIP=str(ownership), X3M_ADMISSION=str(admission),
                       X3M_TELEMETRY='0', X3M_CAPTURE_START='0', X3M_CAPTURE_FRAMES='0',
                       X3M_DEPTH_COPY='0', X3M_SCENE_DEPTH_CAPTURE='0', X3M_OBJECT_TRACE='0',
                       X3M_FINITE_POSITIONS='0', X3M_MOTION_CAPTURE='0', X3M_MESH_CACHE='0')
            command = [WINE, '--bottle', bottle.BOTTLE, '--no-update', '--dll', override,
                       '--workdir', str(directory), str(run_exe), 'proxy' if variant else 'native']
            report = RESULTS / ('hook-admission-performance-' + label + '.txt')
            wine_log = RESULTS / ('hook-admission-performance-' + label + '-wine.log')
            with report.open('wb') as out, wine_log.open('wb') as err:
                run = subprocess.run(command, cwd=ROOT, env=env, stdout=out, stderr=err, timeout=90)
            report_text = report.read_text()
            parsed = parse_report(report_text)
            modules = re.findall(r'^MODULE (.+)$', report_text, re.M)
            normalized = modules[0].replace('\\', '/').lower() if len(modules) == 1 else ''
            if variant:
                drive = re.fullmatch(r'([a-z]:)/(.*)', normalized)
                resolved = (Path(drives_before[drive[1]]) / drive[2].lstrip('/')).resolve() if drive and drive[1] in drives_before else None
                if resolved != (directory / 'd3d9.dll').resolve():
                    raise RuntimeError('Loaded proxy module path does not map to case: ' + repr(modules))
            elif normalized not in {'c:/windows/system32/d3d9.dll', 'c:/windows/syswow64/d3d9.dll'}:
                raise RuntimeError('Loaded native module path does not match system D3D9: ' + repr(modules))
            parsed['loaded_module'] = modules[0]
            if run.returncode != 0 or sha(run_exe) != meta['fixture_sha256']:
                raise RuntimeError('Runtime failed or fixture changed: ' + label)
            logs = list((directory / 'x3-modern-captures').glob('*.log')) if variant else []
            log = logs[0] if len(logs) == 1 else None
            if variant and log is None:
                raise RuntimeError('Missing unique proxy trace')
            trace = log.read_text() if log else ''
            if variant:
                wrapped = re.findall(r'ownership_factory mode=wrapped(?: |$)', trace)
                if len(wrapped) != ownership or 'ownership_factory mode=native_fallback' in trace:
                    raise RuntimeError('Actual factory ownership differs from timing case: ' + label)
                parsed['wrapped_factory_count'] = len(wrapped)
            if variant == 'current':
                mode = re.findall(r'application_admission_mode requested=(\d+) enabled=(\d+) live_replay=0 coverage_complete=0', trace)
                if mode != [(str(admission), str(admission))]:
                    raise RuntimeError('Admission mode not established: ' + label)
                final = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in trace.splitlines() if 'application_admission_final ' in line]
                if not final or final[-1]['active_roots'] != '0' or final[-1]['waiting_roots'] != '0' or final[-1]['promotions'] != '0':
                    raise RuntimeError('Final admission state invalid: ' + label)
                if admission and int(final[-1]['admitted_roots']) < 320000:
                    raise RuntimeError('Covered calls did not enter admission')
            dll_hash = sha(directory / 'd3d9.dll') if variant else native_before['d3d9.dll']
            expected_hash = meta[variant + '_dll_provenance']['dll_sha256'] if variant else native_before['d3d9.dll']
            if dll_hash != expected_hash:
                raise RuntimeError('DLL changed during runtime')
            retained_trace = RESULTS / ('hook-admission-performance-' + label + '-trace.log')
            if log:
                shutil.copyfile(log, retained_trace)
            meta['cases'][label] = {**parsed, 'command': command, 'ownership': ownership, 'admission': admission,
                                   'exit_code': run.returncode, 'dll_sha256': dll_hash,
                                   'report_sha256': sha(report), 'wine_log_sha256': sha(wine_log),
                                   'trace_sha256': sha(retained_trace) if log else None}
            save()
        if source_hashes(FIXTURE_INPUTS) != fixtures_before or source_hashes(current['sources_at_start']) != current['sources_at_start']:
            raise RuntimeError('Source changed during benchmark')
        if {name: sha(NATIVE / name) for name in native_before} != native_before:
            raise RuntimeError('Native runtime changed')
        if drive_mappings() != drives_before:
            raise RuntimeError('Wine drive mappings changed')
        differences = {}
        for route in ROUTES:
            value = lambda case: meta['cases'][case]['ns_per_call'][route]['median']
            differences[route] = {'current_proxy_minus_native_ns': value('proxy_off') - value('native'),
                                  'current_ownership_minus_native_ns': value('ownership_off') - value('native'),
                                  'proxy_admission_increment_ns': value('proxy_on') - value('proxy_off'),
                                  'ownership_admission_increment_ns': value('ownership_on') - value('ownership_off'),
                                  'proxy_off_change_from_retained_ns': value('proxy_off') - value('previous_proxy'),
                                  'ownership_off_change_from_retained_ns': value('ownership_off') - value('previous_ownership')}
        meta.update(passed=True, phase='complete', differences_of_separate_medians=differences,
                    fixture_hashes_after=source_hashes(FIXTURE_INPUTS), current_sources_after=source_hashes(current['sources_at_start']),
                    native_hashes_after={name: sha(NATIVE / name) for name in native_before}, drive_mappings_after=drive_mappings(),
                    limits=['Whole call CPU wall time includes backend, loop/validation and any command submission; no draw/Present/copy/readback.',
                            'Separate warmed process medians include runtime/thermal variation; differences are not paired causal or game FPS estimates.',
                            'Old-off differences include all changes on that path; invalid Clear already had CpuCallBoundary before this checkpoint.',
                            'Synthetic Preview evidence does not establish native-Windows timing or complete replay coverage.'])
    except (Exception, KeyboardInterrupt) as error:
        meta.update(passed=False, phase='failed', error=repr(error))
    save()
    print(json.dumps({k: meta.get(k) for k in ('passed', 'phase', 'error')}, indent=2))
    return 0 if meta['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
