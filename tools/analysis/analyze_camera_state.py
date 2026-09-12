#!/usr/bin/env python3
"""Cross-check the route's live camera state against the captured draw constants.

Reads a proxy session log (standard library only, never the whole file at once
beyond one pass over its lines) and reports, per device:

- every `camera_state` line (capture frames and the periodic cadence): the
  validity of the scene read, the policy decision of the frame's resolve
  (policy 2 = far-plane camera reprojection of sentinel pixels), the camera
  cut count and the rotation statistics;
- for capture frames that also carry per-draw vertex constants (the capture's
  `constant kind=vs` records with the shader-register metadata of
  camera-numerics.md), the view matrix recovered from the uploaded
  g_mViewInverse (c34-36 for the reviewed families) inverted, compared with
  the rotation/translation the route read from the engine's view buffer, and
  the projection recovered as P = WVP * W^-1 * C compared with the read m00/m11;
- the `object_matrix role=view` rows of object-trace scopes (raw float bits of
  the same engine buffer at draw time), compared bit-exactly with the read.

The register representation follows analyze_camera.py: each captured float4
register is one ROW of a matrix multiplying COLUMN vectors, the transpose of
the engine's row-vector buffers (camera-state-and-frame-routine.md), so the
route's rotation r[i][j] (world axis i, view axis j) is compared with
V_col[j][i] = inverse(C)[j][i] and the translation t[i] with V_col[i][3].
"""
import argparse
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze_camera  # noqa: E402

ROTATION_TOLERANCE = 1e-3     # camera-numerics.md: C^T C - I up to 3.6e-5 on captured views
TRANSLATION_TOLERANCE = 1e-2  # relative to the larger magnitude
PROJECTION_TOLERANCE = 1e-3
REASONS = {0: 'camera_path', 1: 'switch_off', 2: 'current_invalid', 3: 'previous_invalid', 4: 'rotation_cut', 5: 'transform_failed'}


def fields(line):
    out = {}
    for token in line.split(' ')[1:]:
        key, sep, value = token.partition('=')
        if sep:
            out[key] = value
    return out


def bits_to_float(word):
    return struct.unpack('<f', struct.pack('<I', int(word, 16)))[0]


def parse_camera_lines(lines):
    """camera_state and motion_output_frame lines keyed by (device, frame)."""
    states, frames, objects = {}, {}, {}
    current_frame = None
    for line in lines:
        event = line.partition(' ')[0]
        if event == 'camera_state':
            f = fields(line)
            if 'frame' not in f:  # the loader's `camera_state active=.. status=..` line
                continue
            states[(f['device'], int(f['frame']))] = f
        elif event == 'motion_output_frame':
            f = fields(line)
            frames[(f['device'], int(f['frame']))] = f
        elif event == 'object_context':
            f = fields(line)
            current_frame = (f['device'], int(f['frame']), int(f['index']))
        elif event == 'object_matrix' and current_frame is not None:
            f = fields(line)
            if f.get('role') == 'view':
                objects.setdefault(current_frame, {})[int(f['row'])] = [bits_to_float(x) for x in f['bits'].split(',')]
    return states, frames, objects


def state_matrices(state):
    """Rotation rows, translation and projection terms of a camera_state line."""
    if state.get('valid') != '1':
        return None
    r = [[float(state[f'r{i}{j}']) for j in range(3)] for i in range(3)]
    t = [float(x) for x in state['t'].split(',')]
    return dict(r=r, t=t, m00=float(state['p00']), m11=float(state['p11']), m20=float(state['p20']), m21=float(state['p21']))


def compare_draw(state, factored):
    """Deviation of one draw's recovered camera from the route's read."""
    v_col = analyze_camera.inverse(factored['camera'])
    rotation = max(abs(v_col[j][i] - state['r'][i][j]) for i in range(3) for j in range(3))
    translation = max(abs(v_col[i][3] - state['t'][i]) / max(1.0, abs(v_col[i][3]), abs(state['t'][i])) for i in range(3))
    p = factored['projection']
    projection = max(abs(p[0][0] - state['m00']), abs(p[1][1] - state['m11']))
    return dict(rotation=rotation, translation=translation, projection=projection,
                recovered_m00=p[0][0], recovered_m11=p[1][1], recovered_translation=[v_col[i][3] for i in range(3)])


def compare_object_view(state, rows):
    """Bit-level comparison with an object-trace view snapshot (row-vector buffer)."""
    if sorted(rows) != [0, 1, 2, 3]:
        return None
    rotation = max(abs(rows[i][j] - state['r'][i][j]) for i in range(3) for j in range(3))
    translation = max(abs(rows[3][i] - state['t'][i]) / max(1.0, abs(rows[3][i]), abs(state['t'][i])) for i in range(3))
    return dict(rotation=rotation, translation=translation)


def analyze(lines, metadata=None):
    states, frames, objects = parse_camera_lines(lines)
    draw_frames = analyze_camera.parse_capture('\n'.join(lines)) if metadata is not None else {}
    report = dict(devices={}, capture_frames=[], checks=[])
    for (device, frame), state in sorted(states.items(), key=lambda kv: (kv[0][0], kv[0][1])):
        summary = report['devices'].setdefault(device, dict(states=0, valid=0, policy_2=0, camera_cuts=0, reasons={}, max_rotation_deg=0.0,
                                                          read_failures={}, background_rotation_max_deg=0.0, background_fov_mismatch=0))
        summary['states'] += 1
        summary['valid'] += state.get('valid') == '1'
        summary['policy_2'] += state.get('policy') == '2'
        summary['camera_cuts'] += state.get('camera_cut') == '1'
        reason = REASONS.get(int(state.get('reason', '1')), 'unknown')
        summary['reasons'][reason] = summary['reasons'].get(reason, 0) + 1
        summary['max_rotation_deg'] = max(summary['max_rotation_deg'], float(state.get('rotation_deg', '0')))
        if state.get('read_failure', '0') != '0':
            summary['read_failures'][state['read_failure'] + ':' + state.get('failure', '0')] = summary['read_failures'].get(state['read_failure'] + ':' + state.get('failure', '0'), 0) + 1
        if state.get('background_valid') == '1' and state.get('valid') == '1':
            summary['background_rotation_max_deg'] = max(summary['background_rotation_max_deg'], float(state.get('background_rotation_deg', '0')))
            if abs(float(state['background_p00']) - float(state['p00'])) > PROJECTION_TOLERANCE or abs(float(state['background_p11']) - float(state['p11'])) > PROJECTION_TOLERANCE:
                summary['background_fov_mismatch'] += 1
        matrices = state_matrices(state)
        frame_report = dict(device=device, frame=frame, valid=matrices is not None, policy=int(state.get('policy', '1')), reason=reason,
                            camera_cut=state.get('camera_cut') == '1', rotation_deg=float(state.get('rotation_deg', '0')), draws_compared=0,
                            draws_agreeing=0, object_views_compared=0, object_views_agreeing=0,
                            max_rotation_deviation=None, max_translation_deviation=None, max_projection_deviation=None)
        key = f'{device}:{frame}'
        if matrices is not None and key in draw_frames:
            for draw in draw_frames[key]['draws']:
                try:
                    factored = analyze_camera.factor_draw(draw, metadata)
                except ValueError:
                    continue
                if factored is None:
                    continue
                deviation = compare_draw(matrices, factored)
                frame_report['draws_compared'] += 1
                agree = deviation['rotation'] <= ROTATION_TOLERANCE and deviation['projection'] <= PROJECTION_TOLERANCE
                frame_report['draws_agreeing'] += agree
                for name in ('rotation', 'translation', 'projection'):
                    current = frame_report[f'max_{name}_deviation']
                    frame_report[f'max_{name}_deviation'] = deviation[name] if current is None else max(current, deviation[name])
        if matrices is not None:
            for (odevice, oframe, index), rows in objects.items():
                if odevice != device or oframe != frame:
                    continue
                deviation = compare_object_view(matrices, rows)
                if deviation is None:
                    continue
                frame_report['object_views_compared'] += 1
                frame_report['object_views_agreeing'] += deviation['rotation'] <= 1e-6 and deviation['translation'] <= 1e-6
        if key in draw_frames or any(k[0] == device and k[1] == frame for k in objects):
            report['capture_frames'].append(frame_report)
    report['checks'] = build_checks(report)
    report['frames_with_state'] = len(states)
    report['frames_with_route_line'] = len(frames)
    return report


def build_checks(report):
    checks = []
    compared = [f for f in report['capture_frames'] if f['draws_compared']]
    if compared:
        agreeing = sum(f['draws_agreeing'] for f in compared)
        total = sum(f['draws_compared'] for f in compared)
        checks.append(dict(name='draw_constants_agree', status='pass' if agreeing == total else 'fail', draws=total, agreeing=agreeing,
                           max_rotation_deviation=max(f['max_rotation_deviation'] for f in compared),
                           max_projection_deviation=max(f['max_projection_deviation'] for f in compared)))
    else:
        checks.append(dict(name='draw_constants_agree', status='unavailable', reason='no capture frame carries both a valid camera_state and factorable draw constants'))
    objects = [f for f in report['capture_frames'] if f['object_views_compared']]
    if objects:
        agreeing = sum(f['object_views_agreeing'] for f in objects)
        total = sum(f['object_views_compared'] for f in objects)
        checks.append(dict(name='object_trace_view_agrees', status='pass' if agreeing == total else 'fail', views=total, agreeing=agreeing))
    else:
        checks.append(dict(name='object_trace_view_agrees', status='unavailable', reason='no object_matrix role=view rows in a frame with a valid camera_state'))
    for device, summary in report['devices'].items():
        checks.append(dict(name=f'camera_read_valid:{device}', status='pass' if summary['valid'] == summary['states'] else 'fail',
                           states=summary['states'], valid=summary['valid'], read_failures=summary['read_failures']))
    return checks


def render(report):
    lines = [f"camera_state lines: {report['frames_with_state']}; motion_output_frame lines: {report['frames_with_route_line']}"]
    for device, s in report['devices'].items():
        lines.append(f"device {device}: states={s['states']} valid={s['valid']} policy2={s['policy_2']} camera_cuts={s['camera_cuts']} "
                     f"max_rotation_deg={s['max_rotation_deg']:.3f} reasons={s['reasons']} read_failures={s['read_failures']} "
                     f"background_rotation_max_deg={s['background_rotation_max_deg']:.3f} background_fov_mismatch={s['background_fov_mismatch']}")
    for f in report['capture_frames']:
        lines.append(f"frame {f['device']}:{f['frame']} valid={int(f['valid'])} policy={f['policy']} reason={f['reason']} rotation_deg={f['rotation_deg']:.3f} "
                     f"draws={f['draws_agreeing']}/{f['draws_compared']} rotation_dev={f['max_rotation_deviation']} translation_dev={f['max_translation_deviation']} "
                     f"projection_dev={f['max_projection_deviation']} object_views={f['object_views_agreeing']}/{f['object_views_compared']}")
    for c in report['checks']:
        lines.append(f"check {c['name']}: {c['status']} " + ' '.join(f'{k}={v}' for k, v in c.items() if k not in ('name', 'status')))
    return '\n'.join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', type=Path)
    parser.add_argument('--metadata', type=Path, default=None, help='shader-registers.json (verification/results) for the draw-constant cross-check')
    parser.add_argument('--output', type=Path, default=None)
    args = parser.parse_args(argv)
    metadata = json.loads(args.metadata.read_text()) if args.metadata else None
    with args.log.open(errors='replace') as handle:
        lines = [line.rstrip('\n') for line in handle]
    report = analyze(lines, metadata)
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(render(report))
    return 0 if all(c['status'] != 'fail' for c in report['checks']) else 1


if __name__ == '__main__':
    sys.exit(main())
