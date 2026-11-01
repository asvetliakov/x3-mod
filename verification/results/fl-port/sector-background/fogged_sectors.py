#!/usr/bin/env python3
"""FL sectors the volumetric fog renders (docs/architecture/farnhams-legacy-sector.md, "Where to fly").

FL resolves maps/x3_universe and types/TBackgrounds under addon2 (docs/reverse-engineering/fl/sector-fog.md
section 6.2). Reads both members from addon2's catalogues, maps every sector (map object t=1) to its background
(child t=2, attribute s) and prints one line per sector the fog policy (src/proxy/fog_sector_policy.h) gives a
profile from the compiled table: dust > 0 and the family in src/renderer/fog_field_assets.h family_profiles
(families outside it need <game>/x3m/fog-families.bin). Also lists dust > 0 families without a compiled profile.
Sector names: page 7, id 1020000 + 100*(y+1) + x+1 (as tools/analysis/sector_fog_census.py) over every
0001-L044 text member, base, addon, addon2 in order. Native units are 500 per metre. Derived values only; no
game bytes are written.

usage: fogged_sectors.py [FL root, default /mnt/c/X3FL]
"""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import sector_fog_census as s  # noqa: E402

game = Path(sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3FL')


def read(cat, e):
    with cat.with_suffix('.dat').open('rb') as stream:
        stream.seek(e['offset'])
        return s.unpack(bytes(v ^ 0x33 for v in stream.read(e['size'])))


members = {}
for cat in sorted((game / 'addon2').glob('[0-9][0-9].cat')):
    for e in s.read_catalogue(cat):
        members[e['path'].lower()] = (cat, e)
rows = s.backgrounds(read(*members['addon2/types/tbackgrounds.pck']))
universe = ET.fromstring(read(*members['addon2/maps/x3_universe.pck']))
texts = {}
for folder, prefix in (('', ''), ('addon', 'addon/'), ('addon2', 'addon2/')):
    for cat in sorted((game / folder).glob('[0-9][0-9].cat')):
        for e in s.read_catalogue(cat):
            if e['path'].lower() != prefix + 't/0001-l044.pck':
                continue
            for page in ET.fromstring(read(cat, e)).findall('page'):
                for t in page.findall('t'):
                    texts[int(page.attrib['id']) % 10000, int(t.attrib['id'])] = ''.join(t.itertext())
compiled = set(re.findall(r'\{"(\w+)", Profile::', (ROOT / 'src/renderer/fog_field_assets.h').read_text()))
sectors = []
for o in universe.iter('o'):
    if o.get('t') != '1':
        continue
    x, y = int(o.attrib['x']), int(o.attrib['y'])
    bg = [c for c in o.findall('o') if c.get('t') == '2']
    index = int(bg[0].attrib['s']) if len(bg) == 1 else -1
    name = re.sub(r'\([^)]*\)', '', texts.get((7, 1020000 + 100 * (y + 1) + x + 1), '')).strip()
    sectors.append((x, y, name, index))
dusty = [v for v in sectors if 0 <= v[3] < len(rows) and rows[v[3]]['dust'] > 0]
fogged = [v for v in dusty if rows[v[3]]['family'] in compiled]
print(f"records={len(rows)} sectors={len(sectors)} dust_sectors={len(dusty)} compiled_profile_sectors={len(fogged)} "
      f"names_resolved={sum(1 for v in fogged if v[2])}")
print(f"compiled_families={sorted({rows[v[3]]['family'] for v in fogged})}")
print(f"dust_families_without_compiled_profile={sorted({rows[v[3]]['family'] for v in dusty} - compiled)}")
for x, y, n, i in sorted(fogged, key=lambda v: (v[1], v[0])):
    r = rows[i]
    print(f"{x},{y} \"{n}\" index={i} family={r['family']} near={r['near_native']} far={r['far_native']} dust={r['dust']}")
