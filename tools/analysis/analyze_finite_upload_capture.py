#!/usr/bin/env python3
"""Audit finite-upload capture metadata without reading vertex/index payloads.

Input candidates combine recorded gates; they are not replay or TAA eligibility.
Cumulative owner counters are sampled, never summed across repeated batches.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

FIELD = re.compile(r'(\w+)=([^\s]+)')
DRAW_RECORDS = {'draw', 'draw_result', 'motion_input', 'motion_geometry', 'motion_lifetime', 'object_context'}
METRIC_RECORDS = {'finite_upload_metric', 'finite_upload_reason', 'finite_upload_first_refusal'}
COUNTERS = ('uploads', 'publications', 'invalidations', 'allocation_failures', 'scans', 'classified_bytes',
            'scan_ticks', 'queries', 'query_cache_hits', 'position_components')
OPTIONAL_COUNTERS = ('qualifier_ticks', 'query_ticks')
GAUGES = ('payload_bytes', 'peak_payload_bytes', 'sidecars', 'metadata_bytes', 'global_payload_bytes', 'global_sidecars')
BLOCKERS = {1: 'PositionProgram', 2: 'PixelCoverage', 4: 'PositionLayout', 8: 'BufferDescription',
            16: 'BufferRevision', 32: 'DrawRange', 64: 'RasterState', 128: 'TargetLayout',
            256: 'SubmittedRows', 512: 'ObjectScope', 1024: 'UserMemory', 2048: 'QueryFailure', 4096: 'SubmissionFailure'}
PROOFS = {1: 'LifetimeVerified', 2: 'GeometryUnchanged', 4: 'PositionReviewed', 8: 'CoverageSupported', 16: 'SubmissionSucceeded'}
REASONS = ('none', 'disabled', 'unrecognized', 'device_unavailable', 'tracking_unavailable', 'missing_allocation',
           'revision_mismatch', 'pending', 'ambiguous', 'native_contract', 'unsupported_write', 'thread_mismatch',
           'mapping_mismatch', 'unlock_failed', 'invalid_layout', 'invalid_range', 'unknown_cells', 'nonfinite',
           'index_unknown', 'allocation_failure', 'budget', 'metadata_tampered', 'process_vertices')


def integer(text, base=10, bits=64, positive=False, signed=False):
    pattern = r'(?:0x)?[0-9a-fA-F]+' if base == 16 else r'-?[0-9]+' if signed else r'[0-9]+'
    if not isinstance(text, str) or not re.fullmatch(pattern, text):
        return None
    value = int(text, base)
    low, high = (-(1 << (bits - 1)), 1 << (bits - 1)) if signed else (0, 1 << bits)
    return value if low <= value < high and (not positive or value > 0) else None


def ok_hresult(text):
    value = integer(text, 16, 32)
    return value is not None and value < 0x80000000


def exactly_ok(text):
    return integer(text, 16, 32) == 0


def coordinate(fields, draw=False):
    device, frame = integer(fields.get('device'), positive=True), integer(fields.get('frame'))
    index = integer(fields.get('index'), positive=True) if draw else None
    if device is None or frame is None or (draw and index is None):
        return None
    return (device, frame, index) if draw else (device, frame)


def unique(records):
    return records[0] if len(records) == 1 and not records[0]['errors'] else None


def numeric_fields(record, decimal=(), hexes=(), booleans=(), u32=(), hex32=()):
    if not record:
        return False
    f = record['fields']
    return (all(integer(f.get(name)) is not None for name in decimal) and
            all(integer(f.get(name), 16) is not None for name in hexes) and
            all(f.get(name) in ('0', '1') for name in booleans) and
            all(integer(f.get(name), bits=32) is not None for name in u32) and
            all(integer(f.get(name), 16, 32) is not None for name in hex32))


def reason_name(value):
    parsed = integer(value, bits=32)
    return REASONS[parsed] if parsed is not None and parsed < len(REASONS) else 'unknown_reason'


def inspect_draw(records, issues):
    get = lambda name: unique(records.get(name, []))
    draw, result, motion, geometry, context, lifetime, args = [get(name) for name in
        ('draw', 'draw_result', 'motion_input', 'motion_geometry', 'object_context', 'motion_lifetime', 'draw_args')]
    counts = Counter(draws=1)
    out = dict(counts=counts, source='unknown', finite='unknown', index_gate='unknown', blockers=None, proofs=None,
               finite_reason='missing', index_reason='missing', candidate=False)
    if issues or any(len(items) != 1 or items[0]['errors'] for items in records.values()) or not draw or not result or not ok_hresult(result['fields'].get('result')):
        counts['poisoned_draw'] += 1
        return out
    d, m, g = draw['fields'], motion['fields'] if motion else {}, geometry['fields'] if geometry else {}
    ordinary = d.get('kind') in ('primitive', 'indexed')
    a = args['fields'] if args else {}
    args_valid = bool(args and motion and draw['line'] < args['line'] < motion['line'])
    if d.get('kind') == 'primitive':
        args_valid = args_valid and integer(a.get('start_vertex'), bits=32) is not None
    elif d.get('kind') == 'indexed':
        args_valid = (args_valid and integer(a.get('base_vertex'), bits=32, signed=True) is not None and
                      all(integer(a.get(name), bits=32) is not None for name in ('min_vertex', 'num_vertices', 'start_index')))
    if ordinary and not args_valid:
        counts['poisoned_draw'] += 1
        return out
    draw_shape = (ordinary and integer(d.get('topology'), bits=32) in (4, 5) and
                  integer(d.get('primitives'), bits=32, positive=True) is not None)
    motion_valid = numeric_fields(
        motion, ('vb', 'vb_revision', 'ib', 'ib_revision', 'color', 'depth'),
        ('vs', 'ps', 'declaration', 'rows_hash'), ('lifetime_verified',),
        ('proofs', 'position_path', 'width', 'height', 'cull', 'position_offset', 'position_type'), ('blockers',))
    motion_valid = bool(motion_valid and draw['line'] < motion['line'] < result['line'] and
                        integer(m['vs'], 16) == integer(d.get('vs'), 16) and integer(m['ps'], 16) == integer(d.get('ps'), 16) and
                        integer(m['blockers'], 16, 32) is not None and integer(m['proofs'], bits=32) is not None and
                        ('vertex_finite_verified' not in m or m['vertex_finite_verified'] in ('0', '1')))
    if motion_valid:
        out['blockers'], out['proofs'] = integer(m['blockers'], 16, 32), integer(m['proofs'], bits=32)
        counts['finite_flag_' + m.get('vertex_finite_verified', 'missing')] += 1
    else:
        counts['motion_input_unknown'] += 1
    geometry_valid = numeric_fields(geometry,
        ('finite_generation', 'finite_revision', 'index_generation', 'index_revision'), ('source_hash',),
        ('source_qualified', 'finite_requested', 'index_required', 'index_requested', 'index_known', 'index_range_verified', 'index_exact'),
        ('source_words', 'finite_state', 'finite_reason', 'index_min', 'index_max', 'index_reason'), ('finite_status', 'index_status'))
    geometry_valid = bool(geometry_valid and motion_valid and motion['line'] < geometry['line'] < result['line'])
    if geometry_valid:
        out['finite_reason'], out['index_reason'] = reason_name(g['finite_reason']), reason_name(g['index_reason'])
        source_match = (integer(g['source_hash'], 16, positive=True) is not None and
                        integer(g['source_hash'], 16) == integer(m['vs'], 16) == integer(d.get('vs'), 16) and
                        integer(g['source_words'], bits=32, positive=True) is not None)
        if g['source_qualified'] == '0':
            out['source'] = 'unqualified'
        elif source_match:
            out['source'] = 'qualified'
        else:
            counts['source_association_invalid'] += 1
        indexed = d.get('kind') == 'indexed'
        method = d.get('kind') in ('indexed', 'primitive')
        index_consistent = method and g['index_required'] == str(int(indexed))
        if index_consistent and not indexed:
            out['index_gate'] = 'not_required_verified' if g['index_range_verified'] == '1' else 'not_required_unverified'
        elif index_consistent:
            a = args['fields'] if args else {}
            minimum, size, base = integer(a.get('min_vertex'), bits=32), integer(a.get('num_vertices'), bits=32, positive=True), integer(a.get('base_vertex'), bits=32, signed=True)
            low, high = integer(g['index_min'], bits=32), integer(g['index_max'], bits=32)
            bounds_known = (g['index_requested'] == g['index_known'] == '1' and exactly_ok(g['index_status']) and
                            integer(g['index_reason']) == 0 and integer(g['index_generation'], positive=True) is not None and
                            integer(g['index_revision'], positive=True) is not None and integer(g['index_revision']) == integer(m['ib_revision'], positive=True) and
                            integer(m['ib'], positive=True) is not None and low is not None and high is not None and low <= high)
            counts['index_bounds_known'] += bounds_known
            if bounds_known:
                counts['index_bounds_exact' if g['index_exact'] == '1' else 'index_bounds_conservative'] += 1
            range_fits = (args and draw['line'] < args['line'] < motion['line'] and
                          minimum is not None and size is not None and base is not None and low is not None and high is not None and
                          base + minimum >= 0 and minimum <= low <= high < minimum + size)
            if bounds_known and range_fits and g['index_range_verified'] == '1':
                out['index_gate'] = 'verified'
            elif g['index_range_verified'] == '1':
                out['index_gate'] = 'inconsistent'
            else:
                out['index_gate'] = 'unverified'
        else:
            counts['index_method_inconsistent'] += 1
        view_ready = (g['finite_requested'] == '1' and exactly_ok(g['finite_status']) and
                      integer(g['finite_generation'], positive=True) is not None and
                      integer(g['finite_revision'], positive=True) is not None and integer(g['finite_revision']) == integer(m['vb_revision'], positive=True) and
                      integer(m['vb'], positive=True) is not None and
                      out['index_gate'] in ('verified', 'not_required_verified') and
                      (not indexed or integer(g['finite_generation']) == integer(g['index_generation'])))
        state = integer(g['finite_state'], bits=32)
        if view_ready and state == 1 and integer(g['finite_reason']) == 0:
            out['finite'] = 'finite'
        elif view_ready and state == 2 and integer(g['finite_reason']) == 17:
            out['finite'] = 'nonfinite'
        elif state not in (0, 1, 2) or state in (1, 2):
            counts['finite_state_inconsistent'] += 1
    else:
        counts['motion_geometry_unknown'] += 1
    c, l = context['fields'] if context else {}, lifetime['fields'] if lifetime else {}
    valid = integer(c.get('valid'), bits=32)
    scope = bool(context and motion and draw['line'] < context['line'] < motion['line'] and c.get('scoped') == '1' and
                 valid is not None and valid & 7 == 7 and
                 all(integer(c.get(name), 16, 32, positive=True) is not None for name in ('node', 'camera', 'mesh', 'registry')) and
                 all(integer(c.get(name), bits=32, positive=True) is not None for name in ('node_handle', 'camera_handle', 'scope_depth')))
    life = bool(lifetime and motion and motion['line'] < lifetime['line'] < result['line'] and
                (not geometry or geometry['line'] < lifetime['line']) and
                integer(l.get('registry'), 16, 32, positive=True) == integer(c.get('registry'), 16, 32, positive=True) and
                l.get('before_known') == l.get('after_known') == '1' and l.get('before_reason') == l.get('after_reason') == '0' and
                all(integer(l.get(first), positive=True) is not None and integer(l.get(first)) == integer(l.get(last)) for first, last in
                    [('observer_epoch', 'observer_epoch_after'), ('load_epoch', 'load_epoch_after'), ('registry_epoch', 'registry_epoch_after'),
                     ('mutation_before', 'mutation_after'), ('node_serial', 'node_serial_after'), ('camera_serial', 'camera_serial_after')]))
    local = bool(draw_shape and args_valid and motion_valid and out['blockers'] == 0 and out['proofs'] == 31 and m.get('lifetime_verified') == '1' and scope and life and
                 integer(m['vs'], 16, positive=True) is not None and integer(m['ps'], 16, positive=True) is not None)
    counts['legacy_local_gates_and_lifetime'] += local
    out['candidate'] = local and out['source'] == 'qualified' and out['finite'] == 'finite' and m.get('vertex_finite_verified') == '1'
    counts['input_candidates'] += out['candidate']
    return out


def inspect_metrics(batches):
    latest_owner, latest_generation = {}, {}
    previous = {}
    anomalies = []
    for batch in batches:
        record = batch['metric']
        fields = record['fields']
        device, generation = integer(fields.get('device'), positive=True), integer(fields.get('generation'))
        valid = (not batch['errors'] and not record['errors'] and numeric_fields(record, COUNTERS + GAUGES + ('generation',), booleans=('requested', 'active'), hex32=('result', 'status')) and
                 integer(fields.get('frame')) is not None and device is not None and bool(fields.get('phase')) and
                 ok_hresult(fields.get('result')) and generation is not None and generation > 0)
        optional = {key: integer(fields.get(key)) if key in fields else None for key in OPTIONAL_COUNTERS}
        if any(key in fields and optional[key] is None for key in OPTIONAL_COUNTERS):
            valid = False
        values = {key: integer(fields.get(key)) for key in COUNTERS + GAUGES}
        values.update(optional)
        sample = dict(line=record['line'], frame=integer(fields.get('frame')), phase=fields.get('phase'), generation=generation,
                      known=bool(valid), result=fields.get('result'), status=fields.get('status'), requested=fields.get('requested'), active=fields.get('active'),
                      values=values if valid else None, emitted_reasons=batch['reasons'] if valid else None,
                      first_refusal=batch['first_refusal'] if valid else None,
                      errors=record['errors'] + batch['errors'] + ([] if valid else ['metric_snapshot_unavailable_or_malformed']), reasons_listing_exhaustive=False)
        if device is None:
            anomalies.append(dict(line=record['line'], issue='unattributed_metric'))
            continue
        old = previous.get(device)
        if valid and old:
            if generation is not None and old['generation'] is not None and generation < old['generation']:
                anomalies.append(dict(device=device, line=record['line'], issue='generation_regressed'))
            for key in COUNTERS + OPTIONAL_COUNTERS:
                if values[key] is not None and old['values'][key] is not None and values[key] < old['values'][key]:
                    anomalies.append(dict(device=device, line=record['line'], issue='cumulative_counter_regressed', counter=key))
        if valid:
            previous[device] = sample
        # The latest malformed batch stays unknown: never fall back to an older
        # positive sample and present it as the current owner state.
        latest_owner[str(device)] = sample
        latest_generation[f'{device}:{generation if generation is not None and generation > 0 else "unavailable"}'] = sample
    return dict(batches=len(batches), latest_per_owner=latest_owner, latest_observed_per_generation=latest_generation,
                anomalies=anomalies, counter_scope='cumulative owner lifetime, including earlier generations; snapshots are not added',
                gauges_scope='point-in-time reservations; global gauges repeated per owner are not additive',
                timing_scope='raw inclusive QPC scopes; qualifier/query may overlap; scan_ticks is not total observer overhead')


def analyze(lines, max_frames=2048, max_draws=200000, max_records=1000000):
    frames, active, batches = {}, {}, []
    current_draw = current_metric = None
    errors = Counter()
    headers = count = draw_count = 0
    frequency = set()
    def frame_for(key):
        if key not in frames:
            if len(frames) >= max_frames:
                raise ValueError('frame limit exceeded')
            frames[key] = dict(begin=[], end=[], draws={}, errors=[])
        return frames[key]
    for line_number, line in enumerate(lines, 1):
        event = line.partition(' ')[0].strip()
        if event not in DRAW_RECORDS | METRIC_RECORDS | {'frame_begin', 'frame_end', 'draw_args', 'x3-modern-renderer', 'telemetry_start'}:
            continue
        count += 1
        if count > max_records:
            raise ValueError('metadata record limit exceeded')
        pairs = FIELD.findall(line)
        fields = dict(pairs)
        record = dict(fields=fields, line=line_number, errors=[])
        if len(fields) != len(pairs):
            record['errors'].append('duplicate_scalar_field')
        if event == 'x3-modern-renderer':
            headers += 1
            if headers > 1:
                raise ValueError('multiple sessions require separate snapshots')
            continue
        if event == 'telemetry_start':
            value = integer(fields.get('qpc_frequency'), positive=True)
            if value is not None and not record['errors']:
                frequency.add(value)
            continue
        if event == 'finite_upload_metric':
            current_metric = dict(metric=record, reasons={}, first_refusal=None, errors=[])
            batches.append(current_metric)
            continue
        if event in ('finite_upload_reason', 'finite_upload_first_refusal'):
            parent = current_metric['metric']['fields'] if current_metric else {}
            same = current_metric is not None and all(fields.get(key) is not None and fields.get(key) == parent.get(key) for key in ('device', 'frame', 'phase'))
            if not same:
                errors['orphan_metric_detail'] += 1
                if current_metric:
                    current_metric['errors'].append('metric_detail_scope_mismatch')
                continue
            reason = integer(fields.get('reason'), bits=32)
            if record['errors'] or reason is None or not fields.get('name'):
                current_metric['errors'].append('malformed_metric_detail')
                continue
            if event == 'finite_upload_reason':
                value = integer(fields.get('count'))
                if value is None or str(reason) in current_metric['reasons']:
                    current_metric['errors'].append('duplicate_or_malformed_reason')
                else:
                    current_metric['reasons'][str(reason)] = dict(name=fields['name'], count=value)
            else:
                valid = numeric_fields(record, u32=('type', 'format', 'pool', 'size'), hex32=('usage', 'lock_flags'))
                if not valid or current_metric['first_refusal'] is not None:
                    current_metric['errors'].append('duplicate_or_malformed_first_refusal')
                else:
                    current_metric['first_refusal'] = fields
            continue
        current_metric = None
        if event == 'draw_args':
            if current_draw is None:
                errors['unattributed_draw_record'] += 1
            else:
                current_draw['records'].setdefault(event, []).append(record)
            continue
        key = coordinate(fields)
        if key is None:
            errors['unattributed_draw_record'] += 1
            continue
        if event == 'frame_end' and key not in frames and fields.get('capture') == '0':
            continue
        frame = frame_for(key)
        if event == 'frame_begin':
            frame['begin'].append(record)
            if key[0] in active:
                frames[active[key[0]]]['errors'].append('overlapping_frame')
                frame['errors'].append('overlapping_frame')
            active[key[0]] = key
            current_draw = None
        elif event == 'frame_end':
            frame['end'].append(record)
            if active.get(key[0]) != key:
                frame['errors'].append('out_of_scope_frame_end')
            else:
                del active[key[0]]
            current_draw = None
        else:
            where = coordinate(fields, draw=True)
            if where is None:
                frame['errors'].append('invalid_draw_coordinate')
                continue
            index = where[2]
            if index not in frame['draws']:
                draw_count += 1
                if draw_count > max_draws:
                    raise ValueError('draw limit exceeded')
                frame['draws'][index] = dict(records={}, errors=[], coordinate=where)
            draw = frame['draws'][index]
            draw['records'].setdefault(event, []).append(record)
            if active.get(key[0]) != key:
                frame['errors'].append('out_of_scope_draw_record')
            if event == 'draw':
                current_draw = draw
            elif current_draw is None or current_draw['coordinate'] != where:
                draw['errors'].append('diagnostic_outside_current_draw')
    totals, output, combinations = Counter(), [], Counter()
    for key, frame in frames.items():
        begin, end = unique(frame['begin']), unique(frame['end'])
        issues = list(frame['errors'])
        if not begin or not end:
            issues.append('missing_duplicate_or_malformed_frame_boundary')
        indices = list(frame['draws'])
        if indices != list(range(1, len(indices) + 1)):
            issues.append('draw_indices_not_contiguous')
        if end and (end['fields'].get('capture') != '1' or integer(end['fields'].get('draws')) != len(indices) or not ok_hresult(end['fields'].get('present'))):
            issues.append('frame_count_capture_or_present_invalid')
        for draw in frame['draws'].values():
            records = draw['records']
            d, r = unique(records.get('draw', [])), unique(records.get('draw_result', []))
            if not d or not r or not ok_hresult(r['fields'].get('result')) or d['line'] >= r['line']:
                issues.append('missing_duplicate_malformed_or_failed_draw_result')
            if begin and end and any(not begin['line'] < item['line'] < end['line'] for values in records.values() for item in values):
                issues.append('record_outside_frame')
            if d and r and any(not d['line'] < item['line'] < r['line'] for name, values in records.items() if name not in ('draw', 'draw_result') for item in values):
                draw['errors'].append('record_outside_draw_interval')
        if errors['unattributed_draw_record']:
            issues.append('unattributed_draw_record_in_snapshot')
        counts, masks, proofs, sources, finite, indices_gates, finite_reasons, index_reasons = [Counter() for _ in range(8)]
        valid = not issues
        if valid:
            for draw in frame['draws'].values():
                facts = inspect_draw(draw['records'], draw['errors'])
                counts.update(facts['counts'])
                sources[facts['source']] += 1
                finite[facts['finite']] += 1
                indices_gates[facts['index_gate']] += 1
                finite_reasons[facts['finite_reason']] += 1
                index_reasons[facts['index_reason']] += 1
                if facts['blockers'] is not None:
                    masks[f'{facts["blockers"]:08x}'] += 1
                if facts['proofs'] is not None:
                    proofs[str(facts['proofs'])] += 1
                combinations[(facts['source'], facts['finite'], facts['index_gate'], facts['candidate'])] += 1
            totals.update(counts)
            totals['complete_successful_frames'] += 1
            for label, counter in [('source', sources), ('finite', finite), ('index_gate', indices_gates)]:
                for name, value in counter.items():
                    totals[label + '_' + name] += value
        output.append(dict(device=key[0], frame=key[1], complete_successful_count_matched=valid,
                           errors=sorted(set(issues)), observed_draw_slots=len(indices), counts=dict(counts),
                           source_qualification=dict(sources), finite_evidence=dict(finite), index_range_gate=dict(indices_gates),
                           blocker_masks=dict(masks), proof_masks=dict(proofs), finite_reasons=dict(finite_reasons), index_reasons=dict(index_reasons)))
    return dict(totals=dict(totals), frames=output, parse_diagnostics=dict(errors),
                gate_combinations=[dict(source=key[0], finite=key[1], index_gate=key[2], input_candidate=key[3], draws=value) for key, value in sorted(combinations.items())],
                owner_metrics=inspect_metrics(batches), qpc_frequency=next(iter(frequency)) if len(frequency) == 1 else None,
                blocker_enum={str(k): v for k, v in BLOCKERS.items()}, proof_enum={str(k): v for k, v in PROOFS.items()},
                finite_state_enum={'0': 'unknown', '1': 'finite', '2': 'nonfinite'},
                candidate_meaning='qualified source + finite position/index evidence + local gates + stable lifetime + successful draw; not replay/TAA eligibility')


def analyze_snapshot(path, expected_sha256=None):
    source_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    before = path.stat()
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        def lines():
            for raw in stream:
                digest.update(raw)
                if not raw.endswith(b'\n'):
                    raise ValueError('snapshot ends in an incomplete record')
                yield raw.decode('utf-8')
        report = analyze(lines())
    after = path.stat()
    if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
        raise ValueError('capture changed during analysis; use a stable snapshot')
    if expected_sha256 and digest.hexdigest() != expected_sha256.lower():
        raise ValueError('capture hash mismatch')
    if source_hash != hashlib.sha256(Path(__file__).read_bytes()).hexdigest():
        raise ValueError('analyzer source changed during analysis')
    report['provenance'] = dict(snapshot=str(path), bytes=after.st_size, sha256=digest.hexdigest(),
                                analyzer_sha256=source_hash)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--expected-sha256')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    # Prevent a failed new invocation from leaving an old candidate report at
    # this requested output path. Raw input is never changed.
    if args.capture.resolve() == args.output.resolve() or (args.capture.exists() and args.output.exists() and args.capture.samefile(args.output)):
        raise ValueError('output must differ from capture')
    args.output.write_text(json.dumps({'analysis_complete': False}) + '\n')
    report = analyze_snapshot(args.capture, args.expected_sha256)
    report['analysis_complete'] = True
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['totals'], indent=2))


if __name__ == '__main__':
    main()
