"""Original metadata fixtures: lifetime evidence never fills missing facts."""
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location('iteration05_lifetimes', Path(__file__).parents[2] / 'tools/analysis/analyze_iteration05_lifetimes.py')
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


def frame(device=1, number=10, node='00002000', serial=10, load=1, result='00000000', complete=True):
    coordinate = f'device={device} frame={number} index=1'
    lines = [f'frame_begin device={device} frame={number}',
             f'draw {coordinate} vs=aaa ps=bbb',
             f'object_context {coordinate} scoped=1 valid=127 registry=00001000 node={node} node_handle=42 camera=00003000 camera_handle=7',
             f'motion_input {coordinate} lifetime_verified=1',
             f'motion_lifetime {coordinate} registry=00001000 before_known=1 after_known=1 before_reason=0 after_reason=0 observer_epoch=1 load_epoch={load} registry_epoch=2 mutation_before=9 mutation_after=9 node_serial={serial} camera_serial=20 observer_epoch_after=1 load_epoch_after={load} registry_epoch_after=2 node_serial_after={serial} camera_serial_after=20',
             f'draw_result {coordinate} result={result}']
    if complete:
        lines.append(f'frame_end device={device} frame={number} draws=1 capture=1 present=00000000')
    return lines


class Iteration05LifetimeTests(unittest.TestCase):
    def test_separate_devices_reusing_frame_numbers(self):
        report = AUDIT.audit(frame() + frame(device=2))
        self.assertEqual(report['totals_successful_complete_frames']['frames'], 2)
        self.assertEqual(report['totals_successful_complete_frames']['known_consistent_successful'], 2)
        self.assertEqual(report['neighboring_captured_frame_storage_relations'], [])

    def test_missing_lifetime_is_unknown_without_invented_epoch(self):
        report = AUDIT.audit([line for line in frame() if not line.startswith('motion_lifetime ')])
        self.assertEqual(report['totals_successful_complete_frames']['known_consistent_successful'], 0)
        self.assertEqual(report['known_epochs'], [])
        self.assertEqual(report['totals_successful_complete_frames']['missing_lifetime'], 1)

    def test_failed_draw_not_known_successful(self):
        report = AUDIT.audit(frame(result='8876086c'))
        self.assertEqual(report['totals_successful_complete_frames']['known_consistent_successful'], 0)
        self.assertEqual(report['observed_node_serials'], 0)

    def test_incomplete_frame_excluded_from_aggregate(self):
        report = AUDIT.audit(frame(complete=False))
        self.assertEqual(report['totals_successful_complete_frames'], {})
        self.assertFalse(report['frames'][0]['successful_complete_capture'])

    def test_before_after_discontinuity_is_not_known(self):
        lines = [line.replace('mutation_after=9', 'mutation_after=10') for line in frame()]
        report = AUDIT.audit(lines)
        self.assertEqual(report['before_after_mismatches'], {'mutation_before': 1})
        self.assertEqual(report['totals_successful_complete_frames']['known_consistent_successful'], 0)

    def test_wrong_coordinate_record_not_attached(self):
        lines = [line.replace('frame=10', 'frame=11') if line.startswith('motion_lifetime ') else line for line in frame()]
        report = AUDIT.audit(lines)
        self.assertEqual(report['totals_successful_complete_frames']['missing_lifetime'], 1)

    def test_unscoped_placeholder_not_read_failure_or_stable_zero_epoch(self):
        lines = [line.replace('scoped=1 valid=127', 'scoped=0') for line in frame()]
        lines = [line.replace('before_known=1 after_known=1 before_reason=0 after_reason=0', 'before_known=0 after_known=0 before_reason=8 after_reason=8') for line in lines]
        report = AUDIT.audit(lines)
        self.assertEqual(report['totals_successful_complete_frames']['known_consistent_successful'], 0)
        self.assertEqual(report['known_epochs'], [])
        self.assertEqual(report['unknown_paths'][0]['scoped'], '0')

    def test_serial_replacement_across_load_is_recorded_without_matching_claim(self):
        report = AUDIT.audit(frame() + frame(number=20, node='00004000', serial=30, load=2))
        self.assertEqual(report['serial_to_multiple_entities'], 0)
        self.assertGreaterEqual(report['handle_reappearances_across_load_epochs'], 1)
        relation = report['neighboring_captured_frame_storage_relations'][0]
        self.assertFalse(relation['adjacent'])
        self.assertEqual(relation['shared_entities'], 0)

    def test_duplicate_lifetime_and_incomplete_context_fail_closed(self):
        lines = frame()
        lines.insert(-1, next(line for line in lines if line.startswith('motion_lifetime ')))
        self.assertEqual(AUDIT.audit(lines)['totals_successful_complete_frames']['known_consistent_successful'], 0)
        lines = [line.replace(' camera=00003000', '') for line in frame()]
        self.assertEqual(AUDIT.audit(lines)['totals_successful_complete_frames']['known_consistent_successful'], 0)

    def test_matrix_hash_requires_exact_four_raw_rows(self):
        rows = {str(i): '3f800000,00000000,00000000,00000000' for i in range(4)}
        self.assertIsNotNone(AUDIT.matrix_hash(rows))
        rows['4'] = rows.pop('3')
        self.assertIsNone(AUDIT.matrix_hash(rows))
        rows['3'] = rows.pop('4')
        rows['0'] = '3f800000'
        self.assertIsNone(AUDIT.matrix_hash(rows))

    def test_duplicate_frame_end_cannot_replace_a_failed_present(self):
        lines = frame()
        lines[-1] = lines[-1].replace('present=00000000', 'present=8876086c')
        lines.append(frame()[-1])
        report = AUDIT.audit(lines)
        self.assertTrue(report['frames'][0]['duplicate_frame_end'])
        self.assertEqual(report['totals_successful_complete_frames'], {})

    def test_zero_alias_nonnumeric_and_overflow_context_reject(self):
        for name, original in [('node', '00002000'), ('camera', '00003000'), ('registry', '00001000'), ('node_handle', '42'), ('camera_handle', '7')]:
            for replacement in ('0x0', '0000000000000000', 'garbage', '18446744073709551616'):
                lines = [line.replace(name + '=' + original, name + '=' + replacement) for line in frame()]
                with self.subTest(name=name, replacement=replacement):
                    self.assertEqual(AUDIT.audit(lines)['totals_successful_complete_frames']['known_consistent_successful'], 0)
        for name in ('observer_epoch', 'load_epoch', 'registry_epoch', 'node_serial', 'camera_serial', 'mutation_before'):
            lines = frame()
            import re
            lines = [re.sub(r'\b' + name + r'=\d+', name + '=18446744073709551616', line) for line in lines]
            self.assertEqual(AUDIT.audit(lines)['totals_successful_complete_frames']['known_consistent_successful'], 0)

    def test_explicit_resource_bounds(self):
        with self.assertRaises(ValueError):
            AUDIT.audit(frame(), max_draws=0)
        with self.assertRaises(ValueError):
            AUDIT.audit(frame(), max_frames=0)


if __name__ == '__main__':
    unittest.main()
