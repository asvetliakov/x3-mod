#!/usr/bin/env python3
"""Summarise the music lines of a flight log (x3m.log) for music-script-driver.md, without reading the log whole
into the report: counts per line kind, the sequence of music plays (frame, id, start_ms, action, seconds since the previous play from qpc), whether any combat
id (1001..1042, 200::ChangeTrackToCombatMusic in the Mayhem object) or same-id replay occurs, and whether the
--music-trace lines (music_trace_*) are present.

    python3 -I verification/results/music-script-driver/flight_music_summary.py [/mnt/c/X3/x3m.log]
"""
import collections, hashlib, re, sys
from pathlib import Path

log = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3/x3m.log')
kinds = collections.Counter()
plays = []
freq = 0
h = hashlib.sha256()
with open(log, 'rb') as f:
    for raw in f:
        h.update(raw)
        line = raw.decode('latin1', 'replace')
        fq = re.search(r'qpc_frequency=(\d+)', line)
        if fq and not freq:
            freq = int(fq.group(1))  # the clock_anchor line; later lines may carry other qpc_frequency fields
        m = re.match(r'(music_[a-z_]+)', line)
        if not m:
            continue
        kinds[m.group(1)] += 1
        if m.group(1) in ('music_keep_seek', 'music_trace_play'):
            g = dict(re.findall(r'(\w+)=(\S+)', line))
            plays.append((int(g.get('frame', 0)), int(g.get('id', 0)), g.get('start_ms'), g.get('action', g.get('name')),
                          int(g.get('qpc', 0))))
print('log %s sha256 %s' % (log, h.hexdigest()))
print('kinds ' + ', '.join('%s=%d' % kv for kv in sorted(kinds.items())))
print('music_trace present: %s' % any(k.startswith('music_trace') for k in kinds))
ids = [p[1] for p in plays]
print('plays %d, distinct ids %d, combat ids (1001..1042) %d, same-id consecutive replays %d' % (
    len(plays), len(set(ids)), sum(1001 <= i <= 1042 for i in ids), sum(a == b for a, b in zip(ids, ids[1:]))))
prev = None
for fr, i, ms, act, q in plays:
    dt = '%.1f s after the previous play' % ((q - prev) / freq) if prev and freq else ''
    print('  frame %-7d id %-6d start_ms %-4s %-8s %s' % (fr, i, ms, act, dt))
    prev = q
