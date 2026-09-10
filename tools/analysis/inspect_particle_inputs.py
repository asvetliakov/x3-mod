#!/usr/bin/env python3
"""Derive bounded particle-input evidence without retaining vertex payloads.

Draw results carry their own device/frame/index scope and are attached by that
key, independently of intervening capture events. Only begun capture=1 frames
with matching observed/reported draw counts count as complete captures.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re

PARTICLE = '36f98d151fd6b0c6'
ALTERNATE = '2eea471bc86935f2'
WANTED = {PARTICLE, ALTERNATE, '1279d081455f5815', '6059306306203243',
          'cbbf26102694c961', 'f36fc43f30b19d71', '5e484a06672e28fb'}
RECORDS = {'object_context', 'geometry', 'stream', 'vertex_buffer', 'index_buffer',
           'buffer_content', 'vertex_element', 'draw_args', 'state', 'surface',
           'constants', 'viewport', 'texture', 'texture_desc'}


def scope(fields, draw=False):
    names = ('device', 'frame', 'index') if draw else ('device', 'frame')
    return tuple(fields[name] for name in names)


def extract(lines):
    rows, frames, selected = [], {}, {}
    current = None
    telemetry_ends = 0
    for raw in lines:
        line = raw.strip()
        event = line.partition(' ')[0]
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
        if event == 'frame_begin':
            key = scope(fields)
            if key in frames:
                raise ValueError('Duplicate frame scope: ' + str(key))
            frames[key] = {'observed_draws': 0, 'complete': False, 'ended': False}
            current = None
        elif event == 'draw':
            current = None
            frame = frames.get(scope(fields))
            if frame is None or frame['ended']:
                continue
            frame['observed_draws'] += 1
            if fields['vs'] in WANTED:
                key = scope(fields, True)
                if key in selected:
                    raise ValueError('Duplicate draw scope: ' + str(key))
                current = {'draw': fields, 'records': defaultdict(list)}
                rows.append(current)
                selected[key] = current
        elif event == 'draw_result':
            # Never compare the full scoped dictionary to a result-only dict.
            row = selected.get(scope(fields, True))
            if row is not None:
                row['records']['draw_result'].append(fields)
        elif event == 'frame_end':
            current = None
            if fields.get('capture') != '1':
                telemetry_ends += 1
                continue
            frame = frames.get(scope(fields))
            if frame is not None:
                if frame['ended']:
                    raise ValueError('Duplicate captured frame end')
                frame['ended'] = True
                frame['present_result'] = fields['present']
                frame['complete'] = frame['observed_draws'] == int(fields['draws'])
        elif event == 'capture_event':
            current = None
        elif current is not None:
            keep_constant = (event == 'constant' and fields.get('type') == 'f'
                             and ((fields.get('kind') == 'vs' and int(fields['reg']) < 8)
                                  or (fields.get('kind') == 'ps' and fields['reg'] == '0')))
            if event in RECORDS or keep_constant:
                current['records'][event].append(fields)
    return {'rows': rows, 'frames': frames, 'telemetry_frame_end_count': telemetry_ends}


def draw_succeeded(row):
    results = row['records'].get('draw_result', [])
    return len(results) == 1 and results[0].get('result') == '00000000'


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1048576), b''):
            digest.update(block)
    return digest.hexdigest()


def report(data, source, inventory):
    rows = [row for row in data['rows'] if row['draw']['vs'] == PARTICLE]
    if not rows:
        raise ValueError('No captured particle draws')
    first = rows[0]['records']
    stream = next(item for item in first['stream'] if item['slot'] == '0')
    result = {
        'schema': 2,
        'scope': 'Derived input-availability review, no vertex bytes or motion correspondence captured',
        'source': {'name': source.name, 'bytes': source.stat().st_size, 'sha256': sha256(source)},
        'extractor_sha256': sha256(Path(__file__)),
        'particle_vs': PARTICLE, 'particle_ps': '222bee0defcb1852',
        'archive_alternate_vs': ALTERNATE,
        'archive_alternate_captured_draws': sum(row['draw']['vs'] == ALTERNATE for row in data['rows']),
        'draw_count': len(rows),
        'complete_frame_count': sum(frame['complete'] for frame in data['frames'].values()),
        'captured_frame_begin_count': len(data['frames']),
        'ignored_telemetry_frame_end_count': data['telemetry_frame_end_count'],
        'all_draws_succeeded': all(draw_succeeded(row) for row in rows),
        'all_particle_frames_complete': all(data['frames'][scope(row['draw'])]['complete'] for row in rows),
        'declaration': first['vertex_element'], 'stream': stream,
        'vertex_buffer': first['vertex_buffer'][0],
        'bound_but_unused_index_buffer': first['index_buffer'][0],
        'render_states': {item['id']: int(item['value']) for item in first['state']},
        'all_particle_draw_states_equal': all(row['records']['state'] == first['state'] for row in rows),
        'all_unscoped': all(row['records']['object_context'][0]['scoped'] == '0'
                            and row['records']['object_context'][0]['valid'] == '0' for row in rows),
        'particle_rows': [],
        'related_vs_draw_counts': dict(Counter(row['draw']['vs'] for row in data['rows'])),
        'matrix_digest_encoding': 'SHA256 of four captured row bit strings joined with ASCII comma; not a binary float digest',
    }
    vertices = []
    for row in rows:
        draw, records = row['draw'], row['records']
        vertex = next(item for item in records['buffer_content'] if item['kind'] == 'vertex')
        vertices.append(vertex)
        constants = {int(item['reg']): item['bits'] for item in records['constant']
                     if item['kind'] == 'vs' and item['type'] == 'f'}
        def matrix_digest(start):
            return hashlib.sha256(','.join(constants[i] for i in range(start, start + 4)).encode()).hexdigest()
        if draw['kind'] != 'primitive' or draw['topology'] != '4':
            raise ValueError('Particle draw is not a non-indexed triangle list')
        result['particle_rows'].append({
            'device': int(draw['device']), 'frame': int(draw['frame']), 'draw': int(draw['index']),
            'draw_result': records.get('draw_result', []),
            'topology': int(draw['topology']), 'start_vertex': int(records['draw_args'][0]['start_vertex']),
            'primitive_count': int(draw['primitives']), 'vertex_count': 3 * int(draw['primitives']),
            'consumed_bytes': 3 * int(draw['primitives']) * int(stream['stride']),
            'vertex_revision': int(vertex['revision']), 'last_lock_flags': vertex['flags'],
            'view_rows_sha256': matrix_digest(0), 'projection_rows_sha256': matrix_digest(4)})
    pairs = [(a, b) for a, b in zip(result['particle_rows'], result['particle_rows'][1:])
             if a['device'] == b['device'] and b['frame'] == a['frame'] + 1]
    result.update({
        'view_variants': len({row['view_rows_sha256'] for row in result['particle_rows']}),
        'projection_variants': len({row['projection_rows_sha256'] for row in result['particle_rows']}),
        'adjacent_capture_pairs': len(pairs),
        'adjacent_revision_increments_one': all(b['vertex_revision'] == a['vertex_revision'] + 1 for a, b in pairs),
        'all_vertex_revisions_known': all(item['known'] == '1' for item in vertices),
        'all_buffer_statuses_successful_unambiguous_no_pending': all(
            item['result'] == item['status'] == '00000000' and item['requested'] == item['known'] == '1'
            and item['ambiguous'] == item['pending'] == '0' for item in vertices),
        'all_particle_layout_stream_and_buffer_descriptions_equal': all(
            row['records']['vertex_element'] == first['vertex_element']
            and row['records']['vertex_buffer'] == first['vertex_buffer']
            and next(item for item in row['records']['stream'] if item['slot'] == '0') == stream for row in rows),
        'all_particle_constant_queries_successful': all(
            bool(row['records']['constants']) and all(item['result'] == '00000000'
                for item in row['records']['constants']) for row in rows),
        'shader_provenance': [{key: item[key] for key in ('id', 'sha256', 'disassembly_sha256')}
            for item in inventory['programs'] if item['id'] in
            {'vs_' + PARTICLE, 'vs_' + ALTERNATE, 'ps_222bee0defcb1852'}],
    })
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with args.trace.open() as source:
        data = extract(source)
    result = report(data, args.trace, json.loads(args.inventory.read_text()))
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: result[key] for key in ('draw_count', 'complete_frame_count',
                      'ignored_telemetry_frame_end_count', 'all_draws_succeeded')}))


if __name__ == '__main__':
    main()
