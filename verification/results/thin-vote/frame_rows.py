#!/usr/bin/env python3
"""Prints the thin_vote_frame fields the ledger cites from the four thin cases' capture logs (bottle X3):
per frame draw_us / voted / known, and the hostile case's final totals (lock_us, measure_us, refusal counters)."""
from pathlib import Path

RESULTS = Path(__file__).resolve().parents[1] / 'bottle-X3'
FIELDS = ('frame', 'draws', 'opaque', 'known', 'voted', 'draw_us')
TOTALS = ('reads', 'measured', 'unreadable_total', 'retries', 'not_managed', 'not_readable', 'stale', 'range', 'geometry',
          'not_quiet', 'lock_failed', 'triangles', 'invalidated', 'dropped_entries', 'overflows', 'volatile_buffers',
          'volatile_refused', 'lock_us', 'measure_us', 'max_measure_us')


def rows(case):
    path = RESULTS / f'motion-output-seam-thin-vote-{case}-capture.log'
    out = []
    with path.open(errors='replace') as log:
        for line in log:
            at = line.find('thin_vote_frame ')
            if at >= 0:
                out.append(dict(p.split('=', 1) for p in line[at:].split()[1:] if '=' in p))
    return out


for case in ('far-on', 'near-on', 'hostile'):
    frames = rows(case)
    print(case, 'frames', len(frames))
    for f in frames:
        print('  ' + ' '.join(f'{k}={f.get(k)}' for k in FIELDS))
    if frames:
        print('  totals ' + ' '.join(f'{k}={frames[-1].get(k)}' for k in TOTALS))
print('far-off thin_vote_frame rows', len(rows('far-off')))
