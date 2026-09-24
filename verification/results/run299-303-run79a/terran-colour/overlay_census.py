"""Kept census rows of overlay bodies (addon/05 + 06) per burst frame: frame model s lod T_pad slot body.
Usage: python3 overlay_census.py LOG FRAMES(comma)"""
import sys, re, json
from pathlib import Path
ADDON = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/addon'
slot = {}
for s in ('05', '06'):
    for b in json.load(open(ADDON / f'{s}.x3m-lod.json'))['bodies']:
        slot[b['name'].lower()] = s
frames = set(sys.argv[2].split(','))
seen = set()
for line in open(sys.argv[1], errors='replace'):
    if not line.startswith('cull_census device'):
        continue
    d = dict(re.findall(r'(\w+)=(\S+)', line))
    if d.get('frame') not in frames or d.get('verdict') != 'kept':
        continue
    name = d.get('body', '-').replace('\\', '/').lower()
    if name not in slot:
        continue
    k = (d['frame'], d['node'])
    if k in seen:
        continue
    seen.add(k)
    print(d['frame'], d['model'], d['node'], 's=' + d['s'], 'lod=' + d['lod'], 'thr=' + d['thr'], slot[name], d['body'])
