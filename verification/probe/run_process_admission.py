#!/usr/bin/env python3
"""Race the actual process-admission configuration; CPU-only, no game/D3D."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_guard import game_running  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'verification/probe'
BUILD = PROBE / 'build/process-admission'
OUT = ROOT / 'verification/results'
WINE = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')
INPUTS = ['src/ownership/application_admission.h', 'src/ownership/application_admission.cpp',
          'src/ownership/application_admission_abi.h', 'src/ownership/application_admission_abi.cpp',
          'verification/probe/build_admission_dependencies.sh',
          'verification/probe/process_admission_fixture.cpp', 'verification/probe/run_process_admission.py']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sources():
    return {name: sha(ROOT / name) for name in INPUTS}


def no_game():
    if game_running():
        raise RuntimeError('X3AP running or process inventory unavailable')


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'scope': 'process getter first-use publication and CPU state; no complete proxy/live replay claim', 'runs': []}
    summary = OUT / 'process-admission-summary.json'
    # A killed build/run must not leave an earlier successful report visible.
    OUT.mkdir(parents=True, exist_ok=True)
    summary.write_text(json.dumps(report, indent=2) + '\n')
    try:
        before = sources()
        report['sources'] = before
        report['compiler'] = subprocess.run(['i686-w64-mingw32-g++', '--version'], check=True, capture_output=True, text=True).stdout
        subprocess.run(['sh', str(PROBE / 'build_admission_dependencies.sh'), str(BUILD)], check=True, capture_output=True, text=True, timeout=60)
        exe = BUILD / 'process_admission_fixture.exe'
        command = ['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread', '-static',
                   '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
                   str(PROBE / 'process_admission_fixture.cpp'), str(BUILD / 'application_admission.o'),
                   str(BUILD / 'application_admission_abi.o'), '-o', str(exe)]
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
        report['build_command'] = command
        artifacts = {str(path.relative_to(ROOT)): sha(path) for path in (exe, BUILD / 'application_admission.o', BUILD / 'application_admission_abi.o')}
        report['artifacts'] = artifacts
        runtime = sha(WINE)
        report['runtime_sha256'] = runtime
        for mode in ('enabled', 'disabled', 'malformed', 'long'):
            if sources() != before:
                raise RuntimeError('source changed during verification')
            no_game()
            launch = [str(WINE), '--bottle', 'Steam', '--no-update', '--workdir', str(BUILD), str(exe), mode]
            run = subprocess.run(launch, capture_output=True, text=True, timeout=45)
            log, errors = OUT / f'process-admission-{mode}.txt', OUT / f'process-admission-{mode}-stderr.txt'
            log.write_text(run.stdout)
            errors.write_text(run.stderr)
            lines = run.stdout.splitlines()
            expected = f'RESULT PASS checks=49 failures=0 threads=8 enabled={int(mode == "enabled")}'
            if run.returncode or not lines or lines[-1] != expected or len(lines) != 50:
                raise RuntimeError(f'{mode} failed: {run.stdout[-1000:]}')
            if any(not line.startswith('CHECK ') or not line.endswith(' PASS') for line in lines[:-1]):
                raise RuntimeError('unexpected check inventory')
            report['runs'].append({'mode': mode, 'checks': 49, 'launch': launch, 'log_sha256': sha(log), 'stderr_sha256': sha(errors)})
        if sources() != before or sha(WINE) != runtime or any(sha(ROOT / name) != value for name, value in artifacts.items()):
            raise RuntimeError('source/runtime/artifact changed')
        report['passed'] = True
    finally:
        summary.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'passed': True, 'modes': len(report['runs']), 'checks_each': 49}))


if __name__ == '__main__':
    main()
