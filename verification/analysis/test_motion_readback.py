"""Synthetic fixtures for tools/analysis/analyze_motion_readback.py.

No game data: small capture logs and RGBA32F readbacks are generated in a
temporary directory with the exact line formats the proxy writes.  The
readback pixels are produced by an independent forward model (object-space
point -> previous rows -> UV) so the analyzer's inverse mapping is not used to
build its own expectations.
"""
import array
import contextlib
import io
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/analysis'))
import analyze_motion_readback as amr  # noqa: E402

W = H = 16
VS, PS = '53a0a641107ed76c', '8759c7838bbc86c2'


def bits(*values):
    return ','.join('%08x' % struct.unpack('<I', struct.pack('<f', v))[0] for v in values)


def rows_bits(rows):
    return [bits(*row) for row in rows]


def affine(tx, ty, tz):
    return ((1.0, 0.0, 0.0, tx), (0.0, 1.0, 0.0, ty), (0.0, 0.0, 1.0, tz), (0.0, 0.0, 0.0, 1.0))


def perspective_x(tx, wx):
    """clip = (x + tx, y, z, wx * x + 1)."""
    return ((1.0, 0.0, 0.0, tx), (0.0, 1.0, 0.0, 0.0), (0.0, 0.0, 1.0, 0.0), (wx, 0.0, 0.0, 1.0))


KEYS = {
    'A': dict(node='00001000', camera='00002000', node_handle='7', camera_handle='9', node_serial='11',
              camera_serial='21', load_epoch='3', registry_epoch='5', model='00000011', lod='00000002', vb='3',
              ib='0', declaration='0cdf6a8c884ad955', offset='0', stride='24', topology='4', first='0',
              primitives='1', base_vertex='0', min_vertex='0', vertex_count='0', indexed='0'),
}
KEYS['B'] = dict(KEYS['A'], node='00001100', node_handle='8', node_serial='12', model='00000012', vb='4')
KEYS['C'] = dict(KEYS['A'], node='00001200', node_handle='9', node_serial='13', model='00000013', vb='5')


def route_line(frame, index, gate, key, rows_hash):
    fields = ' '.join(f'{name}={key[name]}' for name in amr.KEY_FIELDS if name in key)
    routed = 1 if gate in (0, 5, 6) else 0
    return (f'motion_route device=1 frame={frame} index={index} gate={gate} routed={routed} '
            f'matched={1 if gate == 0 else 0} vs={VS} ps={PS} {fields} position_offset=0 position_type=16 '
            f'rows_hash={rows_hash} result=00000000')


def draw_block(frame, index, gate, key, rows, rows_hash, viewport=(0, 0, W, H), route=True):
    lines = [f'draw device=1 frame={frame} index={index} kind=primitive topology=4 primitives=1 vs={VS} ps={PS}',
             'geometry stream=0', f'viewport x={viewport[0]} y={viewport[1]} w={viewport[2]} h={viewport[3]} minz=0 maxz=1',
             'state id=7 value=00000001',
             'constants kind=vs type=f count=256 result=00000000 encoding=sparse_zero']
    for reg, row in zip(amr.ROW_REGISTERS, rows_bits(rows)):
        lines.append(f'constant kind=vs type=f reg={reg} bits={row}')
    lines.append('constants kind=vs type=i count=16 result=00000000')
    lines.append('constant kind=vs type=i reg=0 bits=00000002,00000000,00000000,00000000')
    lines.append('draw_args start_vertex=0')
    if route:
        lines.append(route_line(frame, index, gate, key, rows_hash))
    lines.append(f'draw_result device=1 frame={frame} index={index} result=00000000')
    return lines


def frame_lines(frame, draws, captured=True, readback=True, committed=1, counters=None, reset_after=None,
                depth_readback=False, extra='', cut=None, taa_readback=False):
    """draws: list of (gate, key_name, rows, rows_hash); index 1 is a non-scene background draw.
    depth_readback adds the RT2 readback line, extra is appended to the frame summary
    (jitter and cut fields of temporal step 1), cut adds a motion_output_cut line."""
    lines = [f'frame_begin device=1 frame={frame}']
    lines += draw_block(frame, 1, None, None, affine(0, 0, 0), '0', route=False)
    for position, (gate, key_name, rows, rows_hash) in enumerate(draws):
        lines += draw_block(frame, position + 2, gate, KEYS[key_name], rows, rows_hash)
        if reset_after is not None and position + 1 == reset_after:
            lines.append('motion_output_reset device=1 result=00000000 generation=2')
    if readback:
        lines.append(f'motion_output_readback device=1 frame={frame} file=motion_1_{frame}.rgba32f width={W} '
                     f'height={H} format=rgba32f_row_major result=00000000 bytes={W * H * 16}')
    if depth_readback:
        lines.append(f'motion_output_depth_readback device=1 frame={frame} file=depth_1_{frame}.r32f width={W} '
                     f'height={H} format=r32f_row_major result=00000000 bytes={W * H * 4}')
    if cut:
        lines.append(f'motion_output_cut device=1 frame={frame} ' + ' '.join(f'{k}={v}' for k, v in cut.items()))
    if taa_readback:
        lines.append(f'motion_output_color_readback device=1 frame={frame} file=color_1_{frame}.bgra8 width={W} '
                     f'height={H} format=bgra8_row_major result=00000000 bytes={W * H * 4}')
        lines.append(f'motion_output_taa_readback device=1 frame={frame} file=taa_1_{frame}.rgba16f width={W} '
                     f'height={H} format=rgba16f_row_major result=00000000 bytes={W * H * 8}')
    gates = {g: sum(1 for d in draws if d[0] == g) for g in range(7)}
    routed = sum(1 for d in draws if d[0] in (0, 5, 6))
    summary = dict(draws=len(draws) + 1, routed=routed, matched=gates[0], gate1=0, gate2=1, gate3=gates[3],
                   gate4=gates[4], gate5=gates[5], gate6=gates[6])
    if counters:
        summary.update(counters)
    lines.append('motion_output_frame device=1 frame={f} latched=1 filled=1 fill_result=00000000 fill_restore=00000000 '
                 'draws={draws} routed={routed} matched={matched} gate1={gate1} gate2={gate2} gate3={gate3} gate4={gate4} '
                 'gate5={gate5} gate6={gate6} apply_failures=0 restore_failures=0 history_previous=0 history_current=0 '
                 'committed={c} selector_state=2 present=00000000'.format(f=frame, c=committed, **summary) + extra)
    lines.append(f'frame_end device=1 frame={frame} draws={len(draws) + 1} capture={1 if captured else 0} present=00000000')
    return lines


class Image:
    def __init__(self):
        self.pixels = [(0.0, 0.0, 0.0, -1.0)] * (W * H)

    def set(self, px, py, u, v, z, a=1.0):
        self.pixels[py * W + px] = (u, v, z, a)

    def valid(self, px, py):
        return self.pixels[py * W + px][3] == 1.0

    def write(self, path):
        with path.open('wb') as stream:
            for pixel in self.pixels:
                stream.write(struct.pack('<4f', *pixel))


def ndc_of_pixel(px, py):
    return 2.0 * px / W - 1.0, 1.0 - 2.0 * py / H


def uv_of_ndc(x, y):
    return x * 0.5 + 0.5 + 0.5 / W, -y * 0.5 + 0.5 + 0.5 / H


def paint_affine(image, footprint, current, previous, depth=0.5):
    """Object point behind pixel under `current`; previous UV/depth under `previous`."""
    for px, py in footprint:
        x, y = ndc_of_pixel(px, py)
        p = (x - current[0][3], y - current[1][3], depth - current[2][3])
        xp, yp, zp = p[0] + previous[0][3], p[1] + previous[1][3], p[2] + previous[2][3]
        u, v = uv_of_ndc(xp, yp)
        image.set(px, py, u, v, zp)


def paint_perspective(image, footprint, tx_cur, tx_prev, wx, depth=0.3):
    for px, py in footprint:
        x, y = ndc_of_pixel(px, py)
        object_x = (x - tx_cur) / (1.0 - wx * x)
        w = wx * object_x + 1.0
        object_y = y * w
        xp, yp = (object_x + tx_prev) / w, object_y / w
        u, v = uv_of_ndc(xp, yp)
        image.set(px, py, u, v, depth)


def half_bits(value):
    """binary32 -> binary16 bits, round to nearest even (normal range only)."""
    bits = struct.unpack('<I', struct.pack('<f', value))[0]
    sign = (bits >> 16) & 0x8000
    exponent = ((bits >> 23) & 255) - 127 + 15
    mantissa = bits & 0x7fffff
    if value == 0:
        return sign
    rounded = mantissa + 0xfff + ((mantissa >> 13) & 1)
    return sign | ((exponent << 10) + (rounded >> 13))


def rect(x0, x1, y0, y1):
    return [(px, py) for py in range(y0, y1) for px in range(x0, x1)]


A0, B0, C0 = affine(0.25, 0.0, 0.0), affine(-0.5, 0.25, 0.1), perspective_x(0.5, 0.25)
A2, B2, C2 = affine(0.5, 0.0, 0.0), affine(-0.5, 0.125, 0.0), perspective_x(0.6, 0.25)
FOOT_A1, FOOT_B1 = rect(2, 8, 2, 8), rect(9, 14, 9, 14)
FOOT_A2, FOOT_B2, FOOT_C2 = rect(4, 11, 2, 8), rect(9, 14, 10, 15), rect(12, 16, 0, 2)


def build_scenario(directory, **variant):
    """Frames 0..2: 0 records A/B, 1 is static (A/B matched, C recorded), 2 moves A/B/C."""
    log = []
    log.append('x3-modern-renderer version=0.4 schema=2')
    log.append('motion_output_device device=1 enabled=1 reason=ok detail=stage=compare mrt=4 vs_constants=256')
    # C is withheld (gate 5) in frame 0, so frame 1's C lookup misses (gate 6) and records it.
    log += frame_lines(0, [(6, 'A', A0, 'a0'), (6, 'B', B0, 'b0'), (5, 'C', C0, 'c0')])
    image0 = Image()
    image0.write(directory / 'motion_1_0.rgba32f')
    frame1_gate_c = variant.get('frame1_gate_c', 6)
    log += frame_lines(1, [(0, 'A', A0, 'a0'), (0, 'B', B0, 'b0'), (frame1_gate_c, 'C', C0, 'c0')],
                       counters=variant.get('frame1_counters'))
    image1 = Image()
    paint_affine(image1, FOOT_A1, A0, A0)
    paint_affine(image1, FOOT_B1, B0, B0, depth=0.75)
    if variant.get('static_violation'):
        image1.set(3, 3, (3 + 0.5) / W + 0.02, (3 + 0.5) / H, 0.5)
    if variant.get('nan_pixel'):
        image1.set(0, 0, float('nan'), 0.0, 0.0)
    if variant.get('odd_alpha'):
        image1.set(0, 1, 0.0, 0.0, 0.0, 0.5)
    image1.write(directory / 'motion_1_1.rgba32f')
    if variant.get('truncate'):
        data = (directory / 'motion_1_1.rgba32f').read_bytes()
        (directory / 'motion_1_1.rgba32f').write_bytes(data[:-16])
    gate_a2 = variant.get('frame2_gate_a', 0)
    log += frame_lines(2, [(gate_a2, 'A', A2, 'a2'), (0, 'B', B2, 'b2'), (0, 'C', C2, 'c2')],
                       readback=not variant.get('no_readback_frame2', False),
                       depth_readback=variant.get('depth_readback_lines', False),
                       extra=variant.get('frame2_extra', ''), cut=variant.get('frame2_cut'),
                       taa_readback=variant.get('taa_image', False))
    image2 = Image()
    paint_affine(image2, FOOT_A2, A2, A0)
    # B's object points sit at z=0.65: depth 0.75 under B0 (tz=0.1) in frame 1, 0.65 under B2 now.
    paint_affine(image2, FOOT_B2, B2, B0, depth=0.65)
    paint_perspective(image2, FOOT_C2, 0.6, 0.5, 0.25)
    if variant.get('huge_displacement'):
        image2.set(15, 15, 5.0, 5.0, 0.5)
    image2.write(directory / 'motion_1_2.rgba32f')
    if variant.get('depth_image'):
        # Frame 1's RT2: device depth where A and B were drawn; elsewhere the
        # clear value 1.0 (a pre-step-1 image) or the route's -1 sentinel.
        background = -1.0 if variant['depth_image'] == 'sentinel' else 1.0
        depth = [background] * (W * H)
        shift = variant.get('depth_shift_px', 0)
        for px, py in FOOT_A1:
            depth[py * W + px + shift] = 0.5
        for px, py in FOOT_B1:
            depth[py * W + px + shift] = 0.75
        if variant.get('depth_bad_values'):
            depth[0] = float('nan')
            depth[1] = 1.5
            depth[2] = -0.25
        with (directory / 'depth_1_1.r32f').open('wb') as stream:
            stream.write(struct.pack('<%df' % (W * H), *depth))
        if variant.get('depth_readback_lines'):
            log = [line for line in log]
            for number in (0, 2):
                image = [-1.0] * (W * H)
                for px, py in (FOOT_A2 + FOOT_B2 + FOOT_C2) if number == 2 else []:
                    image[py * W + px] = 0.25
                with (directory / f'depth_1_{number}.r32f').open('wb') as stream:
                    stream.write(struct.pack('<%df' % (W * H), *image))
    if variant.get('taa_image'):
        # Frame 2's pre-resolve color (gray 128 everywhere) and the resolved FP16
        # image: unchanged outside A's footprint, moved by 8/255 inside it (history
        # blended in), and one NaN pixel when requested.
        color = bytearray()
        for _ in range(W * H):
            color += bytes((128, 128, 128, 255))
        (directory / 'color_1_2.bgra8').write_bytes(bytes(color))
        resolved = array.array('H')
        for py in range(H):
            for px in range(W):
                value = 128 / 255 + (8 / 255 if (px, py) in FOOT_A2 else 0)
                bits = half_bits(value)
                if variant['taa_image'] == 'nan' and (px, py) == (0, 0):
                    bits = 0x7e00
                resolved.extend((bits, bits, bits, half_bits(1.0)))
        (directory / 'taa_1_2.rgba16f').write_bytes(resolved.tobytes())
    if variant.get('drop_readback_lines'):
        log = [line for line in log if not line.startswith('motion_output_readback')]
    (directory / 'session.log').write_text('\n'.join(log) + '\n', encoding='utf-8')
    return image1, image2


def run(directory, **options):
    return amr.analyze(directory / 'session.log', directory, amr.default_options(**options), 'depth_{device}_{frame}.r32f')


def frame(summary, number):
    return next(f for f in summary['frames'] if f['frame'] == number)


class MotionReadbackTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_static_frame_passes_with_exact_centres(self):
        build_scenario(self.dir)
        summary = run(self.dir)
        self.assertEqual(summary['status'], 'PASS', summary['failed_checks'])
        f1 = frame(summary, 1)
        self.assertEqual(f1['static_state'], 'static')
        self.assertEqual(f1['static_test']['status'], 'evaluated')
        self.assertEqual(f1['static_test']['violations'], 0)
        self.assertLess(f1['static_test']['max_error_px'], 1e-5)
        self.assertEqual(f1['readback']['validity_counts'], {'valid': 61, 'zero': 0, 'sentinel': 195, 'other': 0})
        self.assertEqual(summary['checks']['static_consistency']['status'], 'pass')
        # Frame 0 cannot be judged: its previous frame was never captured.
        self.assertEqual(frame(summary, 0)['static_state'], 'no_matched')
        self.assertIn('previous frame not captured', frame(summary, 0)['notes'][0])
        # Frame 1 draw C was recorded but unmatched (gate 6) and is predicted so.
        self.assertEqual(f1['history_pairing']['disagreements'], [])
        self.assertEqual(summary['checks']['history_pairing']['disagreements'], 0)

    def test_static_violation_is_counted(self):
        build_scenario(self.dir, static_violation=True)
        summary = run(self.dir)
        f1 = frame(summary, 1)
        self.assertEqual(f1['static_test']['violations'], 1)
        self.assertAlmostEqual(f1['static_test']['max_error_px'], 0.02 * W, places=4)
        self.assertEqual(summary['checks']['static_consistency']['status'], 'fail')
        self.assertEqual(summary['status'], 'FAIL')
        # The tolerance is a parameter: a looser one accepts the same pixel.
        loose = run(self.dir, static_tolerance_px=0.5)
        self.assertEqual(loose['checks']['static_consistency']['status'], 'pass')

    def test_displacement_statistics_and_row_consistency(self):
        build_scenario(self.dir)
        summary = run(self.dir)
        f2 = frame(summary, 2)
        self.assertEqual(f2['static_state'], 'moving')
        pixels = f2['pixels']
        self.assertEqual(pixels['valid_pixels'], len(FOOT_A2) + len(FOOT_B2) + len(FOOT_C2))
        self.assertEqual(pixels['suspicious_over_bound'], 0)
        # A moved +0.25 NDC = +2 px, so its previous position is 2 px to the left.
        self.assertAlmostEqual(pixels['displacement_x_px']['min'], -2.0, places=4)
        # B moved -0.125 NDC in y = +1 px down: previous position is 1 px up.
        self.assertAlmostEqual(pixels['displacement_y_px']['min'], -1.0, places=4)
        self.assertEqual(sum(pixels['displacement_histogram'].values()), pixels['valid_pixels'])
        self.assertAlmostEqual(pixels['depth_range'][0], 0.3, places=6)
        self.assertAlmostEqual(pixels['depth_range'][1], 0.75, places=6)
        rc = f2['row_consistency']
        self.assertEqual(rc['status'], 'evaluated')
        self.assertEqual(rc['sampled_pixels'], pixels['valid_pixels'])
        self.assertEqual(rc['unexplained_pixels'], 0)
        self.assertLess(rc['max_error_px'], 1e-4)
        self.assertEqual(len(rc['groups']), 3)
        per_draw = {entry['index']: entry for entry in rc['per_draw']}
        self.assertAlmostEqual(per_draw[2]['expected_origin_displacement_px'][0], -2.0, places=5)
        self.assertAlmostEqual(per_draw[2]['group_median_displacement_px'][0], -2.0, places=4)
        self.assertEqual(per_draw[2]['group_attributed_pixels'], len(FOOT_A2))
        self.assertAlmostEqual(per_draw[3]['expected_origin_displacement_px'][1], -1.0, places=5)
        self.assertEqual(per_draw[3]['group_attributed_pixels'], len(FOOT_B2))
        self.assertEqual(per_draw[4]['group_attributed_pixels'], len(FOOT_C2))
        self.assertAlmostEqual(per_draw[4]['expected_origin_displacement_px'][0], -0.1 * 0.5 * W, places=5)
        self.assertEqual(summary['checks']['row_consistency']['status'], 'pass')

    def test_row_consistency_rejects_wrong_previous_rows(self):
        build_scenario(self.dir)
        # Corrupt the frame-2 readback of draw A: shift its previous UV by 2 px.
        path = self.dir / 'motion_1_2.rgba32f'
        data = bytearray(path.read_bytes())
        for px, py in FOOT_A2:
            offset = (py * W + px) * 16
            u = struct.unpack_from('<f', data, offset)[0]
            struct.pack_into('<f', data, offset, u + 2.0 / W)
        path.write_bytes(bytes(data))
        summary = run(self.dir)
        rc = frame(summary, 2)['row_consistency']
        self.assertEqual(rc['unexplained_pixels'], len(FOOT_A2))
        self.assertEqual(summary['checks']['row_consistency']['status'], 'fail')

    def test_suspicious_displacement_flagged(self):
        build_scenario(self.dir, huge_displacement=True)
        summary = run(self.dir)
        f2 = frame(summary, 2)
        self.assertEqual(f2['pixels']['suspicious_over_bound'], 1)
        self.assertEqual(f2['pixels']['previous_uv_outside_unit'], 1)
        strict = run(self.dir, max_suspicious_fraction=0.0)
        self.assertEqual(strict['checks']['displacement']['status'], 'fail')

    def test_temporal_cross_check(self):
        image1, image2 = build_scenario(self.dir)
        summary = run(self.dir)
        t = frame(summary, 2)['temporal']
        self.assertEqual(t['status'], 'evaluated')
        self.assertEqual(t['previous_frame'], 1)
        covered = uncovered = 0
        for py in range(H):
            for px in range(W):
                u, v, _, a = image2.pixels[py * W + px]
                if a != 1.0:
                    continue
                if image1.valid(int(math.floor(u * W)), int(math.floor(v * H))):
                    covered += 1
                else:
                    uncovered += 1
        self.assertEqual((t['previous_covered'], t['previous_uncovered']), (covered, uncovered))
        # Column x=10 of A (6 px) maps to x=8, outside A's frame-1 footprint, and C
        # was unmatched in frame 1 (sentinel there).
        self.assertEqual(uncovered, 6 + len(FOOT_C2))
        self.assertAlmostEqual(t['covered_fraction'], covered / (covered + uncovered))
        # Frame 1 matched 2 of 3 routed draws: the criterion does not apply.
        self.assertFalse(t['criterion_applies'])
        self.assertEqual(summary['checks']['temporal_coverage']['status'], 'informative')
        # Frame 1 -> frame 0: frame 0 has no valid pixels but frame 1 does: fully uncovered.
        t1 = frame(summary, 1)['temporal']
        self.assertEqual(t1['covered_fraction'], 0.0)

    def test_temporal_criterion_applies_when_all_routed_matched(self):
        build_scenario(self.dir, frame1_gate_c=3)   # C not routed in frame 1: routed == matched == 2
        summary = run(self.dir)
        t = frame(summary, 2)['temporal']
        self.assertTrue(t['criterion_applies'])
        self.assertEqual(summary['checks']['temporal_coverage']['status'], 'fail')
        self.assertLess(summary['checks']['temporal_coverage']['worst_covered_fraction'], 0.9)
        relaxed = run(self.dir, temporal_coverage_min=0.5)
        self.assertEqual(relaxed['checks']['temporal_coverage']['status'], 'pass')

    def test_history_pairing_disagreement(self):
        build_scenario(self.dir, frame2_gate_a=6)   # DLL says no history although frame 1 recorded A
        summary = run(self.dir)
        pairing = frame(summary, 2)['history_pairing']
        self.assertEqual(len(pairing['disagreements']), 1)
        self.assertEqual(pairing['disagreements'][0]['index'], 2)
        self.assertEqual(summary['checks']['history_pairing']['status'], 'fail')

    def test_counter_mismatch_detected(self):
        build_scenario(self.dir, frame1_counters={'matched': 1})
        summary = run(self.dir)
        counters = frame(summary, 1)['counters']
        self.assertEqual(counters['status'], 'fail')
        self.assertEqual(counters['mismatches'][0]['field'], 'matched')
        self.assertEqual(counters['non_scene_draws_in_counters'], 1)
        self.assertEqual(summary['checks']['counter_consistency']['status'], 'fail')

    def test_depth_cross_check_with_optional_image(self):
        build_scenario(self.dir, depth_image=True)
        summary = run(self.dir)
        d = frame(summary, 2)['depth']
        self.assertEqual(d['status'], 'evaluated')
        # A and B pixels whose previous positions fall inside their frame-1 footprints
        # agree exactly; A's x=10 column and C land on clear depth 1.
        self.assertEqual(d['compared_pixels'], len(FOOT_A2) + len(FOOT_B2) + len(FOOT_C2))
        self.assertEqual(d['within_tolerance'], len(FOOT_A2) - 6 + len(FOOT_B2))
        self.assertEqual(summary['checks']['depth']['status'], 'fail')
        (self.dir / 'depth_1_1.r32f').unlink()
        self.assertEqual(run(self.dir)['checks']['depth']['status'], 'unavailable')

    def test_depth_sentinel_taps_are_excluded_and_integrity_is_checked(self):
        # Route-style image: -1 where no routed draw wrote. A's six pixels whose
        # previous position lies outside its frame-1 footprint and C (unwritten in
        # frame 1) become sentinel taps, not depth errors, so the check passes.
        build_scenario(self.dir, depth_image='sentinel', depth_readback_lines=True,
                       frame2_extra=' depth=1 depth_routed=3 jitter=0 jitter_index=0 jitter_x=0.000000 jitter_y=0.000000'
                                    ' jitter_previous_x=0.000000 jitter_previous_y=0.000000 jittered=0 cut=0'
                                    ' cut_median_px=1.2500 cut_missing=0.0000 cut_samples=3',
                       frame2_cut={'samples': 3, 'median_px': '1.2500', 'keyed': 3, 'missing': 0, 'missing_fraction': '0.0000',
                                   'bound_px': '0.600', 'bound_missing': '0.250', 'cut': 0})
        summary = run(self.dir)
        d = frame(summary, 2)['depth']
        self.assertEqual(d['status'], 'evaluated')
        self.assertEqual(d['sampling'], 'nearest')
        self.assertEqual(d['previous_jitter_px'], [0.0, 0.0])
        self.assertEqual(d['compared_pixels'], len(FOOT_A2) - 6 + len(FOOT_B2))
        self.assertEqual(d['previous_sentinel'], 6 + len(FOOT_C2))
        self.assertEqual(d['within_fraction'], 1.0)
        self.assertEqual(d['error']['max'], 0.0)
        self.assertEqual(summary['checks']['depth']['status'], 'pass')
        integrity = summary['checks']['depth_image_integrity']
        self.assertEqual(integrity['status'], 'pass')
        self.assertEqual(integrity['frames'], [0, 1, 2])
        self.assertEqual(integrity['valid_motion_without_depth'], 0)
        # Frame 2's logged readback names the file; its stats follow the RT2 contract.
        image = frame(summary, 2)['depth_image']
        self.assertEqual((image['status'], image['file']), ('loaded', 'depth_1_2.r32f'))
        self.assertEqual(image['written'], len(FOOT_A2) + len(FOOT_B2) + len(FOOT_C2))
        self.assertEqual(image['sentinel'], W * H - image['written'])
        self.assertEqual(image['written_range'], [0.25, 0.25])
        self.assertTrue(image['clean'])
        self.assertAlmostEqual(image['sentinel_fraction'], 1 - image['written'] / (W * H))
        # Frame 1 (pattern only, no log line): written where A and B were drawn.
        self.assertEqual(frame(summary, 1)['depth_image']['written'], len(FOOT_A1) + len(FOOT_B1))
        cut = frame(summary, 2)['cut']
        self.assertEqual(cut['status'], 'reported')
        self.assertEqual((cut['cut'], cut['median_px'], cut['samples'], cut['bound_px']), (0, 1.25, 3, 0.6))
        self.assertEqual(frame(summary, 1)['cut']['status'], 'unavailable')
        self.assertIn('depth image:', amr.render_report(summary))
        self.assertIn('cut detector: cut=0', amr.render_report(summary))

    def test_resolved_image_sanity_signal(self):
        build_scenario(self.dir, taa_image=True)
        summary = run(self.dir)
        taa = frame(summary, 2)['taa']
        self.assertEqual((taa['status'], taa['file'], taa['color_file']), ('loaded', 'taa_1_2.rgba16f', 'color_1_2.bgra8'))
        self.assertEqual((taa['nonfinite'], taa['differing'], taa['pixels']), (0, len(FOOT_A2), W * H))
        self.assertAlmostEqual(taa['differing_fraction'], len(FOOT_A2) / (W * H))
        self.assertAlmostEqual(taa['max_difference'], 8 / 255, places=3)
        self.assertTrue(taa['clean'])
        check = summary['checks']['taa_image']
        self.assertEqual((check['status'], check['frames'], check['unclean_frames']), ('pass', [2], []))
        self.assertIn('resolved image: taa_1_2.rgba16f', amr.render_report(summary))
        # A larger threshold hides the history contribution; the check still passes.
        self.assertEqual(frame(run(self.dir, taa_threshold=0.5), 2)['taa']['differing'], 0)
        self.assertEqual(run(self.dir)['checks']['readback_integrity']['status'], 'pass')
        # Frames without the debug readback report the check as unavailable.
        self.assertEqual(frame(summary, 1).get('taa'), None)
        # A nonfinite resolved value fails the image and the integrity check.
        build_scenario(self.dir, taa_image='nan')
        summary = run(self.dir)
        self.assertEqual(frame(summary, 2)['taa']['nonfinite'], 1)
        self.assertEqual(summary['checks']['taa_image']['status'], 'fail')
        self.assertEqual(summary['checks']['readback_integrity']['status'], 'fail')
        # A logged resolved image whose color image is missing is malformed.
        (self.dir / 'color_1_2.bgra8').unlink()
        summary = run(self.dir)
        self.assertEqual(frame(summary, 2)['taa']['status'], 'malformed')
        self.assertEqual(summary['checks']['taa_image']['status'], 'unavailable')

    def test_depth_image_integrity_rejects_nonfinite_and_out_of_range(self):
        build_scenario(self.dir, depth_image='sentinel', depth_bad_values=True)
        summary = run(self.dir)
        image = frame(summary, 1)['depth_image']
        self.assertEqual((image['nonfinite'], image['out_of_range'], image['clean']), (1, 2, False))
        self.assertEqual(summary['checks']['depth_image_integrity']['status'], 'fail')
        self.assertEqual(summary['checks']['depth_image_integrity']['unclean_frames'], [1])
        self.assertEqual(summary['checks']['readback_integrity']['status'], 'fail')
        self.assertTrue(any('depth_1_1.r32f' in error for error in summary['hard_errors']))

    def test_depth_comparison_applies_the_previous_raster_jitter(self):
        # Frame 1 was rasterized one pixel to the right (jitter +1 px); its depth
        # image is shifted accordingly while the producer's RG stays unjittered.
        # Frame 2 logs jitter_previous_x=1, which the comparison adds back.
        extra = ' jitter=1 jitter_index=2 jitter_x=-0.250000 jitter_y=0.166667 jitter_previous_x=1.000000 jitter_previous_y=0.000000'
        build_scenario(self.dir, depth_image='sentinel', depth_shift_px=1, frame2_extra=extra)
        summary = run(self.dir)
        d = frame(summary, 2)['depth']
        self.assertEqual(d['previous_jitter_px'], [1.0, 0.0])
        self.assertEqual(d['compared_pixels'], len(FOOT_A2) - 6 + len(FOOT_B2))
        self.assertEqual(d['within_fraction'], 1.0)
        # Without the jitter field the comparison reads one texel to the left of
        # the shifted image: A's leftmost column of taps becomes sentinel or wrong.
        build_scenario(self.dir, depth_image='sentinel', depth_shift_px=1)
        shifted = frame(run(self.dir), 2)['depth']
        self.assertLess(shifted['compared_pixels'] - shifted['previous_sentinel'], d['compared_pixels'])

    def test_bilinear_depth_sampling(self):
        build_scenario(self.dir, depth_image='sentinel')
        d = frame(run(self.dir, depth_sampling='bilinear'), 2)['depth']
        self.assertEqual(d['sampling'], 'bilinear')
        # Interior taps agree exactly; taps at the footprint border average with
        # dropped sentinel neighbours, i.e. still the written value.
        self.assertEqual(d['within_fraction'], 1.0)
        self.assertEqual(d['error']['max'], 0.0)
        depth = array.array('f', [-1.0] * (W * H))
        depth[5 * W + 5], depth[5 * W + 6] = 0.5, 0.7
        # Halfway between the two texel centres: nearest picks (6,5), bilinear averages.
        u, v = 6.0 / W, 5.5 / H
        nearest = amr.sample_depth(depth, W, H, u, v, False)
        self.assertAlmostEqual(nearest[0], 0.7, places=6)
        self.assertIsNone(nearest[1])
        self.assertAlmostEqual(amr.sample_depth(depth, W, H, u, v, True)[0], 0.6)
        # Quarter of the way: the sentinel row below is dropped and renormalized.
        self.assertAlmostEqual(amr.sample_depth(depth, W, H, 5.75 / W, 5.75 / H, True)[0], 0.55)
        self.assertEqual(amr.sample_depth(depth, W, H, 10.5 / W, 10.5 / H, True), (None, 'sentinel'))
        self.assertEqual(amr.sample_depth(depth, W, H, 1.5, 0.5, True), (None, 'offscreen'))
        self.assertEqual(amr.sample_depth(depth, W, H, 1.5, 0.5, False), (None, 'offscreen'))

    def test_malformed_readback_rejected(self):
        build_scenario(self.dir, truncate=True)
        summary = run(self.dir)
        f1 = frame(summary, 1)
        self.assertEqual(f1['readback']['status'], 'malformed')
        self.assertIn('size', f1['readback']['reason'])
        self.assertEqual(summary['checks']['readback_integrity']['status'], 'fail')
        self.assertEqual(summary['status'], 'FAIL')
        # Frame 2 still analyses; its temporal check has no usable predecessor.
        self.assertEqual(frame(summary, 2)['temporal']['status'], 'unavailable')

    def test_nonfinite_and_out_of_abi_values_rejected(self):
        build_scenario(self.dir, nan_pixel=True, odd_alpha=True)
        summary = run(self.dir)
        rb = frame(summary, 1)['readback']
        self.assertEqual(rb['nonfinite_values'], 1)
        self.assertEqual(rb['validity_counts']['other'], 2)
        self.assertFalse(rb['abi_clean'])
        self.assertEqual(summary['checks']['readback_integrity']['status'], 'fail')

    def test_log_without_readback_lines_is_malformed(self):
        build_scenario(self.dir, drop_readback_lines=True)
        with self.assertRaises(amr.MalformedInput):
            run(self.dir)

    def test_cli_writes_summary_and_report(self):
        build_scenario(self.dir)
        results = self.dir / 'results'
        with contextlib.redirect_stdout(io.StringIO()):
            code = amr.main([str(self.dir / 'session.log'), '--label', 'unit', '--results-dir', str(results), '--no-draw-details'])
        self.assertEqual(code, 0)
        summary = json.loads((results / 'motion-readback-unit-summary.json').read_text())
        self.assertEqual(summary['status'], 'PASS')
        self.assertNotIn('draws', summary['frames'][0])
        report = (results / 'motion-readback-unit.txt').read_text()
        self.assertIn('static test: evaluated', report)
        self.assertIn('row consistency: sampled=', report)

    def test_matrix_inverse(self):
        m = perspective_x(0.5, 0.25)
        product = amr.matrix_multiply(m, amr.matrix_inverse(m))
        for i in range(4):
            for j in range(4):
                self.assertAlmostEqual(product[i][j], 1.0 if i == j else 0.0, places=12)
        singular = ((1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0), (0.0, 0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))
        self.assertIsNone(amr.matrix_inverse(singular))


if __name__ == '__main__':
    unittest.main()
