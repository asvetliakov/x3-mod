#!/usr/bin/env python3
"""Bounded host verification of the portable observed execution state core."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
RESULT = ROOT / 'verification/results/execution-state-summary.json'
LOG = ROOT / 'verification/results/execution-state.txt'
INPUTS = ['src/ownership/execution_state.h', 'src/ownership/execution_state.cpp',
          'verification/probe/execution_state_fixture.cpp',
          'verification/probe/run_execution_state.py']
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror']

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def hashes():
    return {p: sha(ROOT / p) for p in INPUTS}

def run():
    RESULT.parent.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'scope': 'portable CPU execution-state core; no native hook/GPU claim'}
    RESULT.write_text(json.dumps(report, indent=2) + '\n')
    try:
        before = hashes()
        output = ROOT / 'build/verification/execution-state'
        output.mkdir(parents=True, exist_ok=True)
        compiler = subprocess.run(['clang++', '--version'], capture_output=True, text=True,
                                  check=True, timeout=15).stdout
        report.update(sources=before, compiler=compiler, flags=FLAGS, runs=[])
        for suffix, extra in [('', []), ('-sanitized', ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
            exe = output / ('fixture' + suffix)
            command = ['clang++', *FLAGS, *extra, str(ROOT / INPUTS[1]), str(ROOT / INPUTS[2]), '-o', str(exe)]
            subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
            if hashes() != before:
                raise RuntimeError('source changed during build')
            executable = sha(exe)
            start = time.monotonic()
            completed = subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=60)
            lines = completed.stdout.splitlines()
            results = [line for line in lines if line.startswith('RESULT')]
            if len(results) != 1 or not lines or lines[-1] != results[0]:
                raise RuntimeError('missing/duplicate/nonterminal result')
            match = re.fullmatch(r'RESULT PASS checks=(\d+)', results[0])
            if not match or completed.stderr or int(match[1]) != 60365:
                raise RuntimeError('unexpected result/stderr/check count')
            if hashes() != before or sha(exe) != executable:
                raise RuntimeError('source or executable changed during run')
            log = LOG.with_name(LOG.stem + suffix + LOG.suffix)
            log.write_text(completed.stdout)
            report['runs'].append({'command': command, 'executable_sha256': executable,
                                   'log': str(log.relative_to(ROOT)), 'log_sha256': sha(log),
                                   'checks': int(match[1]), 'elapsed_seconds': time.monotonic() - start})
        if report['runs'][0]['checks'] != report['runs'][1]['checks']:
            raise RuntimeError('optimized/sanitized count mismatch')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        RESULT.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    run()
