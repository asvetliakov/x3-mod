#!/usr/bin/env python3
"""Run190 speed-after-gate triage (2026-10-23): the rows that bound which Run190 change can act at a transit.
Usage: python3 -I speed_reset_rows.py <run189 x3m.log> <run190 x3m.log>
Prints per log: seta_restore arm/cancel/restore rows, chase_view_restore results, and each music_alert
suppressed episode (the only engine write of the Run190 delta) bracketed by the nearest frame= rows."""
import re, sys
FRAME = re.compile(r'\bframe=(\d+)')
KEEP = re.compile(r'^(seta_restore (armed|cancelled|restored|restore_failed)|chase_view_restore result|music_alert (suppressed|edge))')
for path in sys.argv[1:]:
    print('==', path)
    last = None
    pending = []
    for line in open(path, errors='replace'):
        m = FRAME.search(line)
        if m:
            f = int(m.group(1))
            for p in pending: print('   %s (frame bracket %s..%d)' % (p, last, f))
            pending = []
            last = f
        if KEEP.match(line):
            fields = dict(kv.split('=', 1) for kv in line.split() if '=' in kv)
            want = ('frame', 'reason', 'ship', 'old', 'new', 'ret_name', 'ready_sector', 'frames_since_arm',
                    'generation_to', 'ship_id_from', 'ship_id_to', 'vanilla', 'frames')
            row = ' '.join(line.split()[:2]) + ' ' + ' '.join('%s=%s' % (k, fields[k]) for k in want if k in fields)
            if line.startswith('music_alert'): pending.append(row)
            else: print('  ', row)
    for p in pending: print('   %s (frame bracket %s..end)' % (p, last))
