#!/usr/bin/env python3
"""Summarise merge_census.py output: counts of bodies by cull-differs share and diffuse-off share. Usage: census_summary.py OUT.txt"""
import re, sys
rows = []; other = {}
for l in open(sys.argv[1]):
    m = re.search(r'^(\S+) .*cull differs ([\d.]+)%; diffuse range ([\d.]+)\.\.([\d.]+); faces >25% off the merged diffuse ([\d.]+)%', l)
    if m: rows.append((m.group(1), float(m.group(2)), float(m.group(5)), float(m.group(3)), float(m.group(4)))); continue
    k = 'error' if ': error' in l else 'no_atlas' if 'no atlas' in l else 'single' if 'single record' in l else 'other'
    other[k] = other.get(k, 0) + 1
print('bodies with an atlas material', len(rows), 'others', other)
for t in (1, 10, 20):
    print(f'cull differs > {t}%:', sum(r[1] > t for r in rows))
for t in (10, 25, 50):
    print(f'faces >25% off merged diffuse > {t}%:', sum(r[2] > t for r in rows))
print('both cull>10% and diffuse-off>25%:', sum(r[1] > 10 and r[2] > 25 for r in rows))
print('top 12 by diffuse-off:', [(r[0], r[2], r[1]) for r in sorted(rows, key=lambda r: -r[2])[:12]])
print('stations/sadiablo rows:', [(r[0], r[1], r[2]) for r in rows if 'sadiablo' in r[0]])
