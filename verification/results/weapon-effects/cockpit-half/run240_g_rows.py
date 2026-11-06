#!/usr/bin/env python3
"""Run240 Flight G (2026-11-06) cockpit half-screen triage: the log rows that decide. Usage: run240_g_rows.py [x3m.log]"""
import collections, sys
path = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3/x3m.log'
OWN = '4a80a238'  # chase_camera first_applied ref= (the own ship)
tot, rows, own = collections.Counter(), 0, collections.Counter()
for line in open(path, errors='replace'):
    if line.startswith('weapon_effects_frame'):
        rows += 1
        d = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
        for k in ('patches', 'bubbles', 'bubble_fallback', 'bubble_clamped', 'faces_culled', 'area_dropped', 'dropped', 'spills'):
            tot[k] += int(float(d.get(k, 0)))
    elif line.startswith('weapon_event') and f'owner={OWN}' in line:
        d = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
        y, dy = int(d['pos'].split(',')[1]), int(d['dir'].split(',')[1])
        era = 'late' if int(d['frame']) >= 60000 else 'early'
        own[(era, 'pos_y<0' if y < 0 else 'pos_y>=0')] += 1
        own[(era, 'dir_y>0' if dy > 0 else 'dir_y<=0')] += 1
    elif line.startswith(('weapon_patch ', 'weapon_hull_frame')):
        own['capture_rows'] += 1
print('weapon_effects_frame rows', rows, dict(tot))
print('own-ship hits', dict(own))
