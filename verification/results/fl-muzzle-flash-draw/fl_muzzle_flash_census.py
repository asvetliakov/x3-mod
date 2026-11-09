"""FL muzzle-flash body census (owning note: docs/reverse-engineering/fl/muzzle-flash-draw.md).

Reads the FL installation's catalogues (and the AP root for the twin): the TBullets muzzle column (21), the Effects
rows it names, the CUT scenes those rows use, the scene part bodies, their MATERIAL3 texture ids and the Materials
row flags of those ids. Then it lists every other Effects element and TBullets/TShips column that reaches the same
scenes or bodies, so a re-shade keyed on the body id knows what else it hits. Prints derived facts only (ids, counts,
flags, UV rectangles); no copyrighted bytes are written.

    python3 -I verification/results/fl-muzzle-flash-draw/fl_muzzle_flash_census.py [--game /mnt/c/X3FL] [--ap /mnt/c/X3AP-orig]
"""
import argparse
import collections
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import catalogue_mounts  # noqa: E402
from sector_fog_census import Assets  # noqa: E402

MPF = {1: 'ALPHATEST', 2: 'DESTINATIONBLEND', 4: 'ALPHABLEND', 0x10: '2SIDED', 0x40: 'MULTIPLY2X', 0x80: 'MULTIPLY',
       0x100: 'NOFILTERING', 0x400: 'SRCCOLOR', 0x8000: 'BESTQUALITY'}


def mount(root, edition):
    root = pathlib.Path(root)
    cats = [p.relative_to(root).as_posix() for p in catalogue_mounts.mounted_catalogues(root, edition)]
    return Assets(root, catalogues=cats)


def text(a, key):
    hits = a.entries.get(key.lower())
    if not hits:
        return None, None
    return a.read_entry(hits[-1]).decode('latin1'), hits[-1]['source']


def type_rows(a, exp, name):
    data, src = text(a, f'{exp}/types/{name}.txt')
    if data is None:
        data, src = text(a, f'types/{name}.txt')
    lines = [l.split('//')[0].strip() for l in data.splitlines()]
    lines = [l for l in lines if l and not l.startswith('/')]
    return [l.split(';') for l in lines[1:]], src


def _num(v):
    try:
        return int(v, 0)
    except ValueError:
        return -2  # a named scene or body (no numeric id)


def effects(a, exp):
    data, src = text(a, f'{exp}/types/effects.txt')
    if data is None:
        data, src = text(a, 'types/effects.txt')
    lines = [l.split('//')[0].strip() for l in data.splitlines()]
    lines = [l for l in lines if l and not l.startswith('/')]
    rows, i = {}, 1
    while i < len(lines):
        m = re.match(r'^(\d+);\s*(\d+);\s*([A-Z_|0-9]+);', lines[i])
        if not m:
            i += 1
            continue
        rid, n = int(m.group(1)), int(m.group(2))
        els = []
        for k in range(n):
            flags = lines[i + 1 + 2 * k].split(';')
            vals = [v.strip() for v in lines[i + 2 + 2 * k].split(';')]
            els.append(dict(eedf=flags[0].strip(), i3df=flags[1].strip(), body=_num(vals[0]), size_a=vals[1],
                            size_b=vals[2], timeout=vals[3], offset=tuple(vals[5:8]), start=vals[11] if len(vals) > 11 else '?'))
        rows[rid] = dict(edf=m.group(3), elements=els)
        i += 1 + 2 * n
    return rows, src


def scene_bodies(a, sid):
    data, src = text(a, 'objects/cut/%05d.bod' % sid)
    if data is None:
        return None, None, None
    parts = [int(b) for b in re.findall(r'^\s*P\s*\d+\s*;\s*B\s*(\d+)\s*;', data, re.M)]
    keys = re.findall(r'\{\s*(\d+);([^}]*)\}', data)
    return parts, len(keys), src


def body(a, bid):
    data, src = text(a, 'objects/v/%05d.bod' % bid)
    if data is None:
        return None
    mats = re.findall(r'^MATERIAL(\d):\s*(\d+);\s*(\d+);', data, re.M)
    coords = data.split('-1;  -1;   -1;')[0] if '-1;  -1;   -1;' in data else ''
    nverts = len(re.findall(r'^\s*-?\d+;\s*-?\d+;\s*-?\d+;\s*/\d+', coords, re.M))
    faces = re.findall(r'^\s*(\d+);\s*(\d+);\s*(\d+);\s*(\d+);\s*-?\d+;\s*1;\s*([-\d.;\s]+?)/\((\d+)\)', data, re.M)
    uv = [float(x) for f in faces for x in f[4].replace(';', ' ').split()]
    us, vs = uv[0::2], uv[1::2]
    part = re.findall(r'Part\s+\d+:[^"]*"([^"]*)"', data)
    return dict(src=src, materials=[(int(t), int(i), int(tex)) for t, i, tex in mats], vertices=nverts,
                uv_faces=len(faces), part_names=part,
                uv_rect=(round(min(us), 3), round(min(vs), 3), round(max(us), 3), round(max(vs), 3)) if us else None)


def materials_row(a, exp, tex):
    rows, src = type_rows(a, exp, 'materials')
    r = rows[tex]
    return r[12].strip(), r[15].strip(), src


def run(game, edition, exp, label):
    a = mount(game, edition)
    tb, tsrc = type_rows(a, exp, 'TBullets')
    eff, esrc = effects(a, exp)
    print(f'== {label}: TBullets {tsrc} ({len(tb)} rows), Effects {esrc} ({len(eff)} rows)')
    muzzle_rows = collections.OrderedDict()
    for i, r in enumerate(tb):
        m = int(r[21])
        if m:
            muzzle_rows.setdefault(m, []).append(f'{i}:{r[48].strip()}')
    scenes, bodies_of = {}, {}
    for rid, users in muzzle_rows.items():
        row = eff.get(rid)
        if row is None:
            print(f'muzzle row {rid}: missing ({users})')
            continue
        desc = []
        for e in row['elements']:
            kind = 'CUT' if 'EEDF_CUT' in e['eedf'] else 'LIGHT' if 'EEDF_LIGHT' in e['eedf'] else \
                'EMPTY' if e['body'] <= 0 else 'BODY'
            desc.append(f"{kind} {e['body']} [{e['eedf'].replace('EEDF_', '')}|{e['i3df']}] size {e['size_a']}..{e['size_b']}"
                        f" start {e['start']} ms, timeout {e['timeout']} ms, offset {'/'.join(e['offset'])}")
            if kind == 'CUT':
                scenes.setdefault(e['body'], set()).add(rid)
        print(f'muzzle row {rid} ({row["edf"]}) <- {", ".join(users)}')
        for d in desc:
            print(f'    {d}')
    print('scenes used by muzzle rows:')
    for sid in sorted(scenes):
        parts, nkeys, src = scene_bodies(a, sid)
        bodies_of[sid] = parts
        print(f'  scene {sid:05d} ({src}): part bodies {parts}, {nkeys} keys; muzzle rows {sorted(scenes[sid])}')
    seen = sorted({b for p in bodies_of.values() if p for b in p})
    for b in seen:
        info = body(a, b)
        mats = []
        for t, idx, tex in info['materials']:
            fl, flags, msrc = materials_row(a, exp, tex)
            bits = 0
            for tok in flags.split('|'):
                for k, v in MPF.items():
                    if tok.strip() == 'MPF_' + v:
                        bits |= k
            mats.append(f'MATERIAL{t} tex {tex} -> Materials row {tex} flags {flags} = {bits:#x}')
        print(f'  body {b:05d} ({info["src"]}) part {info["part_names"]}: {info["vertices"]} vertices, '
              f'{info["uv_faces"]} textured faces, UV rect {info["uv_rect"]}; ' + '; '.join(mats))
    # who else reaches these scenes or bodies
    print('every Effects element reaching a muzzle scene or its bodies:')
    target_scenes, target_bodies = set(scenes), set(seen)
    reach = collections.defaultdict(list)
    for rid, row in sorted(eff.items()):
        for k, e in enumerate(row['elements']):
            cut = 'EEDF_CUT' in e['eedf']
            if cut and e['body'] in target_scenes:
                reach[rid].append(f'el{k} CUT {e["body"]:05d}')
            elif cut:
                parts, _, _ = scene_bodies(a, e['body'])
                if parts and target_bodies & set(parts):
                    reach[rid].append(f'el{k} CUT {e["body"]:05d} (parts {parts})')
            elif not 'EEDF_LIGHT' in e['eedf'] and e['body'] in target_bodies:
                reach[rid].append(f'el{k} BODY {e["body"]}')
    cols = {19: 'flight', 20: 'impact', 21: 'muzzle', 23: 'col23'}
    for rid, items in reach.items():
        users = [f'{cols[c]}:{i}' for i, r in enumerate(tb) for c in cols if len(r) > c and r[c].strip() == str(rid)]
        print(f'  row {rid}: {", ".join(items)}; TBullets users {users or "none"}')
    ts, _ = type_rows(a, exp, 'TShips')
    reached = set(reach)
    ship_hits = collections.Counter(r[47].strip() for r in ts if len(r) > 47 and r[47].strip().isdigit() and int(r[47]) in reached)
    print(f'  TShips death col 47 hitting those rows: {dict(ship_hits) or "none"}')
    # bodies: does any scene other than the muzzle scenes use these bodies?
    scene_users = collections.defaultdict(list)
    for key in a.entries:
        m = re.fullmatch(r'objects/cut/(\d+)\.bod', key)
        if m:
            parts, _, _ = scene_bodies(a, int(m.group(1)))
            for b in parts or ():
                if b in target_bodies:
                    scene_users[b].append(int(m.group(1)))
    for b in sorted(scene_users):
        print(f'  scenes using body {b:05d}: {sorted(scene_users[b])}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', default='/mnt/c/X3FL')
    ap.add_argument('--ap', default='/mnt/c/X3AP-orig')
    o = ap.parse_args()
    run(o.game, 'fl', 'addon2', 'FL')
    if pathlib.Path(o.ap).exists():
        run(o.ap, 'ap', 'addon', 'AP')


if __name__ == '__main__':
    main()
