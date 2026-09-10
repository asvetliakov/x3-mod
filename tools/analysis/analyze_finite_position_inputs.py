#!/usr/bin/env python3
"""Read-only sizing audit for exact archive SM3 row-dot position candidates.

Retains buffer metadata, never payload. It does not establish finite values,
initial upload ranges, GPU replay eligibility, peak memory or scan timing.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
FIELDS = re.compile(r'(\w+)=([^\s]+)')


def coordinate(fields):
    return tuple(fields.get(k) for k in ('device', 'frame', 'index'))


def succeeded(result):
    return isinstance(result, str) and bool(re.fullmatch('[0-9a-fA-F]{8}', result)) and int(result, 16) < 0x80000000


def audit(lines, profiles):
    selected = {p['fnv1a64'] for p in profiles['vertices'] if
                p['position_path'] == 'homogeneous_row_dots' and p.get('shader_version') == 0xfffe0300}
    frames, records, current = {}, [], None
    for line in lines:
        event = line.partition(' ')[0]
        if event not in {'draw', 'frame_begin', 'frame_end', 'capture_event', 'vertex_buffer',
                         'index_buffer', 'stream', 'draw_args', 'motion_input', 'draw_result',
                         'buffer_content', 'vertex_element'}:
            continue
        fields = dict(FIELDS.findall(line))
        frame_key = tuple(fields.get(k) for k in ('device', 'frame'))
        if event in ('draw', 'frame_begin', 'frame_end', 'capture_event'):
            current = None
        if event == 'frame_begin':
            if frame_key in frames or None in frame_key or len(frames) >= 1024:
                raise ValueError('Duplicate/malformed frame or frame bound exceeded')
            frames[frame_key] = dict(indices=set(), complete=False, ended=False)
        elif event == 'draw':
            if frame_key not in frames:
                raise ValueError('Draw without captured frame begin')
            frame = frames[frame_key]
            index = fields.get('index')
            if frame['ended'] or index is None or index in frame['indices']:
                raise ValueError('Duplicate/malformed/late draw')
            frame['indices'].add(index)
            if sum(len(f['indices']) for f in frames.values()) > 100000:
                raise ValueError('Draw bound exceeded')
            if fields.get('vs') in selected:
                current = dict(draw=fields, elements=[])
                records.append(current)
        elif event == 'frame_end':
            if frame_key in frames:
                frame = frames[frame_key]
                if frame['ended']:
                    raise ValueError('Duplicate captured frame end')
                frame['ended'] = True
                frame['complete'] = (fields.get('capture') == '1' and succeeded(fields.get('present')) and
                                     int(fields.get('draws', '-1')) == len(frame['indices']))
        elif current is not None:
            if event in ('vertex_buffer', 'index_buffer', 'stream', 'draw_args', 'motion_input', 'draw_result'):
                if event == 'stream' and fields.get('slot') != '0':
                    continue
                if event in ('motion_input', 'draw_result') and coordinate(fields) != coordinate(current['draw']):
                    raise ValueError('Diagnostic coordinate mismatch')
                if event in current:
                    raise ValueError('Duplicate candidate metadata')
                current[event] = fields
            elif event == 'buffer_content':
                name = fields.get('kind', '') + '_content'
                if name in current:
                    raise ValueError('Duplicate buffer content metadata')
                current[name] = fields
            elif event == 'vertex_element':
                current['elements'].append(fields)
    combinations, extra, flags_vb, flags_ib = Counter(), Counter(), Counter(), Counter()
    vbs, ibs, ranges = {}, {}, {}
    revisions, vb_frames = defaultdict(set), defaultdict(set)
    successful_profiles = set()
    candidate_count = 0
    for record in records:
        draw = record['draw']
        frame_key = tuple(draw[k] for k in ('device', 'frame'))
        if not frames[frame_key]['complete']:
            extra['excluded_incomplete_frame_draws'] += 1
            continue
        if not succeeded(record.get('draw_result', {}).get('result')):
            extra['excluded_failed_or_missing_result'] += 1
            continue
        candidate_count += 1
        successful_profiles.add(draw['vs'])
        if draw['kind'] != 'indexed':
            extra['excluded_nonindexed_candidates'] += 1
            continue
        vb, ib, stream, motion, args, vc, ic = (record[name] for name in
            ('vertex_buffer', 'index_buffer', 'stream', 'motion_input', 'draw_args', 'vertex_content', 'index_content'))
        if not (vb['identity'] == vc['identity'] == stream['identity'] == motion['vb'] and
                ib['identity'] == ic['identity'] == motion['ib']):
            raise ValueError('Buffer metadata identity mismatch')
        positions = [e for e in record['elements'] if e.get('stream') != '255' and e.get('usage') == '0' and e.get('index') == '0']
        if len(positions) != 1 or positions[0].get('stream') != '0' or positions[0].get('method') != '0':
            raise ValueError('Unreviewed POSITION input layout')
        position = positions[0]
        if (position['type'] != motion['position_type'] or position['offset'] != motion['position_offset'] or
                position['type'] not in ('2', '16')):
            raise ValueError('POSITION metadata mismatch or unsupported type')
        key = (vb['usage'], vb['pool'], ib['usage'], ib['pool'], stream['stride'], position['type'], draw['kind'])
        combinations[key] += 1
        # Allocation identities are process-wide, but device is retained explicitly.
        vk, ik = (draw['device'], vb['identity']), (draw['device'], ib['identity'])
        if (vk in vbs and vbs[vk] != vb) or (ik in ibs and ibs[ik] != ib):
            raise ValueError('Allocation descriptor changed')
        vbs[vk], ibs[ik] = vb, ib
        revisions[vk].add(vc['revision']); vb_frames[vk].add(frame_key)
        first = int(args['base_vertex']) + int(args['min_vertex'])
        count = int(args['num_vertices'])
        range_key = (vk, vc['revision'], stream['offset'], stream['stride'], position['offset'], position['type'], first, count)
        ranges[range_key] = count * (6 if position['type'] == '16' else 12)
        extra['entire_vb_declared'] += (int(args['base_vertex']) == 0 and int(args['min_vertex']) == 0 and
            count * int(stream['stride']) == int(vb['bytes']) and stream['offset'] == '0')
        if draw['topology'] not in ('4', '5') or ib['format'] not in ('101', '102'):
            raise ValueError('Unreviewed index topology/format')
        consumed = int(draw['primitives']) * 3 if draw['topology'] == '4' else int(draw['primitives']) + 2
        extra['entire_ib_drawn'] += int(args['start_index']) == 0 and consumed * (2 if ib['format'] == '101' else 4) == int(ib['bytes'])
        extra['vb_revision_one'] += vc['revision'] == '1'; extra['ib_revision_one'] += ic['revision'] == '1'
        extra['fvf_zero'] += vb['fvf'] == '0'
        extra['zero_offsets_unit_frequency'] += (stream['offset'] == position['offset'] == '0' and stream['frequency'] == '1')
        extra['known_unambiguous_nonpending'] += all(c['known'] == '1' and c['ambiguous'] == '0' and c['pending'] == '0' for c in (vc, ic))
        flags_vb[vc['flags']] += 1; flags_ib[ic['flags']] += 1
    return dict(profile_filter='Reviewed homogeneous_row_dots and shader_version == 0xfffe0300',
        selected_archive_profiles=len(selected), captured_successful_profiles=len(successful_profiles),
        successful_candidate_draws=candidate_count, successful_indexed_candidates=sum(combinations.values()),
        successful_nonindexed_candidates=candidate_count-sum(combinations.values()),
        complete_capture_frames=sum(frame['complete'] for frame in frames.values()),
        descriptor_combinations=[dict(vb_usage=k[0], vb_pool=k[1], ib_usage=k[2], ib_pool=k[3], stride=k[4],
                                     position_type=k[5], method=k[6], draws=n) for k, n in sorted(combinations.items())],
        extra_counts=dict(extra), unique_vbs=len(vbs), unique_ibs=len(ibs),
        sum_historical_vb_allocation_bytes=sum(int(v['bytes']) for v in vbs.values()),
        sum_historical_ib_allocation_bytes=sum(int(v['bytes']) for v in ibs.values()),
        vbs_observed_in_multiple_frames=sum(len(value) > 1 for value in vb_frames.values()),
        vb_revision_count_distribution=dict(Counter(len(value) for value in revisions.values())),
        unique_declared_ranges=len(ranges), xyz_bytes_in_unique_declared_ranges=sum(ranges.values()),
        vb_last_lock_flags=dict(flags_vb), ib_last_lock_flags=dict(flags_ib))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('--expected-sha256', required=True)
    parser.add_argument('--registry', type=Path, default=ROOT / 'verification/results/shader-profile-registry.json')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    profile_bytes = args.registry.read_bytes()
    analyzer_bytes = Path(__file__).read_bytes()
    before, digest = args.snapshot.stat(), hashlib.sha256()
    with args.snapshot.open('rb') as source:
        def lines():
            for raw in source:
                digest.update(raw)
                yield raw.decode('utf-8', errors='strict')
        report = audit(lines(), json.loads(profile_bytes))
    after = args.snapshot.stat()
    if ((before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns) or
            digest.hexdigest() != args.expected_sha256 or args.registry.read_bytes() != profile_bytes or
            Path(__file__).read_bytes() != analyzer_bytes):
        raise ValueError('Snapshot or profile registry changed/mismatched')
    report['provenance'] = dict(snapshot=str(args.snapshot), snapshot_bytes=after.st_size, snapshot_sha256=digest.hexdigest(),
        registry=str(args.registry), registry_sha256=hashlib.sha256(profile_bytes).hexdigest(),
        analyzer_sha256=hashlib.sha256(analyzer_bytes).hexdigest())
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('successful_indexed_candidates', 'unique_vbs', 'sum_historical_vb_allocation_bytes')}))


if __name__ == '__main__':
    main()
