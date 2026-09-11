#!/usr/bin/env python3
"""Fresh host-only structure/refusal checks for every profile-table row.

Local game bytes are read by the fixture at run time from the sweep directory
and never enter the repository or the reports; the summary records their
SHA-256 as provenance and cross-checks it against the derived profile JSON.
"""
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
HEADER = ROOT / 'src/renderer/motion_output_profiles_inc.h'
PROFILES = RESULTS / 'motion-output-profiles.json'
PROGRAMS = Path('/tmp/x3-shader-sweep/programs')
SOURCES = [
    'src/renderer/material_motion.cpp', 'src/renderer/material_motion.h',
    'src/renderer/motion_output_profiles.h', 'src/renderer/motion_output_profiles_inc.h',
    'src/renderer/rigid_motion_pixel_program.h',
    'src/renderer/rigid_motion_pixel_program_inc.h',
    'verification/probe/material_motion_structure.cpp',
    'verification/probe/run_material_motion_structure.py',
]
ROW_CHECKS = [
    'pair_and_per_stage_lookups_applied_identically',
    'all_original_words_preserved',
    'vertex_previous_clip_and_public_abi',
    'authored_fragment_register_bits_literals_and_opcodes',
    'invalid_wrong_pair_truncated_appended_atomic_refusal',
    'twenty_one_row_perturbations_refused_atomically',
    None,  # program perturbations: class-dependent, see PROGRAM_PERTURBATIONS
    'every_input_dword_every_bit_atomic_refusals',
    'six_input_output_alias_layouts_applied', 'cross_alias_refusal_atomic',
]
# Every class proves refusal of an unterminated vertex-side block before the
# position dots; straight-line classes prove refusal of a well-formed pixel
# static branch; class C additionally proves the balance, depth, opcode,
# condition and in-branch register refusals of the transformer's branch rule.
PROGRAM_PERTURBATIONS = {
    'A': (26, 'twenty_six_program_perturbations_refused_by_revalidation'),
    'B': (26, 'twenty_six_program_perturbations_refused_by_revalidation'),
    'C': (40, 'forty_program_perturbations_refused_by_revalidation')}
CLASS_LETTER = {'ReferenceRegisters': 'A', 'RelocatedRegisters': 'B',
                'RelocatedRegistersWithBranches': 'C'}
ARGON_CHECK = 'argon_output_byte_identical_to_previous_transformer'
ARGON = ('53a0a641107ed76c', '8759c7838bbc86c2')
ROW_PATTERN = re.compile(
    r'\{0x([0-9a-f]{16})ull, (\d+), 0xfffe0300u,\s*0x([0-9a-f]{16})ull, (\d+), 0xffff0300u,\s*'
    r'MotionOutputClass::(\w+),')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sources():
    return {name: sha(ROOT / name) for name in SOURCES}


def table_rows():
    """Rows of the generated header: (vs, vs_dwords, ps, ps_dwords, class)."""
    rows = [(m[1], int(m[2]), m[3], int(m[4]), m[5]) for m in ROW_PATTERN.finditer(HEADER.read_text())]
    assert rows, 'no rows parsed from the generated header'
    assert HEADER.read_text().count('{0x') == len(rows)
    return rows


def row_inputs(rows):
    """Local program files per row with their SHA-256 (None when absent)."""
    programs = json.loads(PROFILES.read_text())['programs']
    result = []
    for vs, vs_dwords, ps, ps_dwords, klass in rows:
        entry = {}
        for stage, fingerprint, dwords in (('vs', vs, vs_dwords), ('ps', ps, ps_dwords)):
            path = PROGRAMS / f'{stage}_{fingerprint}.bin'
            if path.exists():
                digest = sha(path)
                expected = programs[f'{stage}_{fingerprint}']
                assert path.stat().st_size == dwords * 4 and expected['dword_count'] == dwords
                assert expected['sha256'] == digest, f'{path} differs from the reviewed sweep'
                entry[stage] = {'path': str(path), 'sha256': digest, 'bytes': dwords * 4}
            else:
                entry[stage] = None
        result.append(entry)
    return result


def save(report):
    temporary = SUMMARY.with_suffix('.tmp')
    temporary.write_text(json.dumps(report, indent=2) + '\n')
    temporary.replace(SUMMARY)


def validate(text, rows, inputs):
    """Exact per-row check inventory and terminal line; returns row results."""
    lines = text.splitlines()
    assert lines and lines[0] == f'TABLE rows={len(rows)}'
    assert len(re.findall(r'^RESULT ', text, re.M)) == 1 and lines[-1].startswith('RESULT PASS ')
    results, checks, mutations, aliases = [], 0, 0, 0
    at = 1
    for index, (row, files) in enumerate(zip(rows, inputs)):
        vs, vs_dwords, ps, ps_dwords, klass = row
        present = files['vs'] is not None and files['ps'] is not None
        if not present:
            missing = ''.join(f' {stage}' for stage in ('vs', 'ps') if files[stage] is None)
            assert lines[at] == f'ROW index={index} vs={vs} ps={ps} status=SKIP reason=missing_local_program{missing}', lines[at]
            results.append({'index': index, 'vs': vs, 'ps': ps, 'class': klass, 'status': 'SKIP',
                            'reason': 'missing_local_program' + missing})
            at += 1
            continue
        letter = CLASS_LETTER[klass]
        program_perturbations, perturbation_check = PROGRAM_PERTURBATIONS[letter]
        expected = ['CHECK row=%d %s' % (index, name or perturbation_check) for name in ROW_CHECKS]
        if (vs, ps) == ARGON:
            expected.insert(1, 'CHECK row=%d %s' % (index, ARGON_CHECK))
        assert lines[at:at + len(expected)] == expected, (index, lines[at:at + len(expected)])
        at += len(expected)
        row_mutations = 32 * (vs_dwords + ps_dwords)
        vertex_words, pixel_words = vs_dwords + 19, ps_dwords + 132
        assert lines[at] == (f'ROW index={index} vs={vs} ps={ps} class={letter} status=PASS '
                             f'vertex_words={vertex_words} pixel_words={pixel_words} mutations={row_mutations} '
                             f'program_perturbations={program_perturbations}'), lines[at]
        at += 1
        checks += len(expected); mutations += row_mutations; aliases += 6
        results.append({'index': index, 'vs': vs, 'ps': ps, 'class': letter, 'status': 'PASS',
                        'checks': len(expected), 'vertex_words': vertex_words, 'pixel_words': pixel_words,
                        'single_bit_mutations': row_mutations, 'row_perturbations': 21,
                        'program_perturbations': program_perturbations, 'alias_layouts': 6})
    transformed = sum(r['status'] == 'PASS' for r in results)
    skipped = len(results) - transformed
    terminal = (f'RESULT PASS rows={len(rows)} transformed={transformed} skipped={skipped} '
                f'checks={checks} mutations={mutations} aliases={aliases}')
    assert lines[at] == terminal and at == len(lines) - 1, (lines[at], terminal)
    assert any(r['status'] == 'PASS' and (r['vs'], r['ps']) == ARGON for r in results), 'Argon row must run'
    return results, {'checks': checks, 'single_bit_mutations': mutations, 'alias_layouts': aliases,
                     'rows': len(rows), 'transformed': transformed, 'skipped': skipped}


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
        rows = table_rows()
        before, raw_before = sources(), row_inputs(rows)
        report.update(sources_before=before, programs_directory=str(PROGRAMS), inputs_before=raw_before,
                      profiles_json_sha256=sha(PROFILES),
                      compiler=subprocess.check_output([compiler, '--version'], text=True).strip(),
                      compiler_path=compiler, host_only=True,
                      expected_row_checks=[name or 'program_perturbations_by_class' for name in ROW_CHECKS],
                      expected_program_perturbations={k: v[0] for k, v in PROGRAM_PERTURBATIONS.items()},
                      expected_argon_check=ARGON_CHECK)
        # Input SHA256s are provenance; the transformer independently qualifies
        # each exact pair using its full-program fingerprint, size and structure.
        for mode, flags in [('release', ['-O2']), ('asan-ubsan', ['-O1', '-g',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
            executable = BUILD / mode
            executable.unlink(missing_ok=True)
            command = [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                       *flags, str(ROOT / SOURCES[0]), str(ROOT / SOURCES[6]),
                       '-o', str(executable)]
            subprocess.run(command, check=True, cwd=ROOT)
            if sources() != before or row_inputs(rows) != raw_before:
                raise RuntimeError('Source/input changed during build')
            exe_before = sha(executable)
            environment = dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            completed = subprocess.run([str(executable), str(PROGRAMS)],
                                       cwd=ROOT, env=environment, text=True,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       timeout=600)
            log = RESULTS / f'material-motion-structure-{mode}.txt'
            log.write_text(completed.stdout + completed.stderr)
            if completed.returncode != 0 or completed.stderr:
                raise RuntimeError(f'{mode} failed; see {log}')
            row_results, totals = validate(completed.stdout, rows, raw_before)
            if sha(executable) != exe_before or sources() != before or row_inputs(rows) != raw_before:
                raise RuntimeError('Source/input/executable changed during execution')
            report['runs'].append({'mode': mode, 'command': command, 'fresh_build': True,
                                   'executable': str(executable), 'executable_sha256_before': exe_before,
                                   'executable_sha256_after': sha(executable), **totals,
                                   'row_results': row_results,
                                   'report': str(log.relative_to(ROOT)), 'report_sha256': sha(log),
                                   'passed': True})
            save(report)
        totals = report['runs'][0]
        report.update(result='PASS', passed=True, sources_after=sources(), inputs_after=row_inputs(rows),
                      source_input_executable_unchanged=True)
        save(report)
        print('PASS material motion structure: %d rows (%d transformed, %d skipped) / %d checks / '
              '%d mutations / %d aliases in release and ASan/UBSan' % (
                  totals['rows'], totals['transformed'], totals['skipped'], totals['checks'],
                  totals['single_bit_mutations'], totals['alias_layouts']))
    except Exception as error:
        report.update(result='FAIL', passed=False, error=str(error))
        save(report)
        raise


if __name__ == '__main__':
    main()
