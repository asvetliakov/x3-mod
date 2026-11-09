#!/usr/bin/env python3
"""FL 62 A: the muzzle flash's per-period counters from the flight's weapon_effects_frame rows (the ledger paragraph
"FL 62 A: flash invisible; life moved to wall-clock ms (run 63)"). Prints the session id, the rows with any muzzle or
flash activity, those where muzzle_skipped == flash_matched, the activity rows with flash_drawn / pulse_drawn non-zero,
and the sums of every muzzle / flash / pulse counter.
usage: python3 verification/results/fl-run62/flash_rows.py [C:\\X3FL\\x3m.log as /mnt/c/X3FL/x3m.log]"""
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3FL/x3m.log'
field = re.compile(r'\b((?:muzzle|flash|pulse)_[a-z_]+)=(\d+)')
session, rows = None, []
with open(path, errors='replace') as f:
    for line in f:
        if session is None and line.startswith('log_open '):
            m = re.search(r'session=(\S+)', line)
            session = m.group(1) if m else '?'
        if line.startswith('weapon_effects_frame '):
            rows.append({k: int(v) for k, v in field.findall(line)})
active = [r for r in rows if any(r.values())]
keys = sorted({k for r in rows for k in r})
print('session', session, 'frame_rows', len(rows), 'active_rows', len(active))
print('skipped_eq_matched', sum(r.get('muzzle_skipped') == r.get('flash_matched') for r in active))
print('flash_drawn_rows', sum(r.get('flash_drawn', 0) > 0 for r in active),
      'pulse_drawn_rows', sum(r.get('pulse_drawn', 0) > 0 for r in active))
print('sums', {k: sum(r.get(k, 0) for r in rows) for k in keys})
