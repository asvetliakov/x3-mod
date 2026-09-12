#!/usr/bin/env python3
"""Standalone Win32 admission adapter CPU witnesses/timing; no D3D or game."""
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_guard import game_running  # noqa: E402
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory

ROOT = Path(__file__).resolve().parents[2]
OUT = bottle.results_dir(ROOT)
BUILD = ROOT / 'verification/probe/build'
WINE = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')
INPUTS = [
    'src/ownership/application_admission.h', 'src/ownership/application_admission.cpp',
    'src/ownership/application_admission_abi.h', 'src/ownership/application_admission_abi.cpp',
    'verification/probe/application_admission_abi_fixture.cpp',
    'verification/probe/build_application_admission_abi.sh',
    'verification/probe/run_application_admission_abi.py',
]
CASES = ['disabled', 'ordinary', 'handoff', 'nested_order', 'vetoes', 'replay_refusals',
         'different_monitor', 'foreign_finish', 'waiting', 'existing_roots', 'four_byte']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sources():
    return {p: sha(ROOT / p) for p in INPUTS}


def parse(data):
    lines = data.splitlines()
    terminal = 'RESULT PASS checks=130 failures=0 samples=21'
    if not lines or lines[-1] != terminal or sum(x.startswith('RESULT') for x in lines) != 1:
        raise ValueError('missing, duplicate or nonterminal result')
    checks = [x for x in lines if x.startswith('CHECK ')]
    if len(checks) != 130 or any(not x.endswith(' PASS') for x in checks):
        raise ValueError('incorrect checks or failed witness')
    if [x for x in lines if x.startswith('CASE ')] != [f'CASE {x} PASS' for x in CASES]:
        raise ValueError('incorrect case inventory')
    entries = [x for x in lines if x.startswith('SAMPLE ')]
    samples = {}
    for entry in entries:
        match = re.fullmatch(r'SAMPLE mode=(disabled|outer|nested) trial=([1-7]) iterations=100000 ns_per_entry=([0-9.]+)', entry)
        if not match:
            raise ValueError('invalid sample')
        mode, trial, value = match.groups()
        key = (mode, int(trial))
        value = float(value)
        if key in samples or not math.isfinite(value) or value <= 0:
            raise ValueError('duplicate or invalid sample')
        samples[key] = value
    if set(samples) != {(mode, trial) for mode in ('disabled', 'outer', 'nested') for trial in range(1, 8)}:
        raise ValueError('incomplete sample matrix')
    if len(lines) != 130 + len(CASES) + 21 + 1:
        raise ValueError('unexpected report text')
    return {mode: {'median_ns_per_entry': statistics.median(samples[mode, trial] for trial in range(1, 8)),
                   'samples_ns_per_entry': [samples[mode, trial] for trial in range(1, 8)]}
            for mode in ('disabled', 'outer', 'nested')}


def no_game():
    if game_running():
        raise RuntimeError('game running or inventory failed; postpone CPU fixture')


def main():
    report = {'passed': False, 'bottle': bottle.describe(), 'scope': 'standalone adapter ordinary-return CPU preservation and diagnostic timing; no D3D, gameplay, native-Windows or enclosing-hook ABI claim'}
    summary = OUT / 'application-admission-abi-summary.json'
    summary.write_text(json.dumps(report, indent=2) + '\n')
    try:
        before = sources()
        report['source_hashes_before_build'] = before
        report['runtime_path'] = str(WINE)
        report['runtime_sha256_before'] = sha(WINE)
        report['compiler'] = subprocess.run(['i686-w64-mingw32-g++', '--version'], capture_output=True, text=True, check=True, timeout=10).stdout
        subprocess.run(['sh', str(ROOT / INPUTS[5])], check=True, capture_output=True, text=True, timeout=60)
        report['source_hashes_after_build'] = sources()
        if sources() != before:
            raise RuntimeError('source changed during build')
        obj = BUILD / 'application_admission_abi.o'
        dump = subprocess.run(['i686-w64-mingw32-objdump', '-r', '-Cd', str(obj)], capture_output=True, text=True, check=True, timeout=15).stdout
        if re.search(r'SjLj|personality|gcc_except', dump):
            raise RuntimeError('adapter compiler EH bookends present')
        for opcode in ('fnsave', 'frstor', 'stmxcsr', 'ldmxcsr'):
            if opcode not in dump:
                raise RuntimeError('missing state transport in adapter object')
        audit = OUT / 'application-admission-abi-object.txt'
        audit.write_text(dump)
        report['adapter_object_sha256'] = sha(obj)
        report['adapter_disassembly_sha256'] = sha(audit)
        report['adapter_no_eh_references'] = True
        # Build-policy failure must be explicit, not silently emit a different ABI.
        wrong = subprocess.run(['i686-w64-mingw32-g++', '-std=c++17', '-c', str(ROOT / INPUTS[3]), '-o', str(BUILD / 'application_admission_abi_wrong.o')], capture_output=True, text=True, timeout=30)
        if wrong.returncode == 0 or 'Compile only this ABI adapter translation unit' not in wrong.stderr:
            raise RuntimeError('missing enforced per-source exception policy')
        report['wrong_exception_policy_rejected'] = True
        exe = BUILD / 'application_admission_abi_fixture.exe'
        report['executable_sha256_before_run'] = sha(exe)
        no_game()
        command = [str(WINE), '--bottle', bottle.BOTTLE, '--no-update', '--workdir', str(BUILD), str(exe)]
        report['command'] = command
        run = subprocess.run(command, capture_output=True, text=True, timeout=60)
        log = OUT / 'application-admission-abi.txt'
        err = OUT / 'application-admission-abi-stderr.txt'
        log.write_text(run.stdout)
        err.write_text(run.stderr)
        report['exit_code'] = run.returncode
        report['report_sha256'] = sha(log)
        report['stderr_sha256'] = sha(err)
        if run.returncode:
            raise RuntimeError('native fixture failed')
        report['timing'] = parse(run.stdout)
        report['source_hashes_after_run'] = sources()
        report['runtime_sha256_after'] = sha(WINE)
        report['executable_sha256_after_run'] = sha(exe)
        if sources() != before or sha(exe) != report['executable_sha256_before_run'] or sha(WINE) != report['runtime_sha256_before']:
            raise RuntimeError('source/executable/runtime changed')
        historical_path = OUT / 'application-admission-abi-initial-summary.json'
        historical = json.loads(historical_path.read_text())
        historical_log = OUT / 'application-admission-abi-initial.txt'
        if not historical['passed'] or sha(historical_log) != historical['report_sha256']:
            raise RuntimeError('historical report provenance mismatch')
        if not (historical['source_hashes_before_build'] == historical['source_hashes_after_build'] == historical['source_hashes_after_run']):
            raise RuntimeError('historical build/run source mismatch')
        old = parse(historical_log.read_text())
        report['historical_initial'] = {'summary_sha256': sha(historical_path), 'report_sha256': sha(historical_log),
                                      'executable_sha256': historical['executable_sha256'],
                                      'scope': 'retained prototype report and its original source maps, not reproducible by current source',
                                      'timing': old}
        report['comparison'] = {mode: {'initial_ns': old[mode]['median_ns_per_entry'],
                                      'current_ns': report['timing'][mode]['median_ns_per_entry']}
                                for mode in old}
        report['checks'] = 130
        report['samples'] = 21
        report['passed'] = True
    finally:
        summary.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('passed', 'checks', 'samples', 'comparison')}, indent=2))


if __name__ == '__main__':
    main()
