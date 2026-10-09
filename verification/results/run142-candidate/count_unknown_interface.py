"""Run142 candidate: break down DXVK 'Unknown interface query' warnings in a scene-capture Wine log.

Usage: python3 <this> <scene-capture-wine.log>
Prints the total, the count per reporting class and per queried IID (the GUID is
on the line after each warning). 580ca87e-1d3c-4d54-991d-b7d3e3c298ce is
IID_IDirect3DBaseTexture9 (the describe_surface GetContainer probe).
"""
import collections, json, re, sys

lines = open(sys.argv[1], errors='replace').read().splitlines()
by_class, by_iid = collections.Counter(), collections.Counter()
for i, line in enumerate(lines):
    if 'Unknown interface query' in line:
        m = re.search(r'(\w+)::QueryInterface', line)
        by_class[m.group(1) if m else '?'] += 1
        nxt = lines[i + 1] if i + 1 < len(lines) else ''
        g = re.search(r'[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}', nxt)
        by_iid[g.group(0) if g else '?'] += 1
print(json.dumps({'total': sum(by_class.values()), 'by_class': dict(by_class), 'by_iid': dict(by_iid)}, indent=1))
