"""Original metadata fixtures for fail-closed deterministic table generation."""
import copy
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/analysis'))
from generate_shader_profiles import emit_tables, read_pinned, replay_metadata


class Tables(unittest.TestCase):
    def setUp(self):
        self.v = dict(fnv1a64='0000000000000002', word_count=15,
                      position_path='homogeneous_row_dots', matrix_register=6,
                      named_world_view_projection=False, shader_version=0xfffe0300,
                      position_write_order='XYZW', homogeneous_constructor='MadXYZIdentityWFromX')
        self.p = dict(fnv1a64='0000000000000003', word_count=18,
                      coverage=dict(qualified=True))

    def test_deterministic_sorted_categories(self):
        other = dict(fnv1a64='0000000000000001', word_count=9,
                     position_path='direct_clip_xyzw')
        a = emit_tables([self.v, other], [self.p])
        self.assertEqual(a, emit_tables([other, self.v], [self.p]))
        self.assertIn('0x0000000000000002ull, 15, 6, false', a['rigid_position_profiles_inc.h'])
        self.assertIn('0xfffe0300u, PositionWriteOrder::XYZW, HomogeneousConstructor::MadXYZIdentityWFromX',
                      a['rigid_position_profiles_inc.h'])
        self.assertIn('VertexPositionPath::DirectClipXYZW', a['position_path_profiles_inc.h'])
        self.assertIn('0x0000000000000003ull, 18', a['pixel_coverage_profiles_inc.h'])

    def test_unknown_coverage_excluded(self):
        self.p['coverage']['qualified'] = False
        self.assertNotIn('{0x', emit_tables([self.v], [self.p])['pixel_coverage_profiles_inc.h'])

    def test_duplicate_rejects(self):
        with self.assertRaises(ValueError): emit_tables([self.v, self.v], [])
        self.p['fnv1a64'] = self.v['fnv1a64']
        with self.assertRaises(ValueError): emit_tables([self.v], [self.p])

    def test_stage_limits(self):
        for record, bound, stage in ((self.v, 769, 'vs'), (self.p, 1883, 'ps')):
            for count in (0, -1, bound + 1, True, 1.5):
                p = copy.deepcopy(record); p['word_count'] = count
                with self.assertRaises(ValueError): emit_tables([p] if stage == 'vs' else [], [p] if stage == 'ps' else [])

    def test_unknown_layout_and_category(self):
        self.v['matrix_register'] = 4
        with self.assertRaises(ValueError): emit_tables([self.v], [])
        self.v['position_path'] = 'unproved'
        with self.assertRaises(ValueError): emit_tables([self.v], [])

    def test_malformed_fingerprint(self):
        for value in ('1', 'fffffffffffffffff', '-000000000000001', 'z' * 16):
            self.v['fnv1a64'] = value
            with self.assertRaises(ValueError): emit_tables([self.v], [])

    def test_missing_coverage_is_not_false(self):
        self.p['coverage'] = {}
        with self.assertRaises(KeyError): emit_tables([], [self.p])

    def test_changed_inventory_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'inventory.json'; path.write_text('{}')
            with self.assertRaises(ValueError): read_pinned(path, '0' * 64)

    def test_replay_metadata_retains_issue_order(self):
        proof = dict(qualified_position_math=True, contract='FloatXYZForceWOneSubmittedRowDots',
                     input_w_used=False, version='0xfffe0200', position_dot_dwords_xyzw=[20, 24, 28, 32])
        self.assertEqual(replay_metadata(proof)['position_write_order'], 'XYZW')
        proof['position_dot_dwords_xyzw'] = [24, 28, 32, 20]
        result = replay_metadata(proof)
        self.assertEqual(result['position_write_order'], 'WXYZ')
        self.assertEqual(result['shader_version'], 0xfffe0200)
        record = dict(self.v, **result)
        self.assertIn('PositionWriteOrder::WXYZ', emit_tables([record], [])['rigid_position_profiles_inc.h'])
        for offsets in ([20, 24, 28], [20, 24, 24, 28], [0, 24, 28, 32], [True, 24, 28, 32]):
            with self.subTest(offsets=offsets), self.assertRaises(ValueError):
                replay_metadata(dict(proof, position_dot_dwords_xyzw=offsets))
        for field, value in (('qualified_position_math', False), ('contract', 'unknown'), ('input_w_used', True)):
            with self.subTest(field=field), self.assertRaises(ValueError):
                replay_metadata(dict(proof, **{field: value}))

    def test_unknown_replay_contract_never_gets_defaulted(self):
        for field, invalid in (('shader_version', (0, True, 0xfffe0301, '0xfffe0300')),
                               ('position_write_order', ('Unknown', 'YXZW', 'WXYZ')),
                               ('homogeneous_constructor', ('Unknown', 'CopyXYZLiteralW'))):
            with self.subTest(field=field), self.assertRaises(KeyError):
                record = dict(self.v); del record[field]; emit_tables([record], [])
            for value in invalid:
                with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    emit_tables([dict(self.v, **{field: value})], [])

if __name__ == '__main__': unittest.main()
