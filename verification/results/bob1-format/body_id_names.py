#!/usr/bin/env python3
"""Body id -> name -> resolved file, checked against measured draw/primitive joins.

Read-only. Implements two findings of docs/reverse-engineering/body-format-bob1.md
§6-§7 and checks them against data already on disk:

1. The dynamic body-name table (ids >= 20000, filled by 0x0046e400) is persisted
   in every savegame by 0x0046f1c0 as `NAME` + u32 BE count + count x (u8 len,
   bytes); 0x0046ee20 restores it on load, so a session loaded from a save has
   id 20000 + i == entry i of that save's table.
2. The file resolver 0x004e7590 (via 0x004e8e10) picks, for "objects\\<name>":
   a loose file under the game directory first; otherwise the catalogue slots
   from the highest index down (mod slot, addon\\NN.cat high to low, NN.cat high
   to low), stopping at the first catalogue holding any accepted extension;
   inside that layer the lowest index in "pbb bob pbd bod" wins, and a
   "-L<lang>" variant beats the plain name.

For each (model id, LOD, draws, primitives) join already measured in
docs/reverse-engineering/lod-selection.md the script maps the id through each
save's table, resolves the body with rule 2, parses it with the BOB1 reader and
compares draws = sum of PART group counts and primitives = sum of face counts
at that LOD. Prints counts and names only; no body or save bytes are written.

Usage: PYTHONPATH=tools/analysis python3 verification/results/bob1-format/body_id_names.py
"""
import gzip
import os
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'tools' / 'analysis'))
sys.path.insert(0, str(HERE))
from sector_fog_census import read_catalogue, unpack  # noqa: E402
from bob1_roundtrip import parse  # noqa: E402

ROOT = Path(os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'))
EXTS = ['pbb', 'bob', 'pbd', 'bod']          # 0x00561180, index = rank (lower wins)

# (model id hex, lod, draws, primitives, source) from lod-selection.md (measured joins)
JOINS = [
    ('542a', 1, 1, 1842, 'run49'), ('542a', 0, 15, 11538, 'run36'),
    ('5427', 1, 1, 2592, 'run36'), ('5427', 0, 14, 10967, 'run36'),
    ('5436', 1, 1, 3002, 'run39'), ('5436', 0, 13, 11921, 'run36'),
    ('543f', 1, 1, 3439, 'run49'), ('543f', 0, 15, 16191, 'run36'),
    ('542b', 1, 1, 2517, 'run48'), ('542b', 0, 15, 18544, 'run48'),
    ('546b', 1, 3, 1207, 'run48'), ('546b', 0, 24, 17060, 'run48'),
    ('53ab', 3, 1, 467, 'run240'), ('53ab', 0, 17, 16011, 'run240'),
    ('5411', 1, 20, 54709, '-'), ('5411', 0, 35, 107397, '-'),
    ('546d', 1, 18, 77128, '-'), ('546d', 0, 25, 152900, '-'),
    ('5530', 1, 19, 44395, '-'), ('5530', 0, 20, 90006, '-'),
]


def save_body_names(path):
    """Dynamic body-name table as written by 0x0046f1c0 (first section after the marker)."""
    d = gzip.open(path).read()
    start = 0
    while True:
        i = d.find(b'NAME', start)
        if i < 0:
            raise ValueError('no body-name table in ' + str(path))
        start = i + 1
        pos = i + 4
        n = struct.unpack('>I', d[pos:pos + 4])[0]
        pos += 4
        names, ok = [], 0 < n < 100000
        for _ in range(n if ok else 0):
            ln = d[pos]
            s = d[pos + 1:pos + 1 + ln]
            if ln == 0 or any(c < 0x20 or c > 0x7e for c in s):
                ok = False
                break
            names.append(s.decode('ascii'))
            pos += 1 + ln
        if ok:
            return names


class Resolver:
    """0x004e7590 order for the installed tree (no mod slot: addon/mods is absent)."""

    def __init__(self, root):
        self.layers = []          # searched in this order
        cats = sorted(root.glob('[0-9][0-9].cat')) + sorted((root / 'addon').glob('[0-9][0-9].cat'))
        self.mods = sorted((root / 'addon' / 'mods').glob('*.cat')) if (root / 'addon' / 'mods').is_dir() else []
        for cat in reversed(cats):  # slot count-2 .. 0 (slot count-1 = mod, empty here)
            idx = {}
            for e in read_catalogue(cat):
                idx.setdefault(e['path'].replace('/', '\\').upper(), dict(e, cat=cat))
            self.layers.append((cat.relative_to(root).as_posix(), idx))
        self.root = root

    def resolve(self, name):
        base = ('objects\\' + name).replace('/', '\\')
        loose_dir = self.root / Path(*base.split('\\')[:-1])
        stem = base.split('\\')[-1]
        if loose_dir.is_dir():
            files = {p.name.lower(): p for p in loose_dir.iterdir() if p.is_file()}
            for ext in EXTS:
                if f'{stem}.{ext}'.lower() in files:
                    return 'loose', ext, files[f'{stem}.{ext}'.lower()].read_bytes()
        key = base.upper()
        for source, idx in self.layers:
            for ext in EXTS:
                e = idx.get(f'{key}.{ext.upper()}')
                if e:
                    with e['cat'].with_suffix('.dat').open('rb') as s:
                        s.seek(e['offset'])
                        raw = s.read(e['size'])
                    return source, ext, unpack(bytes(v ^ 0x33 for v in raw))
        return None, None, None


def lod_stats(data):
    if data[:4] != b'BOB1':
        return None
    lods = dict(parse(data))['BODY']
    return [(sum(len(p['groups']) for p in L['parts']),
             sum(len(g['faces']) for p in L['parts'] for g in p['groups'])) for L in lods]


def main():
    res = Resolver(ROOT)
    print(f"layers searched (after loose): {[s for s, _ in res.layers]}; mod cats present: {len(res.mods)}")
    saves = sorted((ROOT / 'save').glob('*.sav'))
    tables = {}
    for sv in saves:
        names = save_body_names(sv)
        tables[sv.name] = names
        norm = Counter(n.replace('/', '\\').lower() for n in names)
        print(f"{sv.name}: {len(names)} dynamic body names (ids 20000..{20000 + len(names) - 1}); "
              f"names registered twice after '/'->'\\\\' + case folding: {sum(1 for v in norm.values() if v > 1)}")
    # consistency of the id -> name prefix across saves
    a, b, c = (tables[s.name] for s in saves[:3]) if len(saves) >= 3 else (None, None, None)
    if a is not None:
        common = min(len(a), len(b), len(c))
        same = sum(1 for i in range(common) if a[i] == b[i] == c[i])
        print(f"ids with the same name in all {len(saves)} saves: {same} of first {common}")
    for sv, names in tables.items():
        hits = [20000 + i for i, n in enumerate(names) if n.replace('/', '\\').lower() == 'stations\\docks\\argon_dock_center']
        print(f"{sv}: example stations\\docks\\argon_dock_center -> ids {hits}")
    for sv, names in tables.items():
        ok = bad = 0
        rows = []
        cache = {}
        for mid, lod, draws, prims, src in JOINS:
            i = int(mid, 16) - 20000
            name = names[i] if 0 <= i < len(names) else None
            if name not in cache:
                layer, ext, data = res.resolve(name) if name else (None, None, None)
                cache[name] = (layer, ext, lod_stats(data) if data else None)
            layer, ext, st = cache[name]
            got = st[lod] if st and lod < len(st) else None
            match = got == (draws, prims)
            ok += match
            bad += not match
            rows.append(f"  {mid} ({int(mid, 16)}) LOD{lod} measured {draws}/{prims} [{src}] -> {name} "
                        f"[{layer}:{ext}, nLOD {len(st) if st else '-'}] body {got} {'MATCH' if match else 'differs'}")
        print(f"{sv}: joins matching the resolved body: {ok} of {ok + bad}")
        for r in rows:
            print(r)
    # same-stem conflicts in the installed tree under the engine rule
    by_stem = defaultdict(list)
    for source, idx in res.layers:
        for k in idx:
            if k.startswith('OBJECTS\\') and k.rsplit('.', 1)[-1].lower() in EXTS:
                by_stem[k.rsplit('.', 1)[0]].append((source, k.rsplit('.', 1)[-1].lower()))
    order = {source: n for n, (source, _) in enumerate(res.layers)}
    multi = {s: sorted(v, key=lambda t: (order[t[0]], EXTS.index(t[1]))) for s, v in by_stem.items() if len(v) > 1}
    mixed_ext = sum(1 for v in multi.values() if len({e for _, e in v}) > 1)
    same_layer_mixed = sum(1 for v in multi.values()
                           if any(len({e for l2, e in v if l2 == l}) > 1 for l, _ in v))
    print(f"body stems under objects/: {len(by_stem)}; in more than one layer or extension: {len(multi)}; "
          f"with mixed extensions: {mixed_ext}; mixed extensions inside one layer: {same_layer_mixed}")
    for s, v in sorted(multi.items())[:5]:
        print(f"  {s.lower()}: {v} -> engine takes {v[0]}")


if __name__ == '__main__':
    main()
