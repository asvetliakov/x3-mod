"""Structure and Argon reference checks for the archive-wide motion-output profile table.

Runs under pytest or `python3 -m unittest`. The generated JSON is committed, so
these tests read it directly; they never touch local game bytecode or the
archives. Synthetic token fixtures exercise the parser and the pass-container
parser themselves, so a regression fails here even when the committed table is
stale. The JSON is queried, never printed.
"""
import json
from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from inspect_motion_output_profiles import (  # noqa: E402
    DEFERRED_CLASSES, HEADER_CLASSES, HEADER_FIELDS, REFERENCE, REFERENCE_DEFINITION_DWORDS,
    REFERENCE_POSITION_DWORDS, SM2_GROUPS, check, classify, header_rows, profile,
    render_header, sm2_feasibility)
from effect_passes import parse_effect  # noqa: E402

RESULT = ROOT / 'verification/results/motion-output-profiles.json'
HEADER = ROOT / 'src/renderer/motion_output_profiles_inc.h'
ARGON_VS, ARGON_PS = REFERENCE['vs'][0], REFERENCE['ps'][0]
CLASS_C = 'C_relocated_registers_with_static_branches'
# Archive facts the sweep established (docs/reverse-engineering/shader-sweep.md).
EFFECT_COUNT, PASS_COUNT, PROGRAM_COUNT = 3480, 6752, 751
# Archive-wide classification of the 180 SM3 pass pairings.
SM3_PAIRS = 180
CLASS_COUNTS = {'A_reference_registers': 56, 'B_relocated_registers': 101, CLASS_C: 12,
                'X_position_not_row_dot': 9, 'X_unsupported': 2}
# Rows whose four position dots are not adjacent (asteroid, moon and
# planet_haze light-free variants); the transformer revalidates the span.
SPACED_QUAD_ROWS = {('0c223ad11bce02d5', '7a0c3388065bb08d'), ('12b8a13f13fe8cfe', '550c2a4d4d3ed70f'),
                    ('233d17d26ce0c1fc', '7a0c3388065bb08d'), ('330ceb9dd874ede2', '550c2a4d4d3ed70f'),
                    ('8198903322dd82fb', '6aaaa2cb27e92cc8'), ('d706d31100be1be9', 'c002d0727537c5de')}
SM2_PAIRS, SM2_GROUP_COUNTS = 466, {'hostable_with_ps_2_0_fragment': 115,
                                    'hostable_with_ps_2_x_fragment': 267,
                                    'exceeds_limits': 16, 'structurally_unsupported': 68}
SM1_PAIRS, INCOMPLETE_PASSES = 168, 3
SCENE_DRAWS = 11493


def load():
    return json.loads(RESULT.read_text())


def words(*values):
    return struct.pack('<%dI' % len(values), *values)


def instruction(opcode, *operands):
    return [opcode | len(operands) << 24, *operands]


def source(kind, number, swizzle=0xe4):
    return 0x80000000 | (kind & 7) << 28 | (kind & 24) << 8 | swizzle << 16 | number


def destination(kind, number, mask=15):
    return 0x80000000 | (kind & 7) << 28 | (kind & 24) << 8 | mask << 16 | number


def synthetic_vertex(position_opcode=9, rows=(24, 25, 26, 27), gap=False, version=0xfffe0300):
    """A minimal vs_3_0: one DEF, three DCLs, a constructor and four row dots.
    With an SM2 version the position target is oPos (rastout 0) and the
    texture-coordinate output is oT2."""
    sm3 = version >> 8 & 255 == 3
    body = [instruction(0x51, destination(2, 42), 0x3f800000, 0, 0, 0),
            instruction(0x1f, 0, destination(1, 0))]
    if sm3:
        body += [instruction(0x1f, 5, destination(6, 2)), instruction(0x1f, 0, destination(6, 0))]
    body.append(instruction(4, destination(0, 1), source(1, 0, 0x24),
                            source(2, 42, 0x40), source(2, 42, 0x15)))
    target = (6, 0) if sm3 else (4, 0)
    for lane, row in enumerate(rows):
        body.append(instruction(position_opcode, destination(*target, 1 << lane),
                                source(0, 1), source(2, row)))
        if gap and lane == 1:
            body.append(instruction(1, destination(0, 3), source(0, 1)))
    body.append(instruction(1, destination(6, 2), source(0, 1)))
    return words(version, *(w for item in body for w in item), 0x0000ffff)


def synthetic_pixel(extra=(), version=0xffff0300, texture_input=None):
    body = [instruction(0x51, destination(2, 8), 0, 0, 0, 0),
            instruction(0x1f, 10, destination(1, 0)),
            instruction(0x1f, 5, destination(1, 1))]
    if texture_input is not None:
        body.append(instruction(0x1f, 0, destination(3, texture_input)))
    body += [*extra, instruction(1, destination(8, 0), source(1, 0))]
    return words(version, *(w for item in body for w in item), 0x0000ffff)


# Static branch tokens: `if` (0x28) takes one source, `else`/`endif` none.
IF_B0 = instruction(0x28, source(14, 0))
IF_B1 = instruction(0x28, source(14, 1))
ELSE, ENDIF = instruction(0x2a), instruction(0x2b)
MOVE = instruction(1, destination(0, 0), source(1, 0))
STATIC_BLOCKS = [*IF_B0, *MOVE, *ELSE, *MOVE, *ENDIF, *IF_B1, *MOVE, *ENDIF]


def synthetic_effect(passes):
    """A minimal D3DXFX 0xfeff0901 container whose technique passes bind the
    given shader blobs: passes is a list of {'vs': bytes|None, 'ps': bytes|None}."""
    magic = b'\x01\x09\xff\xfe'
    data = bytearray()  # Everything after the 8-byte header.
    def string(text):
        offset = len(data)
        raw = text.encode() + b'\0'
        data.extend(struct.pack('<I', len(raw)) + raw + b'\0' * (-len(raw) % 4))
        return offset
    technique_name = string('T')
    pass_names = [string('P%d' % index) for index in range(len(passes))]
    section = len(data)
    body = bytearray(struct.pack('<IIII', 0, 1, 0, 0))
    body += struct.pack('<III', technique_name, 0, len(passes))
    states = []
    for index, item in enumerate(passes):
        operations = [(146, item.get('vs')), (147, item.get('ps'))]
        body += struct.pack('<III', pass_names[index], 0, len(operations))
        for state_index, (operation, blob) in enumerate(operations):
            body += struct.pack('<IIII', operation, 0, 0, 0)
            if blob is not None:
                states.append((index, state_index, blob))
    body += struct.pack('<II', 0, len(states))
    for pass_index, state_index, blob in states:
        body += struct.pack('<IIIIII', 0, pass_index, 0, state_index, 0, len(blob))
        body += blob + b'\0' * (-len(blob) % 4)
    data.extend(body)
    return magic + struct.pack('<I', section) + bytes(data)


class MotionOutputProfileTests(unittest.TestCase):
    def setUp(self):
        self.result = load()
        self.programs = self.result['programs']
        self.pairs = self.result['pairs']

    # -- committed table structure ------------------------------------------
    def test_schema_and_provenance(self):
        assert self.result['schema'] == 2
        for key in ('scope', 'tool_sha256', 'inventory_sha256', 'limitations', 'pass_table',
                    'capture', 'transformation_classes', 'pairs', 'unsupported_pairs',
                    'sm2_pairs', 'sm2_groups', 'sm2_limits', 'sm1_pairs', 'sm1_models',
                    'incomplete_passes', 'programs', 'reference_check',
                    'captured_pairs_outside_archive'):
            assert key in self.result, key
        assert len(self.result['tool_sha256']) == 64
        assert len(self.result['inventory_sha256']) == 64
        assert self.result['limitations']
        table = self.result['pass_table']
        assert table['effect_count'] == EFFECT_COUNT and table['pass_count'] == PASS_COUNT
        assert table['status_counts'] == {'ps:null': 96, 'ps:program': 6656,
                                          'vs:not_a_program': 1, 'vs:program': 6751}

    def test_capture_metadata_is_derived_only_and_complete(self):
        capture = self.result['capture']
        assert len(capture['source_sha256']) == 64
        assert capture['source_bytes'] > 0
        assert capture['scene_segment'] == 1
        assert capture['scene_draw_count'] == self.result['scene_draw_total'] == SCENE_DRAWS
        assert capture['draw_count'] >= capture['scene_draw_count']
        # Every captured Scene draw lands on an archive pairing of some model.
        observed = sum(pair['observed_draws'] for key in ('pairs', 'sm2_pairs', 'sm1_pairs', 'incomplete_passes')
                       for pair in self.result[key])
        assert observed == SCENE_DRAWS
        assert self.result['captured_pairs_outside_archive'] == []

    def test_sm3_pairs_cover_the_archive_and_are_ordered(self):
        assert self.result['pair_count'] == len(self.pairs) == SM3_PAIRS
        assert self.pairs == sorted(self.pairs, key=lambda p: (-p['observed_draws'], p['vs'], p['ps']))
        assert len({(p['vs'], p['ps']) for p in self.pairs}) == SM3_PAIRS
        assert sum(pair['observed_draws'] > 0 for pair in self.pairs) == 20
        for pair in self.pairs:
            assert pair['vs_model'] == pair['ps_model'] == '3_0'
            effects = pair['effects']
            assert effects['pass_occurrences'] > 0 and effects['basenames'] and effects['techniques']
            assert effects['profile_directories'] == ['3_0']
            share = pair['scene_draw_share']
            assert share == round(pair['observed_draws'] / SCENE_DRAWS, 6)

    def test_every_pair_has_a_class_and_consistent_evidence(self):
        classes = self.result['transformation_classes']
        assert {name: item['pairs'] for name, item in classes.items()} == CLASS_COUNTS
        totals = {name: 0 for name in classes}
        for pair in self.pairs:
            name = pair['transformation_class']
            totals[name] += pair['observed_draws']
            blocked = name.startswith('X_')
            assert bool(pair['blocking_reasons']) == blocked, pair
            assert bool(pair['insertion_plan']) != blocked, pair
            assert pair['hosts_reference_registers'] == (name == 'A_reference_registers')
            for identifier in ('vs_' + pair['vs'], 'ps_' + pair['ps']):
                assert identifier in self.programs, identifier
        for name, summary in classes.items():
            assert summary['observed_draws'] == totals[name]
        assert sum(item['observed_draws'] for item in classes.values()) == 11334
        assert abs(sum(classes[name]['scene_share'] for name in HEADER_CLASSES) - 0.9764) < 1e-3

    def test_unsupported_sm3_pairs_carry_explicit_reasons(self):
        unsupported = self.result['unsupported_pairs']
        assert len(unsupported) == 11
        assert {(u['vs'], u['ps']) for u in unsupported} == {
            (p['vs'], p['ps']) for p in self.pairs if p['transformation_class'].startswith('X_')}
        for item in unsupported:
            assert item['blocking_reasons'] and item['basenames']
        reasons = {}
        for item in unsupported:
            for reason in item['blocking_reasons']:
                reasons[reason] = reasons.get(reason, 0) + 1
        assert reasons == {'vs_position_unknown:position_write_is_mov': 9,
                           'ps_no_free_input_register': 2,
                           'ps_control_flow_not_static_boolean_if': 2}
        # The only non-position refusals: the damage variants whose pixel
        # program also holds an `ifc` block (dynamic comparison, not a b# branch).
        other = [u for u in unsupported if u['transformation_class'] == 'X_unsupported']
        assert {u['ps'] for u in other} == {'31445adb0a62d134', 'd51cf763125cb85a'}
        for item in other:
            assert self.programs['ps_' + item['ps']]['control_flow_counts']['ifc'] == 1
            assert item['observed_draws'] == 0

    def test_insertion_plans_are_ordered_offsets(self):
        for pair in self.pairs:
            plan = pair['insertion_plan']
            if not plan:
                continue
            vertex = self.programs['vs_' + pair['vs']]
            pixel = self.programs['ps_' + pair['ps']]
            assert plan['vs_declaration_insert_dword'] == vertex['header_end_dword']
            assert plan['vs_declaration_insert_dword'] < plan['vs_arithmetic_insert_dword']
            assert plan['vs_arithmetic_insert_dword'] <= vertex['end_dword']
            assert (plan['ps_definition_insert_dword']
                    <= plan['ps_declaration_insert_dword'] < plan['ps_append_dword'])
            assert plan['ps_append_dword'] == pixel['end_dword'] == pixel['dword_count'] - 1
            assert 0 <= plan['vs_output_register'] < 12
            assert 0 <= plan['ps_input_register'] < 10
            assert len(set(plan['ps_temporaries'])) == 3
            assert plan['vs_output_register'] in vertex['free_output_registers']
            assert plan['ps_input_register'] in pixel['free_input_registers']
            assert set(plan['ps_temporaries']) <= set(pixel['free_temporaries'])
            assert plan['texcoord_index'] not in vertex['declared_texcoord_output_indices']
            assert plan['texcoord_index'] not in pixel['declared_texcoord_input_indices']
            # Two clip-row families: the point-light programs read c24-27 under
            # a relative light loop; the light-free variants read c0-3 directly.
            assert (plan['vs_matrix_register'], plan['light_loop_bound_required']) in ((24, True), (0, False))
            assert plan['light_loop_bound_required'] == vertex['relative_addressing']['present']
            assert plan['vs_matrix_register'] + 4 <= plan['vs_constant_base'] == 252
            assert plan['ps_constant_base'] == 216 and plan['ps_output_register'] == 1
            # Dots in XYZW order, each at least one instruction after the previous;
            # adjacent unless the pair is one of the spaced-quad rows, whose span
            # rewrites no position temporary and holds no control flow.
            dots = plan['vs_position_dp4_dwords']
            assert all(dots[i] >= dots[i - 1] + 4 for i in range(1, 4))
            assert plan['vs_arithmetic_insert_dword'] == dots[3] + 4
            spaced = (pair['vs'], pair['ps']) in SPACED_QUAD_ROWS
            assert pair['position_quad_contiguous'] == plan['vs_position_quad_contiguous'] == (not spaced)
            assert (dots == list(range(dots[0], dots[0] + 16, 4))) == (not spaced), pair['vs']
            assert vertex['position_temporary_rewritten_inside_quad'] == []
            assert vertex['position_flow_inside_quad'] == []
            assert ('vs_position_quad_not_contiguous' in pair['differences_from_reference']) == spaced

    def test_rows_sharing_a_program_agree_on_its_side(self):
        """Mirror of the static_assert in motion_output_profiles.h."""
        vertex_side = ('vs_declaration_insert_dword', 'vs_arithmetic_insert_dword', 'vs_matrix_register',
                       'vs_position_temporary', 'vs_position_dp4_dwords', 'vs_output_register',
                       'texcoord_index', 'light_loop_bound_required')
        pixel_side = ('ps_definition_insert_dword', 'ps_declaration_insert_dword', 'ps_append_dword',
                      'ps_input_register', 'ps_temporary_base', 'texcoord_index')
        by_vs, by_ps = {}, {}
        for pair in header_rows(self.result):
            plan = pair['insertion_plan']
            vertex = tuple(json.dumps(plan[key]) for key in vertex_side)
            pixel = tuple(json.dumps(plan[key]) for key in pixel_side)
            assert by_vs.setdefault(pair['vs'], vertex) == vertex, pair['vs']
            assert by_ps.setdefault(pair['ps'], pixel) == pixel, pair['ps']
        assert len(by_vs) == 32 and len(by_ps) == 108

    def test_program_facts_are_internally_consistent(self):
        assert len(self.programs) == PROGRAM_COUNT - 3  # Two z-only VS and the anomalous PS's partner are never paired.
        models = {}
        for identifier, program in self.programs.items():
            assert identifier.startswith(('vs_', 'ps_'))
            if not program.get('parsed'):
                assert program['reason']
                continue
            models[program['model']] = models.get(program['model'], 0) + 1
            assert program['fnv1a64'] == identifier.split('_', 1)[1]
            assert len(program['sha256']) == 64
            assert program['bytes'] == 4 * program['dword_count']
            assert program['end_dword'] == program['dword_count'] - 1
            assert 1 <= program['definition_end_dword'] <= program['header_end_dword']
            assert program['header_end_dword'] <= program['end_dword']
            assert program['executable_instruction_count'] <= program['instruction_count']
            assert program['comment_dword_count'] >= 0
            relative = program['relative_addressing']
            assert 'sites' not in relative and relative['site_count'] >= len(relative['base_registers'])
            assert relative['present'] == (relative['site_count'] > 0)
            if program['model'] == '3_0':
                for definition in program['literal_definitions']:
                    assert definition['end_dword'] == definition['dword'] + definition['dword_count']
                    assert definition['end_dword'] <= program['header_end_dword']
                declared = program['declared_inputs'] + program.get('declared_outputs', []) + program.get('declared_samplers', [])
                assert len(declared) + len(program.get('declared_misc_registers', [])) == program['declaration_count']
                for declaration in declared:
                    assert declaration['dword'] < program['header_end_dword']
            else:
                assert 'declarations' not in program and 'literal_definitions' not in program
        # Two z-only vertex programs and the anomalous pixel program's partner
        # occur only in incomplete passes and are never profiled.
        assert models == {'3_0': 158, '2_1': 227, '2_0': 171, '1_1': 168, '1_4': 24}

    def test_json_carries_no_shader_words(self):
        versions = {'0xfffe0101', '0xfffe0200', '0xfffe0201', '0xfffe0300',
                    '0xffff0101', '0xffff0104', '0xffff0200', '0xffff0201', '0xffff0300'}
        keys = set()

        def walk(node):
            if isinstance(node, dict):
                for key, value in node.items():
                    keys.add(key)
                    walk(value)
            elif isinstance(node, list):
                for value in node:
                    walk(value)

        walk(self.result)
        for forbidden in ('token', 'tokens', 'words', 'literal_values', 'bytecode',
                          'disassembly', 'instructions', 'body'):
            assert forbidden not in keys, forbidden
        assert set(re.findall(r'"0x[0-9a-fA-F]+"', RESULT.read_text())) <= {
            '"%s"' % version for version in versions}
        for program in self.programs.values():
            if program.get('parsed'):
                assert program['version'] in versions

    # -- SM2 feasibility and SM1 remainder ------------------------------------
    def test_sm2_feasibility_records(self):
        pairs = self.result['sm2_pairs']
        assert self.result['sm2_pair_count'] == len(pairs) == SM2_PAIRS
        groups = self.result['sm2_groups']
        assert {name: item['pairs'] for name, item in groups.items()} == SM2_GROUP_COUNTS
        limits = self.result['sm2_limits']
        assert limits['fragment'] == {'pixel_arithmetic': 25, 'pixel_texture': 0, 'pixel_temporaries': 3,
                                      'pixel_constants': 5, 'vertex_instructions': 4}
        assert limits['pixel_profiles']['2_0'] == {'arithmetic_slots': 64, 'texture_slots': 32,
                                                    'temporaries': 12, 'constants': 32}
        for pair in pairs:
            assert pair['group'] in SM2_GROUPS
            assert pair['vs_model'] in ('2_0', '2_1') and pair['ps_model'] in ('2_0', '2_1')
            assert pair['pixel_profile'] in ('2_0', '2_a', '2_b')
            assert (pair['pixel_profile'] == '2_0') == (pair['ps_model'] == '2_0')
            assert bool(pair['reasons']) == (pair['group'] == 'structurally_unsupported')
            assert (pair['group'] == 'exceeds_limits') == (bool(pair['exceeded']) and not pair['reasons'])
            assert all(value > 0 for value in pair['exceeded'].values())
            pixel = pair['pixel']
            budget = pixel['with_fragment']
            if pair['pixel_profile'] == '2_0':
                assert budget['arithmetic_slots'] == pixel['arithmetic_slots'] + 25
                assert budget['texture_slots'] == pixel['texture_slots']
            else:
                assert budget['instruction_slots'] == pixel['arithmetic_slots'] + pixel['texture_slots'] + 25
            if pair['group'].startswith('hostable'):
                assert pair['vertex']['previous_clip_link_indices']
                assert pixel['free_temporary_count'] >= 3 and pixel['free_constant_count'] >= 5
                assert pixel['oC1_free'] and pixel['color_outputs'] == [0]
                assert not pixel['texkill_dwords'] and not pixel['depth_output_dwords']
                assert pair['position']['shape'] == 'row_dot_quad'
            assert pair['group'] != 'hostable_with_ps_2_0_fragment' or budget['arithmetic_slots'] <= 64
        assert self.result['sm2_reasons'] == {'no_free_oT_t_link_index': 32,
                                              'vs_position_issue_order_wxyz': 16,
                                              'vs_position_unknown:position_write_is_mov': 20}
        # The same span rule as the SM3 classifier: of the 30 spaced-quad SM2
        # pairs, 14 (asteroid_0000, moon_0000, adeffects) are hostable; the
        # effects/engine ones still issue WXYZ.
        spaced = [p for p in pairs if p['position'].get('contiguous_quad') is False]
        assert len(spaced) == 30 and sum(p['group'].startswith('hostable') for p in spaced) == 14
        assert self.result['sm2_exceeded'] == {'arithmetic_slots': 30, 'temporaries': 5}
        assert max(p['exceeded'].get('arithmetic_slots', 0) for p in pairs if p['group'] == 'exceeds_limits') == 24
        # The two captured SM2 pairs: effects/engine issues WXYZ (refused);
        # adeffects has a spaced quad and would be hostable with a ps_2_0 fragment.
        observed = {(p['vs'], p['ps']): (p['observed_draws'], p['group'], p['reasons']) for p in pairs if p['observed_draws']}
        assert observed == {
            ('d5e1c75351ed3f04', '8360f422de08b5bd'): (103, 'structurally_unsupported',
                                                       ['vs_position_issue_order_wxyz']),
            ('ac2319bc3953efc6', '03a16e5c63daa6e8'): (8, 'hostable_with_ps_2_0_fragment', [])}

    def test_sm1_and_incomplete_passes(self):
        pairs = self.result['sm1_pairs']
        assert self.result['sm1_pair_count'] == len(pairs) == SM1_PAIRS
        assert all(p['vs_model'] == '1_1' and p['ps_model'] in ('1_1', '1_4') for p in pairs)
        assert self.result['sm1_models'] == {'1_1': {'pairs': 168, 'observed_draws': 48, 'pass_occurrences': 2968,
                                                     'scene_share': round(48 / SCENE_DRAWS, 6)}}
        assert 'oC1' in self.result['sm1_reason']
        assert {(p['vs'], p['ps']): p['observed_draws'] for p in pairs if p['observed_draws']} == {
            ('5e484a06672e28fb', '0a523f33ac47ae05'): 24, ('36f98d151fd6b0c6', '222bee0defcb1852'): 20,
            ('be199829a9bb78db', 'cd6d6eb4b3d99443'): 4}
        incomplete = self.result['incomplete_passes']
        assert len(incomplete) == INCOMPLETE_PASSES
        assert sorted(i['reason'] for i in incomplete) == ['pass_without_ps', 'pass_without_ps', 'pass_without_vs']
        anomalous = next(i for i in incomplete if i['reason'] == 'pass_without_vs')
        assert anomalous['ps'] == 'd66dd16fc0a6c3a3' and anomalous['vs_status'] == ['not_a_program']
        assert all(i['effects']['basenames'][0].startswith('z_only') for i in incomplete if i['reason'] == 'pass_without_ps')
        assert sum(i['effects']['pass_occurrences'] for i in incomplete) == 97

    # -- Argon reference numbers from the candidate review -------------------
    def test_reference_check_recorded_and_reproducible(self):
        assert self.result['reference_check'] == {
            'pair': 'argon_sm3', 'passed': True, 'failures': []}
        assert check(self.result) == []

    def test_argon_vertex_offsets(self):
        vertex = self.programs[ARGON_VS]
        assert vertex['model'] == '3_0'
        assert vertex['version'] == '0xfffe0300'
        assert vertex['dword_count'] == 526
        assert vertex['header_end_dword'] == 335
        assert vertex['definition_end_dword'] == 308
        assert vertex['literal_definitions'][0]['dword'] == REFERENCE_DEFINITION_DWORDS['vs'] == 302
        assert vertex['literal_definitions'][0]['dword_count'] == 6
        assert vertex['literal_definitions'][0]['register'] == 42
        assert sorted(d['dword'] for d in vertex['declared_inputs'] + vertex['declared_outputs']) == list(range(308, 335, 3))
        assert vertex['highest_direct_constant'] == 42
        assert vertex['free_output_registers'] == [6, 7, 8, 9, 10, 11]
        assert vertex['reserved_constants'] == [252, 253, 254, 255]
        assert vertex['reserved_constants_free'] is True
        assert vertex['vertex_texture_fetch_dwords'] == []
        assert [(d['register'], d['usage_name'], d['usage_index'])
                for d in vertex['declared_inputs']] == [
                    (0, 'position', 0), (1, 'texcoord', 0), (2, 'normal', 0)]
        assert [(d['register'], d['usage_name'], d['usage_index'])
                for d in vertex['declared_outputs']] == [
                    (0, 'position', 0), (1, 'color', 0), (2, 'texcoord', 0),
                    (3, 'texcoord', 1), (4, 'texcoord', 2), (5, 'texcoord', 3)]
        assert len(vertex['centroid_declarations']) == 3

    def test_argon_position_site(self):
        site = self.programs[ARGON_VS]['position_output']
        assert site['shape'] == 'row_dot_quad'
        assert site['dwords_xyzw'] == REFERENCE_POSITION_DWORDS == [450, 454, 458, 462]
        assert site['rows_xyzw'] == [24, 25, 26, 27]
        assert site['rows_consecutive'] is True
        assert site['source_temporary'] == 1
        assert site['issue_order'] == 'xyzw'
        assert site['contiguous_quad'] is True
        assert site['insertion_dword'] == 466
        assert self.programs[ARGON_VS]['matrix_register'] == 24
        assert self.programs[ARGON_VS]['position_temporary_rewritten_inside_quad'] == []
        for lane, item in zip('xyzw', site['operands_xyzw']):
            assert item['opcode'] == 'dp4'
            assert item['destination']['name'] == 'o0'
            assert item['destination']['mask'] == lane
            assert [s['name'] for s in item['sources']] == [
                'r1', 'c%d' % (24 + 'xyzw'.index(lane))]
            assert [s['swizzle'] for s in item['sources']] == ['xyzw', 'xyzw']

    def test_argon_relative_light_loop_is_reported_unbounded(self):
        relative = self.programs[ARGON_VS]['relative_addressing']
        assert relative['present'] is True
        assert relative['address_registers'] == ['a0']
        assert relative['rep_count'] == 1
        assert relative['mova_count'] == 1
        assert relative['loop_bound_integer_registers'] == [0]
        assert relative['bounded_by_static_analysis'] is False
        assert relative['base_registers'] == [0, 1, 2] and relative['site_count'] == 3

    def test_argon_pixel_offsets(self):
        pixel = self.programs[ARGON_PS]
        assert pixel['model'] == '3_0'
        assert pixel['dword_count'] == 1260
        assert pixel['literal_definitions'][0]['dword'] == REFERENCE_DEFINITION_DWORDS['ps'] == 1041
        assert pixel['definition_end_dword'] == 1047
        assert pixel['header_end_dword'] == 1074
        assert sorted(d['dword'] for d in pixel['declared_inputs'] + pixel['declared_samplers']) == list(range(1047, 1074, 3))
        assert pixel['append_dword'] == pixel['end_dword'] == 1259
        assert [d['register'] for d in pixel['declared_inputs']] == [0, 1, 2, 3, 4]
        assert pixel['declared_misc_registers'] == []
        assert [d['register'] for d in pixel['declared_samplers']] == [0, 1, 2, 3]
        assert pixel['temporary_registers'] == [0, 1, 2, 3, 4]
        assert pixel['highest_direct_constant'] == 8
        assert pixel['color_outputs'] == [0]
        assert pixel['depth_output_dwords'] == []
        assert pixel['texkill_dwords'] == []
        assert pixel['control_flow_counts'] == {}
        assert pixel['free_input_registers'] == [5, 6, 7, 8, 9]
        assert pixel['free_temporaries'][:3] == [5, 6, 7]
        assert pixel['reserved_constants'] == [216, 217, 218, 219, 220]
        assert all(pixel[key] for key in ('reserved_constants_free', 'reserved_temporaries_free',
                                          'reserved_input_free', 'reserved_output_free'))

    def test_argon_pair_uses_the_reference_registers(self):
        pair = next(p for p in self.pairs if p['vs'] == ARGON_VS[3:] and p['ps'] == ARGON_PS[3:])
        assert pair['transformation_class'] == 'A_reference_registers'
        assert pair['hosts_reference_registers'] is True
        assert pair['observed_draws'] == 2180
        assert pair['blocking_reasons'] == []
        assert pair['differences_from_reference'] == []
        assert pair['vs_relative_light_loop'] is True
        assert pair['vs_loop_bound_integer_registers'] == [0]
        assert pair['effects']['basenames'] == ['argon'] and pair['effects']['techniques'] == ['DEFAULT']
        assert pair['insertion_plan'] == {
            'vs_output_register': 6, 'ps_input_register': 5, 'ps_temporary_base': 5,
            'ps_temporaries': [5, 6, 7], 'texcoord_index': 4,
            'vs_declaration_insert_dword': 335, 'vs_arithmetic_insert_dword': 466,
            'vs_matrix_register': 24, 'vs_position_temporary': 1,
            'vs_position_dp4_dwords': [450, 454, 458, 462],
            'vs_position_lane_masks': [1, 2, 4, 8], 'vs_constant_base': 252,
            'ps_constant_base': 216, 'ps_output_register': 1,
            'ps_definition_insert_dword': 1047, 'ps_declaration_insert_dword': 1074,
            'ps_append_dword': 1259, 'light_loop_bound_required': True,
            'light_loop_max_count': 8, 'vs_position_quad_contiguous': True}

    # -- parser behaviour on original synthetic fixtures ---------------------
    def test_synthetic_pair_reproduces_the_reference_shape(self):
        vertex = profile(synthetic_vertex(), 'vs_test', 'vs', '3_0')
        pixel = profile(synthetic_pixel(), 'ps_test', 'ps', '3_0')
        assert vertex['parsed'] and pixel['parsed']
        site = vertex['position_output']
        assert site['shape'] == 'row_dot_quad'
        assert site['contiguous_quad'] is True
        assert site['rows_xyzw'] == [24, 25, 26, 27]
        assert site['source_temporary'] == 1
        name, blocking, differences, plan = classify(vertex, pixel)
        assert blocking == []
        assert name == 'A_reference_registers'
        assert plan['vs_arithmetic_insert_dword'] == site['insertion_dword']

    def test_synthetic_non_row_dot_position_is_rejected(self):
        vertex = profile(synthetic_vertex(position_opcode=1), 'vs_test', 'vs', '3_0')
        assert vertex['position_output']['shape'] == 'unknown'
        assert vertex['position_output']['reason'] == 'position_write_is_mov'
        name, blocking, _, plan = classify(vertex, profile(synthetic_pixel(), 'ps_test', 'ps', '3_0'))
        assert name == 'X_position_not_row_dot'
        assert plan == {}
        assert blocking

    def test_synthetic_non_consecutive_rows_and_split_quad_are_reported(self):
        vertex = profile(synthetic_vertex(rows=(24, 25, 26, 30)), 'vs_test', 'vs', '3_0')
        assert vertex['position_output']['rows_consecutive'] is False
        split = profile(synthetic_vertex(gap=True), 'vs_test', 'vs', '3_0')
        assert split['position_output']['contiguous_quad'] is False

    def test_synthetic_reserved_register_collisions_block(self):
        pixel = profile(synthetic_pixel(
            extra=[instruction(1, destination(0, 6), source(1, 0)),
                   instruction(1, destination(8, 1), source(0, 6))]),
            'ps_test', 'ps', '3_0')
        assert 6 in pixel['temporary_registers']
        assert pixel['color_outputs'] == [0, 1]
        name, blocking, _, _ = classify(profile(synthetic_vertex(), 'vs_test', 'vs', '3_0'), pixel)
        assert name == 'X_unsupported'
        assert 'ps_output_oC1_used' in blocking

    def test_synthetic_texkill_and_depth_block(self):
        for extra, reason in ((instruction(0x41, source(1, 0)), 'ps_texkill'),
                              (instruction(1, destination(9, 0, 1), source(1, 0)),
                               'ps_depth_output')):
            pixel = profile(synthetic_pixel(extra=[extra]), 'ps_test', 'ps', '3_0')
            _, blocking, _, _ = classify(profile(synthetic_vertex(), 'vs_test', 'vs', '3_0'), pixel)
            assert reason in blocking, (reason, blocking)

    def test_class_c_programs_hold_static_boolean_blocks(self):
        rows = [p for p in self.pairs if p['transformation_class'] == CLASS_C]
        assert len(rows) == 12 and sum(p['observed_draws'] for p in rows) == 4680
        shapes = {}
        for pair in rows:
            pixel = self.programs['ps_' + pair['ps']]
            branches = pixel['static_branches']
            blocks = pixel['control_flow_counts']['if']
            assert pixel['control_flow_counts'] == {'else': blocks, 'endif': blocks, 'if': blocks}
            assert pixel['control_flow_balanced'] is True
            assert pixel['control_flow_max_depth'] == 1
            assert pixel['control_flow_depth_at_end'] == 0
            assert branches['only_boolean_if'] is True
            assert branches['boolean_conditions'] == ['b0', 'b1'][:blocks]
            assert [s['opcode'] for s in branches['sites']] == ['if', 'else', 'endif'] * blocks
            assert all(s['dword'] < pixel['append_dword'] for s in branches['sites'])
            assert pixel['texkill_dwords'] == [] and pixel['depth_output_dwords'] == []
            assert pixel['predicated_or_coissued_dwords'] == []
            assert pixel['relative_addressing']['present'] is False
            assert 'ps_control_flow_else,endif,if' in pair['differences_from_reference']
            shapes[blocks] = shapes.get(blocks, 0) + 1
        assert shapes == {2: 6, 1: 6}
        # The four captured rows are among them, and every class C row is an
        # xt_* material of the 3_0 directory.
        assert {(p['vs'], p['ps']) for p in rows} >= {
            ('494fe349b8bc12ec', 'fffdabd910793aba'), ('37c34a7478544c14', '5f82ecacd39529cd'),
            ('37c34a7478544c14', 'f1b0e820c7b488c3'), ('494fe349b8bc12ec', 'e6794b6ec37ff71a')}
        assert all(b.startswith('xt_') for p in rows for b in p['effects']['basenames'])

    def test_synthetic_static_boolean_blocks_are_class_c(self):
        pixel = profile(synthetic_pixel(extra=[STATIC_BLOCKS]), 'ps_test', 'ps', '3_0')
        assert pixel['control_flow_balanced'] and pixel['control_flow_max_depth'] == 1
        assert pixel['static_branches']['only_boolean_if'] is True
        assert pixel['static_branches']['boolean_conditions'] == ['b0', 'b1']
        name, blocking, differences, plan = classify(
            profile(synthetic_vertex(), 'vs_test', 'vs', '3_0'), pixel)
        assert name == CLASS_C and blocking == [], blocking
        assert 'ps_control_flow_else,endif,if' in differences
        assert plan['ps_append_dword'] == pixel['end_dword']

    def test_synthetic_other_control_flow_is_not_class_c(self):
        vertex = profile(synthetic_vertex(), 'vs_test', 'vs', '3_0')
        cases = {
            'nested': [*IF_B0, *IF_B1, *MOVE, *ENDIF, *ENDIF],
            'if_comp': [*instruction(0x29, source(1, 0), source(1, 0)), *MOVE, *ENDIF],
            'float_condition': [*instruction(0x28, source(2, 8)), *MOVE, *ENDIF],
            'predicate_condition': [*instruction(0x28, source(19, 0)), *MOVE, *ENDIF],
            'rep': [*instruction(0x26, source(7, 0)), *MOVE, *instruction(0x27)],
            'break_inside': [*IF_B0, *instruction(0x2c), *ENDIF],
            'missing_endif': [*IF_B0, *MOVE],
            'else_without_if': [*ELSE, *MOVE],
        }
        for label, extra in cases.items():
            pixel = profile(synthetic_pixel(extra=[extra]), 'ps_test', 'ps', '3_0')
            name, blocking, _, plan = classify(vertex, pixel)
            assert name.startswith('X_') and blocking and plan == {}, (label, name, blocking)
            if label == 'nested':
                assert 'ps_control_flow_depth_2_exceeds_1' in blocking
            elif label in ('missing_endif', 'else_without_if'):
                assert 'ps_control_flow_not_balanced' in blocking
            else:
                assert 'ps_control_flow_not_static_boolean_if' in blocking, (label, blocking)

    def test_synthetic_sm2_pair_is_hostable_and_links_by_index(self):
        vertex = profile(synthetic_vertex(version=0xfffe0200), 'vs_test', 'vs', '2_0')
        pixel = profile(synthetic_pixel(version=0xffff0200, texture_input=0), 'ps_test', 'ps', '2_0')
        assert vertex['parsed'] and pixel['parsed']
        assert vertex['position_output']['shape'] == 'row_dot_quad'
        assert vertex['free_texcoord_outputs'] == [0, 1, 3, 4, 5, 6, 7]  # oT2 is written.
        assert pixel['free_texture_inputs'] == [1, 2, 3, 4, 5, 6, 7]  # t0 is declared.
        record = sm2_feasibility(vertex, pixel, ['2_0'])
        assert record['group'] == 'hostable_with_ps_2_0_fragment' and record['reasons'] == []
        assert record['vertex']['previous_clip_link_indices'] == [1, 3, 4, 5, 6, 7]
        assert record['pixel']['with_fragment'] == {'arithmetic_slots': 26, 'texture_slots': 0}
        assert record['pixel']['free_temporary_count'] == 12 and record['pixel']['free_constant_count'] == 31
        # The same programs seen from a 2_x effect directory use the 2_x budget.
        assert sm2_feasibility(vertex, pixel, ['2_a'])['pixel_profile'] == '2_0'
        # A pixel program using every t# register cannot receive the carrier.
        crowded = profile(synthetic_pixel(version=0xffff0200, extra=[
            instruction(1, destination(0, 0), source(3, n)) for n in range(8)]), 'ps_test', 'ps', '2_0')
        blocked = sm2_feasibility(vertex, crowded, ['2_0'])
        assert blocked['group'] == 'structurally_unsupported' and 'no_free_oT_t_link_index' in blocked['reasons']
        # Budget: a program with 40 arithmetic instructions plus the 25-instruction
        # fragment exceeds ps_2_0 by one slot.
        busy = profile(synthetic_pixel(version=0xffff0200, texture_input=0, extra=[MOVE] * 39), 'ps_test', 'ps', '2_0')
        over = sm2_feasibility(vertex, busy, ['2_0'])
        assert over['group'] == 'exceeds_limits' and over['exceeded'] == {'arithmetic_slots': 1}

    def test_synthetic_effect_container_yields_pass_pairings(self):
        vertex, pixel = synthetic_vertex(), synthetic_pixel()
        passes = parse_effect(synthetic_effect([{'vs': vertex, 'ps': pixel}, {'vs': vertex, 'ps': None},
                                                {'vs': b'\x01\x02\x03\x04', 'ps': pixel}]))
        assert [(p['technique'], p['pass']) for p in passes] == [('T', 'P0'), ('T', 'P1'), ('T', 'P2')]
        first = {stage: (fingerprint, dwords, status) for _, stage, fingerprint, dwords, status in passes[0]['states']}
        assert first['vs'][2] == first['ps'][2] == 'program'
        assert first['vs'][1] == len(vertex) // 4 and first['ps'][1] == len(pixel) // 4
        assert first['vs'][0] == profile(vertex, 'vs_test', 'vs', '3_0')['fnv1a64']
        assert [s[4] for s in passes[1]['states']] == ['program', 'null']
        assert [s[4] for s in passes[2]['states']] == ['not_a_program', 'program']
        with self.assertRaises(ValueError):
            parse_effect(b'\x00\x00\x00\x00')

    def test_malformed_programs_fail_closed(self):
        for code, label in ((b'\x00\x03\xfe\xff', 'no_end'),
                            (words(0xfffe0300, 0x0000ffff, 0x0000ffff), 'trailing'),
                            (b'\xfe\xff\x00', 'unaligned')):
            result = profile(code, 'vs_test', 'vs', '3_0')
            assert result['parsed'] is False, label
            assert result['reason'], label
            name, blocking, _, plan = classify(result, profile(synthetic_pixel(), 'p', 'ps', '3_0'))
            assert name == 'X_unparsed' and plan == {} and blocking


def parse_header(text):
    """Split the generated row list into flat comma-separated token lists."""
    body = '\n'.join(line for line in text.splitlines() if not line.startswith('//'))
    rows, depth, start = [], 0, None
    for index, character in enumerate(body):
        if character == '{':
            if depth == 0:
                start = index + 1
            depth += 1
        elif character == '}':
            depth -= 1
            if depth == 0:
                rows.append(body[start:index])
    return [[token.strip() for token in row.replace('{', '').replace('}', '').split(',')
             if token.strip()] for row in rows]


ARGON_HEADER_ROW = [
    '0x53a0a641107ed76cull', '526', '0xfffe0300u',
    '0x8759c7838bbc86c2ull', '1260', '0xffff0300u',
    'MotionOutputClass::ReferenceRegisters', '24', '1',
    '450', '454', '458', '462', '1', '2', '4', '8',
    '335', '466', '1047', '1074', '1259',
    '6', '4', '5', '5', '1', '252', '216', 'true', '8', '2180']

# The sixteen rows the capture-derived table emitted (commit 66d91a4), without
# the trailing observed_scene_draws field the archive-wide table added. Their
# fields and relative order must survive regeneration from the archive.
OBSERVED_ROWS = [
    ['0x494fe349b8bc12ecull', '526', '0xfffe0300u', '0xfffdabd910793abaull', '1648', '0xffff0300u', 'MotionOutputClass::RelocatedRegistersWithBranches', '24', '1', '450', '454', '458', '462', '1', '2', '4', '8', '335', '466', '1280', '1313', '1647', '6', '4', '6', '6', '1', '252', '216', 'true', '8'],
    ['0x53a0a641107ed76cull', '526', '0xfffe0300u', '0x8759c7838bbc86c2ull', '1260', '0xffff0300u', 'MotionOutputClass::ReferenceRegisters', '24', '1', '450', '454', '458', '462', '1', '2', '4', '8', '335', '466', '1047', '1074', '1259', '6', '4', '5', '5', '1', '252', '216', 'true', '8'],
    ['0x37c34a7478544c14ull', '768', '0xfffe0300u', '0x5f82ecacd39529cdull', '1765', '0xffff0300u', 'MotionOutputClass::RelocatedRegistersWithBranches', '24', '2', '633', '637', '641', '645', '1', '2', '4', '8', '500', '649', '1325', '1370', '1764', '9', '7', '8', '6', '1', '252', '216', 'true', '8'],
    ['0x4944d81dfe531b37ull', '556', '0xfffe0300u', '0xca6bfa4a6cca7e2aull', '1328', '0xffff0300u', 'MotionOutputClass::RelocatedRegisters', '24', '2', '477', '481', '485', '489', '1', '2', '4', '8', '344', '493', '1062', '1095', '1327', '7', '5', '6', '7', '1', '252', '216', 'true', '8'],
    ['0xb0602757fce6e870ull', '520', '0xfffe0300u', '0x517540ae6d5e5410ull', '397', '0xffff0300u', 'MotionOutputClass::ReferenceRegisters', '24', '1', '441', '445', '449', '453', '1', '2', '4', '8', '335', '457', '233', '257', '396', '6', '4', '5', '5', '1', '252', '216', 'true', '8'],
    ['0x4944d81dfe531b37ull', '556', '0xfffe0300u', '0x5e0a10fe752b6140ull', '1354', '0xffff0300u', 'MotionOutputClass::RelocatedRegisters', '24', '2', '477', '481', '485', '489', '1', '2', '4', '8', '344', '493', '1062', '1098', '1353', '7', '5', '6', '7', '1', '252', '216', 'true', '8'],
    ['0x37c34a7478544c14ull', '768', '0xfffe0300u', '0xf1b0e820c7b488c3ull', '1791', '0xffff0300u', 'MotionOutputClass::RelocatedRegistersWithBranches', '24', '2', '633', '637', '641', '645', '1', '2', '4', '8', '500', '649', '1325', '1373', '1790', '9', '7', '8', '6', '1', '252', '216', 'true', '8'],
    ['0x53a0a641107ed76cull', '526', '0xfffe0300u', '0x63f96eba9eea7880ull', '1292', '0xffff0300u', 'MotionOutputClass::ReferenceRegisters', '24', '1', '450', '454', '458', '462', '1', '2', '4', '8', '335', '466', '1053', '1083', '1291', '6', '4', '5', '5', '1', '252', '216', 'true', '8'],
    ['0x167eb2d5629ab9d3ull', '566', '0xfffe0300u', '0xd44db87778a43b61ull', '448', '0xffff0300u', 'MotionOutputClass::RelocatedRegisters', '24', '2', '471', '475', '479', '483', '1', '2', '4', '8', '347', '487', '241', '274', '447', '8', '6', '7', '5', '1', '252', '216', 'true', '8'],
    ['0x494fe349b8bc12ecull', '526', '0xfffe0300u', '0xe6794b6ec37ff71aull', '1674', '0xffff0300u', 'MotionOutputClass::RelocatedRegistersWithBranches', '24', '1', '450', '454', '458', '462', '1', '2', '4', '8', '335', '466', '1280', '1316', '1673', '6', '4', '6', '6', '1', '252', '216', 'true', '8'],
    ['0x53a0a641107ed76cull', '526', '0xfffe0300u', '0x3b94320087e81945ull', '1264', '0xffff0300u', 'MotionOutputClass::ReferenceRegisters', '24', '1', '450', '454', '458', '462', '1', '2', '4', '8', '335', '466', '1047', '1074', '1263', '6', '4', '5', '5', '1', '252', '216', 'true', '8'],
    ['0x53a0a641107ed76cull', '526', '0xfffe0300u', '0x462342e3e5781384ull', '1250', '0xffff0300u', 'MotionOutputClass::ReferenceRegisters', '24', '1', '450', '454', '458', '462', '1', '2', '4', '8', '335', '466', '1053', '1080', '1249', '6', '4', '5', '5', '1', '252', '216', 'true', '8'],
    ['0x4944d81dfe531b37ull', '556', '0xfffe0300u', '0x64bac8bb307eb896ull', '1392', '0xffff0300u', 'MotionOutputClass::RelocatedRegisters', '24', '2', '477', '481', '485', '489', '1', '2', '4', '8', '344', '493', '1112', '1148', '1391', '7', '5', '6', '7', '1', '252', '216', 'true', '8'],
    ['0x494fe349b8bc12ecull', '526', '0xfffe0300u', '0x7c83ed50c9894e44ull', '1298', '0xffff0300u', 'MotionOutputClass::ReferenceRegisters', '24', '1', '450', '454', '458', '462', '1', '2', '4', '8', '335', '466', '1097', '1124', '1297', '6', '4', '5', '5', '1', '252', '216', 'true', '8'],
    ['0x4944d81dfe531b37ull', '556', '0xfffe0300u', '0x0c1f3f0f440e4a0cull', '1366', '0xffff0300u', 'MotionOutputClass::RelocatedRegisters', '24', '2', '477', '481', '485', '489', '1', '2', '4', '8', '344', '493', '1112', '1145', '1365', '7', '5', '6', '7', '1', '252', '216', 'true', '8'],
    ['0xc30104cb0efb6675ull', '550', '0xfffe0300u', '0xa66fb1981ba755b2ull', '309', '0xffff0300u', 'MotionOutputClass::RelocatedRegisters', '24', '1', '453', '457', '461', '465', '1', '2', '4', '8', '338', '469', '122', '149', '308', '7', '4', '6', '5', '1', '252', '216', 'true', '8'],
]
OBSERVED_DRAWS = [2232, 2180, 1804, 1440, 940, 456, 348, 332, 308, 296, 236, 233, 148, 104, 88, 77]


class GeneratedHeaderTests(unittest.TestCase):
    def setUp(self):
        self.result = load()
        self.text = HEADER.read_text()
        self.rows = parse_header(self.text)

    def test_regeneration_is_byte_identical(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'motion_output_profiles_inc.h'
            target.write_text(render_header(self.result))
            assert target.read_bytes() == HEADER.read_bytes()

    def test_banner_names_generator_inventory_reference_and_archive(self):
        banner = [line for line in self.text.splitlines() if line.startswith('//')]
        assert banner[0].endswith('derived metadata only.')
        assert 'tools/analysis/inspect_motion_output_profiles.py' in banner[0]
        assert any(self.result['inventory_sha256'] in line for line in banner)
        assert any(ARGON_VS in line and ARGON_PS in line for line in banner)
        assert any('%d installed compiled effects (%d passes, %d SM3 pairs)' % (
            EFFECT_COUNT, PASS_COUNT, SM3_PAIRS) in line for line in banner)
        for name, symbol in HEADER_CLASSES.items():
            assert any('%s = MotionOutputClass::%s' % (name, symbol) in line
                       for line in banner), name
        for name, symbol in DEFERRED_CLASSES.items():
            assert any('%s = MotionOutputClass::%s' % (name, symbol) in line
                       for line in banner), name
        assert any('observed_scene_draws' in line for line in banner)
        assert any('need not be adjacent' in line for line in banner)

    def test_every_transformable_archive_pair_is_a_row(self):
        expected = header_rows(self.result)
        assert len(self.rows) == len(expected) == sum(CLASS_COUNTS[name] for name in HEADER_CLASSES) == 169
        emitted = {'MotionOutputClass::' + symbol for symbol in HEADER_CLASSES.values()}
        blocked = {(pair['vs'], pair['ps']) for pair in self.result['pairs']
                   if pair['transformation_class'].startswith('X_')}
        for row, pair in zip(self.rows, expected):
            assert row[6] in emitted, row[6]
            assert row[6] == 'MotionOutputClass::' + HEADER_CLASSES[pair['transformation_class']]
            assert row[0] == '0x%sull' % pair['vs']
            assert row[3] == '0x%sull' % pair['ps']
            assert row[-1] == str(pair['observed_draws'])
        # A blocked pair is never a row, even when its programs host other rows
        # (the asteroid_0000 pixel programs pair with a transformable VS too).
        assert not blocked & {(row[0][2:-3], row[3][2:-3]) for row in self.rows}
        assert len(blocked) == 11
        classes = [row[6].split('::')[1] for row in self.rows]
        assert classes.count('ReferenceRegisters') == 56
        assert classes.count('RelocatedRegisters') == 101
        assert classes.count('RelocatedRegistersWithBranches') == 12
        # Two clip-row families; the bound flag follows the matrix register.
        assert {(row[7], row[-3]) for row in self.rows} == {('24', 'true'), ('0', 'false')}
        assert sum(row[7] == '0' for row in self.rows) == 62
        spaced = [(row[0][2:-3], row[3][2:-3]) for row in self.rows
                  if [int(v) for v in row[9:13]] != list(range(int(row[9]), int(row[9]) + 16, 4))]
        assert set(spaced) == SPACED_QUAD_ROWS and len(spaced) == 6

    def test_rows_carry_their_derived_numbers(self):
        for row in self.rows:
            pair = next(p for p in self.result['pairs']
                        if '0x%sull' % p['vs'] == row[0] and '0x%sull' % p['ps'] == row[3])
            plan = pair['insertion_plan']
            assert row[1] == str(self.result['programs']['vs_' + pair['vs']]['dword_count'])
            assert row[4] == str(self.result['programs']['ps_' + pair['ps']]['dword_count'])
            assert [int(v) for v in row[7:9]] == [plan['vs_matrix_register'], plan['vs_position_temporary']]
            assert [int(v) for v in row[9:13]] == plan['vs_position_dp4_dwords']
            assert [int(v) for v in row[17:22]] == [
                plan['vs_declaration_insert_dword'], plan['vs_arithmetic_insert_dword'],
                plan['ps_definition_insert_dword'], plan['ps_declaration_insert_dword'],
                plan['ps_append_dword']]
            assert [int(v) for v in row[22:27]] == [
                plan['vs_output_register'], plan['texcoord_index'], plan['ps_input_register'],
                plan['ps_temporary_base'], plan['ps_output_register']]
            assert int(row[25]) == plan['ps_temporary_base']

    def test_rows_are_ordered_by_observed_draws_then_fingerprints(self):
        expected = header_rows(self.result)
        assert expected == sorted(expected, key=lambda p: (-p['observed_draws'], p['vs'], p['ps']))
        assert [(row[0], row[3]) for row in self.rows] == [
            ('0x%sull' % pair['vs'], '0x%sull' % pair['ps']) for pair in expected]
        draws = [int(row[-1]) for row in self.rows]
        assert draws[:16] == OBSERVED_DRAWS and all(d == 0 for d in draws[16:])

    def test_observed_rows_keep_their_fields_and_order(self):
        assert [row[:-1] for row in self.rows[:16]] == OBSERVED_ROWS

    def test_every_row_has_the_documented_field_count(self):
        assert len(HEADER_FIELDS) == 26 and HEADER_FIELDS[-1] == 'observed_scene_draws'
        for row in self.rows:
            assert len(row) == len(ARGON_HEADER_ROW), row
            # Four position DP4 offsets, then the XYZW single-lane masks.
            assert row[13:17] == ['1', '2', '4', '8']
            assert [int(value) for value in row[9:13]] == sorted(
                int(value) for value in row[9:13])
            assert row[-3] in ('true', 'false')
            assert row[-5:-3] == ['252', '216']
            assert row[-2] == '8'

    def test_argon_row_matches_the_reference_numbers(self):
        assert ARGON_HEADER_ROW in self.rows
        # Second by captured draws, behind the largest class C pair; unchanged
        # by the class C and archive-wide extensions.
        assert self.rows[1] == ARGON_HEADER_ROW
        assert self.rows.index(ARGON_HEADER_ROW) == [
            (p['vs'], p['ps']) for p in header_rows(self.result)].index(
                (ARGON_VS[3:], ARGON_PS[3:]))
        vertex = self.result['programs'][ARGON_VS]
        pixel = self.result['programs'][ARGON_PS]
        assert vertex['literal_definitions'][0]['dword'] == 302
        assert pixel['literal_definitions'][0]['dword'] == 1041
        assert pixel['definition_end_dword'] == 1047

    def test_header_carries_no_shader_words(self):
        literals = set(re.findall(r'0x[0-9a-fA-F]+', self.text))
        fingerprints = {'0x' + pair['vs'] for pair in self.result['pairs']}
        fingerprints |= {'0x' + pair['ps'] for pair in self.result['pairs']}
        allowed = fingerprints | {'0xfffe0300', '0xffff0300'}
        assert literals <= allowed, literals - allowed


if __name__ == '__main__':
    unittest.main()
