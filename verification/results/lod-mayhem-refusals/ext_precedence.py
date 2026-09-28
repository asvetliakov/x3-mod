#!/usr/bin/env python3
"""Q1 census: for every body the bake record refuses as ambiguous_body_ext, list every member
(.pbb/.bob/.pbd/.bod, with or without -Lnnn) across the mounted layers and pick the winner with
the engine rule of body-format-bob1.md section 7 (0x004e7590: loose files first; otherwise the
catalogue slots from the highest down, stopping at the first slot that holds the stem under an
accepted extension; the extension rank "pbb bob pbd bod" applies only inside that slot).
Read-only over the game tree. Usage:
  PYTHONPATH=tools/analysis python3 ext_precedence.py [game_root]  > ext_precedence_out.txt"""
import collections
import json
import re
import sys
from pathlib import Path

from inspect_x3 import read_catalogue

GAME = Path(sys.argv[1] if len(sys.argv) > 1 else
            Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3')
EXTS = ['pbb', 'bob', 'pbd', 'bod']          # 0x00561180, rank = index (0x004e7470)
BINARY = {'pbb', 'bob'}


def mounted(root):
    """Slots in mount order (0x004ec9e0): NN.cat from 01 until the first gap, then addon/NN.cat."""
    slots = []
    for base in (root, root / 'addon'):
        n = 1
        while (base / f'{n:02d}.cat').exists():
            slots.append(base / f'{n:02d}.cat'); n += 1
    return slots


def key(path):
    return path.replace('/', '\\').upper()     # 0x004ec960 / 0x004ed750


def parse_member(name):
    """'OBJECTS\\A\\B-L044.PBB' -> ('OBJECTS\\A\\B', 'pbb', 44) or None."""
    m = re.fullmatch(r'(.*?)(?:-L(\d{3}))?\.(PBB|BOB|PBD|BOD)', name)
    return (m.group(1), m.group(3).lower(), int(m.group(2)) if m.group(2) else None) if m else None


def main():
    record = json.load(open(GAME / 'addon' / 'x3m-lod-batch.json'))
    rows = [b for b in record['bodies'] if 'ambiguous_body_ext' in (b.get('refuse') or [])]
    slots = mounted(GAME)
    overlay = {s for s in slots if (s.with_suffix('.x3m-lod.json')).exists()}
    index = collections.defaultdict(list)       # stem key -> [(slot index, source, ext, lang)]
    for si, cat in enumerate(slots):
        src = cat.relative_to(GAME).as_posix()
        for e in read_catalogue(cat):
            p = parse_member(key(e['path']))
            if p:
                index[p[0]].append((si, src, p[1], p[2]))
    loose = collections.defaultdict(list)
    for f in (GAME / 'objects').rglob('*'):
        if f.is_file():
            p = parse_member(key(f.relative_to(GAME).as_posix()))
            if p:
                loose[p[0]].append(('loose', p[1], p[2]))
    print(f'game {GAME}')
    print('slots (mount order): ' + ' '.join(s.relative_to(GAME).as_posix() for s in slots))
    print('x3m overlay slots: ' + ' '.join(sorted(s.relative_to(GAME).as_posix() for s in overlay)))
    print(f'loose body files under objects/: {sum(len(v) for v in loose.values())}')
    print(f'ambiguous_body_ext rows: {len(rows)} ({dict(collections.Counter(b["cat"] for b in rows))})')

    def winner(stem, skip_overlay):
        lo = loose.get(stem)
        if lo:
            return 'loose', min(lo, key=lambda t: (EXTS.index(t[1]), t[2] is None))
        for si in range(len(slots) - 1, -1, -1):
            if skip_overlay and slots[si] in overlay:
                continue
            here = [t for t in index[stem] if t[0] == si]
            if here:     # -Lnnn only for the running language; ranked before the plain name
                return 'catalogue', min(here, key=lambda t: (EXTS.index(t[2]), t[3] is None))
        return None, None

    cases, wins = collections.Counter(), collections.Counter()
    for b in sorted(rows, key=lambda b: (b['cat'] == 'other', b['name'].lower())):
        stem = key('objects/' + b['name'])
        members = sorted(index[stem]) + loose.get(stem, [])
        langs = [t for t in members if t[-1] is not None]
        cat_kinds = collections.defaultdict(set)
        for t in index[stem]:
            cat_kinds[t[1]].add('bin' if t[2] in BINARY else 'text')
        top = max((t[0] for t in index[stem] if slots[t[0]] not in overlay), default=None)
        top_src = slots[top].relative_to(GAME).as_posix() if top is not None else None
        if loose.get(stem):
            case = 'loose_vs_catalogue'
        elif cat_kinds[top_src] == {'bin', 'text'}:
            case = 'same_catalogue'
        else:
            case = 'different_catalogues'
        kind, w = winner(stem, skip_overlay=True)
        wsrc = w[0] if kind == 'loose' else w[1]
        wext = w[1] if kind == 'loose' else w[2]
        kind2, w2 = winner(stem, skip_overlay=False)
        ov = '' if w2 == w else f' now_overlay={w2[1]}:{w2[2]}'
        wkind = 'binary' if wext in BINARY else 'text'
        if b['cat'] != 'other':
            cases[case] += 1; wins[(case, wkind)] += 1
        rec = b['member']
        agree = 'record_member_wins' if rec.lower() == f'{wsrc}:{b["name"]}.{wext}'.lower().replace(':', ':objects/', 1) else f'record_member={rec}'
        print(f'{b["cat"]:7} {b["name"]}  case={case}  winner={wsrc}:{wext} ({wkind}){ov}  {agree}'
              f'  lang_variants={len(langs)}  members=' +
              ','.join(f'{t[1]}:{t[2]}' if t[0] != 'loose' else f'loose:{t[1]}' for t in members)
              + f'  refuse={"+".join(b["refuse"])}')
    print('ship/station cases: ' + json.dumps(dict(cases)))
    print('ship/station winner kinds: ' + json.dumps({f'{c}/{k}': n for (c, k), n in sorted(wins.items())}))


main()
