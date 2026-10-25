#!/usr/bin/env python3
"""Replays the Run 203 A lod_strength_frame rows through the follow-up build's row rule (a row on a change of seen /
table / stale / released / twins / refused / failed, at a no_twin onset, or with draws pending once per 10 s of
elapsed_ms from frame_end) and prints the old and the new row counts. A frame without an old row had no draws and no
change (the old rule wrote one whenever draws || marked || a total changed). Usage: frame_row_volume.py LOG"""
import collections
import sys

elapsed, rows = {}, []
with open(sys.argv[1], errors='replace') as log:
    for line in log:
        if line.startswith('frame_end '):
            d = dict(f.split('=', 1) for f in line.split()[1:] if '=' in f)
            elapsed[int(d['frame'])] = int(d['elapsed_ms'])
        elif line.startswith('lod_strength_frame '):
            d = dict(f.split('=', 1) for f in line.split()[1:] if '=' in f)
            rows.append(d)
logged, last_ms, previous_no_twin, pending, reasons = None, None, False, False, collections.Counter()
previous_frame = None
for d in rows:
    frame = int(d['frame'])
    if previous_frame is not None and frame != previous_frame + 1:
        previous_no_twin = False  # the frames between had no row: no draws, so no no_twin draw
    previous_frame = frame
    totals = (d['seen'], d['table'], d['stale'], d['released'], d['twins'], d['refused'], d['failed'])
    no_twin = int(d['no_twin']) > 0
    onset = no_twin and not previous_no_twin
    previous_no_twin = no_twin
    pending = pending or int(d['draws']) > 0 or int(d['marked']) > 0
    now = elapsed.get(frame)
    reason = 'change' if totals != logged else 'onset' if onset else None
    if not reason and pending and now is not None and (last_ms is None or now - last_ms >= 10000):
        reason = 'cadence'
    if reason:
        reasons[reason] += 1
        logged, pending = totals, False
        last_ms = now if now is not None else last_ms
span = [elapsed.get(int(d['frame'])) for d in (rows[0], rows[-1])] if rows else [None, None]
print('old rows', len(rows), 'new rows', sum(reasons.values()), dict(reasons), 'first/last row elapsed_ms', span)
