#!/usr/bin/env python3
"""Run 77 A: for every overlay body (slot 05/06 markers) appearing in cull_census rows, the census
ladder (lods, thr) and verdict counts vs the marker thresholds. Usage: census_ladders.py LOG ADDON_DIR"""
import re, sys, json, collections
log, addon = sys.argv[1], sys.argv[2]
mk = {}
for s in ('05', '06'):
    for b in json.load(open(f'{addon}/{s}.x3m-lod.json'))['bodies']:
        mk[b['name'].lower()] = (s, b['thresholds'], b['source_thresholds'])
CEN = re.compile(r'^cull_census device=\d+ frame=(\d+) .*? lod=(-?\d+) verdict=(\w+)(?: scope=\w+)?(?: lods=(\S+) thr=(\S+)(?: body=(\S+))?)?')
rows = collections.defaultdict(collections.Counter)
for line in open(log, errors='replace'):
    if not line.startswith('cull_census '): continue
    m = CEN.match(line)
    if not m or not m.group(6): continue
    n = m.group(6).lower().replace('\\', '/')
    if n in mk: rows[n][(m.group(3), m.group(4), m.group(5), m.group(2))] += 1
for n, c in sorted(rows.items()):
    s, thr, src = mk[n]
    print(f'slot{s} {n} marker_thr={thr} source_thr={src}')
    for (v, lods, t, lod), k in sorted(c.items()): print(f'   {k:4d} verdict={v} lod={lod} lods={lods} thr={t}')
