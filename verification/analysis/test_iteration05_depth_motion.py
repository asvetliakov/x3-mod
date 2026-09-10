"""Original diagnostic fixtures validate bookkeeping, not the game selector profile."""
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location('iteration05_depth_motion', Path(__file__).parents[2] / 'tools/analysis/analyze_iteration05_depth_motion.py')
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


def fixture(device=1, frame=10, epoch=5):
    coord = f'device={device} frame={frame}'
    draw = coord + ' index=1'
    return [
        f'ownership_copy_depth phase=create_after device={device} result=00000000 status=00000000 requested=1 available=1 source_bound=1 generation=1 source_width=64 source_height=64 source_format=77 source_msaa=0',
        f'scene_depth_frame phase=begin {coord} generation=1',
        f'frame_begin {coord}',
        f'capture_event {coord} seq=1 after_draw=0 op=draw_begin result=00000000',
        f'draw {draw} vs=aa ps=bb',
        f'object_context {draw} scoped=1 valid=127 node=00001000 camera=00002000 registry=00003000 mesh=00004000 node_handle=42 camera_handle=7 scope_depth=1',
        f'motion_input {draw} blockers=00000000 proofs=31 vs=aa ps=bb lifetime_verified=1 vertex_finite_verified=0',
        f'motion_lifetime {draw} before_known=1 after_known=1 before_reason=0 after_reason=0 registry=00003000 observer_epoch=1 observer_epoch_after=1 load_epoch=1 load_epoch_after=1 registry_epoch=1 registry_epoch_after=1 mutation_before=3 mutation_after=3 node_serial=1 node_serial_after=1 camera_serial=2 camera_serial_after=2',
        f'draw_result {draw} result=00000000',
        f'scene_depth_copy {coord} event=2 result=00000000 valid=1 generation=1 source_epoch={epoch} copy_epoch={epoch} color=1 depth=2',
        f'scene_depth_boundary {coord} event=2 confirmed=1 generation=1 copy_epoch={epoch} source_epoch={epoch+1}',
        f'capture_event {coord} seq=2 after_draw=1 op=clear result=00000000',
        'clear flags=2 rect_count=0 z=1',
        'surface role=clear_rt0 identity=1 width=64 height=64 format=21 msaa=0',
        'surface role=clear_depth identity=2 width=64 height=64 format=77 msaa=0',
        'clear_viewport result=00000000 x=0 y=0 w=64 h=64 minz=0 maxz=1',
        f'scene_depth_frame phase=end {coord} generation=1 events=2 attempted=1 copied=1 confirmed=1 state=8 rejection=0 rejection_event=0 present=00000000',
        f'frame_end {coord} draws=1 capture=1 present=00000000',
    ]


class Iteration05DepthMotionTests(unittest.TestCase):
    def check_not_selected(self, lines):
        result = AUDIT.analyze(lines)
        self.assertNotEqual(result['frames'][0]['classification'], 'selected_depth_boundary')
        return result

    def test_coherent_metadata_not_numeric_or_live_taa_proof(self):
        result = AUDIT.analyze(fixture())
        self.assertEqual(result['frames'][0]['classification'], 'selected_depth_boundary')
        self.assertEqual(result['totals']['local_metadata_candidate'], 1)
        self.assertEqual(result['totals']['candidate_with_finite_payload_attestation'], 0)
        self.assertFalse(result['numeric_depth_readback_proven'])
        self.assertFalse(result['live_taa_eligibility_proven'])

    def test_devices_reusing_frame_numbers_stay_separate(self):
        result = AUDIT.analyze(fixture() + fixture(device=2))
        self.assertEqual(result['totals']['selected_depth_boundary_frames'], 2)
        self.assertEqual(result['adjacent_selected_frame_epoch_checks'], [])

    def test_duplicate_copy_boundary_frame_scope_fail_closed(self):
        for prefix in ('scene_depth_copy ', 'scene_depth_boundary ', 'scene_depth_frame phase=begin ', 'scene_depth_frame phase=end ', 'frame_end '):
            lines = fixture()
            index = next(i for i, line in enumerate(lines) if line.startswith(prefix))
            lines.insert(index, lines[index])
            self.check_not_selected(lines)

    def test_missing_copy_boundary_clear_or_viewport_fail_closed(self):
        for prefix in ('scene_depth_copy ', 'scene_depth_boundary ', 'clear ', 'clear_viewport ', 'ownership_copy_depth '):
            self.check_not_selected([line for line in fixture() if not line.startswith(prefix)])

    def test_copy_boundary_order_and_frame_generation(self):
        lines = fixture()
        lines[9], lines[10] = lines[10], lines[9]
        self.check_not_selected(lines)
        self.check_not_selected([line.replace('generation=1', 'generation=2') if line.startswith('scene_depth_boundary ') else line for line in fixture()])

    def test_epoch_copy_equals_source_then_advances_once(self):
        for replacement in ('source_epoch=5', 'source_epoch=7', 'source_epoch=invalid'):
            self.check_not_selected([line.replace('source_epoch=6', replacement) if line.startswith('scene_depth_boundary ') else line for line in fixture()])
        self.check_not_selected([line.replace('source_epoch=5', 'source_epoch=4') if line.startswith('scene_depth_copy ') else line for line in fixture()])

    def test_failed_clear_draw_present_or_copy_rejects_selection(self):
        for prefix in ('capture_event device=1 frame=10 seq=2 ', 'draw_result ', 'frame_end ', 'scene_depth_copy '):
            lines = [line.replace('00000000', '8876086c') if line.startswith(prefix) else line for line in fixture()]
            self.check_not_selected(lines)

    def test_target_and_viewport_must_match(self):
        self.check_not_selected([line.replace('identity=2', 'identity=9') if line.startswith('surface role=clear_depth ') else line for line in fixture()])
        self.check_not_selected([line.replace('w=64', 'w=32') if line.startswith('clear_viewport ') else line for line in fixture()])
        self.check_not_selected([line.replace('rect_count=0', 'rect_count=1') if line.startswith('clear ') else line for line in fixture()])

    def test_event_after_draw_count_cannot_lie(self):
        self.check_not_selected([line.replace('after_draw=1', 'after_draw=0') if 'seq=2 ' in line else line for line in fixture()])

    def test_missing_duplicate_mismatched_or_unscoped_context_no_candidate(self):
        for action in ('missing', 'duplicate', 'coordinate', 'unscoped', 'invalid_pointer'):
            lines = fixture()
            index = next(i for i, line in enumerate(lines) if line.startswith('object_context '))
            if action == 'missing':
                lines.pop(index)
            elif action == 'duplicate':
                lines.insert(index, lines[index])
            elif action == 'coordinate':
                lines[index] = lines[index].replace('index=1', 'index=2')
            elif action == 'unscoped':
                lines[index] = lines[index].replace('scoped=1', 'scoped=0')
            else:
                lines[index] = lines[index].replace('node=00001000', 'node=0x0')
            self.assertEqual(AUDIT.analyze(lines)['totals'].get('local_metadata_candidate', 0), 0)

    def test_duplicate_or_missing_motion_and_failed_draw_no_candidate(self):
        for prefix in ('motion_input ', 'motion_lifetime ', 'draw_result '):
            lines = fixture()
            index = next(i for i, line in enumerate(lines) if line.startswith(prefix))
            lines.insert(index, lines[index])
            self.assertEqual(AUDIT.analyze(lines)['totals'].get('local_metadata_candidate', 0), 0)
        result = AUDIT.analyze([line.replace('00000000', '8876086c') if line.startswith('draw_result ') else line for line in fixture()])
        self.assertEqual(result['totals'].get('local_metadata_candidate', 0), 0)

    def test_adjacent_epoch_delta_uses_successful_source_depth_clears(self):
        result = AUDIT.analyze(fixture() + fixture(frame=11, epoch=6))
        self.assertTrue(result['adjacent_selected_frame_epoch_checks'][0]['consistent'])
        result = AUDIT.analyze(fixture() + fixture(frame=11, epoch=8))
        self.assertFalse(result['adjacent_selected_frame_epoch_checks'][0]['consistent'])

    def test_unknown_proof_or_blocker_bits_not_accepted(self):
        for old, new in [('proofs=31', 'proofs=63'), ('blockers=00000000', 'blockers=80000000')]:
            result = AUDIT.analyze([line.replace(old, new) if line.startswith('motion_input ') else line for line in fixture()])
            self.assertEqual(result['totals'].get('local_metadata_candidate', 0), 0)

    def test_copy_draw_or_diagnostic_outside_frame_is_rejected(self):
        for prefix in ('scene_depth_copy ', 'draw ', 'motion_input ', 'capture_event '):
            lines = fixture()
            index = next(i for i, line in enumerate(lines) if line.startswith(prefix))
            moved = lines.pop(index)
            lines.insert(0, moved)
            report = self.check_not_selected(lines)
            self.assertFalse(report['frames'][0]['complete_successful_capture'])
        lines = fixture()
        end = lines.pop()
        lines.insert(8, end)
        self.assertFalse(self.check_not_selected(lines)['frames'][0]['complete_successful_capture'])

    def test_draw_begin_success_placeholder_does_not_override_failed_draw(self):
        lines = [line.replace('result=00000000', 'result=8876086c') if line.startswith('draw_result ') else line for line in fixture()]
        report = self.check_not_selected(lines)
        self.assertFalse(report['frames'][0]['complete_successful_capture'])
        self.assertEqual(report['totals'], {})

    def test_duplicate_scalar_fields_rejected(self):
        for line in ('scene_depth_copy ', 'motion_input ', 'frame_end '):
            lines = [item + (' result=80004005' if line == 'scene_depth_copy ' else ' frame=999') if item.startswith(line) else item for item in fixture()]
            with self.assertRaises(ValueError):
                AUDIT.analyze(lines)

    def test_missing_zero_invalid_or_overflow_shader_identity_no_candidate(self):
        for replacement in ('', ' vs=0 ps=0', ' vs=invalid ps=invalid', ' vs=10000000000000000 ps=10000000000000000'):
            lines = [line.replace(' vs=aa ps=bb', replacement) if line.startswith(('draw ', 'motion_input ')) else line for line in fixture()]
            result = AUDIT.analyze(lines)
            self.assertEqual(result['totals'].get('local_metadata_candidate', 0), 0)

    def test_resource_bounds(self):
        for argument in ('max_frames', 'max_draws', 'max_events', 'max_records'):
            with self.assertRaises(ValueError):
                AUDIT.analyze(fixture(), **{argument: 0})


if __name__ == '__main__':
    unittest.main()
