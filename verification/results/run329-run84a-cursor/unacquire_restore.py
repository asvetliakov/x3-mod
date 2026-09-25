#!/usr/bin/env python3
"""Pointer position at each deactivation (first cursor_snapshot with foreground != game) vs the position at the
preceding fire (first snapshot at the fire frame) and the last active snapshot before deactivation.
Tests whether the Win32 pointer jumps back to the acquire-time position on deactivation (dinput exclusive unacquire)."""
import re, sys
KV = re.compile(r'(\w+)=(\S+)'); G = '000a0064'
fire_xy = None; last_active = None; want_fire = None; fg_prev = G
for line in open(sys.argv[1], errors='replace'):
    k = line.split(' ', 1)[0]
    if k == 'cursor_reassert' and 'action=fired' in line:
        want_fire = int(KV.search(line[line.index('frame='):]).group(2))
    elif k == 'cursor_snapshot':
        d = dict(KV.findall(line)); xy = (int(d['x']), int(d['y'])); f = int(d['frame'])
        if want_fire is not None and f >= want_fire: fire_xy = (f, xy); want_fire = None
        if d['foreground'] == G: last_active = (f, xy)
        elif fg_prev == G:
            print(f"deact snap f={f} xy={xy} | last active snap {last_active} | at preceding fire {fire_xy} | equal_to_fire={xy == (fire_xy[1] if fire_xy else None)}")
        fg_prev = d['foreground']
