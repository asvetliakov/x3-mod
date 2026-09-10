"""Noncommuting synthetic camera/world transforms test convention and rejection.

Fixtures are generated here, independent of any copyrighted game shader bytes.
"""
import math
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/analysis'))
from analyze_camera import analyze, identity, inverse, multiply, named_matrix, parse_capture


PARAMETERS = [dict(name=name, register=reg, count=count, register_set=2,
                   parameter_class=3, rows=4, columns=4)
              for name, reg, count in [('g_mWorldViewProjection', 24, 4),
                                       ('g_mWorld', 28, 3), ('g_mViewInverse', 34, 3)]]
METADATA = {'vs_test': PARAMETERS}
CAMERA = [[0, 0, 1, 10], [0, 1, 0, -7], [-1, 0, 0, 4], [0, 0, 0, 1]]
PROJECTION = [[0.75, 0, 0, 0], [0, 1.25, 0, 0], [0, 0, 1.01, -2.02], [0, 0, 1, 0]]
WORLDS = [[[2, 0, 0, 22], [0, 3, 0, 13], [0, 0, 4, -16], [0, 0, 0, 1]],
          [[0, -2, 0, -18], [3, 0, 0, 31], [0, 0, 5, 17], [0, 0, 0, 1]]]


def fixture(typed=False, perturb=False):
    lines = ['frame_begin frame=3']
    for index, world in enumerate(WORLDS, 1):
        lines.append(f'draw frame=3 index={index} kind=indexed vs=test ps=none')
        if typed:
            lines.extend(['constants kind=vs type=f count=256 result=00000000 encoding=sparse_zero',
                          'constant kind=vs type=i reg=24 values=7,8,9,10',
                          'constant kind=vs type=b reg=28 values=1'])
        wvp = multiply(multiply(PROJECTION, inverse(CAMERA)), world)
        if perturb and index == 2:
            wvp[0][0] += 2
        for reg, matrix, count in [(24, wvp, 4), (28, world, 3), (34, CAMERA, 3)]:
            for offset, row in enumerate(matrix[:count]):
                if any(row):
                    bits = ','.join(f'{struct.unpack("<I", struct.pack("<f", x))[0]:08x}' for x in row)
                    lines.append(f'constant kind=vs {"type=f " if typed else ""}reg={reg+offset} bits={bits}')
        lines.append(f'draw_result frame=3 index={index} result=00000000')
    lines.append('frame_end frame=3 draws=2 capture=1 present=00000000')
    return '\n'.join(lines)


class CameraAnalysisTests(unittest.TestCase):
    def test_noncommuting_world_view_projection_cross_draw_fit(self):
        result = analyze(fixture(), METADATA)['frames']['3']
        self.assertFalse(result['errors'])
        group = result['groups'][0]
        self.assertEqual(group['unique_world_matrices'], 2)
        self.assertLess(group['reconstructed_transform_max_relative_error'], 1e-6)
        self.assertGreater(group['reversed_multiplication_min_relative_error'], .1)
        self.assertAlmostEqual(group['projection_fit']['near_candidate'], 2, places=4)
        self.assertAlmostEqual(group['projection_fit']['far_candidate'], 202, places=2)

    def test_typed_registers_do_not_alias_float_namespace(self):
        self.assertEqual(analyze(fixture(), METADATA), analyze(fixture(typed=True), METADATA))

    def test_inconsistent_second_draw_is_detectable(self):
        group = analyze(fixture(perturb=True), METADATA)['frames']['3']['groups'][0]
        self.assertGreater(group['reconstructed_transform_max_relative_error'], .01)

    def test_unavailable_queries_exclude_draws(self):
        for marker in ['constants kind=vs unavailable=8876086c',
                       'constants kind=vs type=f count=256 result=8876086c encoding=sparse_zero']:
            trace = fixture().replace('frame_end ', marker + '\nframe_end ')
            frame = analyze(trace, METADATA)['frames']['3']
            self.assertEqual(frame['groups'][0]['draw_count'], 1)
            self.assertEqual(frame['errors'][0]['reason'], 'float snapshot unavailable')

    def test_integer_query_failure_does_not_invalidate_float(self):
        trace = fixture().replace('frame_end ', 'constants kind=vs type=i result=8876086c\nframe_end ')
        self.assertEqual(analyze(trace, METADATA), analyze(fixture(), METADATA))

    def test_incomplete_frame_is_not_evidence(self):
        trace = fixture().partition('frame_end')[0]
        frame = analyze(trace, METADATA)['frames']['3']
        self.assertFalse(frame['complete'])
        self.assertFalse(frame['groups'])

    def test_sparse_rows_are_zero_without_cross_draw_leakage(self):
        trace = 'draw frame=1 index=1 vs=test\nconstant kind=vs reg=24 bits=3f800000,00000000,00000000,00000000\ndraw frame=1 index=2 vs=test\n'
        draws = parse_capture(trace)['1']['draws']
        self.assertEqual(named_matrix(draws[0], METADATA, 'g_mWorldViewProjection')[0][0], 1)
        self.assertEqual(named_matrix(draws[1], METADATA, 'g_mWorldViewProjection'), [[0]*4 for _ in range(4)])

    def test_invalid_matrices_rejected(self):
        for matrix in [[[0]*4 for _ in range(4)], [[math.nan]*4 for _ in range(4)]]:
            with self.assertRaises(ValueError):
                inverse(matrix)
        wrong = {'vs_test': [dict(p, parameter_class=2) for p in PARAMETERS]}
        frame = analyze(fixture(), wrong)['frames']['3']
        self.assertEqual(len(frame['errors']), 2)
        self.assertFalse(frame['groups'])

    def test_v2_devices_reusing_frame_numbers_stay_separate(self):
        def device_trace(device):
            return '\n'.join(line.replace('frame=3', f'device={device} frame=3')
                             for line in fixture(typed=True).splitlines())
        result = analyze(device_trace(1) + '\n' + device_trace(2), METADATA)
        self.assertEqual(set(result['frames']), {'1:3', '2:3'})
        for frame in result['frames'].values():
            self.assertTrue(frame['complete'])
            self.assertEqual(frame['groups'][0]['draw_count'], 2)

    def test_failed_draw_result_is_excluded_and_reported(self):
        trace = fixture(typed=True).replace('index=2 result=00000000', 'index=2 result=8876086c')
        frame = analyze(trace, METADATA)['frames']['3']
        self.assertEqual(frame['groups'][0]['draw_count'], 1)
        self.assertEqual(frame['errors'], [dict(draw=2, reason='draw failed: 8876086c')])

    def test_v2_short_float_query_does_not_supply_missing_zero_rows(self):
        # Even explicitly logged higher registers cannot override query coverage.
        trace = fixture(typed=True).replace('count=256', 'count=36')
        frame = analyze(trace, METADATA)['frames']['3']
        self.assertFalse(frame['groups'])
        self.assertEqual([e['reason'] for e in frame['errors']],
                         ['float snapshot does not cover g_mViewInverse'] * 2)

    def test_v2_missing_float_status_rejected(self):
        for typed in (False, True):
            trace = 'x3-modern-renderer schema=2\n' + '\n'.join(
                line for line in fixture(typed=typed).splitlines() if not line.startswith('constants '))
            frame = analyze(trace, METADATA)['frames']['3']
            self.assertFalse(frame['groups'])
            self.assertEqual([e['reason'] for e in frame['errors']],
                             ['v2 float snapshot status missing or incomplete'] * 2)

    def test_v2_missing_draw_result_rejected(self):
        trace = '\n'.join(line for line in fixture(typed=True).splitlines()
                          if not line.startswith('draw_result '))
        frame = analyze(trace, METADATA)['frames']['3']
        self.assertFalse(frame['groups'])
        self.assertEqual([e['reason'] for e in frame['errors']], ['v2 draw result missing'] * 2)

    def test_reported_draw_count_mismatch_rejects_frame(self):
        frame = analyze(fixture().replace('draws=2', 'draws=3'), METADATA)['frames']['3']
        self.assertFalse(frame['complete'])
        self.assertFalse(frame['groups'])


if __name__ == '__main__':
    unittest.main()
