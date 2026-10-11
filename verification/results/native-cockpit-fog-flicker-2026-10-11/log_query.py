#!/usr/bin/env python3
"""Native Windows run 2026-10-11 (x3m.log session 20261011-195134-28476): cockpit-vs-chase fog evidence.
Usage: python3 log_query.py /mnt/c/X3/x3m.log
Prints: chase_camera frame-frames offsets and verdict tuples (internal view would raise refused/verdict=1),
volumetric_fog_frame reason histogram (a reason change always writes a row), and card refusal transitions."""
import re, sys, collections
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
off, verd, fog = collections.Counter(), collections.Counter(), collections.Counter()
prev = None
for n, l in enumerate(open(sys.argv[1], errors='replace'), 1):
    if l.startswith('chase_camera frame='):
        d = kv(l)
        if d['frames'] != '0':
            off[int(d['frame']) - int(d['frames'])] += 1
            verd[(d['verdict'], d['refused'], d['mode'], d['connect'])] += 1
    elif l.startswith('volumetric_fog_frame'):
        d = kv(l); fog[(d['reason'], d['shadow_maps'], d['calls'])] += 1
        if d['reason'] != 'ok': print('fog', n, d['frame'], d['reason'])
    elif l.startswith('volumetric_fog_cards '):
        d = kv(l); key = (d['refused_total'], d['mode'], d['refusal'])
        if key != prev: print('cards', n, d['frame'], key)
        prev = key
print('chase frame-frames offsets', dict(off)); print('chase (verdict,refused,mode,connect)', dict(verd))
print('fog (reason,shadow_maps,calls)', dict(fog))
