#!/usr/bin/env python3
"""FL universe (addon2/maps/x3_universe): sectors whose background (o t=2, attribute s) is TBackgrounds row
INDEX (default 14 = the run-19 'foggreenoutlands' row, sector_background index=14), and their object counts by t code.
Read-only; prints counts only.  usage: fog_sectors.py [INDEX]"""
import collections, re, sys
import xml.etree.ElementTree as ET
from pathlib import Path
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import catalogue_mounts  # noqa: E402
from sector_fog_census import Assets  # noqa: E402
R = Path('/mnt/c/X3FL')
a = Assets(R, catalogues=[p.relative_to(R).as_posix() for p in catalogue_mounts.mounted_catalogues(R, 'fl')])
idx = sys.argv[1] if len(sys.argv) > 1 else '14'
data, prov = a.get('addon2/maps/x3_universe.xml')
root = ET.fromstring(data)
print('source', prov['source'], prov['member'])
for sec in root.iter('o'):
    if sec.get('t') != '1':
        continue
    bg = [c for c in sec.findall('o') if c.get('t') == '2']
    if not bg or bg[0].get('s') != idx:
        continue
    cnt = collections.Counter(c.get('t') for c in sec.findall('o'))
    print(f"sector x={sec.get('x')} y={sec.get('y')} r={sec.get('r')} f={sec.get('f')} objects={sum(cnt.values())} by_t={dict(sorted(cnt.items(), key=lambda kv: int(kv[0])))}")
