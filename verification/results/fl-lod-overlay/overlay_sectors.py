#!/usr/bin/env python3
"""Inferred station draw load of the four FL sectors with TBackgrounds row 14 (foggreenoutlands) under the FL
merged-LOD overlay baked by lod_overlay.py --batch (OUT/addon2/NN.cat), by the method of
../fl-run18-triage/draw_census/station_sectors.py: map stations (o t=5 dock / t=6 factory, s = TDocks/TFactories
row) -> scene (field 11) -> parts (P n; B body) -> ladder in the FL view (stock LOD0 draws and the coarsest record
drawable at Very High) and, for a part the overlay holds, the overlay's merged record 1 draws (else its LOD0
draws). Groups summed over every part, hidden parts included, as in station_sectors.py; a part drawn at another
LOD, culled or hidden is not modelled. Read-only; counts only.
usage: overlay_sectors.py OUT [INDEX]"""
import collections
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import bob1  # noqa: E402
import catalogue_mounts  # noqa: E402
from sector_fog_census import Assets  # noqa: E402
PART = re.compile(rb'^P (\d+); B ([^;]+);([^\n]*)$', re.M)
R = Path('/mnt/c/X3FL')
OUT = Path(sys.argv[1])
idx = sys.argv[2] if len(sys.argv) > 2 else '14'
cats = [p.relative_to(R).as_posix() for p in catalogue_mounts.mounted_catalogues(R, 'fl')]
fl = Assets(R, catalogues=cats)
overlay_cats = sorted((OUT / 'addon2').glob('[0-9][0-9].cat'))
over = Assets(R, catalogues=cats, mods=overlay_cats)          # the overlay as the top layer
overlay_sources = {c.as_posix() for c in overlay_cats}
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
        r = dict(src=e['source'], thr=[l['value'] for l in ls[1:]],
                 draws=[sum(len(p['groups']) for p in l['parts']) for l in ls])
    except Exception:
        pass
    cache[key] = r
    return r


def table(name):
    data = fl.read_entry(fl.entries['addon2/types/' + name + '.txt'][-1]).decode('latin1')
    lines = [l for l in data.splitlines() if l.strip() and not l.lstrip().startswith('//')]
    rows = [l.split(';') for l in lines[1:int(lines[0].split(';')[1]) + 1]]
    out = {str(i): r for i, r in enumerate(rows)}
    out.update({r[-2].strip(): r for r in rows})
    return out


tables = {'5': table('tdocks'), '6': table('tfactories')}
uni = ET.fromstring(fl.get('addon2/maps/x3_universe.xml')[0])
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
        stem = bob1.body_stem(row[11].strip())
        sc = None
        for ext in ('.bod', '.bob'):
            if fl.candidates(stem + ext):
                sc = fl.get(stem + ext)[0]
                break
        c['stations'] += 1
        c['terran_stations'] += row[13].strip() == '18'
        if sc is None:
            c['scene_missing'] += 1
            continue
        for m in PART.finditer(sc):
            body = bob1.body_stem(m.group(2).decode('latin1').strip())
            r = ladder(fl, body)
            c['parts'] += 1
            if not r:
                c['parts_unread'] += 1
                continue
            d0 = r['draws'][0]
            drawable = bob1.drawable(r['thr']) if r['thr'] else [0]
            c['lod0_draws'] += d0
            c['coarsest_stock_draws'] += min(r['draws'][k] for k in drawable)
            ro = ladder(over, body)
            covered = bool(ro) and ro['src'] in overlay_sources and len(ro['draws']) > 1
            c['overlay_coarse_draws'] += ro['draws'][1] if covered else d0
            c['parts_overlay_covered'] += covered
            c['parts_single_lod'] += not r['thr']
    print(f"sector x={sec.get('x')} y={sec.get('y')} " + ' '.join(f'{k}={v}' for k, v in sorted(c.items())))
    tot += c
print('total ' + ' '.join(f'{k}={v}' for k, v in sorted(tot.items())))
print(f"overlay {', '.join(c.name for c in overlay_cats)}: station LOD0 draws {tot['lod0_draws']} -> overlay coarse"
      f" {tot['overlay_coarse_draws']} ({tot['lod0_draws'] - tot['overlay_coarse_draws']} fewer,"
      f" {100 * (1 - tot['overlay_coarse_draws'] / max(1, tot['lod0_draws'])):.0f} %); stock coarsest"
      f" {tot['coarsest_stock_draws']}")
