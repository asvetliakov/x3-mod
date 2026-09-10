#!/usr/bin/env python3
"""Audit recorded scene-depth ordering and local motion-input metadata.

API-copy bookkeeping is not numerical GPU-depth readback. Local proof bits do
not establish finite vertex payloads, replay stability, correspondence or TAA.
"""
import argparse
from bisect import bisect_left
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re

FIELDS = re.compile(r'(\w+)=([^\s]+)')
BLOCKERS = {1: 'PositionProgram', 2: 'PixelCoverage', 4: 'PositionLayout',
            8: 'BufferDescription', 16: 'BufferRevision', 32: 'DrawRange',
            64: 'RasterState', 128: 'TargetLayout', 256: 'SubmittedRows',
            512: 'ObjectScope', 1024: 'UserMemory', 2048: 'QueryFailure', 4096: 'SubmissionFailure'}
PROOFS = {1: 'LifetimeVerified', 2: 'GeometryUnchanged', 4: 'PositionReviewed',
          8: 'CoverageSupported', 16: 'SubmissionSucceeded'}
FRAME_EVENTS = {'frame_begin', 'frame_end', 'capture_event', 'draw', 'draw_result',
                'object_context', 'motion_input', 'motion_lifetime', 'scene_depth_frame',
                'scene_depth_copy', 'scene_depth_boundary', 'scene_depth_reject',
                'scene_depth_unsupported', 'scene_depth_color_fill'}
DETAILS = {'clear', 'clear_viewport', 'surface'}


def number(value, base=10, bits=64):
    pattern = r'(?:0x)?[0-9a-fA-F]+' if base == 16 else r'[0-9]+'
    if not isinstance(value, str) or not re.fullmatch(pattern, value):
        return None
    result = int(value, base)
    return result if result < 1 << bits else None


def good_hresult(value):
    result = number(value, 16, 32)
    return result is not None and result < 0x80000000


def positive(value, base=10, bits=64):
    result = number(value, base, bits)
    return result is not None and result > 0


def real_equal(value, expected):
    try:
        parsed = float(value)
        return math.isfinite(parsed) and parsed == expected
    except (ValueError, TypeError):
        return False


def one(records):
    return records[0] if len(records) == 1 else None


def frame_key(fields):
    if not positive(fields.get('device')) or number(fields.get('frame')) is None:
        raise ValueError('missing or invalid device/frame coordinate')
    return fields['device'], fields['frame']


def analyze(lines, max_frames=1024, max_draws=100000, max_events=200000, max_records=400000):
    frames = {}
    ownership = []
    active_detail = None
    draw_count = event_count = retained_records = 0
    for line_number, line in enumerate(lines, 1):
        name = line.partition(' ')[0].strip()
        if name not in FRAME_EVENTS | DETAILS | {'ownership_copy_depth'}:
            continue
        retained_records += 1
        if retained_records > max_records:
            raise ValueError('metadata record bound exceeded')
        pairs = FIELDS.findall(line)
        fields = dict(pairs)
        if len(fields) != len(pairs):
            raise ValueError('duplicate scalar field in recognized diagnostic')
        fields['_line'] = line_number
        if name == 'ownership_copy_depth':
            if len(ownership) >= 4096:
                raise ValueError('ownership metadata bound exceeded')
            ownership.append(fields)
            continue
        if name in DETAILS:
            if active_detail is not None:
                if name != 'surface' or fields.get('role') in ('clear_rt0', 'clear_depth'):
                    active_detail['details'].setdefault(name if name != 'surface' else fields['role'], []).append(fields)
            continue
        key = frame_key(fields)
        # Noncaptured periodic frame_end records are outside this audit.
        if name == 'frame_end' and key not in frames and fields.get('capture') != '1':
            active_detail = None
            continue
        if key not in frames:
            if len(frames) >= max_frames:
                raise ValueError('frame bound exceeded')
            frames[key] = dict(records={}, draws={}, events=[])
        frame = frames[key]
        if name == 'capture_event':
            event_count += 1
            if event_count > max_events:
                raise ValueError('capture-event bound exceeded')
            active_detail = dict(fields=fields, details={})
            frame['events'].append(active_detail)
        elif name == 'draw':
            active_detail = None
            index = fields.get('index')
            if not positive(index):
                raise ValueError('invalid draw index')
            draw_count += 1
            if draw_count > max_draws:
                raise ValueError('draw bound exceeded')
            frame['draws'].setdefault(index, {}).setdefault('draw', []).append(fields)
        elif name in ('draw_result', 'object_context', 'motion_input', 'motion_lifetime'):
            index = fields.get('index')
            if not positive(index):
                raise ValueError('invalid diagnostic draw index')
            # Bound orphan diagnostic slots too; a real matching draw is required later.
            if index not in frame['draws'] and len(frame['draws']) >= max_draws:
                raise ValueError('draw diagnostic bound exceeded')
            frame['draws'].setdefault(index, {}).setdefault(name, []).append(fields)
        else:
            tag = name + ':' + fields.get('phase', '') if name == 'scene_depth_frame' else name
            records = frame['records'].setdefault(tag, [])
            if len(records) >= max_events:
                raise ValueError('frame diagnostic bound exceeded')
            records.append(fields)
            if name in ('frame_begin', 'frame_end'):
                active_detail = None
    output = []
    totals = Counter()
    groups = {}
    prior_selected = {}
    epoch_transitions = []
    for (device, frame_number), frame in frames.items():
        record = lambda name: one(frame['records'].get(name, []))
        begin, end = record('frame_begin'), record('frame_end')
        sb, se = record('scene_depth_frame:begin'), record('scene_depth_frame:end')
        copy, boundary = record('scene_depth_copy'), record('scene_depth_boundary')
        reject = record('scene_depth_reject')
        actual_draws = [one(item.get('draw', [])) for item in frame['draws'].values() if item.get('draw')]
        draw_indices = [number(draw.get('index')) for draw in actual_draws if draw]
        frame_valid = bool(begin and end and end.get('capture') == '1' and good_hresult(end.get('present')) and
                           begin['_line'] < end['_line'] and all(actual_draws) and
                           draw_indices == list(range(1, len(actual_draws) + 1)) and
                           number(end.get('draws')) == len(actual_draws))
        events = frame['events']
        sequence = [number(event['fields'].get('seq')) for event in events]
        event_sequence_valid = bool(events and sequence == list(range(1, len(events) + 1)))
        events_successful = all(good_hresult(event['fields'].get('result')) for event in events)
        draw_events = [event['fields'] for event in events if event['fields'].get('op') == 'draw_begin']
        draw_event_alignment = bool(len(draw_events) == len(actual_draws) and all(
            draw and number(event.get('after_draw')) == number(draw['index']) - 1 and event['_line'] < draw['_line']
            for event, draw in zip(draw_events, actual_draws)))
        draw_lines = [draw['_line'] for draw in actual_draws if draw]
        event_after_draw_alignment = all(number(event['fields'].get('after_draw')) == bisect_left(draw_lines, event['fields']['_line']) for event in events)
        all_draw_results_valid = all((one(item.get('draw_result', [])) is not None and good_hresult(one(item['draw_result']).get('result')) and one(item.get('draw', [])) is not None and one(item['draw'])['_line'] < one(item['draw_result'])['_line']) for item in frame['draws'].values())
        ordinary_lines = []
        for tag, records in frame['records'].items():
            if tag not in ('frame_begin', 'frame_end', 'scene_depth_frame:begin'):
                ordinary_lines.extend(item['_line'] for item in records)
        for records in frame['draws'].values():
            ordinary_lines.extend(item['_line'] for items in records.values() for item in items)
        for event in events:
            ordinary_lines.append(event['fields']['_line'])
            ordinary_lines.extend(item['_line'] for items in event['details'].values() for item in items)
        records_inside_frame = bool(begin and end and all(begin['_line'] < line < end['_line'] for line in ordinary_lines))
        frame_valid = frame_valid and records_inside_frame and all_draw_results_valid
        depth_errors = []
        require = lambda condition, label: depth_errors.append(label) if not condition else None
        require(frame_valid, 'complete_successful_frame_missing_or_invalid')
        require(event_sequence_valid and events_successful and draw_event_alignment and event_after_draw_alignment and all_draw_results_valid, 'capture_event_sequence_or_results_invalid')
        require(bool(sb and se and begin and end and sb['_line'] < begin['_line'] < se['_line'] < end['_line']), 'scene_frame_scope_missing_duplicate_or_misordered')
        require(bool(sb and se and positive(sb.get('generation')) and sb.get('generation') == se.get('generation') and
                     number(se.get('events')) == len(events) and end and se.get('present') == end.get('present')),
                'scene_frame_generation_events_or_present_mismatch')
        generation = sb.get('generation') if sb else None
        descriptions = [item for item in ownership if item.get('device') == device and item.get('generation') == generation and begin and item['_line'] < begin['_line']]
        descriptor = descriptions[-1] if descriptions else None
        require(bool(descriptor and good_hresult(descriptor.get('result')) and good_hresult(descriptor.get('status')) and
                     descriptor.get('requested') == descriptor.get('available') == descriptor.get('source_bound') == '1' and
                     positive(descriptor.get('source_width')) and positive(descriptor.get('source_height')) and
                     descriptor.get('source_format') == '77' and descriptor.get('source_msaa') == '0'),
                'matching_ownership_source_description_unavailable')
        boundary_event = None
        if copy:
            candidates = [event for event in events if event['fields'].get('seq') == copy.get('event')]
            boundary_event = one(candidates)
        selected_errors = list(depth_errors)
        def selected_require(condition, label):
            if not condition:
                selected_errors.append(label)
        selected_require(bool(copy and boundary), 'unique_copy_or_boundary_missing')
        selected_require(bool(copy and boundary and sb and se and copy.get('event') == boundary.get('event') and
                              copy.get('generation') == boundary.get('generation') == sb.get('generation') == se.get('generation')),
                         'copy_boundary_generation_or_event_mismatch')
        selected_require(bool(copy and good_hresult(copy.get('result')) and copy.get('valid') == '1' and
                              positive(copy.get('copy_epoch')) and copy.get('copy_epoch') == copy.get('source_epoch') and
                              positive(copy.get('color')) and positive(copy.get('depth'))), 'copy_result_or_source_epoch_invalid')
        selected_require(bool(boundary and copy and boundary.get('confirmed') == '1' and
                              boundary.get('copy_epoch') == copy.get('copy_epoch') and
                              number(boundary.get('source_epoch')) == (number(copy.get('copy_epoch')) or 0) + 1),
                         'boundary_confirmation_or_epoch_increment_invalid')
        selected_require(bool(se and all(se.get(key) == '1' for key in ('attempted', 'copied', 'confirmed')) and
                              se.get('state') == '8' and se.get('rejection') == se.get('rejection_event') == '0' and
                              not frame['records'].get('scene_depth_reject') and not frame['records'].get('scene_depth_unsupported')),
                         'frame_end_does_not_retain_selection')
        ce = boundary_event['fields'] if boundary_event else {}
        details = boundary_event['details'] if boundary_event else {}
        clear = one(details.get('clear', []))
        rt, ds = one(details.get('clear_rt0', [])), one(details.get('clear_depth', []))
        viewport = one(details.get('clear_viewport', []))
        selected_require(bool(copy and boundary and boundary_event and se and copy['_line'] < boundary['_line'] < ce['_line'] < se['_line'] and
                              ce.get('op') == 'clear' and good_hresult(ce.get('result'))), 'copy_before_confirmation_before_matching_clear_log_not_proven')
        selected_require(bool(clear and clear.get('flags') == '2' and clear.get('rect_count') == '0' and real_equal(clear.get('z'), 1)),
                         'matching_full_depth_only_clear_missing')
        selected_require(bool(rt and ds and copy and descriptor and rt.get('identity') == copy.get('color') and
                              ds.get('identity') == copy.get('depth') and ds.get('format') == '77' and rt.get('format') == '21' and
                              rt.get('msaa') == ds.get('msaa') == '0' and
                              rt.get('width') == ds.get('width') == descriptor.get('source_width') and
                              rt.get('height') == ds.get('height') == descriptor.get('source_height')),
                         'clear_surfaces_not_matching_copy_source_and_main_color')
        selected_require(bool(viewport and descriptor and good_hresult(viewport.get('result')) and
                              viewport.get('x') == viewport.get('y') == '0' and
                              viewport.get('w') == descriptor.get('source_width') and viewport.get('h') == descriptor.get('source_height') and
                              real_equal(viewport.get('minz'), 0) and real_equal(viewport.get('maxz'), 1)),
                         'matching_clear_viewport_unknown_or_partial')
        selected_require(bool(clear and rt and ds and viewport and ce['_line'] < clear['_line'] < rt['_line'] < ds['_line'] < viewport['_line']), 'clear_detail_order_invalid')
        if copy and ce and number(ce.get('after_draw')) is not None and number(ce.get('after_draw')) > 0:
            previous_result = one(frame['draws'].get(ce['after_draw'], {}).get('draw_result', []))
            selected_require(bool(previous_result and previous_result['_line'] < copy['_line']), 'copy_precedes_completion_of_prior_draw')
        selected = not selected_errors
        initial = events[0] if events else {'fields': {}, 'details': {}}
        initial_clear = one(initial['details'].get('clear', []))
        initial_rejection = bool(not depth_errors and reject and se and not frame['records'].get('scene_depth_copy') and
                                 not frame['records'].get('scene_depth_boundary') and reject.get('event') == '1' and
                                 reject.get('operation') == 'Clear' and reject.get('reason') == 'Pattern' and reject.get('rejection') == '5' and
                                 se.get('state') == '9' and se.get('rejection') == '5' and se.get('rejection_event') == '1' and
                                 all(se.get(key) == '0' for key in ('attempted', 'copied', 'confirmed')) and
                                 initial['fields'].get('op') == 'clear' and initial_clear and initial_clear.get('flags') == '1')
        classification = 'selected_depth_boundary' if selected else 'initial_color_only_pattern_rejection' if initial_rejection else 'unverified'
        source_clears = []
        if selected:
            for event in events:
                c = one(event['details'].get('clear', []))
                d = one(event['details'].get('clear_depth', []))
                if event['fields'].get('op') == 'clear' and c and d and d.get('identity') == copy.get('depth') and good_hresult(event['fields'].get('result')) and number(c.get('flags')) is not None and number(c['flags']) & 2:
                    source_clears.append(dict(event=int(event['fields']['seq']), after_draw=int(event['fields']['after_draw']), flags=int(c['flags'])))
            previous = prior_selected.get(device)
            if previous and previous['frame'] + 1 == int(frame_number) and previous['generation'] == generation and previous['depth'] == copy['depth']:
                expected = previous['clears_after_copy'] + sum(c['event'] < int(copy['event']) for c in source_clears)
                observed = int(copy['copy_epoch']) - previous['copy_epoch']
                epoch_transitions.append(dict(device=device, previous_frame=previous['frame'], frame=int(frame_number), expected_source_epoch_delta=expected, observed_source_epoch_delta=observed, consistent=expected == observed))
            prior_selected[device] = dict(frame=int(frame_number), generation=generation, depth=copy['depth'], copy_epoch=int(copy['copy_epoch']), clears_after_copy=sum(c['event'] >= int(copy['event']) for c in source_clears))
        counts, blocker_masks, proof_masks, blockers, proofs = Counter(), Counter(), Counter(), Counter(), Counter()
        joint = Counter()
        for index, draw_records in frame['draws'].items():
            draw = one(draw_records.get('draw', []))
            result = one(draw_records.get('draw_result', []))
            motion = one(draw_records.get('motion_input', []))
            ctx = one(draw_records.get('object_context', []))
            life = one(draw_records.get('motion_lifetime', []))
            if not draw:
                counts['missing_or_duplicate_draw'] += 1
                continue
            counts['draws'] += 1
            successful = bool(result and good_hresult(result.get('result')) and draw['_line'] < result['_line'])
            counts['successful_draws'] += successful
            counts['missing_or_duplicate_motion_record'] += motion is None
            counts['missing_or_duplicate_scope_record'] += ctx is None
            counts['missing_or_duplicate_lifetime_record'] += life is None
            mask = number(motion.get('blockers'), 16, 32) if motion else None
            proof = number(motion.get('proofs'), 10, 32) if motion else None
            scope = bool(ctx and motion and result and draw['_line'] < ctx['_line'] < motion['_line'] < result['_line'] and ctx.get('scoped') == '1' and ctx.get('valid') == '127' and
                         all(positive(ctx.get(name), 16, 32) for name in ('node', 'camera', 'registry', 'mesh')) and
                         all(positive(ctx.get(name), 10, 32) for name in ('node_handle', 'camera_handle', 'scope_depth')))
            lifetime = bool(life and motion and result and motion['_line'] < life['_line'] < result['_line'] and life.get('registry') == (ctx or {}).get('registry') and
                            life.get('before_known') == life.get('after_known') == '1' and
                            life.get('before_reason') == life.get('after_reason') == '0' and
                            all(positive(life.get(a)) and life.get(a) == life.get(b) for a, b in
                                [('observer_epoch', 'observer_epoch_after'), ('load_epoch', 'load_epoch_after'),
                                 ('registry_epoch', 'registry_epoch_after'), ('mutation_before', 'mutation_after'),
                                 ('node_serial', 'node_serial_after'), ('camera_serial', 'camera_serial_after')]))
            motion_valid = bool(successful and motion and result and draw['_line'] < motion['_line'] < result['_line'] and
                                mask is not None and proof is not None and mask & ~8191 == 0 and proof & ~31 == 0 and
                                all(number(motion.get(name), 16, 64) is not None and motion.get(name) == draw.get(name) for name in ('vs', 'ps')) and
                                ((mask != 0 and proof != 31) or all(positive(motion.get(name), 16, 64) for name in ('vs', 'ps'))) and
                                motion.get('lifetime_verified') in ('0', '1') and motion.get('vertex_finite_verified') in ('0', '1'))
            if not motion_valid:
                counts['invalid_motion_metadata'] += 1
                continue
            blocker_masks[f'{mask:08x}'] += 1
            proof_masks[str(proof)] += 1
            joint[(f'{mask:08x}', proof)] += 1
            for bit, name in BLOCKERS.items():
                blockers[name] += bool(mask & bit)
            for bit, name in PROOFS.items():
                proofs[name] += bool(proof & bit)
            counts['vertex_finite_verified'] += motion.get('vertex_finite_verified') == '1'
            counts['vertex_finite_unverified'] += motion.get('vertex_finite_verified') == '0'
            counts['scoped_valid'] += scope
            counts['lifetime_verified_flag'] += motion.get('lifetime_verified') == '1'
            candidate = bool(mask == 0 and proof == 31 and scope and lifetime and motion.get('lifetime_verified') == '1')
            counts['local_metadata_candidate'] += candidate
            if candidate and selected:
                region = 'before_selected_clear' if int(index) <= int(ce['after_draw']) else 'after_selected_clear'
                counts['local_metadata_candidate_' + region] += 1
            counts['candidate_with_finite_payload_attestation'] += candidate and motion.get('vertex_finite_verified') == '1'
        item = dict(device=device, frame=int(frame_number), complete_successful_capture=frame_valid,
                    classification=classification, depth_validation_errors=selected_errors if not selected else [],
                    generation=generation, source_description=descriptor, source_depth_clears=source_clears,
                    scene_begin=sb, scene_end=se, copy=copy, boundary=boundary, first_rejection=reject,
                    capture_events=len(events), event_sequence_valid=event_sequence_valid, records_inside_frame=records_inside_frame, all_draw_results_valid=all_draw_results_valid,
                    selected_clear=dict(event=ce, clear=clear, color=rt, depth=ds, viewport=viewport) if boundary_event else None,
                    counts=dict(counts), blocker_masks=dict(blocker_masks), proof_masks=dict(proof_masks),
                    blocker_bits={key: value for key, value in blockers.items() if value},
                    proof_bits=dict(proofs), blocker_proof_pairs=[dict(blockers=key[0], proofs=key[1], draws=value) for key, value in sorted(joint.items())])
        output.append(item)
        if frame_valid:
            totals.update(counts)
            totals['frames'] += 1
            totals[classification + '_frames'] += 1
            group = groups.setdefault(classification, dict(counts=Counter(), blocker_masks=Counter(), proof_masks=Counter()))
            group['counts'].update(counts)
            group['blocker_masks'].update(blocker_masks)
            group['proof_masks'].update(proof_masks)
    return dict(scope='completed-session fixed snapshot; diagnostics only', totals=dict(totals), frames=output, adjacent_selected_frame_epoch_checks=epoch_transitions,
                groups={key: {name: dict(value) for name, value in group.items()} for key, group in groups.items()},
                blocker_enum={str(key): value for key, value in BLOCKERS.items()}, proof_enum={str(key): value for key, value in PROOFS.items()},
                numeric_depth_readback_proven=False, live_taa_eligibility_proven=False,
                limitations=['copy success/epochs are API bookkeeping, not numerical depth sampling',
                             'local proof bits exclude finite vertex payload and whole-scene/replay/history proof',
                             'initial color-only rejection is a pattern classification, not a universal menu detector'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('--expected-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    before = args.snapshot.stat()
    digest = hashlib.sha256()
    with args.snapshot.open('rb') as stream:
        def lines():
            for raw in stream:
                digest.update(raw)
                yield raw.decode('utf-8')
        report = analyze(lines())
    after = args.snapshot.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns) or digest.hexdigest() != args.expected_sha256:
        raise ValueError('snapshot changed or expected hash mismatch')
    root = Path(__file__).resolve().parents[2]
    inputs = ['tools/analysis/analyze_iteration05_depth_motion.py', 'src/proxy/scene_capture.cpp',
              'src/proxy/capture.cpp', 'src/proxy/draw_input.cpp', 'src/proxy/draw_input.h',
              'src/renderer/scene_boundary.h', 'src/renderer/motion_history.h', 'src/ownership/d3d9_ownership.cpp']
    report['provenance'] = dict(snapshot=str(args.snapshot), size=after.st_size, sha256=digest.hexdigest(),
                                local_interpretation_source_sha256={name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in inputs})
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['totals'], indent=2))


if __name__ == '__main__':
    main()
