#!/usr/bin/env python3
"""FL runs 11-13 log: frames whose frame_end dt_ms exceeds 200 ms (transit/load stalls), streamed; prints rows only.

usage: run13_stall.py [LOG]   (default /mnt/c/X3FL/x3m.log, the 2026-11-01 runs 11-13 session)
"""
import sys

path = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3FL/x3m.log'
with open(path, errors='replace') as f:
    for line in f:
        if line.startswith('frame_end ') or line.startswith('volumetric_fog_handover '):
            kv = dict(p.split('=', 1) for p in line.split()[1:] if '=' in p)
            if line.startswith('volumetric_fog_handover '):
                print('handover', {k: kv.get(k) for k in ('frame', 'arm_frame', 'frames', 'ms', 'fill_ms', 'coldfill')})
            elif int(kv.get('dt_ms', '0')) > 200:
                print('stall', {k: kv.get(k) for k in ('frame', 'elapsed_ms', 'dt_ms')})
