"""Parser and classification checks for `tools/analysis/shader_coverage_offline.py`.

Effect containers are assembled here from the documented D3DXFX layout; no
archive bytes, shader words or extracted assets are used. The registry parsers
run against the real generated headers, which hold derived metadata only.
"""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from effect_passes import parse_container  # noqa: E402
from index_shaders import fnv1a64  # noqa: E402
from shader_coverage_offline import (CLASS_STATES, NULL_HASH, _program,  # noqa: E402
                                     _state_value, archive_pairs, census,
                                     classify_pass, flight_pairs, markdown_table,
                                     prepass_vertices, registered_pairs,
                                     rewriter_verdicts)

MAGIC = b'\x01\x09\xff\xfe'
VERTEX_SHADER, PIXEL_SHADER = 146, 147
Z_WRITE_ENABLE, ALPHA_BLEND_ENABLE, DEST_BLEND, COLOR_WRITE = 3, 13, 7, 73


def _pad(payload):
    return payload + b'\0' * (-len(payload) % 4)


def program(version, extra=0):
    """A minimal SM token stream: version, `extra` NOP instructions and END."""
    return struct.pack('<I', version) + b'\0\0\0\0' * extra + struct.pack('<I', 0x0000ffff)


class Effect:
    """One compiled effect with a single technique/pass and literal state values."""

    def __init__(self, states, resources=()):
        self.pool = bytearray()
        self.states = states          # (operation, value) records, value None = no literal
        self.resources = list(resources)  # (state_index, usage, payload)

    def _add(self, payload):
        offset = len(self.pool)
        self.pool += _pad(payload)
        return offset

    def build(self):
        name = self._add(struct.pack('<I', 2) + b'T\0')
        pass_name = self._add(struct.pack('<I', 2) + b'P\0')
        records = []
        for operation, value in self.states:
            typedef = self._add(struct.pack('<5I', 2, 2, 0, 0, 0))
            records.append((operation, typedef, self._add(struct.pack('<I', value or 0))))
        body = bytearray(struct.pack('<4I', 0, 1, 0, 0))
        body += struct.pack('<3I', name, 0, 1)
        body += struct.pack('<3I', pass_name, 0, len(records))
        for operation, typedef, value in records:
            body += struct.pack('<4I', operation, 0, typedef, value)
        body += struct.pack('<2I', 0, len(self.resources))
        for state_index, usage, payload in self.resources:
            body += struct.pack('<6I', 0, 0, 0xffffffff, state_index, usage, len(payload))
            body += _pad(payload)
        return MAGIC + struct.pack('<I', len(self.pool)) + bytes(self.pool) + bytes(body)


def parsed(effect):
    """`(values, programs)` of the single pass, as the census reads them."""
    data = effect.build()
    container = parse_container(data)
    _, states = container['techniques'][0][1][0]
    values, programs = {}, {}
    for index, state in enumerate(states):
        if state[0] in (VERTEX_SHADER, PIXEL_SHADER):
            programs['vs' if state[0] == VERTEX_SHADER else 'ps'] = _program(
                data, container, 0, 0, index)
            continue
        name = CLASS_STATES.get(state[0])
        if name is None:
            continue
        source, value = _state_value(data, container, 0, 0, index, state)
        values[name] = value if source == 'literal' else 'dynamic'
    return values, programs


class StateValueTests(unittest.TestCase):
    def test_literal_values_are_read_from_the_container(self):
        values, _ = parsed(Effect([(Z_WRITE_ENABLE, 1), (ALPHA_BLEND_ENABLE, 0),
                                   (COLOR_WRITE, 15)]))
        self.assertEqual(values, {'zwrite': 1, 'blend': 0, 'mask': 15})

    def test_expression_and_parameter_states_are_dynamic(self):
        values, _ = parsed(Effect([(Z_WRITE_ENABLE, 1), (ALPHA_BLEND_ENABLE, 0)],
                                  [(0, 0, b'FXLC'), (1, 1, b'g_Blend\0')]))
        self.assertEqual(values, {'zwrite': 'dynamic', 'blend': 'dynamic'})

    def test_program_identity_and_null_shader(self):
        blob = program(0xfffe0300, extra=1)
        _, programs = parsed(Effect([(VERTEX_SHADER, 0), (PIXEL_SHADER, 0)],
                                    [(0, 0, blob)]))
        self.assertEqual(programs['vs'], (fnv1a64(blob), 3, 0xfffe0300))
        self.assertIsNone(programs['ps'])


class ClassifyPassTests(unittest.TestCase):
    def test_classes(self):
        cases = [
            ({'zwrite': 1, 'blend': 0, 'mask': 15}, 'opaque_depth'),
            ({'zwrite': 1, 'mask': 0}, 'depth_only_mask'),
            ({'zwrite': 1, 'blend': 1}, 'blended_depth'),
            ({'zwrite': 0, 'blend': 1, 'dst': 2}, 'additive'),
            ({'zwrite': 0, 'blend': 1, 'dst': 6}, 'blended'),
            ({'zwrite': 0, 'blend': 0}, 'no_depth'),
            ({'zwrite': 'dynamic', 'blend': 0}, 'depth_dynamic'),
            ({'blend': 0}, 'depth_inherited'),
        ]
        for values, expected in cases:
            self.assertEqual(classify_pass(values), expected, values)

    def test_a_dynamic_blend_over_a_depth_write_still_counts_as_depth(self):
        self.assertEqual(classify_pass({'zwrite': 1, 'blend': 'dynamic'}), 'opaque_depth')


class RegistryParserTests(unittest.TestCase):
    def test_profile_rows_parse_as_pairs(self):
        rows = registered_pairs(ROOT / 'src/renderer/motion_output_profiles_inc.h')
        self.assertEqual(len(rows), 171)
        self.assertIn(('53a0a641107ed76c', '8759c7838bbc86c2'), rows)  # reference pair
        for vertex, pixel in rows:
            self.assertRegex(vertex, r'^[0-9a-f]{16}$')
            self.assertRegex(pixel, r'^[0-9a-f]{16}$')

    def test_prepass_rows_parse(self):
        rows = prepass_vertices(ROOT / 'src/renderer/depth_prepass_profiles.h')
        self.assertEqual(rows, ['4b63594a775cbde0', '803ebfd17f79e413',
                                'c78b4c68a87fce74', 'd2e63b1e5b0e24df'])  # z_only + z_only_0000/0001

    def test_missing_rows_fail_closed(self):
        empty = Path(__file__)  # this file holds no profile rows
        self.assertRaises(ValueError, registered_pairs, empty)


class VerdictAndFlightTests(unittest.TestCase):
    def test_rewriter_verdicts_split_by_model(self):
        verdicts = rewriter_verdicts({
            'pairs': [{'vs': 'a' * 16, 'ps': 'b' * 16,
                       'transformation_class': 'B_relocated_registers', 'blocking_reasons': []},
                      {'vs': 'c' * 16, 'ps': 'd' * 16,
                       'transformation_class': 'X_position_not_row_dot',
                       'blocking_reasons': ['vs_position_unknown']}],
            'sm2_pairs': [{'vs': 'e' * 16, 'ps': 'f' * 16, 'group': 'hostable', 'reasons': []}],
            'sm1_pairs': [{'vs': '0' * 16, 'ps': '1' * 16, 'reason': 'ps_1_x has no oC1'}]})
        self.assertTrue(verdicts[('a' * 16, 'b' * 16)]['rewritable'])
        self.assertFalse(verdicts[('c' * 16, 'd' * 16)]['rewritable'])
        self.assertEqual(verdicts[('c' * 16, 'd' * 16)]['reasons'], ['vs_position_unknown'])
        self.assertEqual(verdicts[('e' * 16, 'f' * 16)]['reasons'],
                         ['sm2_pair_has_no_generated_row'])
        self.assertEqual(verdicts[('0' * 16, '1' * 16)]['model'], 'sm1')

    def test_flight_pairs_read_route_rows(self):
        path = Path(self.tmp.name)
        path.write_text('motion_route device=1 vs=53a0a641107ed76c ps=8759c7838bbc86c2 '
                        'zwrite=1\nsun_shadow_lane_writer vs=%s ps=%s reason=x\n' %
                        ('a' * 16, 'b' * 16))
        self.assertEqual(flight_pairs(path),
                         [('53a0a641107ed76c', '8759c7838bbc86c2'), ('a' * 16, 'b' * 16)])

    def setUp(self):
        import tempfile
        self.tmp = tempfile.NamedTemporaryFile(suffix='.log', delete=False)
        self.addCleanup(lambda: Path(self.tmp.name).unlink(missing_ok=True))


class CensusTests(unittest.TestCase):
    def archive(self, pairs):
        from collections import Counter
        records = {}
        for key, classes in pairs.items():
            records[key] = {'vs': key[0], 'ps': key[1], 'vs_dwords': 3, 'ps_dwords': 3,
                            'vs_model': '3_0', 'ps_model': None if key[1] == NULL_HASH else '3_0',
                            'pass_occurrences': sum(classes.values()),
                            'classes': Counter(classes), 'basenames': {'effect'},
                            'techniques': {'T'}, 'pass_names': {'P'}, 'profiles': {'3_0'},
                            'toggles': {'(base)'}, 'catalogues': {'01.cat'},
                            'paths': {'shader/3_0/effect.fb'}}
        return {'effect_count': 1, 'overridden_entry_count': 0, 'pass_count': 1,
                'incomplete_pass_count': 0, 'catalogues': {'01.cat': 1}, 'pairs': records}

    def test_coverage_classes_and_cross_check(self):
        known, rewritable, blocked, prepass = ('1' * 16, '2' * 16), ('3' * 16, '4' * 16), \
            ('5' * 16, '6' * 16), ('7' * 16, NULL_HASH)
        archive = self.archive({known: {'opaque_depth': 1}, rewritable: {'depth_dynamic': 1},
                                blocked: {'opaque_depth': 1}, prepass: {'depth_only_mask': 1}})
        verdicts = {rewritable: {'model': 'sm3', 'rewritable': True,
                                 'transformation_class': 'A_reference_registers', 'reasons': []},
                    blocked: {'model': 'sm3', 'rewritable': False,
                              'transformation_class': 'X_position_not_row_dot',
                              'reasons': ['vs_position_unknown']}}
        result = census(archive, [known], [prepass[0]], verdicts, [rewritable])
        coverage = {(entry['vs'], entry['ps']): entry['coverage'] for entry in result['pairs']}
        self.assertEqual(coverage[known], 'registered')
        self.assertEqual(coverage[rewritable], 'rewritable_unregistered')
        self.assertEqual(coverage[blocked], 'not_rewritable')
        self.assertEqual(coverage[prepass], 'prepass_jitter_only')
        self.assertEqual({(entry['vs'], entry['ps']) for entry in result['unknown_depth_pairs']},
                         {rewritable, blocked})
        self.assertEqual(result['unknown_depth_breakdown'],
                         {'dynamic_or_inherited': 1, 'literal_depth_writer': 1})
        self.assertTrue(result['flight_cross_check']['passed'])
        self.assertIn('`%s`' % blocked[0], markdown_table(result))

    def test_a_flight_pair_outside_the_archive_fails_the_cross_check(self):
        known = ('1' * 16, '2' * 16)
        result = census(self.archive({known: {'opaque_depth': 1}}), [known], [], {},
                        [known, ('9' * 16, '9' * 16)])
        self.assertFalse(result['flight_cross_check']['passed'])
        self.assertEqual(result['flight_cross_check']['missing_from_offline_set'],
                         [['9' * 16, '9' * 16]])

    def test_null_pixel_shader_is_never_rewritable(self):
        pair = ('8' * 16, NULL_HASH)
        result = census(self.archive({pair: {'depth_only_mask': 1}}), [], [], {}, [])
        entry = result['pairs'][0]
        self.assertEqual(entry['coverage'], 'not_rewritable')
        self.assertTrue(entry['reasons'][0].startswith('null_pixel_shader'))


class ArchiveTests(unittest.TestCase):
    """`archive_pairs` needs the installed archives; skipped when absent."""

    def test_effective_set_resolves_overrides(self):
        game = Path.home() / ('Library/Application Support/CrossOver/Bottles/X3/'
                              'drive_c/X3')
        if not list(game.glob('[0-9][0-9].cat')):
            self.skipTest('no installed archives')
        archive = archive_pairs(game)
        self.assertEqual(archive['incomplete_pass_count'], 0)
        self.assertGreater(archive['overridden_entry_count'], 0)
        self.assertEqual(archive['effect_count'] + archive['overridden_entry_count'], 3480)
        self.assertIn(('c78b4c68a87fce74', NULL_HASH), archive['pairs'])


if __name__ == '__main__':
    unittest.main()
