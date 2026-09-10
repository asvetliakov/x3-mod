#!/usr/bin/env python3
"""Bounded camera/object/buffer evidence from 0.4 diagnostics; no lifetime claims.

Uses the summary parser's raw diagnostic records. Matrix hypotheses are compared
numerically with named shader inputs; cross-frame keys are only adjacent-frame
candidates, never identities across gaps/reloads. Raw captures stay untracked.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import struct
from analyze_camera import factor_draw, inverse, multiply, normalized_error, error
from analyze_motion import camera_delta
from analyze_pass_boundaries import inspect_frame
from summarize_capture import summarize, fields

KEEP = ('frame_begin ', 'frame_end ', 'draw ', 'draw_result ', 'capture_event ',
        'clear ', 'clear_viewport ', 'set_rt ', 'set_depth ', 'stretch_rect ', 'surface ',
        'object_', 'buffer_content ', 'geometry ', 'stream ', 'indices ', 'vertex_buffer ',
        'index_buffer ', 'draw_args ', 'vertex_element ', 'viewport ', 'texture ',
        'state id=7 ', 'state id=14 ', 'constants kind=vs ', 'constant kind=vs type=f ')


def values(bits, integer=False):
    return [struct.unpack('<i' if integer else '<f', struct.pack('<I', int(x, 16)))[0]
            for x in bits.split(',')]


def transpose(matrix):
    return [list(row) for row in zip(*matrix)]


def matrices(draw):
    rows = defaultdict(dict)
    invalid = set()
    for item in draw.get('object_matrix', []):
        if item['role'] not in ('world', 'world_basis', 'view', 'projection'):
            continue  # Scale is fixed-point/integer storage, not IEEE float rows.
        role = item['role']
        try:
            row, components = int(item['row']), values(item['bits'])
            if row not in range(4) or row in rows[role] or len(components) != 4 or not all(map(math.isfinite, components)):
                invalid.add(role)
            rows[role][row] = components
        except (KeyError, ValueError, struct.error):
            invalid.add(role)
    return {role: [v[r] for r in range(4)] for role, v in rows.items()
            if role not in invalid and set(v) == set(range(4))}


def shader_factor(draw, metadata):
    return factor_draw(dict(index=draw['index'], vs=draw['vs'],
        constants={int(k): values(v) for k, v in draw.get('constants', {}).get('vs', {}).get('f', {}).items()},
        v2=True, float_status=draw.get('constant_status', {}).get('vs', {}).get('f'), unavailable=False,
        draw_result=int(draw.get('draw_result', {}).get('result', '80004005'), 16)), metadata)


def metric(samples):
    return dict(observations=len(samples), maximum=max(samples, default=None),
                above_1e_5=sum(v > 1e-5 for v in samples))


def analyze(summary, metadata):
    report = dict(frames={}, adjacent_frames=[], burst_camera_changes=[], limits=[
        'Node/handle/model/camera tuples are adjacent-frame candidates, not lifetime proof.',
        'No correspondence is inferred across nonconsecutive captures or reloads.',
        'Equal buffer revisions cover observed wrapper writes, not arbitrary native writes.',
        'Double-precision matrix recomposition is a hypothesis test, not exact engine arithmetic.'])
    previous = None
    pointer_handles, handle_pointers = defaultdict(set), defaultdict(set)
    for key, frame in summary['frames'].items():
        if not frame['draws']: continue
        valid = (frame['complete'] and frame['draw_count_matches'] and frame.get('event_sequence_contiguous') and
                 frame.get('present_result') == '00000000' and all(d.get('draw_result', {}).get('result') == '00000000' for d in frame['draws']))
        out = dict(draws=len(frame['draws']), complete_successful=bool(valid))
        report['frames'][key] = out
        if not valid: previous = None; continue
        contexts = Counter(); nodes = defaultdict(list); buffers = defaultdict(list)
        buffer_status = Counter(); cameras = defaultdict(list); errors = defaultdict(list)
        main = Counter(); unavailable = Counter(); unscoped = []
        for draw in frame['draws']:
            c = draw.get('object_context', {})
            contexts[(c.get('scoped', 'missing'), c.get('valid', 'missing'), str(draw.get('object_context_matches_draw')))] += 1
            for b in draw.get('buffer_content', []):
                status = tuple(b.get(k, 'missing') for k in ('result', 'status', 'requested', 'known', 'ambiguous', 'pending'))
                buffer_status[status] += 1
                if status == ('00000000', '00000000', '1', '1', '0', '0'):
                    buffers[(b['kind'], b['identity'])].append(int(b['revision']))
            if c.get('scoped') != '1':
                unscoped.append(dict(draw=draw['index'], vs=draw['vs'], ps=draw['ps'],
                    z_enable=draw['states'].get('7'), z_write=draw['states'].get('14'),
                    buffers=[{k: b[k] for k in ('kind', 'identity', 'revision')} for b in draw.get('buffer_content', [])]))
                continue
            if not draw.get('object_context_matches_draw') or c.get('valid') != '127':
                unavailable['object_status'] += 1; continue
            m = matrices(draw)
            if any(len(m.get(role, [])) != 4 for role in ('world', 'world_basis', 'view', 'projection')):
                unavailable['matrix_rows'] += 1; continue
            if not all(math.isfinite(v) for role in m.values() for row in role for v in row):
                unavailable['nonfinite'] += 1; continue
            pointer_handles[c['node']].add(c['node_handle'])
            handle_pointers[c['node_handle']].add(c['node'])
            node = tuple(c.get(k) for k in ('session', 'node', 'node_handle', 'camera', 'camera_handle', 'model', 'lod'))
            world_bits = tuple(r['bits'] for r in draw['object_matrix'] if r['role'] == 'world')
            nodes[node].append((world_bits, draw['index']))
            camera = (c['session'], c['camera'], c['camera_handle'])
            cameras[camera].append(m['view'])
            main[camera] += 1
            if not int(c['flags130'], 16) & 0x200:
                basis = [values(r['bits'], True) for r in draw.get('object_basis', [])]
                if len(basis) == 3 and all(len(row) == 3 for row in basis):
                    errors['ordinary_node_basis_fixed16_absolute'].append(max(
                        abs(basis[r][col] / 65536 - m['world_basis'][r][col]) for r in range(3) for col in range(3)))
            try: item = shader_factor(draw, metadata)
            except ValueError as exc:
                unavailable[str(exc)] += 1; continue
            if item is None: unavailable['no_named_factor'] += 1; continue
            if item['world'] is not None:
                errors['engine_world_transpose_vs_shader_relative'].append(normalized_error(transpose(m['world']), item['world']))
                errors['wrong_untransposed_world_relative'].append(normalized_error(m['world'], item['world']))
            errors['engine_view_transpose_vs_inverse_shader_camera_relative'].append(normalized_error(transpose(m['view']), inverse(item['camera'])))
            if item['wvp'] is not None:
                predicted = transpose(multiply(multiply(m['world'], m['view']), m['projection']))
                errors['transpose_engine_WVP_vs_shader_relative'].append(normalized_error(predicted, item['wvp']))
                errors['transpose_engine_WVP_vs_shader_absolute'].append(error(predicted, item['wvp']))
        camera_key = main.most_common(1)[0][0] if main else None
        camera_matrix = cameras[camera_key][0] if camera_key else None
        out.update(context_status=[dict(scoped=k[0], valid=k[1], coordinates_match=k[2], draws=n) for k, n in contexts.items()],
            node_candidates=len(nodes), conflicting_world_candidates=sum(len({x[0] for x in v}) != 1 for v in nodes.values()),
            buffer_status=[dict(result=k[0], status=k[1], requested=k[2], known=k[3], ambiguous=k[4], pending=k[5], observations=n) for k,n in buffer_status.items()],
            buffer_allocations=len(buffers), buffers_changing_within_frame=sum(len(set(v))>1 for v in buffers.values()),
            matrix_checks={k: metric(v) for k,v in errors.items()}, unavailable=dict(unavailable), unscoped_draws=unscoped,
            cameras=[dict(session=k[0], pointer=k[1], handle=k[2], draws=len(v),
                          unique_view_matrices=len({tuple(x for row in matrix for x in row) for matrix in v})) for k,v in cameras.items()],
            dominant_camera=dict(pointer=camera_key[1],handle=camera_key[2],view_rows=camera_matrix) if camera_key else None)
        rigid = [d for d in frame['draws'] if d.get('object_context_matches_draw') and
                 d.get('object_context', {}).get('scoped') == '1' and d['object_context'].get('valid') == '127' and
                 (d['object_context']['session'],d['object_context']['camera'],d['object_context']['camera_handle']) == camera_key and
                 not int(d['object_context']['flags130'],16) & 0x200 and d['states'].get('7') == 1 and d['states'].get('14') == 1]
        out['ordinary_dominant_camera_depth_writers'] = dict(draws=len(rigid),
            node_pointer_handle_pairs=len({(d['object_context']['node'],d['object_context']['node_handle']) for d in rigid}))
        boundaries = inspect_frame(frame)
        out['pass_observations'] = dict(initial_clear_flags=boundaries['clears'][0]['flags'],
            background_draws_before_depth_clear=boundaries['clears'][1]['after_draw'],
            background_pairs=sorted({(d['vs'],d['ps']) for d in frame['draws'][:boundaries['clears'][1]['after_draw']]}),
            bloom_chains=[dict(draws=b['draw_indices'], next_depth_clear=b['next_depth_clear']['after_draw']) for b in boundaries['bloom_chains']])
        if previous:
            pkey, pn, pb, pk, pm = previous
            if key.split(':')[0] == pkey.split(':')[0] and int(key.split(':')[1]) == int(pkey.split(':')[1])+1:
                shared = nodes.keys() & pn.keys()
                unambiguous = [k for k in shared if len({x[0] for x in nodes[k]}) == len({x[0] for x in pn[k]}) == 1]
                changed = [k for k in unambiguous if nodes[k][0][0] != pn[k][0][0]]
                comparison = dict(previous=pkey,current=key,shared_node_candidates=len(shared),
                    unambiguous_world_candidates=len(unambiguous),changed_world_candidates=len(changed),
                    newly_seen_candidates=len(nodes.keys()-pn.keys()),missing_candidates=len(pn.keys()-nodes.keys()),
                    changed_first_draw_index=sum(nodes[k][0][1]!=pn[k][0][1] for k in unambiguous),
                    shared_buffer_allocations=len(buffers.keys() & pb.keys()),
                    changed_buffer_revisions=[dict(kind=k[0],identity=k[1],previous_last=pb[k][-1],current_first=buffers[k][0])
                        for k in sorted(buffers.keys() & pb.keys()) if pb[k][-1]!=buffers[k][0]])
                if camera_key is not None and pk == camera_key:
                    comparison['dominant_camera_delta']=camera_delta(inverse(transpose(pm)),inverse(transpose(camera_matrix)))
                report['adjacent_frames'].append(comparison)
            elif camera_key is not None and pk == camera_key:
                report['burst_camera_changes'].append(dict(previous=pkey,current=key,
                    interpretation='Same observed camera pointer/handle across gap; no lifetime inference',
                    delta=camera_delta(inverse(transpose(pm)),inverse(transpose(camera_matrix)))))
        previous = key,nodes,buffers,camera_key,camera_matrix
    report['observed_handle_collisions'] = dict(
        unique_node_pointers=len(pointer_handles), unique_node_handles=len(handle_pointers),
        pointers_with_multiple_handles=sum(len(v)>1 for v in pointer_handles.values()),
        handles_with_multiple_pointers=sum(len(v)>1 for v in handle_pointers.values()),
        interpretation='Absence of observed collisions is not a lifetime or reload guarantee.')
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('trace',type=Path)
    p.add_argument('--metadata',nargs='+',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();metadata={}
    for path in args.metadata:metadata.update(json.loads(path.read_text()))
    retained=[];scene={};current=None;draw=0;sequence=0;digest=hashlib.sha256()
    with args.trace.open('rb') as raw:
        for data in raw:
            digest.update(data);line=data.decode('utf-8')
            if line.startswith(KEEP):retained.append(line)
            if line.startswith('frame_begin '):
                f=fields(line);current=f"{f['device']}:{f['frame']}";draw=0;sequence=0
                scene[current]=dict(unsupported=[],ends=[])
            elif line.startswith('draw '):draw=int(fields(line)['index'])
            elif line.startswith('capture_event '):sequence=int(fields(line)['seq'])
            elif current and line.startswith('scene_depth_unsupported '):
                scene[current]['unsupported'].append(dict(fields(line),after_draw=draw,preceding_capture_sequence=sequence))
            elif current and line.startswith('scene_depth_frame phase=end '):scene[current]['ends'].append(fields(line))
            elif line.startswith('frame_end '):current=None
    report=analyze(summarize(''.join(retained),{},include_floats=True),metadata)
    report['scene_depth_diagnostics']=scene
    report['source']=dict(trace=args.trace.name,sha256=digest.hexdigest(),bytes=args.trace.stat().st_size)
    paths=[Path(__file__),Path(__file__).with_name('summarize_capture.py'),Path(__file__).with_name('analyze_camera.py'),
           Path(__file__).with_name('analyze_motion.py'),Path(__file__).with_name('analyze_pass_boundaries.py'),*args.metadata]
    root=Path(__file__).resolve().parents[2]
    report['sources_sha256']={str(path.resolve().relative_to(root)):hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
    args.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(f"Analyzed {len(report['frames'])} frames and {len(report['adjacent_frames'])} adjacent pairs -> {args.output}")

if __name__=='__main__':main()
