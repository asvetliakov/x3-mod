#!/usr/bin/env python3
"""Fresh host-only structure/refusal checks; local game bytes never enter reports."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'verification/probe/build/material-motion-structure'
RESULTS = ROOT / 'verification/results'
SUMMARY = RESULTS / 'material-motion-structure-summary.json'
SOURCES = [
    'src/renderer/material_motion.cpp', 'src/renderer/material_motion.h',
    'src/renderer/rigid_motion_pixel_program.h',
    'src/renderer/rigid_motion_pixel_program_inc.h',
    'verification/probe/material_motion_structure.cpp',
    'verification/probe/run_material_motion_structure.py',
]
CASE_NAMES = [
    'reviewed_pair_applied_545_1392', 'all_original_words_preserved',
    'vertex_previous_clip_and_public_abi',
    'authored_fragment_register_bits_literals_and_opcodes',
    'invalid_wrong_pair_truncated_appended_atomic_refusal',
    'every_input_dword_every_bit_57152_atomic_refusals',
    'six_input_output_alias_layouts_applied', 'cross_alias_refusal_atomic',
]
INPUTS = [Path('/tmp/x3-shader-sweep/programs') / name for name in
          ('vs_53a0a641107ed76c.bin', 'ps_8759c7838bbc86c2.bin')]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sources():
    return {name: sha(ROOT / name) for name in SOURCES}


def inputs():
    return {str(path): {'sha256': sha(path), 'bytes': path.stat().st_size}
            for path in INPUTS}


def save(report):
    temporary = SUMMARY.with_suffix('.tmp')
    temporary.write_text(json.dumps(report, indent=2) + '\n')
    temporary.replace(SUMMARY)


def main():
    RESULTS.mkdir(parents=True, exist_ok=True)
    report = {'result': 'RUNNING', 'passed': False, 'game_launched': False,
              'gpu_rendered': False, 'scope': 'Host structure and refusal only; no shader execution.',
              'runs': []}
    save(report)  # Invalidate older PASS before reading inputs or building.
    try:
        BUILD.mkdir(parents=True, exist_ok=True)
        compiler = shutil.which('clang++')
        if not compiler:
            raise RuntimeError('clang++ is required for the authored ASan/UBSan run')
        before, raw_before = sources(), inputs()
        report.update(sources_before=before, inputs_before=raw_before,
                      compiler=subprocess.check_output([compiler, '--version'], text=True).strip(),
                      compiler_path=compiler, host_only=True, expected_checks=CASE_NAMES)
        # Input SHA256s are provenance; the transformer independently qualifies
        # the exact pair using its full-program fingerprint, size and structure.
        for mode, flags in [('release', ['-O2']), ('asan-ubsan', ['-O1', '-g',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
            executable = BUILD / mode
            executable.unlink(missing_ok=True)
            command = [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                       *flags, str(ROOT / SOURCES[0]), str(ROOT / SOURCES[4]),
                       '-o', str(executable)]
            subprocess.run(command, check=True, cwd=ROOT)
            if sources() != before or inputs() != raw_before:
                raise RuntimeError('Source/input changed during build')
            exe_before = sha(executable)
            environment = dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            completed = subprocess.run([str(executable), *map(str, INPUTS)],
                                       cwd=ROOT, env=environment, text=True,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       timeout=180)
            log = RESULTS / f'material-motion-structure-{mode}.txt'
            log.write_text(completed.stdout + completed.stderr)
            lines = completed.stdout.splitlines()
            expected_terminal = 'RESULT PASS checks=8 mutations=57152 aliases=6 vertex_words=545 pixel_words=1392'
            if (completed.returncode != 0 or completed.stderr or
                    lines != ['CHECK ' + name for name in CASE_NAMES] + [expected_terminal] or
                    len(re.findall(r'^RESULT ', completed.stdout, re.M)) != 1):
                raise RuntimeError(f'{mode} failed exact check/terminal inventory; see {log}')
            if sha(executable) != exe_before or sources() != before or inputs() != raw_before:
                raise RuntimeError('Source/input/executable changed during execution')
            report['runs'].append({'mode': mode, 'command': command, 'fresh_build': True,
                                   'executable': str(executable), 'executable_sha256_before': exe_before,
                                   'executable_sha256_after': sha(executable), 'checks': 8,
                                   'single_bit_mutations': 57152, 'alias_layouts': 6,
                                   'report': str(log.relative_to(ROOT)), 'report_sha256': sha(log),
                                   'passed': True})
            save(report)
        report.update(result='PASS', passed=True, sources_after=sources(), inputs_after=inputs(),
                      source_input_executable_unchanged=True)
        save(report)
        print('PASS material motion structure: 8 groups / 57152 mutations / 6 aliases in release and ASan/UBSan')
    except Exception as error:
        report.update(result='FAIL', passed=False, error=str(error))
        save(report)
        raise


if __name__ == '__main__':
    main()
