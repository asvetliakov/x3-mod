"""Original scoped-record fixtures; no game trace or vertex payloads."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/analysis'))
from inspect_particle_inputs import PARTICLE, extract, draw_succeeded


def draw(device=1, frame=8, index=1):
    return f'draw device={device} frame={frame} index={index} kind=primitive topology=4 primitives=2 vs={PARTICLE} ps=original'


def result(device=1, frame=8, index=1, status='00000000'):
    return f'draw_result device={device} frame={frame} index={index} result={status}'


class ParticleInputsTests(unittest.TestCase):
    def test_scoped_result_dictionary_is_successful(self):
        data = extract(['frame_begin device=1 frame=8', draw(), result()])
        self.assertTrue(draw_succeeded(data['rows'][0]))

    def test_exact_scope_survives_boundary_and_another_draw(self):
        data = extract(['frame_begin device=1 frame=8', draw(),
                        'capture_event device=1 frame=8 op=draw_end', draw(index=2),
                        result(index=1), result(device=2, index=2),
                        result(frame=9, index=2)])
        self.assertTrue(draw_succeeded(data['rows'][0]))
        self.assertFalse(draw_succeeded(data['rows'][1]))

    def test_failed_or_duplicate_results_are_not_success(self):
        data = extract(['frame_begin device=1 frame=8', draw(),
                        result(status='80004005'), draw(index=2),
                        result(index=2), result(index=2)])
        self.assertFalse(draw_succeeded(data['rows'][0]))
        self.assertFalse(draw_succeeded(data['rows'][1]))

    def test_only_begun_capture_frames_with_matching_counts_are_complete(self):
        data = extract(['frame_end device=1 frame=0 capture=0 draws=0 present=00000000',
                        'frame_begin device=1 frame=8', draw(), result(),
                        'frame_end device=1 frame=8 capture=1 draws=1 present=00000000',
                        'frame_end device=1 frame=9 capture=1 draws=0 present=00000000',
                        'frame_begin device=1 frame=10', draw(frame=10),
                        'frame_end device=1 frame=10 capture=1 draws=2 present=00000000',
                        'frame_begin device=1 frame=11', draw(frame=11)])
        self.assertEqual(sum(f['complete'] for f in data['frames'].values()), 1)
        self.assertEqual(data['telemetry_frame_end_count'], 1)
        self.assertEqual(len(data['frames']), 3)

    def test_uncaptured_or_ended_draws_do_not_join_capture(self):
        data = extract([draw(), 'frame_begin device=1 frame=8', draw(), result(),
                        'frame_end device=1 frame=8 capture=1 draws=1 present=00000000',
                        draw(index=2)])
        self.assertEqual(len(data['rows']), 1)


if __name__ == '__main__':
    unittest.main()
