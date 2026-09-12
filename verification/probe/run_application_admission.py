#!/usr/bin/env python3
"""Build/test portable admission bookkeeping; no game, D3D device or live gate."""
import hashlib
import json
from pathlib import Path
import subprocess
import time
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_guard import game_running  # noqa: E402
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory

ROOT = Path(__file__).resolve().parents[2]
RESULTS = bottle.results_dir(ROOT)
INPUTS = ['src/ownership/application_admission.h', 'src/ownership/application_admission.cpp',
          'verification/probe/application_admission_fixture.cpp',
          'verification/probe/run_application_admission.py']
CASES = ['basic', 'waiting', 'existing_roots', 'vetoes', 'invariants', 'racing', 'nested_stress', 'veto_race']
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread']
WINE = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def hashes():
    return {name: sha(ROOT / name) for name in INPUTS}


def no_game():
    if game_running():
        raise RuntimeError('X3AP running or process inventory unavailable; postpone native fixture')


def main():
    RESULTS.mkdir(parents=True, exist_ok=True)
    summary = RESULTS / 'application-admission-summary.json'
    report = {'passed': False, 'bottle': bottle.describe(), 'scope': 'portable monitor bookkeeping only; no native callback/entry coverage claim', 'runs': []}
    summary.write_text(json.dumps(report, indent=2) + '\n')
    try:
        before = hashes()
        report['sources'] = before
        report['compiler'] = subprocess.run(['clang++', '--version'], check=True, capture_output=True, text=True, timeout=15).stdout
        report['native_compiler'] = subprocess.run(['i686-w64-mingw32-g++', '--version'], check=True, capture_output=True, text=True, timeout=15).stdout
        directory = ROOT / 'build/verification/application-admission'
        directory.mkdir(parents=True, exist_ok=True)
        for name, extra in [('optimized', []), ('sanitized', ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']),
                            ('thread-sanitized', ['-fsanitize=thread', '-fno-omit-frame-pointer']),
                            ('preview-x86', ['-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2', '-static'])]:
            native = name == 'preview-x86'
            if native:
                no_game()
            exe = directory / (name + ('.exe' if native else ''))
            command = ['i686-w64-mingw32-g++' if native else 'clang++', *FLAGS, *extra,
                       str(ROOT / INPUTS[1]), str(ROOT / INPUTS[2]), '-o', str(exe)]
            subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
            if hashes() != before:
                raise RuntimeError('source changed during build')
            executable = sha(exe)
            started = time.monotonic()
            if native:
                no_game()
                launch = [str(WINE), '--bottle', bottle.BOTTLE, '--no-update', '--workdir', str(directory), str(exe)]
            else:
                launch = [str(exe)]
            run = subprocess.run(launch, capture_output=True, text=True, timeout=60)
            log = RESULTS / f'application-admission-{name}.txt'
            errors = RESULTS / f'application-admission-{name}-stderr.txt'
            log.write_text(run.stdout)
            errors.write_text(run.stderr)
            lines = run.stdout.splitlines()
            if run.returncode or (run.stderr and not native) or not lines or lines[-1] != 'RESULT PASS checks=4865 failures=0':
                raise RuntimeError(f'{name} failed or had unexpected inventory')
            if [line for line in lines if line.startswith('CASE ')] != [f'CASE {case} PASS' for case in CASES]:
                raise RuntimeError(f'{name} case inventory mismatch')
            if sum(line.startswith('RESULT') for line in lines) != 1:
                raise RuntimeError(f'{name} duplicate terminal result')
            if hashes() != before or sha(exe) != executable:
                raise RuntimeError('source/executable changed during run')
            report['runs'].append({'name': name, 'command': command, 'launch': launch, 'checks': 4865,
                                   'executable_sha256': executable, 'log_sha256': sha(log),
                                   'stderr_sha256': sha(errors), 'elapsed_seconds': time.monotonic() - started})
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        summary.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'passed': report['passed'], 'variants': len(report['runs']), 'checks_each': 4865}))


if __name__ == '__main__':
    main()
