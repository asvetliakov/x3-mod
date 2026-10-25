#!/usr/bin/env python3
"""Which stop-all ended each track-queue episode in a flight log (music-restart.md section 7.7).

The 1.5.1 queue row `event=end reason=stop_all` does not name the caller. Two witnesses do: the QPC gap to the next
`music_keep_seek` (the woken task's vanilla replay of the parked id; a save blocks the loop for the file write, about
2.5 s, while a stop-all inside a script step lets the replay run in the same pass) and the next `music_keep_stop`
caller name. Prints one row per end plus a summary; QPC at 10 MHz (checked against the save pump's span_ms).

    python3 -I verification/results/music-script-driver/queue_save_gaps.py [x3m.log]
"""
import hashlib, re, sys

PATH = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3/x3m.log'
FREQ = 10_000_000
digest = hashlib.sha256()
rows = []
with open(PATH, 'rb') as f:
    for raw in f:
        digest.update(raw)
        if not raw.startswith(b'music_'):
            continue
        line = raw.decode('utf-8', 'replace')
        m = re.search(r' frame=(\d+).* qpc=(\d+)', line)
        if m:
            rows.append((int(m.group(1)), int(m.group(2)), line.split()[0], line))
print('log', PATH, 'sha256', digest.hexdigest()[:16])
saves = scripted = other = 0
for k, (frame, qpc, kind, line) in enumerate(rows):
    if 'event=end reason=stop_all' not in line:
        continue
    seek = next(((f2, q2) for f2, q2, k2, _ in rows[k + 1:] if k2 == 'music_keep_seek'), None)
    stop = next(((f2, l2) for f2, _, k2, l2 in rows[k + 1:k + 4] if k2 == 'music_keep_stop'), None)
    gap_ms = (seek[1] - qpc) * 1000 / FREQ if seek else None
    kind_guess = 'save' if gap_ms is not None and gap_ms > 1000 else 'script_step' if gap_ms is not None and gap_ms < 5 else 'other'
    saves += kind_guess == 'save'
    scripted += kind_guess == 'script_step'
    other += kind_guess == 'other'
    print('episode=%s end_frame=%d replay_frame=%s gap_ms=%s next_stop=%s class=%s' % (
        re.search(r'episode=(\d+)', line).group(1), frame, seek[0] if seek else '-',
        '%.1f' % gap_ms if gap_ms is not None else '-',
        re.search(r'name=(\w+)', stop[1]).group(1) + '@' + str(stop[0]) if stop else '-', kind_guess))
print('stop_all_ends=%d save=%d script_step=%d other=%d' % (saves + scripted + other, saves, scripted, other))
