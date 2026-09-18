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
    'src/renderer/current_depth_pixel_program.h', 'src/renderer/current_depth_pixel_program_inc.h',
    'src/renderer/damage_motion_validation.h',
]
ROW_CHECKS = [
    'pair_and_per_stage_lookups_applied_identically',
    'all_original_words_preserved',
    'vertex_previous_clip_and_public_abi',            # ..._current_depth_... when the VS exports depth
    'authored_fragment_register_bits_literals_and_opcodes',  # authored_fragments_... when the PS writes depth
    'invalid_wrong_pair_truncated_appended_atomic_refusal',
    'twenty_one_row_perturbations_refused_atomically',  # twenty_six_... for a row with both depth sides
    None,  # program perturbations: class-dependent, see PROGRAM_PERTURBATIONS
    'mutations',  # full or sampled sweep, see MUTATION_CHECKS
    'six_input_output_alias_layouts_applied', 'cross_alias_refusal_atomic',
]
# Current-depth output (temporal step 1): a row whose VS exports the clip z/w
# interpolator adds one declaration and two dots (11 DWORDs) and three row
# perturbations plus two program perturbation sites; a row whose PS divides it
# into oC2 adds one declaration and the rcp/mul pair (10 DWORDs), two row
# perturbations and three program perturbation sites.
DEPTH_NONE = 255
VERTEX_DEPTH_WORDS, PIXEL_DEPTH_WORDS = 11, 13  # PS: dcl (3) + rcp (3) + mul (4) + the .zw mov (3) of the wide RT2 (shadow-receiver-depth.md)
VERTEX_DEPTH_ROW_PERTURBATIONS, PIXEL_DEPTH_ROW_PERTURBATIONS = 3, 2
VERTEX_DEPTH_PROGRAM_PERTURBATIONS, PIXEL_DEPTH_PROGRAM_PERTURBATIONS = 2, 3
DEPTH_CHECK_NAMES = {
    'vertex_previous_clip_and_public_abi': 'vertex_previous_clip_current_depth_and_public_abi',
    'authored_fragment_register_bits_literals_and_opcodes': 'authored_fragments_register_bits_literals_and_opcodes',
    'twenty_one_row_perturbations_refused_atomically': 'twenty_six_row_perturbations_refused_atomically',
}
# Rows the captured session drew (observed_scene_draws != 0 in the table) get
# every bit of every input DWORD; archive-only rows get a deterministic evenly
# strided sample of SAMPLED_MUTATIONS bit positions per program.
SAMPLED_MUTATIONS = 2048
MUTATION_CHECKS = {'full': 'every_input_dword_every_bit_atomic_refusals',
                   'sampled': 'sampled_input_bit_atomic_refusals'}
# Every class proves refusal of an unterminated vertex-side block before the
# position dots; straight-line classes prove refusal of a well-formed pixel
# static branch; class C additionally proves the balance, depth, opcode,
# condition and in-branch register refusals of the transformer's branch rule.
PROGRAM_PERTURBATIONS = {'A': 26, 'B': 26, 'C': 40, 'D': 1131}
PERTURBATION_CHECK = 'program_perturbations_refused_by_revalidation'
# Rows whose four position dots are not adjacent (`quad=spaced`) add two
# sites: the position temporary written between the dots and a balanced
# block between them, both refused by the transformer's span checks.
SPACED_QUAD_PERTURBATIONS = 2
CLASS_LETTER = {'ReferenceRegisters': 'A', 'RelocatedRegisters': 'B',
                'RelocatedRegistersWithBranches': 'C', 'BoundedDamageBranches': 'D'}
# Program perturbation sites an archive program may not offer; the fixture
# reports each as `SKIPPED row=<i> perturbation=<label>` and never for a
# captured row (observed_scene_draws != 0), whose programs offer every site.
ALLOWED_SKIPS = {
    'chosen TEXCOORD index declared', 'chosen output register written',
    'previous-row constant read', 'ABI constant defined', 'chosen temporary written',
    'ABI constant read', 'relative addressing in the pixel program',
    'chosen temporary written inside a branch', 'ABI constant read inside a branch',
    'chosen depth TEXCOORD index declared'}
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
    """Rows of the generated header: (vs, vs_dwords, ps, ps_dwords, class, observed
    draws, position quad adjacent, VS exports depth, PS writes depth)."""
    text = HEADER.read_text()
    heads = [(m[1], int(m[2]), m[3], int(m[4]), m[5]) for m in ROW_PATTERN.finditer(text)]
    quads = [[int(v) for v in m.split(',')] for m in re.findall(r'\{(\d+, \d+, \d+, \d+)\}, \{1, 2, 4, 8\}', text)]
    assert len(quads) == len(heads)
    # The last line of every row: the current-depth registers/index, the
    # depth_output flag and the observed_scene_draws metadata.
    tails = re.findall(r'^ (\d+), (\d+), (\d+), (true|false), (\d+)\},$', text, re.M)
    assert heads and len(tails) == len(heads), 'no rows parsed from the generated header'
    assert text.count('{0x') == len(heads)
    rows = []
    for head, tail, q in zip(heads, tails, quads):
        vertex_register, index, pixel_register, output, draws = int(tail[0]), int(tail[1]), int(tail[2]), tail[3] == 'true', int(tail[4])
        vertex_depth = vertex_register != DEPTH_NONE and index != DEPTH_NONE
        assert not output or (vertex_depth and pixel_register != DEPTH_NONE), 'depth output without its interpolator'
        rows.append((*head, draws, all(q[i] == q[i - 1] + 4 for i in range(1, 4)), vertex_depth, output))
    # The header's metadata must agree with the derived JSON it was rendered from.
    pairs = {(p['vs'], p['ps']): p for p in json.loads(PROFILES.read_text())['pairs']}
    assert all(pairs[(vs, ps)]['observed_draws'] == draws and pairs[(vs, ps)]['position_quad_contiguous'] == adjacent
               for vs, _, ps, _, _, draws, adjacent, _, _ in rows), 'header/JSON metadata differ'
    return rows


def row_inputs(rows):
    """Local program files per row with their SHA-256 (None when absent)."""
    programs = json.loads(PROFILES.read_text())['programs']
    result = []
    for vs, vs_dwords, ps, ps_dwords, klass, _, _, _, _ in rows:
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
    sweeps = {'full': 0, 'sampled': 0}
    at = 1
    for index, (row, files) in enumerate(zip(rows, inputs)):
        vs, vs_dwords, ps, ps_dwords, klass, observed, adjacent, vertex_depth, pixel_depth = row
        vd, pd = int(vertex_depth), int(pixel_depth)
        present = files['vs'] is not None and files['ps'] is not None
        if not present:
            missing = ''.join(f' {stage}' for stage in ('vs', 'ps') if files[stage] is None)
            assert lines[at] == f'ROW index={index} vs={vs} ps={ps} status=SKIP reason=missing_local_program{missing}', lines[at]
            results.append({'index': index, 'vs': vs, 'ps': ps, 'class': klass, 'status': 'SKIP',
                            'reason': 'missing_local_program' + missing})
            at += 1
            continue
        letter = CLASS_LETTER[klass]
        sweep = 'full' if observed else 'sampled'
        program_perturbations = (PROGRAM_PERTURBATIONS[letter] + (0 if adjacent else SPACED_QUAD_PERTURBATIONS) +
                                 vd * VERTEX_DEPTH_PROGRAM_PERTURBATIONS + pd * PIXEL_DEPTH_PROGRAM_PERTURBATIONS)
        row_perturbations = 21 + vd * VERTEX_DEPTH_ROW_PERTURBATIONS + pd * PIXEL_DEPTH_ROW_PERTURBATIONS
        names = {None: PERTURBATION_CHECK, 'mutations': MUTATION_CHECKS[sweep]}
        if vd:
            names['vertex_previous_clip_and_public_abi'] = DEPTH_CHECK_NAMES['vertex_previous_clip_and_public_abi']
        if pd:
            names['authored_fragment_register_bits_literals_and_opcodes'] = DEPTH_CHECK_NAMES['authored_fragment_register_bits_literals_and_opcodes']
        if vd and pd:
            names['twenty_one_row_perturbations_refused_atomically'] = DEPTH_CHECK_NAMES['twenty_one_row_perturbations_refused_atomically']
        expected = ['CHECK row=%d %s' % (index, names.get(name, name)) for name in ROW_CHECKS]
        if (vs, ps) == ARGON:
            expected.insert(1, 'CHECK row=%d %s' % (index, ARGON_CHECK))
        assert lines[at:at + len(expected)] == expected, (index, lines[at:at + len(expected)])
        at += len(expected)
        skips = []
        while lines[at].startswith(f'SKIPPED row={index} perturbation='):
            skips.append(lines[at].split('perturbation=', 1)[1])
            at += 1
        assert set(skips) <= ALLOWED_SKIPS and len(set(skips)) == len(skips), (index, skips)
        assert not (observed and skips), f'captured row {index} skipped {skips}'
        executed = program_perturbations - len(skips)
        row_mutations = 32 * (vs_dwords + ps_dwords) if sweep == 'full' else 2 * SAMPLED_MUTATIONS
        vertex_words, pixel_words = vs_dwords + 19 + vd * VERTEX_DEPTH_WORDS, ps_dwords + 132 + pd * PIXEL_DEPTH_WORDS
        assert lines[at] == (f'ROW index={index} vs={vs} ps={ps} class={letter} status=PASS '
                             f'vertex_words={vertex_words} pixel_words={pixel_words} mutations={row_mutations} '
                             f'sweep={sweep} program_perturbations={executed} skipped={len(skips)} '
                             f'quad={"adjacent" if adjacent else "spaced"} depth={vd + pd}'), lines[at]
        at += 1
        checks += len(expected); mutations += row_mutations; aliases += 6; sweeps[sweep] += 1
        results.append({'index': index, 'vs': vs, 'ps': ps, 'class': letter, 'status': 'PASS',
                        'observed_scene_draws': observed, 'mutation_sweep': sweep,
                        'checks': len(expected), 'vertex_words': vertex_words, 'pixel_words': pixel_words,
                        'single_bit_mutations': row_mutations, 'row_perturbations': row_perturbations,
                        'program_perturbations': executed, 'program_perturbation_sites': program_perturbations,
                        'position_quad_adjacent': adjacent,
                        'vertex_exports_depth': vertex_depth, 'pixel_writes_depth': pixel_depth,
                        'skipped_perturbations': skips, 'alias_layouts': 6})
    transformed = sum(r['status'] == 'PASS' for r in results)
    skipped = len(results) - transformed
    # Table-level lookup oracle (binary searches against a linear scan at every
    # row and at the search boundaries); runs for all rows, local programs or not.
    lookups = re.fullmatch(rf'TABLE lookups=PASS rows={len(rows)} absent_pairs=(\d+)', lines[at])
    assert lookups and int(lookups.group(1)) >= len(rows), lines[at]
    at += 1
    terminal = (f'RESULT PASS rows={len(rows)} transformed={transformed} skipped={skipped} '
                f'checks={checks} mutations={mutations} full_sweeps={sweeps["full"]} '
                f'sampled_sweeps={sweeps["sampled"]} aliases={aliases}')
    assert lines[at] == terminal and at == len(lines) - 1, (lines[at], terminal)
    assert any(r['status'] == 'PASS' and (r['vs'], r['ps']) == ARGON for r in results), 'Argon row must run'
    executed = sum(r.get('program_perturbations', 0) for r in results)
    skipped_sites = sum(len(r.get('skipped_perturbations', ())) for r in results)
    return results, {'checks': checks, 'single_bit_mutations': mutations, 'alias_layouts': aliases,
                     'program_perturbations': executed, 'skipped_perturbation_sites': skipped_sites,
                     'rows_with_skipped_sites': sum(bool(r.get('skipped_perturbations')) for r in results),
                     'full_sweep_rows': sweeps['full'], 'sampled_sweep_rows': sweeps['sampled'],
                     'sampled_mutations_per_program': SAMPLED_MUTATIONS,
                     'lookup_absent_pairs': int(lookups.group(1)),
                     'row_perturbations': sum(r.get('row_perturbations', 0) for r in results),
                     'depth_output_rows': sum(r['status'] == 'PASS' and r['pixel_writes_depth'] for r in results),
                     'motion_only_rows': sum(r['status'] == 'PASS' and not r['pixel_writes_depth'] for r in results),
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
                      expected_program_perturbations=PROGRAM_PERTURBATIONS,
                      spaced_quad_perturbations=SPACED_QUAD_PERTURBATIONS,
                      depth_perturbations={'vertex_row': VERTEX_DEPTH_ROW_PERTURBATIONS, 'pixel_row': PIXEL_DEPTH_ROW_PERTURBATIONS,
                                           'vertex_program': VERTEX_DEPTH_PROGRAM_PERTURBATIONS, 'pixel_program': PIXEL_DEPTH_PROGRAM_PERTURBATIONS},
                      depth_check_names=DEPTH_CHECK_NAMES,
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
        print('PASS material motion structure: %d rows (%d transformed, %d skipped; %d with depth output, %d motion-only) / %d checks / '
              '%d mutations (%d full-sweep rows, %d sampled rows at %d per program) / %d row perturbations / '
              '%d program perturbations (%d absent sites in %d rows) / %d aliases '
              'in release and ASan/UBSan' % (
                  totals['rows'], totals['transformed'], totals['skipped'], totals['depth_output_rows'], totals['motion_only_rows'],
                  totals['checks'], totals['single_bit_mutations'], totals['full_sweep_rows'], totals['sampled_sweep_rows'],
                  totals['sampled_mutations_per_program'], totals['row_perturbations'], totals['program_perturbations'],
                  totals['skipped_perturbation_sites'], totals['rows_with_skipped_sites'], totals['alias_layouts']))
    except Exception as error:
        report.update(result='FAIL', passed=False, error=str(error))
        save(report)
        raise


if __name__ == '__main__':
    main()
