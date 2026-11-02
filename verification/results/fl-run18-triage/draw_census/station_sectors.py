#!/usr/bin/env python3
"""Inferred station draw load of the FL sectors with TBackgrounds row 14 (foggreenoutlands, the run-19 heavy windows):
map stations (o t=5 dock / t=6 factory, attribute s = TDocks/TFactories row) -> scene (column 11) -> parts (P n; B body)
-> body ladder in the FL view (draws of LOD0, of the coarsest record drawable at Very High) and, for bodies the
installed AP overlay covers (/mnt/c/X3 addon/13..15; FL bodies are byte-identical ladders, lod_census_out.txt), the
overlay's coarse record draws. Sums over parts; a part drawn at another LOD, culled, or hidden is not modelled.
Read-only; counts only.   usage: station_sectors.py [INDEX]"""
import collections, re, sys
import xml.etree.ElementTree as ET
from pathlib import Path
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import bob1  # noqa: E402
import catalogue_mounts  # noqa: E402
from sector_fog_census import Assets  # noqa: E402
PART = re.compile(rb'^P (\d+); B ([^;]+);([^\n]*)$', re.M)
R = Path('/mnt/c/X3FL')
fl = Assets(R, catalogues=[p.relative_to(R).as_posix() for p in catalogue_mounts.mounted_catalogues(R, 'fl')])
over = Assets(Path('/mnt/c/X3'))
idx = sys.argv[1] if len(sys.argv) > 1 else '14'
cache = {}


def ladder(a, stem):
    key = (id(a), stem.lower())
    if key in cache:
        return cache[key]
    r = None
    try:
        e = bob1.resolve_body(a, stem)
        data = a.read_entry(e)
        tree = bob1.parse(data, None) if data[:4] == b'BOB1' else bob1.parse_text(data)
        ls = bob1.lods(tree)
        r = dict(src=e['source'], thr=[l['value'] for l in ls[1:]], draws=[sum(len(p['groups']) for p in l['parts']) for l in ls])
    except Exception:
        pass
    cache[key] = r
    return r


def table(name):
    data = fl.read_entry(fl.entries['addon2/types/' + name + '.txt'][-1]).decode('latin1')
    lines = [l for l in data.splitlines() if l.strip() and not l.lstrip().startswith('//')]
    rows = [l.split(';') for l in lines[1:int(lines[0].split(';')[1]) + 1]]
    out = {str(i): r for i, r in enumerate(rows)}
    out.update({r[-2].strip(): r for r in rows})   # map s is the row's id (SS_...) or its index
    return out


tables = {'5': table('tdocks'), '6': table('tfactories')}
uni = ET.fromstring(fl.get('addon2/maps/x3_universe.txt')[0])
tot = collections.Counter()
for sec in uni.iter('o'):
    if sec.get('t') != '1':
        continue
    bg = [c for c in sec.findall('o') if c.get('t') == '2']
    if not bg or bg[0].get('s') != idx:
        continue
    c = collections.Counter()
    for o in sec.findall('o'):
        t = o.get('t')
        if t not in tables:
            continue
        row = tables[t].get(o.get('s'))
        if row is None:
            c['row_missing'] += 1
            continue
        token = row[11].strip()
        stem = bob1.body_stem(token)
        sc = None
        for ext in ('.bod', '.bob'):
            if fl.candidates(stem + ext):
                sc = fl.get(stem + ext)[0]
                break
        c['stations'] += 1
        if sc is None:
            c['scene_missing'] += 1
            continue
        for m in PART.finditer(sc):
            body = bob1.body_stem(m.group(2).decode('latin1').strip())
            comp = re.search(rb'\bC (\d+)', m.group(3)) is not None
            r = ladder(fl, body)
            c['parts'] += 1
            if comp:
                c['component_parts'] += 1
            if not r:
                c['parts_unread'] += 1
                continue
            d0 = r['draws'][0]
            drawable = bob1.drawable(r['thr']) if r['thr'] else [0]
            dmin = min(r['draws'][k] for k in drawable)
            ro = ladder(over, body)
            ov = ro['draws'][1] if ro and re.fullmatch(r'addon/1[3-5]\.cat', ro['src']) and len(ro['draws']) > 1 else d0
            c['lod0_draws'] += d0
            c['coarsest_stock_draws'] += dmin
            c['overlay_coarse_draws'] += ov
            c['parts_overlay_covered'] += ov != d0 or (ro and ro['src'].startswith('addon/1'))
    print(f"sector x={sec.get('x')} y={sec.get('y')} " + ' '.join(f'{k}={v}' for k, v in c.items()))
    tot += c
print('total ' + ' '.join(f'{k}={v}' for k, v in tot.items()))
