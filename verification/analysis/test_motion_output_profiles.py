"""Structure and Argon reference checks for the motion-output profile table.

Runs under pytest or `python3 -m unittest`. The generated JSON is committed, so
these tests read it directly; they never touch local game bytecode. Synthetic
token fixtures exercise the parser itself, so a parser regression fails here
even when the committed table is stale.
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
    DEFERRED_CLASSES, HEADER_CLASSES, REFERENCE, REFERENCE_DEFINITION_DWORDS,
    REFERENCE_POSITION_DWORDS, check, classify, header_rows, profile,
    render_header)

RESULT = ROOT / 'verification/results/motion-output-profiles.json'
HEADER = ROOT / 'src/renderer/motion_output_profiles_inc.h'
ARGON_VS, ARGON_PS = REFERENCE['vs'][0], REFERENCE['ps'][0]


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


def synthetic_vertex(position_opcode=9, rows=(24, 25, 26, 27), gap=False):
    """A minimal vs_3_0: one DEF, three DCLs, a constructor and four row dots."""
    body = [instruction(0x51, destination(2, 42), 0x3f800000, 0, 0, 0),
            instruction(0x1f, 0, destination(1, 0)),
            instruction(0x1f, 5, destination(6, 2)),
            instruction(0x1f, 0, destination(6, 0)),
            instruction(4, destination(0, 1), source(1, 0, 0x24),
                        source(2, 42, 0x40), source(2, 42, 0x15))]
    for lane, row in enumerate(rows):
        body.append(instruction(position_opcode, destination(6, 0, 1 << lane),
                                source(0, 1), source(2, row)))
        if gap and lane == 1:
            body.append(instruction(1, destination(0, 3), source(0, 1)))
    body.append(instruction(1, destination(6, 2), source(0, 1)))
    return words(0xfffe0300, *(w for item in body for w in item), 0x0000ffff)


def synthetic_pixel(extra=()):
    body = [instruction(0x51, destination(2, 8), 0, 0, 0, 0),
            instruction(0x1f, 10, destination(1, 0)),
            instruction(0x1f, 5, destination(1, 1)),
            *extra,
            instruction(1, destination(8, 0), source(1, 0))]
    return words(0xffff0300, *(w for item in body for w in item), 0x0000ffff)


class MotionOutputProfileTests(unittest.TestCase):
    def setUp(self):
        self.result = load()
        self.programs = self.result['programs']
        self.pairs = self.result['pairs']

    # -- committed table structure ------------------------------------------
    def test_schema_and_provenance(self):
        assert self.result['schema'] == 1
        for key in ('scope', 'tool_sha256', 'inventory_sha256', 'limitations',
                    'capture', 'transformation_classes', 'pairs', 'programs'):
            assert key in self.result, key
        assert len(self.result['tool_sha256']) == 64
        assert len(self.result['inventory_sha256']) == 64
        assert self.result['limitations']

    def test_capture_provenance_is_derived_only(self):
        capture = self.result['capture']
        assert len(capture['source_sha256']) == 64
        assert capture['source_bytes'] > 0
        assert capture['scene_segment'] == 1
        assert capture['scene_draw_count'] == self.result['scene_draw_total']
        assert capture['draw_count'] >= capture['scene_draw_count']

    def test_pairs_are_ordered_and_shares_accumulate(self):
        assert self.pairs, 'no captured pairs'
        assert self.result['pair_count'] == len(self.pairs)
        assert sum(pair['draws'] for pair in self.pairs) == self.result['scene_draw_total']
        previous = 0.0
        for pair in self.pairs:
            assert pair['draws'] > 0
            assert pair['cumulative_scene_share'] >= previous
            previous = pair['cumulative_scene_share']
        assert abs(previous - 1.0) < 1e-6
        assert self.pairs == sorted(self.pairs, key=lambda p: -p['draws'])

    def test_every_pair_has_a_class_and_consistent_evidence(self):
        totals = {name: 0 for name in self.result['transformation_classes']}
        for pair in self.pairs:
            name = pair['transformation_class']
            assert name in totals, name
            totals[name] += pair['draws']
            blocked = name.startswith('X_')
            assert bool(pair['blocking_reasons']) == blocked, pair
            assert bool(pair['insertion_plan']) != blocked, pair
            assert pair['hosts_reference_registers'] == (name == 'A_reference_registers')
            for identifier in ('vs_' + pair['vs'], 'ps_' + pair['ps']):
                assert identifier in self.programs, identifier
        for name, summary in self.result['transformation_classes'].items():
            assert summary['draws'] == totals[name]

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

    def test_program_facts_are_internally_consistent(self):
        for identifier, program in self.programs.items():
            assert identifier.startswith(('vs_', 'ps_'))
            if not program.get('parsed'):
                assert program['reason']
                continue
            assert program['fnv1a64'] == identifier.split('_', 1)[1]
            assert len(program['sha256']) == 64
            assert program['bytes'] == 4 * program['dword_count']
            assert program['end_dword'] == program['dword_count'] - 1
            assert 1 <= program['definition_end_dword'] <= program['header_end_dword']
            assert program['header_end_dword'] <= program['end_dword']
            assert program['executable_instruction_count'] <= program['instruction_count']
            assert program['comment_dword_count'] >= 0
            for definition in program['literal_definitions']:
                assert definition['end_dword'] == definition['dword'] + definition['dword_count']
                assert definition['end_dword'] <= program['header_end_dword']
            for declaration in program['declarations']:
                assert declaration['dword'] < program['header_end_dword']

    def test_json_carries_no_shader_words(self):
        versions = {'0xfffe0101', '0xfffe0200', '0xfffe0201', '0xfffe0300',
                    '0xffff0101', '0xffff0200', '0xffff0201', '0xffff0300'}
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
        assert [d['dword'] for d in vertex['declarations']] == list(range(308, 335, 3))
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
        assert sorted(site['base_register'] for site in relative['sites']) == [0, 1, 2]

    def test_argon_pixel_offsets(self):
        pixel = self.programs[ARGON_PS]
        assert pixel['model'] == '3_0'
        assert pixel['dword_count'] == 1260
        assert pixel['literal_definitions'][0]['dword'] == REFERENCE_DEFINITION_DWORDS['ps'] == 1041
        assert pixel['definition_end_dword'] == 1047
        assert pixel['header_end_dword'] == 1074
        assert [d['dword'] for d in pixel['declarations']] == list(range(1047, 1074, 3))
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
        assert pair['blocking_reasons'] == []
        assert pair['differences_from_reference'] == []
        assert pair['vs_relative_light_loop'] is True
        assert pair['vs_loop_bound_integer_registers'] == [0]
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
            'light_loop_max_count': 8}

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
    '6', '4', '5', '5', '1', '252', '216', 'true', '8']


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

    def test_banner_names_generator_inventory_and_reference(self):
        banner = [line for line in self.text.splitlines() if line.startswith('//')]
        assert banner[0].endswith('derived metadata only.')
        assert 'tools/analysis/inspect_motion_output_profiles.py' in banner[0]
        assert any(self.result['inventory_sha256'] in line for line in banner)
        assert any(ARGON_VS in line and ARGON_PS in line for line in banner)
        for name, symbol in HEADER_CLASSES.items():
            assert any('%s = MotionOutputClass::%s' % (name, symbol) in line
                       for line in banner), name
        for name, symbol in DEFERRED_CLASSES.items():
            assert any('%s = MotionOutputClass::%s' % (name, symbol) in line
                       for line in banner), name

    def test_only_classes_a_and_b_are_emitted(self):
        expected = header_rows(self.result)
        assert len(self.rows) == len(expected)
        emitted = {'MotionOutputClass::' + symbol for symbol in HEADER_CLASSES.values()}
        deferred = {pair['ps'] for pair in self.result['pairs']
                    if pair['transformation_class'] in DEFERRED_CLASSES}
        for row, pair in zip(self.rows, expected):
            assert row[6] in emitted, row[6]
            assert row[0] == '0x%sull' % pair['vs']
            assert row[3] == '0x%sull' % pair['ps']
        for fingerprint in deferred:
            assert fingerprint not in self.text

    def test_rows_are_ordered_by_draws_then_fingerprints(self):
        expected = header_rows(self.result)
        assert expected == sorted(expected, key=lambda p: (-p['draws'], p['vs'], p['ps']))
        assert [(row[0], row[3]) for row in self.rows] == [
            ('0x%sull' % pair['vs'], '0x%sull' % pair['ps']) for pair in expected]

    def test_every_row_has_the_documented_field_count(self):
        for row in self.rows:
            assert len(row) == len(ARGON_HEADER_ROW), row
            # Four position DP4 offsets, then the XYZW single-lane masks.
            assert row[13:17] == ['1', '2', '4', '8']
            assert [int(value) for value in row[9:13]] == sorted(
                int(value) for value in row[9:13])
            assert row[-2] in ('true', 'false')
            assert row[-4:-2] == ['252', '216']

    def test_argon_row_matches_the_reference_numbers(self):
        assert ARGON_HEADER_ROW in self.rows
        assert self.rows[0] == ARGON_HEADER_ROW
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
