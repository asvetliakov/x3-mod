"""Negative material texture ids -> types/Animations rows, by the X3AP.exe grammar.

Static rule (docs/reverse-engineering/texture-lookup.md section 10): a material texture name
'-N...' becomes id -N at 0x004f4cb0; 0x00487810 turns a negative material id into an animation
instance of row N of types/Animations (table 0x00608db8, stride 0x44, loaded by 0x004f5460), and
0x004c0150 draws the instance's current frame texture (instance +0x70) instead. This script parses
Animations with the loader's grammar (0x004f5460), prints every row a vanilla body references with
its initial frame texture (0x004f5b60) and every frame name, and resolves each frame name through the
engine's texture chain (0x004f4cb0 -> 0x004f4160 -> 0x004f3510). It also lists the shipped
dds/-N.pck members. Reads bottle X3 read-only (our overlay slots skipped); writes nothing.

    PYTHONPATH=tools/analysis python3 verification/results/texture-lookup-animations/animation_rows.py \
        > verification/results/texture-lookup-animations/animation_rows_out.txt
"""
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
sys.path.insert(0, str(ROOT / 'verification' / 'results' / 'lod-overlay-batch'))

import bob1  # noqa: E402
import body_materials  # noqa: E402
import lod_overlay  # noqa: E402
from sector_fog_census import unpack  # noqa: E402
import texture_lookup_rows as tlr  # noqa: E402

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
TAT = dict(TAT_LOOP=1, TAT_PINGPONG=2, TAT_ONESHOT=3, TAT_MOVIE=4, TAT_TAGLOOP=5, TAT_TAGPINGPONG=6,
           TAT_TAGONESHOT=7, TAT_TAGCOLLECTION=8, TAT_TAGONESHOT_REINIT=9, TAT_SINGLESTEP=10,
           TAT_TAGSINGLESTEP=11, TAT_TAGARRAYSINGLESTEP=12)          # name table 0x0054de40
FLAG = dict(NULL=0, TADF_COORDS=2, TATF_REINITLOOP=1, TATF_COORDS=2)  # 0x0054de28 / 0x0054dea8


def tokens(text):
    out = []
    for line in text.splitlines():
        line = line.split('//', 1)[0]
        out += [t.strip() for t in line.split(';')]
    return [t for t in out if t != '']


def flags(tok):
    v = 0
    for part in tok.split('|'):
        v |= FLAG[part.strip()]
    return v


def parse(text):
    """Rows as 0x004f5460 reads them: type, flags(+4), name(+6), name(+8), [4 floats], then
    TAT_MOVIE: 7 ints; else count(+0x1c), entries(+0x20), total duration(+0x24)."""
    t = iter(tokens(text))
    count = int(next(t))
    rows = []
    for _ in range(count):
        typ = TAT[next(t)]
        fl = flags(next(t))
        row = dict(type=typ, flags=fl, first=next(t), second=next(t), frames=[])
        if fl & 2:
            row['coords'] = [float(next(t)) for _ in range(4)]
        if typ == 4:
            row['movie'] = [int(next(t)) for _ in range(7)]
        else:
            n = int(next(t))
            for _ in range(n):
                if typ == 8:
                    name = next(t)
                    [next(t) for _ in range(6)]
                elif typ == 11:
                    name = next(t)
                else:
                    ef = flags(next(t))
                    name = next(t)
                    next(t)                                   # duration
                    if ef & 2:
                        next(t), next(t)                      # coords
                row['frames'].append(name)
            row['total'] = int(next(t))
        rows.append(row)
    rest = list(t)
    return rows, rest


def initial_name(row):
    """0x004f5b60: instance +0x70 = row +6 when the row has no frame list or is type 8/11,
    else the first frame's texture."""
    if not row['frames'] or row['type'] in (8, 11):
        return row['first']
    return row['frames'][0]


def resolve(assets, mat_rows, name):
    """Engine chain for a texture name read with flags 0 (0x004f4cb0 -> 0x004f4160 -> 0x004f3510)."""
    s = name
    if s in tlr.NULL_NAMES:
        return 'no texture (null name)'
    if len(s) > 4 and s.rfind('.') == len(s) - 4:
        s = s[:-4]
    m = re.match(r'-?\d+', s) if (s[:1].isdigit() or s[:1] == '-') else None
    if m:
        n = int(m.group())
        if n < 0 or n >= len(mat_rows):
            return f'id {n}: no texture'
        r = mat_rows[n]
        if r['texid'] == 0:
            return f'id {n}: row texture id 0, no texture'
        if r['flags'] & tlr.MPF_GENERATED:
            return f'id {n}: MPF_GENERATED surface'
        path = 'textures\\' + r['name'] if r['name'] else f'tex\\true\\{n}'
    else:
        path = 'textures\\' + s
    return tlr.load_chain(assets, path)


def main():
    assets, skipped = lod_overlay.original_assets(GAME)
    data, info = assets.get('addon/types/Animations.pck')
    rows, rest = parse(unpack(data).decode('latin1'))
    mat_rows, _ = tlr.materials_rows(assets)
    print(f'skipped overlay sources {skipped}')
    print(f'Animations: {info["source"]}:{info["member"]} rows={len(rows)} trailing tokens={len(rest)}')
    # negative names in vanilla bodies, by slot kind
    uses = defaultdict(Counter)
    bodies_by = defaultdict(set)
    check = Counter()
    over = []
    for key, entries in list(assets.entries.items()):
        k = key.removeprefix('addon/')
        if not k.startswith('objects/') or not k.endswith(('.bob', '.bod')):
            continue
        try:
            data = assets.read_entry(entries[-1])
            tree = bob1.parse(data, lod_overlay.MAX_TRAILING) if bob1.kind(data) else bob1.parse_text(data)
        except Exception:
            continue
        finally:
            assets.cache.clear()
        groups = {g['material'] for lod in bob1.lods(tree) for part in lod['parts'] for g in part['groups']}
        neg_groups = {g for g in groups if g < 0}
        diff_ids = {}
        for i, mat in enumerate(bob1.materials(tree)):
            raw = body_materials.slots(mat).get('diffuse' if 'params' in mat else 'texture', b'')
            raw = raw if isinstance(raw, bytes) else str(raw).encode()
            m = re.match(r'-\d+', raw.decode('latin1'))
            if m:
                diff_ids.setdefault(int(m.group()), []).append(i)
        # 0x00487810 order: every face group of every LOD and part, one instance per negative index
        # (and per diffuse/bump of an effect material with TexAnimDuration/Rotation != 0), max 20
        mats = bob1.materials(tree)
        slots = []
        for lod in bob1.lods(tree):
            for part in lod['parts']:
                for g in part['groups']:
                    idx = g['material']
                    if idx < 0:
                        slots.append(-idx)
                    elif idx < len(mats) and 'params' in mats[idx]:
                        pv = {n.lower(): v for n, _t, v in mats[idx]['params']}
                        if any(pv.get(k, [0]) not in ([0], b'', None) for k in (b'texanimduration', b'texanimrotation')):
                            slots += ['uv'] * sum(1 for k in (b't_diffusetexture', b't_bumptexture')
                                                  if pv.get(k) not in (None, b'', b'NULL', b'0'))
        check['max instance slots in one body'] = max(check['max instance slots in one body'], len(slots))
        lost = {n for n in neg_groups if -n not in slots[:20]}
        if lost:
            check['negative groups beyond the 20-instance limit (not drawn)'] += len(lost)
            over.append((k, len(slots), sorted(lost)))
        for g in neg_groups:
            check['negative group'] += 1
            check['with a material whose diffuse id matches' if g in diff_ids else
                  'no material diffuse id matches (engine binds material 0)'] += 1
        for n, idx in diff_ids.items():
            if n not in neg_groups:
                check['negative-diffuse material never used by a negative group'] += 1
            if any(i in groups for i in idx):
                check['negative-diffuse material also used by a non-negative group'] += 1
        for mat in bob1.materials(tree):
            effect = 'params' in mat
            for slot, raw in body_materials.slots(mat).items():
                raw = raw if isinstance(raw, bytes) else str(raw).encode()
                s = raw.decode('latin1')
                if len(s) > 4 and s.rfind('.') == len(s) - 4:
                    s = s[:-4]
                m = re.match(r'-\d+', s)
                if not m:
                    continue
                n = -int(m.group())
                uses[n][('effect ' if effect else 'classic ') + slot] += 1
                bodies_by[n].add(k)
    print('negative ids in vanilla bodies (row: slot uses; bodies):')
    for n in sorted(uses):
        print(f'  -{n}: {dict(uses[n])}; bodies {len(bodies_by[n])}, e.g. {sorted(bodies_by[n])[:4]}')
    for key in ('negative-diffuse material also used by a non-negative group',
                'negative groups beyond the 20-instance limit (not drawn)'):
        check.setdefault(key, 0)
    print(f'group/material consistency: {dict(sorted(check.items()))}')
    for k, n, lost in over[:10]:
        print(f'  over the limit: {k} slots={n} lost rows={lost}')
    print('rows referenced:')
    for n in sorted(uses):
        if n >= len(rows):
            print(f'  row {n}: outside the {len(rows)} rows')
            continue
        r = rows[n]
        typ = next(k for k, v in TAT.items() if v == r['type'])
        ini = initial_name(r)
        print(f'  row {n}: {typ} first={r["first"]!r} second={r["second"]!r} frames={len(r["frames"])} '
              f'distinct={len(set(r["frames"]))}')
        print(f'    initial frame {ini!r}: {resolve(assets, mat_rows, ini)}')
        for f in sorted(set(r['frames']) - {ini})[:6]:
            print(f'    frame {f!r}: {resolve(assets, mat_rows, f)}')
    neg = sorted(k for k in assets.entries if re.match(r'(addon/)?dds/-\d+\.(pck|dds)$', k))
    print(f'shipped dds/-N members: {len(neg)}: {neg}')


if __name__ == '__main__':
    main()
