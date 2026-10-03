#!/usr/bin/env python3
"""Run406: dump engine_draw rows of selected frames, body value from engine_bodies.json.
usage: draws.py <session.log> <engine_bodies.json> frame [frame...]"""
import sys, json, re, os
log, bj, frames = sys.argv[1], sys.argv[2], set(sys.argv[3:])
bodies = {k.lower(): v for k, v in json.load(open(bj))['bodies'].items()}
F = 'frame model name z s size origin node handle serial flags verdict'.split()
for line in open(log, errors='replace'):
    if not line.startswith('engine_draw '): continue
    d = dict(t.split('=', 1) for t in line.split() if '=' in t)
    if d['frame'] not in frames: continue
    b = bodies.get(d['name'].lower(), {})
    fl = int(d['flags'], 16)
    tag = ('RCS' if fl & 1 else '') + ('BRAKE' if fl & 2 else '')
    o = [float(x) for x in d['origin'].split(',')]
    print(d['frame'], d['handle'], d['node'], d['model'], d['name'].split('\\')[-1], 'body_value=%s' % b.get('value'),
          'size=%s' % d['size'], 'z=%s' % d['z'], 's=%s' % d['s'], 'flags=%s' % d['flags'], tag or '-', d['verdict'],
          'origin=%.0f,%.0f,%.0f' % tuple(o))
