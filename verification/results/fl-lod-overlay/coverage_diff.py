#!/usr/bin/env python3
"""Parts of the four foggreenoutlands sectors' stations (overlay_sectors.py method) that the installed AP overlay
(C:\\X3 addon/13..15) covers and the FL overlay (OUT/addon2/NN.cat) does not, and the reverse, with the FL batch
record's refusal or filter for each; draws are summed per distinct part body over station instances.
Read-only; counts and body names only.   usage: coverage_diff.py OUT"""
import collections
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import bob1  # noqa: E402
import catalogue_mounts  # noqa: E402
from sector_fog_census import Assets  # noqa: E402
PART = re.compile(rb'^P (\d+); B ([^;]+);', re.M)
R, AP = Path('/mnt/c/X3FL'), Path('/mnt/c/X3')
OUT = Path(sys.argv[1])
cats = [p.relative_to(R).as_posix() for p in catalogue_mounts.mounted_catalogues(R, 'fl')]
fl = Assets(R, catalogues=cats)
ocats = sorted((OUT / 'addon2').glob('[0-9][0-9].cat'))
flo = Assets(R, catalogues=cats, mods=ocats)
apo = Assets(AP, edition='ap')
record = {b['name'].lower(): b for b in json.loads((OUT / 'x3m-lod-batch.json').read_text())['bodies']}


def coarse(a, stem, ok):
    try:
        e = bob1.resolve_body(a, stem)
        if not ok(e['source']):
            return None
        data = a.read_entry(e)
        tree = bob1.parse(data, None) if data[:4] == b'BOB1' else bob1.parse_text(data)
        ls = bob1.lods(tree)
        return [sum(len(p['groups']) for p in l['parts']) for l in ls] if len(ls) > 1 else None
    except Exception:
        return None


def table(name):
    data = fl.read_entry(fl.entries['addon2/types/' + name + '.txt'][-1]).decode('latin1')
    lines = [l for l in data.splitlines() if l.strip() and not l.lstrip().startswith('//')]
    rows = [l.split(';') for l in lines[1:int(lines[0].split(';')[1]) + 1]]
    out = {str(i): r for i, r in enumerate(rows)}
    out.update({r[-2].strip(): r for r in rows})
    return out


tables = {'5': table('tdocks'), '6': table('tfactories')}
uni = ET.fromstring(fl.get('addon2/maps/x3_universe.xml')[0])
uses = collections.Counter()
for sec in uni.iter('o'):
    bg = [c for c in sec.findall('o') if c.get('t') == '2'] if sec.get('t') == '1' else []
    if not bg or bg[0].get('s') != '14':
        continue
    for o in sec.findall('o'):
        row = tables.get(o.get('t'), {}).get(o.get('s'))
        if row is None:
            continue
        stem = bob1.body_stem(row[11].strip())
        sc = next((fl.get(stem + ext)[0] for ext in ('.bod', '.bob') if fl.candidates(stem + ext)), None)
        for m in PART.finditer(sc or b''):
            uses[bob1.body_stem(m.group(2).decode('latin1').strip())] += 1
osrc = {c.as_posix() for c in ocats}
for label, a, b in (('AP covers, FL not', apo, flo), ('FL covers, AP not', flo, apo)):
    print(f'== {label}')
    for stem, n in sorted(uses.items()):
        ca = coarse(a, stem, (lambda s: re.fullmatch(r'addon/1[3-5]\.cat', s)) if a is apo else (lambda s: s in osrc))
        cb = coarse(b, stem, (lambda s: re.fullmatch(r'addon/1[3-5]\.cat', s)) if b is apo else (lambda s: s in osrc))
        if ca and not cb:
            rec = record.get(stem.lower().removeprefix('objects/'), {})
            print(f'{stem.removeprefix("objects/")} x{n}: draws {ca[0]}->{ca[1]};'
                  f' FL batch refuse={rec.get("refuse")} filter={rec.get("filter")}')
