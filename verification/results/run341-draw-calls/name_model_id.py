#!/usr/bin/env python3
"""Name a render model id (node +0x140, `model=` in cull_census / object_context / motion_route rows).

Body-table ids (< 50000: fixed 0..999, 9000..19999; dynamic 20000 + registration order) are what the census
already names (`body=`). A scene-embedded body gets id = local + (cut_id - 1) * 100000 (EXE 0x004920f1..0x00492105
text scenes, 0x00491521..0x00491534 binary CUT1 scenes), where cut_id is the types/CutData row and local is the
scene's `P n; B <local>` value (100000 + n for inline `L { ... }` bodies). The body table has no slot for such an
id, so the census prints `body=-`. This script resolves those ids offline from the installed catalogues
(read-only; nothing is copied) and prints the inline body's LOD ladder via tools/analysis/bob1.py.

Usage: name_model_id.py <hex id> [...]
       name_model_id.py --log <session.log> --frame <n>   (every body=- model id of that frame's census)
Assumes 100000 <= local < 200000 (true for every embedded body in dockCarrier_scene / _quicklaunch_scene)."""
import argparse, re, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/analysis'))
from inspect_x3 import read_catalogue          # noqa: E402
from sector_fog_census import unpack           # noqa: E402
import bob1                                    # noqa: E402

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'


def catalogue_members():
    """Highest slot first, addon before base (the resolver's order; loose files are not scanned here)."""
    cats = sorted(GAME.glob('addon/[0-9][0-9].cat'), reverse=True) + sorted(GAME.glob('[0-9][0-9].cat'), reverse=True)
    for cat in cats:
        for e in read_catalogue(cat):
            yield cat, e


def member(cat, e):
    with open(cat.with_suffix('.dat'), 'rb') as f:
        f.seek(e['offset'])
        return unpack(bytes(v ^ 0x33 for v in f.read(e['size'])))


def find(stem_lower_list):
    """First (highest-precedence) member for each wanted path stem (lowercase, '/' separated, no extension)."""
    want, out = set(stem_lower_list), {}
    for cat, e in catalogue_members():
        p = e['path'].lower().removeprefix('addon/')
        stem, _, ext = p.rpartition('.')
        if stem in want and stem not in out and ext in ('pbb', 'bob', 'pbd', 'bod', 'pck', 'txt'):
            out[stem] = (cat, e)
    return out


def cut_table():
    hit = find(['types/cutdata'])['types/cutdata']
    rows = {}
    for line in member(*hit).decode('latin1').splitlines()[3:]:
        f = [x.strip() for x in line.split(';')]
        if f and f[0].lstrip('-').isdigit():
            rows[int(f[0])] = f[1] if len(f) > 1 and f[1] else f'cut\\{int(f[0]):05d}'
    return rows


def resolve(ids):
    cuts = cut_table()
    for mid in ids:
        if mid < 50000:
            print(f'{mid:08x} ({mid}): body-table id; the census body= field names it'); continue
        cut, local = mid // 100000, 100000 + mid % 100000
        scene = cuts.get(cut)
        if scene is None:
            print(f'{mid:08x} ({mid}): no CutData row {cut}; not a scene-embedded id under this rule'); continue
        stem = 'objects/' + scene.replace('\\', '/').lower()
        hit = find([stem]).get(stem)
        if hit is None:
            print(f'{mid:08x}: cut {cut} = {scene}, scene member not found'); continue
        lines = member(*hit).decode('latin1').split('\n')
        pat = re.compile(rf'^P\s+(\d+)\s*;\s*B\s+{local}\s*;.*?N\s+([^;]+);')
        for i, line in enumerate(lines):
            m = pat.match(line)
            if not m: continue
            head = f'{mid:08x} ({mid}): cut {cut} = {scene} [{hit[0].relative_to(GAME)}:{hit[1]["path"]}] P {m[1]} B {local} N {m[2]}'
            if i + 1 < len(lines) and lines[i + 1].startswith('L {'):
                end = next(j for j in range(i + 2, len(lines)) if lines[j].startswith('} / end of body data'))
                tree = bob1.parse_text('\n'.join(lines[i + 2:end]).encode('latin1'))
                print(head + ' inline body:'); bob1.format_ladder(tree)
            else:
                print(head + ' (references a body-table body, not inline)')
            break
        else:
            print(f'{mid:08x}: cut {cut} = {scene}: no P line with B {local}')


def census_unnamed(log, frame):
    tag, ids = f' frame={frame} ', set()
    with open(log, errors='replace') as fh:
        for line in fh:
            if line.startswith('cull_census ') and tag in line and ' body=- ' in line + ' ':
                ids.add(int(re.search(r' model=([0-9a-f]+)', line)[1], 16))
    return sorted(ids)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('ids', nargs='*')
    ap.add_argument('--log'); ap.add_argument('--frame')
    a = ap.parse_args()
    ids = [int(x, 16) for x in a.ids] + (census_unnamed(a.log, a.frame) if a.log else [])
    resolve(ids)
