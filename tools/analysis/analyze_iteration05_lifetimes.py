#!/usr/bin/env python3
"""Bounded metadata-only lifetime audit of a fixed schema-2 capture snapshot.

Known storage lifetime is not an object-motion correspondence or camera-cut proof.
Unknown/off/missing records never become zero-valued stable epochs.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re

FIELDS = re.compile(r'(\w+)=([^\s]+)')
EVENTS = {'frame_begin', 'frame_end', 'draw', 'draw_result', 'object_context',
          'motion_input', 'motion_lifetime', 'object_lifetime', 'object_matrix'}
PAIRS = [('before_known', 'after_known'), ('observer_epoch', 'observer_epoch_after'),
         ('load_epoch', 'load_epoch_after'), ('registry_epoch', 'registry_epoch_after'),
         ('mutation_before', 'mutation_after'), ('node_serial', 'node_serial_after'),
         ('camera_serial', 'camera_serial_after')]
REASONS = {0: 'Known', 1: 'Disabled', 2: 'MutationInProgress', 3: 'RegistryUnavailable',
           4: 'RegistryMismatch', 5: 'UnknownNodeBirth', 6: 'UnknownCameraBirth',
           7: 'PointerMismatch', 8: 'LookupUnavailable', 9: 'RegistryChanged',
           10: 'CapacityExhausted', 11: 'CounterExhausted', 12: 'ObserverFailure'}


def coordinate(fields):
    return tuple(fields.get(key) for key in ('device', 'frame', 'index'))


def positive_integer(value, bits, base=10):
    pattern = r'(?:0x)?[0-9a-fA-F]+' if base == 16 else r'[0-9]+'
    if not isinstance(value, str) or not re.fullmatch(pattern, value):
        return False
    return 0 < int(value, base) < (1 << bits)


def success(value):
    try:
        return 0 <= int(value, 16) < 0x80000000
    except (TypeError, ValueError):
        return False


def valid_matrix(rows):
    return set(rows) == {'0', '1', '2', '3'} and all(
        re.fullmatch(r'[0-9a-fA-F]{8}(?:,[0-9a-fA-F]{8}){3}', rows[key]) for key in rows)


def matrix_hash(rows):
    if not valid_matrix(rows):
        return None
    return hashlib.sha256(';'.join(rows[str(i)].lower() for i in range(4)).encode()).hexdigest()[:16]


def audit(lines, max_draws=100000, max_frames=1024, completed_session=False):
    frames = {}
    activations = []
    current = None
    draws_seen = 0
    duplicates = Counter()
    for line in lines:
        event = line.partition(' ')[0].strip()
        if event not in EVENTS:
            continue
        fields = dict(FIELDS.findall(line))
        key = (fields.get('device'), fields.get('frame'))
        if event == 'object_lifetime':
            activations.append(fields)
        elif event == 'frame_begin':
            if key in frames:
                raise ValueError(f'duplicate frame begin {key}')
            if len(frames) >= max_frames:
                raise ValueError('frame bound exceeded')
            frames[key] = {'draws': {}, 'end': None, 'duplicate_end': False}
            current = None
        elif event == 'draw' and key in frames:
            if draws_seen >= max_draws:
                raise ValueError('draw bound exceeded')
            index = fields.get('index')
            if index in frames[key]['draws']:
                raise ValueError(f'duplicate draw {key}:{index}')
            current = dict(draw=fields, matrices={})
            frames[key]['draws'][index] = current
            draws_seen += 1
        elif event == 'frame_end':
            if key in frames:
                if frames[key]['end'] is not None:
                    frames[key]['duplicate_end'] = True
                frames[key]['end'] = fields
            current = None
        elif event == 'object_matrix' and current is not None:
            role = fields.get('role')
            if role in ('view', 'projection'):
                rows = current['matrices'].setdefault(role, {})
                row = fields.get('row')
                if row in rows:
                    rows['duplicate'] = ''
                rows[row] = fields.get('bits', '')
        elif event in ('draw_result', 'object_context', 'motion_input', 'motion_lifetime'):
            # These records carry coordinates; never attach a mismatched record
            # to the most recently printed draw merely because it is nearby.
            target = frames.get(key, {}).get('draws', {}).get(fields.get('index'))
            if target is not None:
                if event in target:
                    duplicates[event] += 1
                    target[event + '_duplicate'] = True
                target[event] = fields
    output = []
    total = Counter()
    all_epochs = set()
    serial_entities = defaultdict(set)
    entity_serials = defaultdict(set)
    role_entities = defaultdict(set)
    handles = defaultdict(set)
    prior_by_device = {}
    relations = []
    unknown_paths = Counter()
    mismatches = Counter()
    for (device, frame_number), frame in frames.items():
        records = list(frame['draws'].values())
        end = frame['end'] or {}
        eligible = bool(end and not frame['duplicate_end'] and end.get('capture') == '1' and success(end.get('present')) and
                        int(end.get('draws', '-1')) == len(records))
        item = dict(device=device, frame=int(frame_number), complete=bool(end),
                    successful_complete_capture=eligible, draws=len(records), reported_draws=end.get('draws'),
                    present=end.get('present'), duplicate_frame_end=frame['duplicate_end'])
        counts = Counter()
        reasons = Counter()
        epochs = set()
        revisions = set()
        revision_runs = []
        cameras = {}
        entities = {}
        for draw in records:
            ctx = draw.get('object_context', {})
            motion = draw.get('motion_input', {})
            life = draw.get('motion_lifetime', {})
            scoped = ctx.get('scoped') == '1' and ctx.get('valid') == '127' and not draw.get('object_context_duplicate')
            draw_success = success(draw.get('draw_result', {}).get('result')) and not draw.get('draw_result_duplicate')
            counts['scoped_valid'] += scoped
            counts['unscoped'] += ctx.get('scoped') == '0'
            counts['successful_draws'] += draw_success
            counts['lifetime_records'] += bool(life)
            counts['motion_lifetime_verified'] += motion.get('lifetime_verified') == '1'
            counts['missing_lifetime'] += not life
            counts['duplicate_records'] += any(draw.get(name + '_duplicate') for name in ('object_context', 'draw_result', 'motion_input', 'motion_lifetime'))
            for before, after in PAIRS:
                if before in life and after in life and life[before] != life[after]:
                    counts['before_after_mismatches'] += 1
                    if eligible:
                        mismatches[before] += 1
            if life:
                reason = life.get('before_reason', 'missing') + '/' + life.get('after_reason', 'missing')
                reasons[reason] += 1
            required = [name for pair in PAIRS for name in pair]
            context_complete = (all(positive_integer(ctx.get(name), 32, 16) for name in ('registry', 'node', 'camera')) and
                                all(positive_integer(ctx.get(name), 32) for name in ('node_handle', 'camera_handle')))
            known = bool(scoped and context_complete and life.get('registry') == ctx.get('registry') and draw_success and life and not draw.get('motion_lifetime_duplicate') and
                         all(name in life for name in required) and
                         life.get('before_known') == life.get('after_known') == '1' and
                         life.get('before_reason') == life.get('after_reason') == '0' and
                         all(life[a] == life[b] for a, b in PAIRS) and
                         all(positive_integer(life.get(name), 64) for name in
                             ('observer_epoch', 'load_epoch', 'registry_epoch', 'node_serial', 'camera_serial')) and
                         positive_integer(life.get('mutation_before'), 64))
            counts['known_consistent_successful'] += known
            if not known:
                if eligible:
                    unknown_paths[(ctx.get('scoped', 'missing'), life.get('before_reason', 'missing'),
                                   draw['draw'].get('vs'), draw['draw'].get('ps'))] += 1
                continue
            epoch = tuple(life[name] for name in ('observer_epoch', 'load_epoch', 'registry_epoch'))
            epochs.add(epoch)
            revisions.add(int(life['mutation_before']))
            revision = int(life['mutation_before'])
            if not revision_runs or revision_runs[-1]['revision'] != revision:
                revision_runs.append(dict(revision=revision, first_draw=int(draw['draw']['index']), last_draw=int(draw['draw']['index'])))
            else:
                revision_runs[-1]['last_draw'] = int(draw['draw']['index'])
            camera = tuple(ctx[name] for name in ('camera', 'camera_handle')) + (life['camera_serial'],)
            summary = cameras.setdefault(camera, dict(draws=0, view=set(), projection=set()))
            summary['draws'] += 1
            for role in ('view', 'projection'):
                digest = matrix_hash(draw['matrices'].get(role, {}))
                if digest:
                    summary[role].add(digest)
            for role in ('node', 'camera'):
                entity = (life['registry'], ctx[role], ctx[role + '_handle'])
                serial = (life['observer_epoch'], life[role + '_serial'])
                entity_key = epoch + entity
                entities[entity_key] = life[role + '_serial']
                if eligible:
                    serial_entities[serial].add(entity)
                    entity_serials[entity_key].add(life[role + '_serial'])
                    role_entities[role].add(serial)
                    handles[(role, life['observer_epoch'], life['registry'], ctx[role + '_handle'])].add((life['load_epoch'], life['registry_epoch'], ctx[role], life[role + '_serial']))
        item.update(counts=dict(counts), reasons=dict(reasons), known_epochs=sorted(epochs),
                    mutation_revisions=sorted(revisions), mutation_revision_runs=revision_runs,
                    cameras=[dict(pointer=key[0], handle=key[1], serial=key[2], draws=value['draws'],
                                  view_hashes=sorted(value['view']), projection_hashes=sorted(value['projection']))
                             for key, value in sorted(cameras.items(), key=lambda pair: (-pair[1]['draws'], pair[0]))])
        output.append(item)
        if eligible:
            total.update(counts)
            total['draws'] += len(records)
            total['frames'] += 1
            all_epochs.update(epochs)
            if device in prior_by_device:
                previous_number, previous_entities = prior_by_device[device]
                shared = set(entities) & set(previous_entities)
                relations.append(dict(device=device, previous_frame=previous_number, frame=int(frame_number),
                                      adjacent=int(frame_number) == previous_number + 1,
                                      shared_entities=len(shared),
                                      serial_changes=sum(entities[key] != previous_entities[key] for key in shared)))
            prior_by_device[device] = (int(frame_number), entities)
    reused_handles = [dict(role=key[0], observer_epoch=key[1], registry=key[2], handle=key[3],
                           occurrences=[dict(load_epoch=value[0], registry_epoch=value[1], pointer=value[2], serial=value[3]) for value in sorted(values)])
                      for key, values in sorted(handles.items()) if len({value[0] for value in values}) > 1]
    return dict(handle_reappearances_across_load_epochs=len(reused_handles), handle_reappearance_examples=reused_handles[:10], scope='completed-session fixed snapshot' if completed_session else 'partial fixed snapshot; gameplay may continue', activations=activations,
                frames=output, totals_successful_complete_frames=dict(total), known_epochs=sorted(all_epochs),
                before_after_mismatches=dict(mismatches), duplicate_diagnostic_records=dict(duplicates),
                observed_node_serials=len(role_entities['node']), observed_camera_serials=len(role_entities['camera']),
                serial_to_multiple_entities=sum(len(values) > 1 for values in serial_entities.values()),
                entity_to_multiple_serials=sum(len(values) > 1 for values in entity_serials.values()),
                neighboring_captured_frame_storage_relations=relations,
                unknown_paths=[dict(scoped=key[0], before_reason=key[1], vs=key[2], ps=key[3], draws=value)
                               for key, value in sorted(unknown_paths.items(), key=lambda pair: -pair[1])],
                reason_enum={str(key): value for key, value in REASONS.items()})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('--expected-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--session-complete', action='store_true')
    args = parser.parse_args()
    before = args.snapshot.stat()
    digest = hashlib.sha256()
    with args.snapshot.open('rb') as source:
        def lines():
            for raw in source:
                digest.update(raw)
                yield raw.decode('utf-8', errors='strict')
        report = audit(lines(), completed_session=args.session_complete)
    after = args.snapshot.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns) or digest.hexdigest() != args.expected_sha256:
        raise ValueError('snapshot changed or expected hash mismatch')
    root = Path(__file__).resolve().parents[2]
    sources = ['tools/analysis/analyze_iteration05_lifetimes.py', 'src/proxy/object_lifetime.cpp',
               'src/proxy/object_lifetime.h', 'src/proxy/capture.cpp']
    report['provenance'] = dict(snapshot=str(args.snapshot), size=after.st_size, sha256=digest.hexdigest(),
                                source_sha256={name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sources})
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['totals_successful_complete_frames'], indent=2))


if __name__ == '__main__':
    main()
