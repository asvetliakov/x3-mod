#!/usr/bin/env python3
"""Factor captured named camera matrices; export numeric evidence, never shader code.

Uses the standard library. A register float4 is treated as one row of a
column-vector transform; CTAB MATRIX_COLUMNS therefore describes the transpose
of this representation. Three-register world/view-inverse matrices are extended
with the affine row (0,0,0,1), an explicit hypothesis tested by cross-draw fits.
Draw indices locate observations only and are never used as object identities.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import statistics
import struct

from summarize_capture import fields


def identity():
    return [[float(i == j) for j in range(4)] for i in range(4)]


def multiply(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def inverse(a):
    """Partial-pivot Gauss-Jordan inverse; reject singular/nonfinite inputs."""
    if not all(math.isfinite(x) for row in a for x in row):
        raise ValueError('nonfinite matrix')
    augmented = [list(row) + unit for row, unit in zip(a, identity())]
    for col in range(4):
        pivot = max(range(col, 4), key=lambda r: abs(augmented[r][col]))
        if abs(augmented[pivot][col]) < 1e-14:
            raise ValueError('singular matrix')
        augmented[col], augmented[pivot] = augmented[pivot], augmented[col]
        divisor = augmented[col][col]
        augmented[col] = [x / divisor for x in augmented[col]]
        for row in range(4):
            if row != col:
                factor = augmented[row][col]
                augmented[row] = [x - factor*y for x, y in zip(augmented[row], augmented[col])]
    return [row[4:] for row in augmented]


def error(a, b):
    return max(abs(x-y) for ar, br in zip(a, b) for x, y in zip(ar, br))


def normalized_error(a, b):
    return error(a, b) / max(1.0, max(abs(x) for row in b for x in row))


def matrix_key(a):
    return tuple(x for row in a for x in row)


def parse_capture(trace):
    """Missing float rows mean +0 in successful full sparse snapshots only.

    Legacy capture logs omit all-zero rows. Failed float queries explicitly log
    unavailable; such draws are ineligible. No constants carry across draws.
    """
    frames, draws_by_key, current = {}, {}, None
    schema_v2 = False
    def frame_key(f):
        return f"{f['device']}:{f['frame']}" if 'device' in f else f['frame']
    for line in trace.splitlines():
        f = fields(line)
        event = line.partition(' ')[0]
        if event == 'x3-modern-renderer':
            schema_v2 = int(f.get('schema', '1')) >= 2
        elif event == 'frame_begin':
            frames.setdefault(frame_key(f), dict(complete=False, draws=[]))
            current = None
        elif event == 'draw':
            current = dict(index=int(f['index']), vs=f['vs'], constants={}, unavailable=False,
                           states={}, surfaces={}, viewport={}, v2=schema_v2 or 'device' in f,
                           float_status=None, draw_result=None)
            key = frame_key(f)
            frames.setdefault(key, dict(complete=False, draws=[]))['draws'].append(current)
            draws_by_key[(key, current['index'])] = current
        elif event == 'draw_result':
            # A device's result cannot invalidate another device's latest draw.
            target = draws_by_key.get((frame_key(f), int(f['index'])))
            if target is not None:
                target['draw_result'] = int(f['result'], 16)
        elif event == 'frame_end':
            key = frame_key(f)
            if key in frames:
                frames[key]['complete'] = (f.get('present') == '00000000'
                                          and ('draws' not in f or int(f['draws']) == len(frames[key]['draws'])))
            current = None
        elif current is not None:
            if event == 'constant' and f.get('kind') == 'vs' and f.get('type', 'f') in ('f', 'float'):
                current['v2'] |= 'type' in f
                values = f['bits'].split(',')
                if len(values) != 4:
                    raise ValueError('float register requires four components')
                current['constants'][int(f['reg'])] = [struct.unpack('<f', struct.pack('<I', int(x, 16)))[0] for x in values]
            elif event == 'constants' and f.get('kind') == 'vs' and f.get('type', 'f') in ('f', 'float'):
                current['v2'] |= 'type' in f
                current['float_status'] = f
                if 'unavailable' in f or ('result' in f and int(f['result'], 16) & 0x80000000):
                    current['unavailable'] = True
            elif event == 'viewport':
                current['viewport'] = f
            elif event == 'surface':
                current['surfaces'][f['role']] = f
            elif event == 'state':
                current['states'][f['id']] = int(f['value'])
    return frames


def named_matrix(draw, metadata, name):
    parameters = [p for p in metadata.get('vs_' + draw['vs'], []) if p['name'] == name]
    if not parameters:
        return None
    p = parameters[0]
    if (p['register_set'], p['parameter_class'], p['rows'], p['columns']) != (2, 3, 4, 4) or p['count'] not in (3, 4):
        raise ValueError('unsupported layout for ' + name)
    if draw.get('v2'):
        status = draw.get('float_status')
        if not status or not {'count', 'result', 'encoding'} <= status.keys():
            raise ValueError('v2 float snapshot status missing or incomplete')
        if (int(status['result'], 16) & 0x80000000 or status['encoding'] != 'sparse_zero'
                or not 0 <= int(status['count']) <= 256):
            raise ValueError('v2 float snapshot status invalid')
        if p['register'] < 0 or p['register'] + p['count'] > int(status['count']):
            raise ValueError('float snapshot does not cover ' + name)
    rows = [draw['constants'].get(i, [0.0]*4) for i in range(p['register'], p['register'] + p['count'])]
    if len(rows) == 3:
        rows.append([0.0, 0.0, 0.0, 1.0])
    if not all(math.isfinite(x) for row in rows for x in row):
        raise ValueError('nonfinite matrix')
    return rows


def factor_draw(draw, metadata):
    if draw.get('draw_result') is not None and draw['draw_result'] & 0x80000000:
        raise ValueError(f"draw failed: {draw['draw_result']:08x}")
    if draw.get('v2') and draw.get('draw_result') is None:
        raise ValueError('v2 draw result missing')
    if draw['unavailable']:
        raise ValueError('float snapshot unavailable')
    world = named_matrix(draw, metadata, 'g_mWorld')
    camera = named_matrix(draw, metadata, 'g_mViewInverse')
    wvp = named_matrix(draw, metadata, 'g_mWorldViewProjection')
    vp = named_matrix(draw, metadata, 'g_mViewProjection')
    if camera is None or (wvp is None and vp is None) or (wvp is not None and world is None):
        return None
    if wvp is not None:
        vp = multiply(wvp, inverse(world))
        mode = 'wvp_inverse_world_camera'
    else:
        mode = 'vp_camera'
    inverse(camera)  # A singular inverse-view cannot support a view reconstruction.
    projection = multiply(vp, camera)
    return dict(draw=draw, world=world, camera=camera, wvp=wvp, vp=vp, projection=projection, mode=mode)


def projection_summary(p):
    """Fit a centered +Z perspective shape; near/far depend on that hypothesis."""
    model = [[p[0][0], 0, 0, 0], [0, p[1][1], 0, 0], [0, 0, p[2][2], p[2][3]], [0, 0, 1, 0]]
    result = dict(centered_positive_z_shape_max_error=error(p, model), x_scale=p[0][0], y_scale=p[1][1], depth_a=p[2][2], depth_b=p[2][3])
    if error(p, model) < 0.1 and p[0][0] > 0 and p[1][1] > 0 and p[2][2] > 1 and p[2][3] < 0:
        result.update(near_candidate=-p[2][3]/p[2][2], far_candidate=-p[2][3]/(p[2][2]-1),
                      vertical_fov_degrees=math.degrees(2*math.atan(1/p[1][1])), aspect_candidate=p[1][1]/p[0][0])
    return result


def summarize_group(items):
    matrices = [i['projection'] for i in items]
    median = [[statistics.median(m[r][c] for m in matrices) for c in range(4)] for r in range(4)]
    camera = items[0]['camera']
    view = inverse(camera)
    residuals, wrong_order = [], []
    for item in items:
        predicted = multiply(median, view)
        if item['wvp'] is not None:
            predicted = multiply(predicted, item['world'])
            residuals.append(normalized_error(predicted, item['wvp']))
            wrong_order.append(normalized_error(multiply(multiply(item['world'], view), median), item['wvp']))
        else:
            residuals.append(normalized_error(predicted, item['vp']))
    rotation = [row[:3] for row in camera[:3]]
    orthogonal_error = max(abs(sum(rotation[k][i]*rotation[k][j] for k in range(3)) - float(i == j)) for i in range(3) for j in range(3))
    return dict(draw_indices=[i['draw']['index'] for i in items], draw_count=len(items),
                vertex_shader_draws=dict(Counter(i['draw']['vs'] for i in items)),
                unique_world_matrices=len({matrix_key(i['world']) for i in items if i['world'] is not None}),
                camera_inverse=camera, camera_rotation_orthogonality_max_error=orthogonal_error,
                projection_median=median, projection_fit=projection_summary(median),
                projection_max_absolute_spread=max(error(m, median) for m in matrices),
                reconstructed_transform_max_relative_error=max(residuals),
                reversed_multiplication_min_relative_error=min(wrong_order) if wrong_order else None,
                factorization_modes=dict(Counter(i['mode'] for i in items)),
                depth_enabled_draws=dict(Counter(str(i['draw']['states'].get('7', 'unknown')) for i in items)),
                viewport_draws=dict(Counter(json.dumps(i['draw']['viewport'], sort_keys=True) for i in items)))


def analyze(trace, metadata, selected_frames=None):
    result = dict(convention='float4 registers as rows, column vectors; WVP = P * inverse(ViewInverse) * World',
                  assumptions=['Three-register named affine matrices gain row (0,0,0,1).',
                               'Successful legacy sparse float snapshots omit only all-zero rows.',
                               'Camera groups use exact matrix values, not draw-index correspondence.'], frames={})
    for frame_id, frame in parse_capture(trace).items():
        if selected_frames and frame_id not in selected_frames:
            continue
        report = dict(complete=frame['complete'], total_draws=len(frame['draws']), groups=[], skipped=Counter(), skipped_vertex_shader_draws=Counter(), errors=[])
        result['frames'][frame_id] = report
        if not frame['complete']:
            report['skipped']['incomplete_frame'] = len(frame['draws'])
            continue
        groups = {}
        for draw in frame['draws']:
            try:
                item = factor_draw(draw, metadata)
                if item is None:
                    report['skipped']['no_named_factorization_inputs'] += 1
                    report['skipped_vertex_shader_draws'][draw['vs']] += 1
                    continue
                groups.setdefault(matrix_key(item['camera']), []).append(item)
            except ValueError as exc:
                report['errors'].append(dict(draw=draw['index'], reason=str(exc)))
        report['groups'] = [summarize_group(items) for items in groups.values()]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--metadata', required=True, type=Path)
    parser.add_argument('--frames', nargs='+')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    raw = args.trace.read_bytes()
    result = analyze(raw.decode(), json.loads(args.metadata.read_text()), args.frames)
    result['source'] = dict(trace=args.trace.name, sha256=hashlib.sha256(raw).hexdigest(), metadata=args.metadata.name, metadata_sha256=hashlib.sha256(args.metadata.read_bytes()).hexdigest())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
    print(f'Analyzed {len(result["frames"])} frames -> {args.output}')


if __name__ == '__main__':
    main()
