#!/usr/bin/env python3
"""Compare captured transforms across consecutive frames using candidate draw keys.

Keys identify resource/range/material observations, never engine objects. Duplicate
keys are excluded. Buffer contents and per-vertex animation are not captured, so
even unique keys cannot prove object identity or final motion-vector correctness.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path

from analyze_camera import parse_capture, factor_draw, matrix_key, error
from summarize_capture import summarize, fields


def candidate_key(draw):
    """Return a pointer-free key, or None without complete buffered geometry."""
    if draw.get('geometry', {}).get('source') != 'buffers':
        return None
    streams = draw.get('stream', [])
    if not streams or any(int(s.get('result', '80004005'), 16) & 0x80000000 for s in streams):
        return None
    bound = [s for s in streams if s.get('identity', '0') != '0']
    if not bound or any(not all(k in s for k in ('slot', 'identity', 'offset', 'stride', 'frequency'))
                        or int(s.get('frequency_result', '80004005'), 16) & 0x80000000 for s in bound):
        return None
    indexed = draw['kind'] == 'indexed'
    indices = draw.get('indices', {})
    if indexed and (indices.get('identity', '0') == '0' or int(indices.get('result', '80004005'), 16) & 0x80000000):
        return None
    if not draw.get('draw_args') or 'topology' not in draw:
        return None
    value = dict(device=draw.get('device'), kind=draw['kind'], topology=draw['topology'],
                 vs=draw['vs'], ps=draw['ps'], primitives=draw['primitives'],
                 streams=[{k: s[k] for k in ('slot', 'identity', 'offset', 'stride', 'frequency')} for s in bound],
                 index_identity=indices.get('identity') if indexed else None,
                 args=draw['draw_args'], declaration=draw.get('vertex_element', []),
                 viewport=draw.get('viewport'), states=draw.get('states'),
                 targets=[(s['role'], s.get('identity')) for s in draw.get('targets', [])],
                 depth=draw.get('depth', {}).get('identity'),
                 textures=[(t.get('stage'), t.get('identity')) for t in draw.get('texture', [])])
    return json.dumps(value, sort_keys=True, separators=(',', ':'))


def origin_pixel(item):
    """Project synthetic local origin; this is not a captured geometry vertex."""
    m = item['wvp']
    if m is None or m[3][3] <= 0:
        return None
    x, y, z = [m[i][3]/m[3][3] for i in range(3)]
    if not (-1 <= x <= 1 and -1 <= y <= 1 and 0 <= z <= 1):
        return None
    vp = item['draw']['viewport']
    return [float(vp['x']) + (x+1)*float(vp['w'])/2,
            float(vp['y']) + (1-y)*float(vp['h'])/2]


def camera_delta(a, b):
    forward_a = [a[i][2] for i in range(3)]
    forward_b = [b[i][2] for i in range(3)]
    norm = math.sqrt(sum(x*x for x in forward_a)*sum(x*x for x in forward_b))
    cos = sum(x*y for x,y in zip(forward_a, forward_b))/norm
    delta = [b[i][3]-a[i][3] for i in range(3)]
    return dict(translation_delta=delta, translation_distance=math.sqrt(sum(x*x for x in delta)),
                forward_axis_angle_degrees=math.degrees(math.acos(max(-1, min(1, cos)))),
                rotation_max_element_delta=max(abs(b[i][j]-a[i][j]) for i in range(3) for j in range(3)))


def compare_candidates(previous, current):
    """Only one-to-one candidate keys qualify; report every ambiguity explicitly."""
    shared = set(previous) & set(current)
    matches = []
    ambiguous = []
    for key in sorted(shared):
        before, after = previous[key], current[key]
        digest = hashlib.sha256(key.encode()).hexdigest()[:16]
        if len(before) != 1 or len(after) != 1:
            ambiguous.append(dict(key=digest, previous_count=len(before), current_count=len(after)))
            continue
        a, b = before[0], after[0]
        pa, pb = origin_pixel(a), origin_pixel(b)
        matches.append(dict(key=digest, previous_draw=a['draw']['index'], current_draw=b['draw']['index'],
                            world_unchanged=matrix_key(a['world']) == matrix_key(b['world']),
                            world_max_element_delta=error(a['world'], b['world']),
                            world_translation_delta=[b['world'][i][3]-a['world'][i][3] for i in range(3)],
                            world_linear_max_delta=max(abs(b['world'][i][j]-a['world'][i][j]) for i in range(3) for j in range(3)),
                            wvp_max_element_delta=error(a['wvp'], b['wvp']) if a['wvp'] is not None and b['wvp'] is not None else None,
                            predicted_origin_pixel_delta=[pb[i]-pa[i] for i in range(2)] if pa is not None and pb is not None else None,
                            depth_enabled=a['draw']['states'].get('7')))
    return dict(shared_candidate_keys=len(shared), ambiguous_shared_keys=ambiguous, unique_matches=matches,
                previous_only_keys=len(set(previous)-shared), current_only_keys=len(set(current)-shared),
                unchanged_world_matches=sum(m['world_unchanged'] for m in matches),
                changed_world_matches=sum(not m['world_unchanged'] for m in matches),
                changed_draw_index_matches=sum(m['previous_draw'] != m['current_draw'] for m in matches))


def analyze_motion(trace, metadata):
    captures = parse_capture(trace)
    summaries = summarize(trace, {})['frames']
    # Older summarizers omit topology. Read the actual header rather than infer it.
    headers = {}
    for line in trace.splitlines():
        if line.startswith('draw '):
            f = fields(line)
            frame = f"{f['device']}:{f['frame']}" if 'device' in f else f['frame']
            headers[(frame, int(f['index']))] = f
    frames, candidates, cameras = {}, {}, {}
    for key, frame in captures.items():
        report = dict(complete=frame['complete'], total_draws=len(frame['draws']),
                      accepted_camera_draws=0, rejected=Counter(), candidate_key_count=0,
                      ambiguous_keys_in_frame=0)
        frames[key] = report
        candidates[key] = defaultdict(list)
        if not frame['complete']:
            continue
        summary_draws = {d['index']: d for d in summaries[key]['draws']}
        camera_values = {}
        for draw in frame['draws']:
            try:
                item = factor_draw(draw, metadata)
            except ValueError as exc:
                report['rejected'][str(exc)] += 1
                continue
            if item is None:
                report['rejected']['no_named_factorization_inputs'] += 1
                continue
            report['accepted_camera_draws'] += 1
            camera_values[matrix_key(item['camera'])] = item['camera']
            summary_draw = dict(summary_draws[draw['index']], topology=headers[(key, draw['index'])]['topology'])
            candidate = candidate_key(summary_draw)
            if candidate is None or item['world'] is None or item['wvp'] is None:
                report['rejected']['no_buffered_wvp_candidate_key'] += 1
                continue
            candidates[key][candidate].append(item)
        report['candidate_key_count'] = len(candidates[key])
        report['ambiguous_keys_in_frame'] = sum(len(v) > 1 for v in candidates[key].values())
        if camera_values:
            # Diagnostic selector only: no runtime pass classification is implied.
            cameras[key] = max(camera_values.values(), key=lambda c: sum(c[i][3]**2 for i in range(3)))
            report['largest_translation_camera'] = cameras[key]
    transitions = {}
    keys = list(frames)
    for a, b in zip(keys, keys[1:]):
        a_parts, b_parts = a.split(':'), b.split(':')
        if a_parts[:-1] != b_parts[:-1] or int(b_parts[-1])-int(a_parts[-1]) != 1:
            continue
        if not frames[a]['complete'] or not frames[b]['complete']:
            continue
        transitions[a+'->'+b] = compare_candidates(candidates[a], candidates[b])
        if a in cameras and b in cameras:
            transitions[a+'->'+b]['largest_translation_camera_delta'] = camera_delta(cameras[a], cameras[b])
    return dict(frames=frames, transitions=transitions,
                limits=['Keys are candidate resource/range/material correspondences, not engine object identities.',
                        'Duplicate keys are excluded; mutable vertex contents and shader animation are not captured.',
                        'Projected local origins are synthetic points, not observed vertices or image-space tracking.',
                        'Largest-translation camera selection is diagnostic and is not a scene-pass classifier.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    raw = args.trace.read_bytes()
    result = analyze_motion(raw.decode(), json.loads(args.metadata.read_text()))
    result['source'] = dict(trace=args.trace.name, sha256=hashlib.sha256(raw).hexdigest(),
                          metadata=args.metadata.name, metadata_sha256=hashlib.sha256(args.metadata.read_bytes()).hexdigest())
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    print(f'Analyzed {len(result["transitions"])} consecutive transitions -> {args.output}')


if __name__ == '__main__':
    main()
