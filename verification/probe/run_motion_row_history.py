#!/usr/bin/env python3
"""Fresh host-only unit run of the live route's row history (release + ASan/UBSan)."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'verification/probe/build/motion-row-history'
RESULTS = ROOT / 'verification/results'
SUMMARY = RESULTS / 'motion-row-history-summary.json'
SOURCES = ['src/renderer/motion_row_history.h', 'src/renderer/motion_row_history.cpp',
           'src/renderer/motion_history.h', 'verification/probe/motion_row_history.cpp',
           'verification/probe/run_motion_row_history.py']
CASES = ['first_frame_records_without_history', 'match_consumes_previous_once', 'previous_duplicate_poisons_key',
         'invalid_keys_rejected', 'regime_change_and_failed_present_invalidate', 'capacity_overflow_fails_closed',
         'nonfinite_rows_and_uncommitted_frames_reject', 'not_ready_and_invalidate']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sources():
    return {name: sha(ROOT / name) for name in SOURCES}


def main():
    RESULTS.mkdir(parents=True, exist_ok=True)
    report = {'result': 'RUNNING', 'passed': False, 'game_launched': False, 'host_only': True, 'runs': []}
    SUMMARY.write_text(json.dumps(report, indent=2) + '\n')
    try:
        BUILD.mkdir(parents=True, exist_ok=True)
        compiler = shutil.which('clang++')
        if not compiler:
            raise RuntimeError('clang++ is required')
        before = sources()
        report.update(sources_before=before, compiler=subprocess.check_output([compiler, '--version'], text=True).strip(),
                      expected_checks=CASES)
        for mode, flags in [('release', ['-O2']), ('asan-ubsan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
            executable = BUILD / mode
            executable.unlink(missing_ok=True)
            command = [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                       str(ROOT / SOURCES[1]), str(ROOT / SOURCES[3]), '-o', str(executable)]
            subprocess.run(command, check=True, cwd=ROOT)
            if sources() != before:
                raise RuntimeError('Sources changed during build')
            exe_before = sha(executable)
            env = dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            completed = subprocess.run([str(executable)], cwd=ROOT, env=env, text=True,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
            log = RESULTS / f'motion-row-history-{mode}.txt'
            log.write_text(completed.stdout + completed.stderr)
            lines = completed.stdout.splitlines()
            terminal = f'RESULT PASS checks={len(CASES)}'
            if (completed.returncode != 0 or completed.stderr or lines != ['CHECK ' + c for c in CASES] + [terminal]
                    or len(re.findall(r'^RESULT ', completed.stdout, re.M)) != 1):
                raise RuntimeError(f'{mode} failed; see {log}')
            if sha(executable) != exe_before or sources() != before:
                raise RuntimeError('Executable/source changed during run')
            report['runs'].append({'mode': mode, 'command': command, 'executable_sha256': exe_before,
                                   'checks': len(CASES), 'report': str(log.relative_to(ROOT)), 'report_sha256': sha(log), 'passed': True})
        report.update(result='PASS', passed=True, sources_after=sources())
        print(f'PASS motion row history: {len(CASES)} check groups in release and ASan/UBSan')
    except Exception as error:
        report.update(result='FAIL', passed=False, error=str(error))
        raise
    finally:
        SUMMARY.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
