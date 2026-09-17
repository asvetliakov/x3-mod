#!/usr/bin/env python3
"""Parser and predicates of the caster-candidate counter lines
(docs/architecture/shadow-replay-gates.md, sections 3 and "Implemented").

Two line kinds, read from a capture log without loading it whole:

  shadow_replay_candidates device= frame= routed= zwrite= slice0= bounds= origin=
      fallback= managed= dynamic= default_pool= excluded= unknown= shadow_mismatch=
      leased= capped= reads= serial_changed= readonly_after= writable_after=
      pending= in_flight= quiet= cold_thread= stale= roots= waiting= nested= overflow=
  shadow_replay_lock_witness device= frame= allocation= flags= offset= size= thread=
      serial_delta= revision_delta=

A malformed line (missing or non-numeric field, unknown extra field) raises
MalformedLine; the caller decides whether that fails the run. Sum identities
of every frame line are checked at parse time; the witness cap (16 per device)
and the four decision predicates are functions of the parsed rows.
"""
import re
import sys
from pathlib import Path

FRAME_PREFIX = 'shadow_replay_candidates '
WITNESS_PREFIX = 'shadow_replay_lock_witness '
FRAME_FIELDS = ('device', 'frame', 'routed', 'zwrite', 'slice0', 'bounds', 'origin', 'fallback', 'managed', 'dynamic', 'default_pool',
                'excluded', 'unknown', 'shadow_mismatch', 'leased', 'capped', 'reads', 'serial_changed', 'readonly_after', 'writable_after',
                'pending', 'in_flight', 'quiet', 'cold_thread', 'stale', 'roots', 'waiting', 'nested', 'overflow')
WITNESS_FIELDS = ('device', 'frame', 'allocation', 'flags', 'offset', 'size', 'thread', 'serial_delta', 'revision_delta')
HEX_FIELDS = ('flags',)
WITNESS_CAP = 16
FIELD = re.compile(r'(\w+)=(\S+)')


class MalformedLine(ValueError):
    pass


def _parse(line, prefix, names):
    body = line[len(prefix):].rstrip('\n')
    pairs = FIELD.findall(body)
    if len(pairs) != len(names) or ' '.join(f'{k}={v}' for k, v in pairs) != body.strip():
        raise MalformedLine(line.rstrip('\n'))
    row = {}
    for (key, value), name in zip(pairs, names):
        if key != name:
            raise MalformedLine(line.rstrip('\n'))
        try:
            row[key] = int(value, 16) if key in HEX_FIELDS else int(value)
        except ValueError as error:
            raise MalformedLine(line.rstrip('\n')) from error
        if row[key] < 0:
            raise MalformedLine(line.rstrip('\n'))
    return row


def check_identities(row):
    """The chain routed >= zwrite >= slice0 >= managed and the partitions
    slice0 = bounds + fallback (casters by bounds, or the origin rule while no
    extent is known) = managed + dynamic + default_pool + excluded + unknown +
    shadow_mismatch, origin <= zwrite (the origin rule alone, a statistic),
    leased + capped + overflow <= managed, quiet + stale <= leased, every other
    bookend bucket <= leased - stale (a stale record is compared with nothing)."""
    if not row['routed'] >= row['zwrite'] >= row['slice0'] >= row['managed']:
        raise MalformedLine(f'chain violated: {row}')
    if row['slice0'] != row['bounds'] + row['fallback']:
        raise MalformedLine(f'slice0 != bounds + fallback: {row}')
    if row['origin'] > row['zwrite']:
        raise MalformedLine(f'origin exceeds zwrite: {row}')
    if row['slice0'] != (row['managed'] + row['dynamic'] + row['default_pool'] + row['excluded'] + row['unknown']
                         + row['shadow_mismatch']):
        raise MalformedLine(f'slice0 partition violated: {row}')
    if row['leased'] + row['capped'] + row['overflow'] > row['managed']:
        raise MalformedLine(f'leased+capped+overflow exceeds managed: {row}')
    if row['quiet'] + row['stale'] > row['leased']:
        raise MalformedLine(f'quiet+stale exceeds leased: {row}')
    for key in ('serial_changed', 'readonly_after', 'writable_after', 'pending', 'in_flight', 'cold_thread'):
        if row[key] > row['leased'] - row['stale']:
            raise MalformedLine(f'{key} exceeds leased-stale: {row}')
    if row['readonly_after'] > row['serial_changed']:
        raise MalformedLine(f'readonly_after exceeds serial_changed: {row}')
    return row


def parse_lines(lines):
    """Yields ('frame', row) / ('witness', row) for the two line kinds; other
    lines are ignored. Raises MalformedLine."""
    for line in lines:
        if line.startswith(FRAME_PREFIX):
            yield 'frame', check_identities(_parse(line, FRAME_PREFIX, FRAME_FIELDS))
        elif line.startswith(WITNESS_PREFIX):
            yield 'witness', _parse(line, WITNESS_PREFIX, WITNESS_FIELDS)


def parse_text(text):
    frames, witnesses = [], []
    for kind, row in parse_lines(text.splitlines()):
        (frames if kind == 'frame' else witnesses).append(row)
    return frames, witnesses


def parse_file(path):
    frames, witnesses = [], []
    with open(path, encoding='utf-8', errors='replace') as handle:
        for kind, row in parse_lines(handle):
            (frames if kind == 'frame' else witnesses).append(row)
    return frames, witnesses


def witness_counts(witnesses):
    counts = {}
    for row in witnesses:
        counts[row['device']] = counts.get(row['device'], 0) + 1
    return counts


def check_witness_cap(witnesses, cap=WITNESS_CAP):
    over = {device: n for device, n in witness_counts(witnesses).items() if n > cap}
    if over:
        raise MalformedLine(f'witness cap {cap} exceeded: {over}')
    return witness_counts(witnesses)


def check_frame_uniqueness(frames):
    seen = set()
    for row in frames:
        key = (row['device'], row['frame'])
        if key in seen:
            raise MalformedLine(f'duplicate frame line device={key[0]} frame={key[1]}')
        seen.add(key)
    return len(seen)


def percentile(values, p):
    if not values:
        return None
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(round((len(ordered) - 1) * p))))
    return ordered[index]


def summarize(frames, witnesses):
    """The four decision predicates of section 3 plus the numbers behind them.
    All shares are over frame lines (scene ends); an empty run answers None."""
    n = len(frames)
    share = lambda predicate: (sum(1 for r in frames if predicate(r)) / n) if n else None  # noqa: E731
    slice0 = [r['slice0'] for r in frames]
    summary = {
        'frames': n,
        'devices': sorted({r['device'] for r in frames}),
        'slice0_p50': percentile(slice0, 0.5),
        'slice0_max': max(slice0) if slice0 else None,
        'routed_p50': percentile([r['routed'] for r in frames], 0.5),
        'managed_equals_slice0_share': share(lambda r: r['managed'] == r['slice0']),
        'quiet_equals_leased_share': share(lambda r: r['quiet'] == r['leased']),
        'writable_after_total': sum(r['writable_after'] for r in frames),
        'serial_changed_total': sum(r['serial_changed'] for r in frames),
        'cold_thread_total': sum(r['cold_thread'] for r in frames),
        'waiting_or_nested_frames': sum(1 for r in frames if r['waiting'] or r['nested']),
        'overflow_frames': sum(1 for r in frames if r['overflow']),
        'capped_frames': sum(1 for r in frames if r['capped']),
        'capped_total': sum(r['capped'] for r in frames),
        'bounds_share': share(lambda r: r['slice0'] == r['bounds']),  # frames whose every caster was decided by bounds (no fallback)
        'origin_p50': percentile([r['origin'] for r in frames], 0.5),
        'reads_total': sum(r['reads'] for r in frames),
        'unknown_total': sum(r['unknown'] for r in frames),
        'shadow_mismatch_total': sum(r['shadow_mismatch'] for r in frames),
        'stale_total': sum(r['stale'] for r in frames),
        'witnesses': witness_counts(witnesses),
    }
    summary['predicates'] = {
        'managed_boundary': (summary['slice0_p50'] is not None and summary['slice0_p50'] >= 5
                             and summary['managed_equals_slice0_share'] >= 0.95),
        'lease_contract': (summary['quiet_equals_leased_share'] is not None and summary['quiet_equals_leased_share'] >= 0.99
                           and summary['writable_after_total'] == 0),
        'single_thread': n > 0 and summary['cold_thread_total'] == 0,
        'promotion_possible': n > 0 and summary['waiting_or_nested_frames'] == 0,
    }
    return summary


def main(argv):
    if len(argv) != 2:
        print('usage: shadow_replay_candidates.py <session log>', file=sys.stderr)
        return 2
    frames, witnesses = parse_file(Path(argv[1]))
    check_frame_uniqueness(frames)
    check_witness_cap(witnesses)
    import json
    print(json.dumps(summarize(frames, witnesses), indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
