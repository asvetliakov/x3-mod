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

With sun-shadow cascades on (docs/architecture/shadow-cascades.md, section 4) the
frame line ends with one `c<i>=` per configured cascade (records carrying
cascade i) followed by one `capped<i>=` per cascade (admitted draws whose
cascade i was dropped by that cascade's cap); a log without them parses as
before and its rows carry no `cascades` key. Caster pool control
(shadow-cascade-extents.md, "Caster pool control") adds, each only while its
option is on: `static_only_refused<i>=`, `large_admitted<i>=` and `class_miss<i>=`
per cascade then `class_store= class_ring=` (--shadow-cascade-static-from), and
`dropped_min_size<i>=` (a float) per cascade then `select_us=`
(--shadow-cascade-drop-order importance); parsed into the `cascades` dict as
`static_only_refused`, `large_admitted`, `class_miss`, `classified`, `dropped_min_size` and `select_us`.
Between the two groups sits the minimum-footprint group
(--shadow-cascade-min-footprint; docs/architecture/shadow-cascades.md, "Minimum
caster footprint"): `footprint_refused<i>=` per cascade (live draws the gate
dropped from cascade i) then `footprint_aged<i>=` per cascade (retained records
the same gate dropped in the store's unseen walk), parsed as
`footprint_refused` and `footprint_aged`.
The line then ends with the cascade-membership flip counters
(docs/architecture/shadow-caster-retention.md, "Membership flips"):
`flip_c<i>=` per cascade, the casters whose cascade-i bit entered or left since
the previous frame, then `period2_c<i>=` per cascade, those of them that also
flipped on the previous frame (the period-2 blink signature), then
`flip_untracked=` (casters the table's bounded probe could not place, so an
undercount is never silent) and `flip_reset=` (1 on a seeded frame whose counts
are all zero: the first frame, a gap in the scene ends, or a cascade shape
change); parsed as `flip`, `period2`, `flip_untracked` and `flip_reset`. A log
written before the counters existed parses as before.

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


CASCADE_MAX = 5  # renderer::shadow_cascade_max


CLASS_FIELDS = ('class_store', 'class_ring')


def _cascade_suffix(line, pairs):
    """The optional cascade tail of a frame line: c0..c<n-1> then
    capped0..capped<n-1>, 1 <= n <= 5; then optionally the static-only group
    (static_only_refused0..n-1, large_admitted0..n-1, class_miss0..n-1, class_store, class_ring) and
    optionally the importance group (dropped_min_size0..n-1, select_us), then
    optionally the flip group (flip_c0..n-1, period2_c0..n-1, flip_untracked,
    flip_reset); nothing else."""
    if not pairs:
        return None
    keys = [k for k, _ in pairs]
    count = next((i for i in range(1, CASCADE_MAX + 1) if keys[:2 * i] == [f'c{j}' for j in range(i)] + [f'capped{j}' for j in range(i)]), None)
    if count is None or keys[:count] != [f'c{j}' for j in range(count)]:
        raise MalformedLine(line.rstrip('\n'))

    def integers(values):
        try:
            values = [int(v) for v in values]
        except ValueError as error:
            raise MalformedLine(line.rstrip('\n')) from error
        if any(v < 0 for v in values):
            raise MalformedLine(line.rstrip('\n'))
        return values
    values = integers(v for _, v in pairs[:2 * count])
    row = {'count': count, 'records': values[:count], 'capped': values[count:]}
    rest = pairs[2 * count:]
    static_keys = [f'static_only_refused{j}' for j in range(count)] + [f'large_admitted{j}' for j in range(count)] + [f'class_miss{j}' for j in range(count)] + list(CLASS_FIELDS)
    if rest and rest[0][0] == static_keys[0]:
        if [k for k, _ in rest[:len(static_keys)]] != static_keys:
            raise MalformedLine(line.rstrip('\n'))
        values = integers(v for _, v in rest[:len(static_keys)])
        row['static_only_refused'] = values[:count]
        row['large_admitted'] = values[count:2 * count]
        row['class_miss'] = values[2 * count:3 * count]
        row['classified'] = dict(zip(CLASS_FIELDS, values[3 * count:]))
        rest = rest[len(static_keys):]
    footprint_keys = [f'footprint_refused{j}' for j in range(count)] + [f'footprint_aged{j}' for j in range(count)]
    if rest and rest[0][0] == footprint_keys[0]:
        if [k for k, _ in rest[:len(footprint_keys)]] != footprint_keys:
            raise MalformedLine(line.rstrip('\n'))
        values = integers(v for _, v in rest[:len(footprint_keys)])
        row['footprint_refused'] = values[:count]
        row['footprint_aged'] = values[count:]
        rest = rest[len(footprint_keys):]
    size_keys = [f'dropped_min_size{j}' for j in range(count)] + ['select_us']
    if rest and rest[0][0] == size_keys[0]:
        if [k for k, _ in rest[:len(size_keys)]] != size_keys:
            raise MalformedLine(line.rstrip('\n'))
        try:
            floats = [float(v) for _, v in rest[:len(size_keys)]]
        except ValueError as error:
            raise MalformedLine(line.rstrip('\n')) from error
        if any(v < 0 or v != v for v in floats):
            raise MalformedLine(line.rstrip('\n'))
        row['dropped_min_size'] = floats[:count]
        row['select_us'] = floats[count]
        rest = rest[len(size_keys):]
    flip_keys = [f'flip_c{j}' for j in range(count)] + [f'period2_c{j}' for j in range(count)] + ['flip_untracked', 'flip_reset']
    if rest and rest[0][0] == flip_keys[0]:
        if [k for k, _ in rest[:len(flip_keys)]] != flip_keys:
            raise MalformedLine(line.rstrip('\n'))
        values = integers(v for _, v in rest[:len(flip_keys)])
        row['flip'] = values[:count]
        row['period2'] = values[count:2 * count]
        row['flip_untracked'], row['flip_reset'] = values[2 * count], values[2 * count + 1]
        # A caster can only blink on both of the last two frames if it flipped on this one,
        # and a seeded frame (flip_reset=1) counts nothing at all.
        if any(b > a for a, b in zip(row['flip'], row['period2'])) or row['flip_reset'] > 1 \
                or (row['flip_reset'] and (any(row['flip']) or any(row['period2']))):
            raise MalformedLine(line.rstrip('\n'))
        rest = rest[len(flip_keys):]
    if rest:
        raise MalformedLine(line.rstrip('\n'))
    return row


def _parse(line, prefix, names, suffix=False):
    body = line[len(prefix):].rstrip('\n')
    pairs = FIELD.findall(body)
    if ' '.join(f'{k}={v}' for k, v in pairs) != body.strip() or len(pairs) < len(names) or (len(pairs) != len(names) and not suffix):
        raise MalformedLine(line.rstrip('\n'))
    cascades = _cascade_suffix(line, pairs[len(names):]) if suffix else None
    pairs = pairs[:len(names)]
    row = {}
    if cascades:
        row['cascades'] = cascades
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
    cascades = row.get('cascades')
    if cascades:
        # A record carries at least one cascade and at most all of them; a
        # draw dropped from every cascade is one of `capped`.
        if max(cascades['records']) > row['leased'] or sum(cascades['records']) < row['leased']:
            raise MalformedLine(f'cascade records do not cover leased: {row}')
        if row['capped'] > sum(cascades['capped']):
            raise MalformedLine(f'capped exceeds the per-cascade drops: {row}')
    return row


def parse_lines(lines):
    """Yields ('frame', row) / ('witness', row) for the two line kinds; other
    lines are ignored. Raises MalformedLine."""
    for line in lines:
        if line.startswith(FRAME_PREFIX):
            yield 'frame', check_identities(_parse(line, FRAME_PREFIX, FRAME_FIELDS, suffix=True))
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
    cascade_rows = [r['cascades'] for r in frames if 'cascades' in r]
    if cascade_rows:
        count = max(c['count'] for c in cascade_rows)
        column = lambda key, i: [c[key][i] for c in cascade_rows if i < c['count']]  # noqa: E731
        summary['cascades'] = {'frames': len(cascade_rows), 'count': count,
                               'records_p50': [percentile(column('records', i), 0.5) for i in range(count)],
                               'records_max': [max(column('records', i)) for i in range(count)],
                               'capped_total': [sum(column('capped', i)) for i in range(count)],
                               'capped_frames': [sum(1 for v in column('capped', i) if v) for i in range(count)]}
        static_rows = [c for c in cascade_rows if 'static_only_refused' in c]
        if static_rows:
            summary['cascades']['static_only_refused_total'] = [sum(c['static_only_refused'][i] for c in static_rows if i < c['count']) for i in range(count)]
            summary['cascades']['large_admitted_total'] = [sum(c['large_admitted'][i] for c in static_rows if i < c['count']) for i in range(count)]
            summary['cascades']['class_miss_total'] = [sum(c['class_miss'][i] for c in static_rows if i < c['count']) for i in range(count)]
            summary['cascades']['classified_total'] = {k: sum(c['classified'][k] for c in static_rows) for k in CLASS_FIELDS}
        flip_rows = [c for c in cascade_rows if 'flip' in c]
        if flip_rows:
            summary['cascades']['flip_total'] = [sum(c['flip'][i] for c in flip_rows if i < c['count']) for i in range(count)]
            summary['cascades']['flip_max'] = [max((c['flip'][i] for c in flip_rows if i < c['count']), default=0) for i in range(count)]
            summary['cascades']['period2_total'] = [sum(c['period2'][i] for c in flip_rows if i < c['count']) for i in range(count)]
            summary['cascades']['period2_max'] = [max((c['period2'][i] for c in flip_rows if i < c['count']), default=0) for i in range(count)]
            summary['cascades']['period2_frames'] = [sum(1 for c in flip_rows if i < c['count'] and c['period2'][i]) for i in range(count)]
            summary['cascades']['flip_untracked_total'] = sum(c['flip_untracked'] for c in flip_rows)
            summary['cascades']['flip_reset_frames'] = sum(c['flip_reset'] for c in flip_rows)
        footprint_rows = [c for c in cascade_rows if 'footprint_refused' in c]
        if footprint_rows:
            summary['cascades']['footprint_refused_total'] = [sum(c['footprint_refused'][i] for c in footprint_rows if i < c['count']) for i in range(count)]
            summary['cascades']['footprint_aged_total'] = [sum(c['footprint_aged'][i] for c in footprint_rows if i < c['count']) for i in range(count)]
        size_rows = [c for c in cascade_rows if 'dropped_min_size' in c]
        if size_rows:
            summary['cascades']['dropped_min_size_max'] = [max((c['dropped_min_size'][i] for c in size_rows if i < c['count']), default=0.0) for i in range(count)]
            summary['cascades']['select_us_p50'] = percentile([c['select_us'] for c in size_rows], 0.5)
            summary['cascades']['select_us_max'] = max(c['select_us'] for c in size_rows)
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
