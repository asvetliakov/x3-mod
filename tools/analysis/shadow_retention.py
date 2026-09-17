#!/usr/bin/env python3
"""Parser and census summary of the caster-retention lines
(docs/architecture/shadow-caster-retention.md, stages 1 and 2).

Line kinds, read from a capture log line by line (never loaded whole):

  shadow_retention_frame   one per frame: levels, per-frame expiry counts, cost
  shadow_retention_resight every 300 frames, cumulative: the age-cap calibration
  shadow_retention_caster  F8 frames: one per retained record
  shadow_retention_flush   a whole-store flush that held anything

and the `retained=` flag of `shadow_replay_caster` plus the
`replayed_live<i>` / `replayed_retained<i>` tail of `shadow_replay_depth`.
A malformed frame or resight line raises MalformedLine. `summary()` reduces a
census run to what the contract asks of it: the drift histogram against eps,
the store and budget sizes, the resight table with the age bucket where
`moved + changed` stops being negligible, the transit evidence and the cost.
"""
import json
import re
import statistics
import sys
from pathlib import Path

FRAME_PREFIX = 'shadow_retention_frame '
RESIGHT_PREFIX = 'shadow_retention_resight '
CASTER_PREFIX = 'shadow_retention_caster '
FLUSH_PREFIX = 'shadow_retention_flush '
REPLAY_CASTER_PREFIX = 'shadow_replay_caster '
FRAME_FIELDS = ('device', 'frame', 'mode', 'known', 'nodes_live', 'nodes_unseen', 'records', 'records_unseen', 'static', 'moving',
                'excluded_class', 'unscoped', 'new_nodes', 'first_seen_in_range', 'promoted', 'superseded', 'lod_replaced', 'model_replaced', 'reclassified',
                'retired', 'journal_overflow', 'revalidated', 'mutation_delta', 'buffer_changed', 'buffer_gone', 'buffer_orphaned', 'orphan_probe',
                'box_exit', 'age', 'evicted', 'flush', 'unseen_in_frustum', 'unseen_outside',
                'live_c0', 'live_c1', 'live_c2', 'live_c3', 'live_c4', 'would_c0', 'would_c1', 'would_c2', 'would_c3', 'would_c4', 'capped_c0', 'capped_c1', 'capped_c2', 'capped_c3', 'capped_c4',
                'drift_n', 'drift_p99', 'drift_max', 'age_max', 'refs_held', 'sun_relatch', 'cam_jump', 'transit_survivors', 'us',
                # beyond the contract's list (implementation diagnostics)
                'refused', 'moving_dropped', 'abandoned', 'deferred', 'journal_us', 'walk_us', 'draw_us', 'draw_calls',
                'far_alternate_due_to_retained', 'revalidate_context_lost', 'release_queue_full', 'reclassified_after_unseen', 'admitted_checked', 'buffer_views', 'idle_frames',
                # per-cascade reclassifications at that cascade's eps tier and the static gate's refused-draw sightings (2026-09-18, run 40 A)
                'reclassified_c0', 'reclassified_c1', 'reclassified_c2', 'reclassified_c3', 'reclassified_c4', 'gate_sightings')
FLOAT_FIELDS = ('drift_p99', 'drift_max', 'us', 'journal_us', 'walk_us', 'draw_us')
TEXT_FIELDS = {'mode': ('census', 'live'), 'flush': ('none', 'epoch', 'reset', 'device', 'teardown', 'sun', 'observer', 'idle')}
BUCKETS = 5
BUCKET_LABELS = ('<60', '<600', '<3600', '<14400', '>=14400')
RESIGHT_FIELDS = ('device', 'frame') + tuple(f'b{b}_{k}' for b in range(BUCKETS) for k in ('same', 'moved', 'changed')) \
    + tuple(f'{k}_b{b}' for k in ('expired_retired', 'expired_box', 'expired_gone') for b in range(BUCKETS))
NODE_CAPACITY, RECORD_CAPACITY, RESOURCE_CAPACITY = 1024, 4096, 1024
FIELD = re.compile(r'(\w+)=(\S+)')


class MalformedLine(ValueError):
    pass


def fields(line):
    return dict(FIELD.findall(line))


def parse_frame_line(line):
    body = line[len(FRAME_PREFIX):].strip()
    pairs = FIELD.findall(body)
    if [k for k, _ in pairs] != list(FRAME_FIELDS) or ' '.join(f'{k}={v}' for k, v in pairs) != body:
        raise MalformedLine(line.strip())
    row = {}
    for key, value in pairs:
        if key in TEXT_FIELDS:
            if value not in TEXT_FIELDS[key]:
                raise MalformedLine(line.strip())
            row[key] = value
            continue
        try:
            row[key] = float(value) if key in FLOAT_FIELDS else int(value)
        except ValueError as error:
            raise MalformedLine(line.strip()) from error
        if row[key] < 0:
            raise MalformedLine(line.strip())
    check_frame(row)
    return row


def check_frame(row):
    """Level identities and the census/live split of the contract."""
    nodes = row['nodes_live'] + row['nodes_unseen']
    if nodes > NODE_CAPACITY or row['records'] > RECORD_CAPACITY or row['refs_held'] > RESOURCE_CAPACITY:
        raise MalformedLine(f'a level exceeds the store: {row}')
    if row['static'] + row['moving'] != nodes or row['records_unseen'] > row['records']:
        raise MalformedLine(f'levels inconsistent: {row}')
    if row['unseen_in_frustum'] + row['unseen_outside'] > row['nodes_unseen']:
        raise MalformedLine(f'frustum split exceeds the unseen nodes: {row}')
    if any(row[f'capped_c{c}'] > row[f'would_c{c}'] for c in range(4)):
        raise MalformedLine(f'capped exceeds would: {row}')
    if row['mode'] == 'census' and (row['refs_held'] or row['buffer_orphaned'] or row['orphan_probe']):
        raise MalformedLine(f'the census holds no references and probes nothing: {row}')
    if row['drift_p99'] > row['drift_max'] + 1e-9 or (not row['drift_n'] and row['drift_max']):
        raise MalformedLine(f'drift inconsistent: {row}')
    return row


def parse_resight_line(line):
    body = line[len(RESIGHT_PREFIX):].strip()
    pairs = FIELD.findall(body)
    if [k for k, _ in pairs] != list(RESIGHT_FIELDS):
        raise MalformedLine(line.strip())
    try:
        row = {k: int(v) for k, v in pairs}
    except ValueError as error:
        raise MalformedLine(line.strip()) from error
    if any(v < 0 for v in row.values()):
        raise MalformedLine(line.strip())
    return row


def parse_lines(lines):
    """frames, resights, casters (capture frames), flushes, replay caster flags {retained: count}."""
    frames, resights, casters, flushes, replayed = [], [], [], [], {'0': 0, '1': 0}
    for line in lines:
        if line.startswith(FRAME_PREFIX):
            frames.append(parse_frame_line(line))
        elif line.startswith(RESIGHT_PREFIX):
            resights.append(parse_resight_line(line))
        elif line.startswith(CASTER_PREFIX):
            casters.append(fields(line[len(CASTER_PREFIX):]))
        elif line.startswith(FLUSH_PREFIX):
            flushes.append(fields(line[len(FLUSH_PREFIX):]))
        elif line.startswith(REPLAY_CASTER_PREFIX):
            flag = fields(line).get('retained')
            if flag in replayed:
                replayed[flag] += 1
    return frames, resights, casters, flushes, replayed


def parse_text(text):
    return parse_lines(text.splitlines())


def age_cap_bucket(resight, negligible=0.01):
    """The first unseen-age bucket where moved + changed is no longer negligible
    against same (None: never in this run). The age cap belongs below it."""
    for b in range(BUCKETS):
        same, other = resight[f'b{b}_same'], resight[f'b{b}_moved'] + resight[f'b{b}_changed']
        if other and other > negligible * max(same, 1):
            return b
    return None


def summary(frames, resights, eps=0.05):
    if not frames:
        return {'frames': 0}
    total = lambda key: sum(r[key] for r in frames)
    peak = lambda key: max(r[key] for r in frames)
    verified = [r for r in frames if r['drift_n']]
    cost = [r['us'] for r in frames]
    draws = [r['draw_us'] / r['draw_calls'] for r in frames if r['draw_calls']]
    out = {'frames': len(frames), 'modes': sorted({r['mode'] for r in frames}), 'known_frames': total('known'),
           'levels': {k: peak(k) for k in ('nodes_live', 'nodes_unseen', 'records', 'records_unseen', 'static', 'moving', 'refs_held', 'age_max')},
           'would_peak': [peak(f'would_c{c}') for c in range(4)], 'capped_peak': [peak(f'capped_c{c}') for c in range(4)], 'live_peak': [peak(f'live_c{c}') for c in range(4)],
           'totals': {k: total(k) for k in ('excluded_class', 'unscoped', 'new_nodes', 'first_seen_in_range', 'promoted', 'superseded', 'lod_replaced', 'model_replaced', 'reclassified',
                                            'retired', 'journal_overflow', 'revalidated', 'mutation_delta', 'buffer_changed', 'buffer_gone', 'buffer_orphaned', 'box_exit', 'age',
                                            'evicted', 'sun_relatch', 'cam_jump', 'transit_survivors', 'refused', 'moving_dropped', 'abandoned', 'deferred',
                                            'far_alternate_due_to_retained', 'revalidate_context_lost', 'release_queue_full', 'reclassified_after_unseen', 'admitted_checked', 'buffer_views',
                                            'reclassified_c0', 'reclassified_c1', 'reclassified_c2', 'reclassified_c3', 'reclassified_c4', 'gate_sightings')},
           'flushes': {name: sum(1 for r in frames if r['flush'] == name) for name in TEXT_FIELDS['flush'] if name != 'none'},
           'retired_burst_peak': peak('retired'),
           'drift': {'frames': len(verified), 'samples': sum(r['drift_n'] for r in verified), 'max': max((r['drift_max'] for r in verified), default=0.0),
                     'p99_median': statistics.median(r['drift_p99'] for r in verified) if verified else 0.0, 'eps': eps,
                     'frames_over_eps': sum(1 for r in verified if r['drift_max'] > eps)},
           'us': {'median': statistics.median(cost), 'max': max(cost), 'p99': sorted(cost)[min(len(cost) - 1, (len(cost) * 99) // 100)]},
           'draw_us_per_call': {'median': statistics.median(draws), 'max': max(draws)} if draws else None}
    moving_share = [r['moving'] / (r['static'] + r['moving']) for r in frames if r['static'] + r['moving']]
    out['moving_share_median'] = statistics.median(moving_share) if moving_share else None
    if resights:
        last = resights[-1]
        out['resight'] = {'frame': last['frame'], 'buckets': {BUCKET_LABELS[b]: {k: last[f'b{b}_{k}'] for k in ('same', 'moved', 'changed')} for b in range(BUCKETS)},
                          'expired': {k: [last[f'expired_{k}_b{b}'] for b in range(BUCKETS)] for k in ('retired', 'box', 'gone')}}
        bucket = age_cap_bucket(last)
        out['resight']['age_cap_below_bucket'] = None if bucket is None else BUCKET_LABELS[bucket]
    return out


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 1:
        print('usage: shadow_retention.py <capture log>', file=sys.stderr)
        return 2
    with Path(argv[0]).open(errors='replace') as handle:
        frames, resights, casters, flushes, replayed = parse_lines(handle)
    result = summary(frames, resights)
    result['capture_casters'] = len(casters); result['flush_lines'] = len(flushes); result['replay_caster_flags'] = replayed
    print(json.dumps(result, indent=1, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
