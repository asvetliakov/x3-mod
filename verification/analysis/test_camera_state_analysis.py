"""Synthetic-log tests for tools/analysis/analyze_camera_state.py.

The log is built from a known row-vector camera (yaw plus translation), a
world matrix and the game's projection terms: the draw's registers are the
column-vector transposes the capture records (analyze_camera.py's
representation), the object-trace view rows are the raw engine buffer, and
the camera_state line is what the route logs from the same buffer. No capture
of the game is used.
"""
import importlib.util
import math
from pathlib import Path
import struct
import unittest

PATH = Path(__file__).parents[2] / 'tools/analysis/analyze_camera_state.py'
SPEC = importlib.util.spec_from_file_location('analyze_camera_state', PATH)
ANALYZE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYZE)
VS_HASH = '53a0a641107ed76c'
METADATA = {'vs_' + VS_HASH: [
    dict(name='g_mWorldViewProjection', register_set=2, parameter_class=3, rows=4, columns=4, count=4, register=24),
    dict(name='g_mWorld', register_set=2, parameter_class=3, rows=4, columns=4, count=3, register=28),
    dict(name='g_mViewInverse', register_set=2, parameter_class=3, rows=4, columns=4, count=3, register=34)]}


def multiply(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def transpose(a):
    return [[a[j][i] for j in range(4)] for i in range(4)]


def view_row(yaw, position):
    right, up, forward = (math.cos(yaw), 0, -math.sin(yaw)), (0, 1, 0), (math.sin(yaw), 0, math.cos(yaw))
    v = [[right[i], up[i], forward[i], 0] for i in range(3)] + [[0, 0, 0, 1]]
    v[3][0] = -sum(position[i] * right[i] for i in range(3))
    v[3][1] = -sum(position[i] * up[i] for i in range(3))
    v[3][2] = -sum(position[i] * forward[i] for i in range(3))
    return v


def projection_row(m00=0.8, m11=4 / 3):
    return [[m00, 0, 0, 0], [0, m11, 0, 0], [0, 0, 1.000003, 1], [0, 0, -6.0000184, 0]]


def world_row():
    a = 0.3
    return [[math.cos(a), 0, -math.sin(a), 0], [0, 1, 0, 0], [math.sin(a), 0, math.cos(a), 0], [10, 20, 30, 1]]


def bits(values):
    return ','.join(f'{struct.unpack("<I", struct.pack("<f", v))[0]:08x}' for v in values)


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


def camera_state_line(frame, v, p, policy=2, reason=0, cut=0, rotation=1.0, valid=1):
    r = ' '.join(f'r{i}{j}={f32(v[i][j]):.7g}' for i in range(3) for j in range(3))
    t = ','.join(f'{f32(v[3][i]):.7g}' for i in range(3))
    return (f'camera_state device=1 frame={frame} status=active reads=2 valid={valid} read_failure={0 if valid else 4} failure={0 if valid else 5} '
            f'projection=00a0 view=00b0 p00={p[0][0]:.7g} p11={p[1][1]:.7g} p20=0 p21=0 {r} t={t} background_valid=1 background_p00={p[0][0]:.7g} '
            f'background_p11={p[1][1]:.7g} background_rotation_deg=0.0000 history_view_valid=1 history_view_frame={frame} rotation_deg={rotation:.4f} '
            f'policy={policy} reason={reason} camera_cut={cut} mode=0 cut_deg=20.00')


def synthetic_log(v, p, w, state_v=None, with_object=True):
    wvp = multiply(multiply(w, v), p)
    wvp_t, w_t = transpose(wvp), transpose(w)
    v_inverse_t = transpose(ANALYZE.analyze_camera.inverse(v))
    lines = ['x3-modern-renderer version=0.4 schema=2 capture_start=1 capture_frames=1 pointer_bits=32',
             'frame_begin device=1 frame=7', f'draw device=1 frame=7 index=1 vs={VS_HASH} ps=deadbeef']
    for i in range(4):
        lines.append(f'constant kind=vs type=f reg={24 + i} bits={bits(wvp_t[i])}')
    for i in range(3):
        lines.append(f'constant kind=vs type=f reg={28 + i} bits={bits(w_t[i])}')
    for i in range(3):
        lines.append(f'constant kind=vs type=f reg={34 + i} bits={bits(v_inverse_t[i])}')
    lines.append('constants kind=vs type=f count=256 result=00000000 encoding=sparse_zero')
    if with_object:
        lines.append('object_context device=1 frame=7 index=1 scoped=1 valid=127 session=1 scope_depth=1 mesh=0 node=0')
        for row in range(4):
            lines.append(f'object_matrix role=view row={row} bits={bits(v[row])}')
    lines.append('draw_result device=1 frame=7 index=1 result=00000000')
    lines.append('frame_end device=1 frame=7 present=00000000 draws=1')
    lines.append(camera_state_line(7, state_v or v, p))
    lines.append('motion_output_frame device=1 frame=7 latched=1 taa_resolved=1 camera_policy=2')
    lines.append(camera_state_line(300, state_v or v, p, policy=1, reason=4, cut=1, rotation=25.0))
    lines.append(camera_state_line(600, state_v or v, p, valid=0, policy=1, reason=2))
    return lines


class CameraStateAnalysis(unittest.TestCase):
    def setUp(self):
        self.v, self.p, self.w = view_row(0.4, (1000, -250, 12.5)), projection_row(), world_row()

    def test_consistent_log_agrees(self):
        report = ANALYZE.analyze(synthetic_log(self.v, self.p, self.w), METADATA)
        checks = {c['name']: c for c in report['checks']}
        self.assertEqual(checks['draw_constants_agree']['status'], 'pass')
        self.assertEqual((checks['draw_constants_agree']['draws'], checks['draw_constants_agree']['agreeing']), (1, 1))
        self.assertLess(checks['draw_constants_agree']['max_rotation_deviation'], 1e-5)
        self.assertLess(checks['draw_constants_agree']['max_projection_deviation'], 1e-5)
        self.assertEqual(checks['object_trace_view_agrees']['status'], 'pass')
        frame = report['capture_frames'][0]
        self.assertEqual((frame['frame'], frame['policy'], frame['reason'], frame['draws_compared'], frame['object_views_compared']), (7, 2, 'camera_path', 1, 1))
        self.assertLess(frame['max_translation_deviation'], 1e-5)
        device = report['devices']['1']
        self.assertEqual((device['states'], device['valid'], device['policy_2'], device['camera_cuts']), (3, 2, 1, 1))
        self.assertEqual(device['reasons'], {'camera_path': 1, 'rotation_cut': 1, 'current_invalid': 1})
        self.assertEqual(device['max_rotation_deg'], 25.0)
        self.assertEqual(device['read_failures'], {'4:5': 1})
        self.assertEqual(checks['camera_read_valid:1']['status'], 'fail')  # one of three reads failed
        text = ANALYZE.render(report)
        self.assertIn('check draw_constants_agree: pass', text)

    def test_wrong_rotation_fails(self):
        wrong = view_row(0.5, (1000, -250, 12.5))
        report = ANALYZE.analyze(synthetic_log(self.v, self.p, self.w, state_v=wrong), METADATA)
        checks = {c['name']: c for c in report['checks']}
        self.assertEqual(checks['draw_constants_agree']['status'], 'fail')
        self.assertEqual(checks['object_trace_view_agrees']['status'], 'fail')
        self.assertGreater(report['capture_frames'][0]['max_rotation_deviation'], 0.05)

    def test_projection_mismatch_fails(self):
        report = ANALYZE.analyze(synthetic_log(self.v, projection_row(1.0, 1.0), self.w), METADATA)
        # The draw carries the 1.0/1.0 projection, the state line too: agreement.
        self.assertEqual({c['name']: c for c in report['checks']}['draw_constants_agree']['status'], 'pass')
        lines = synthetic_log(self.v, self.p, self.w)
        lines = [l.replace('p00=0.8 ', 'p00=0.9 ') if l.startswith('camera_state') else l for l in lines]
        report = ANALYZE.analyze(lines, METADATA)
        self.assertEqual({c['name']: c for c in report['checks']}['draw_constants_agree']['status'], 'fail')

    def test_without_metadata_or_objects(self):
        report = ANALYZE.analyze(synthetic_log(self.v, self.p, self.w, with_object=False), None)
        checks = {c['name']: c for c in report['checks']}
        self.assertEqual(checks['draw_constants_agree']['status'], 'unavailable')
        self.assertEqual(checks['object_trace_view_agrees']['status'], 'unavailable')
        self.assertEqual(report['capture_frames'], [])
        self.assertEqual(report['frames_with_state'], 3)

    def test_main_writes_report(self):
        import json
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'session.log'
            log.write_text('\n'.join(synthetic_log(self.v, self.p, self.w)) + '\n')
            metadata = Path(directory) / 'registers.json'
            metadata.write_text(json.dumps(METADATA))
            output = Path(directory) / 'report.json'
            code = ANALYZE.main([str(log), '--metadata', str(metadata), '--output', str(output)])
            self.assertEqual(code, 1)  # the synthetic invalid read at frame 600 fails the read-validity check
            self.assertEqual(json.loads(output.read_text())['frames_with_state'], 3)


if __name__ == '__main__':
    unittest.main()
