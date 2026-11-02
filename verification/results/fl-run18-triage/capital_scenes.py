#!/usr/bin/env python3
"""FL run 18, capital plumes: which engine bodies do ship scenes reference, and are they on the JET list?
Read-only on the game trees; prints names, counts and ids only (no game bytes).

FL view: catalogue_mounts.mounted_catalogues(root, 'fl'), types from addon2/types (TShips, Bodies).
ap-installed view: /mnt/c/X3 (the modded AP install, lod_overlay.original_assets: every catalogue minus the x3m overlay).
AP view: /mnt/c/X3AP-orig, sector_fog_census.STOCK_AP_CATALOGUES, types/TShips + types/Bodies (addon namespace wins).
Per TShips row: class (col 52), scene (col 16), emitter effect (col 11), trail (col 49); per scene part (P n; B body; C mode):
engine parts = body token under effects\\engines (or a numeric token on the JET list); jet = token on that edition's
SBTYPE_JET list (engine_bodies.body_key spelling, case-insensitive); emitter = fx_engine_emitter.

  python3 -I verification/results/fl-run18-triage/capital_scenes.py > verification/results/fl-run18-triage/capital_scenes_summary.txt
"""
import collections
import importlib.util
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
spec = importlib.util.spec_from_file_location('engine_bodies', ROOT / 'tools/effects/engine_bodies.py')
eb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(eb)
import bob1  # noqa: E402
import catalogue_mounts  # noqa: E402
from sector_fog_census import Assets, STOCK_AP_CATALOGUES  # noqa: E402

PART = re.compile(rb'^P (\d+); B ([^;]+);([^\n]*)$', re.M)
CAPITAL = ('OBJ_SHIP_M1', 'OBJ_SHIP_M2', 'OBJ_SHIP_M7', 'OBJ_SHIP_TL', 'OBJ_SHIP_M7M', 'OBJ_SHIP_M7C')
ENG = re.compile(rb'effects[\\/]engines[\\/][A-Za-z0-9_]+')


def view(edition):
    if edition == 'fl':
        root = Path('/mnt/c/X3FL')
        cats = [p.relative_to(root).as_posix() for p in catalogue_mounts.mounted_catalogues(root, 'fl')]
        a = Assets(root, catalogues=cats)
        get = lambda name: a.read_entry(a.entries['addon2/types/' + name.lower() + '.txt'][-1])
    elif edition == 'ap-installed':
        import lod_overlay
        a, _ = lod_overlay.original_assets(Path('/mnt/c/X3'))
        get = lambda name: a.get('types/' + name + '.txt')[0]
    else:
        root = Path('/mnt/c/X3AP-orig')
        a = Assets(root, catalogues=STOCK_AP_CATALOGUES)
        get = lambda name: a.get('types/' + name + '.txt')[0]
    return a, get


def tships(text):
    lines = [l for l in text.splitlines() if l.strip() and not l.lstrip().startswith('//')]
    count = int(lines[0].split(';')[1])
    for line in lines[1:count + 1]:
        f = line.split(';')
        yield dict(cls=f[52], scene=f[16].strip(), eff=f[11], trail=f[49], sid=f[-2])


def scene(a, token):
    stem = bob1.body_stem(token)
    for ext in ('.bod', '.bob'):
        if a.candidates(stem + ext):
            data, prov = a.get(stem + ext)
            return data, prov['source']
    return None, None


def parts(data):
    out = []
    for m in PART.finditer(data):
        c = re.search(rb'\bC (\d+)', m.group(3))
        out.append((m.group(2).decode('latin1').strip(), int(c.group(1)) if c else None))
    return out


def run(edition):
    a, get = view(edition)
    lists = eb.body_lists(get('Bodies').decode('latin1'))
    jet = {k.lower() for k, v in eb.jet_bodies(lists).items() if 'jet' in v['lists']}
    rows = list(tships(get('TShips').decode('latin1')))
    by_cls = collections.defaultdict(lambda: collections.Counter())
    names = collections.defaultdict(collections.Counter)      # class group -> engine body -> parts
    offlist = collections.Counter()
    seen_scene = {}
    per_ship = []
    for r in rows:
        grp = 'capital' if r['cls'] in CAPITAL else r['cls']
        key = r['scene'].lower()
        if key not in seen_scene:
            data, src = scene(a, r['scene'])
            if data is None:
                seen_scene[key] = None
            elif data[:4] == b'CUT1' or not PART.search(data):
                found = [x.decode('latin1') for x in ENG.findall(data)]
                seen_scene[key] = ('binary', src, [(n, None) for n in found])
            else:
                seen_scene[key] = ('text', src, parts(data))
        s = seen_scene[key]
        c = by_cls[grp]
        c['rows'] += 1
        c['eff=' + r['eff']] += 0  # keep keys light; effects tallied below
        if s is None:
            c['scene_missing'] += 1
            continue
        c['scene_' + s[0]] += 1
        n_jet = n_eng = n_emit = n_jet_main = 0
        for body, mode in s[2]:
            k, ident = eb.body_key(body)
            low = k.lower()
            if 'fx_engine_emitter' in low:
                n_emit += 1
                continue
            on = low in jet
            if on or 'effects\\engines\\' in low.replace('/', '\\'):
                n_eng += 1
                n_jet += on
                if on and (mode is None or mode & 0x1001 == 0x1001):
                    n_jet_main += 1
                names[grp][(low.split('\\')[-1], on)] += 1
                if not on:
                    offlist[low] += 1
        c['with_engine_parts'] += n_eng > 0
        c['with_jet_parts'] += n_jet > 0
        c['with_main_jet'] += n_jet_main > 0
        c['with_emitters'] += n_emit > 0
        c['engine_parts'] += n_eng
        c['jet_parts'] += n_jet
        c['emitter_parts'] += n_emit
        per_ship.append((grp, r['sid'], r['scene'], s[0], s[1], n_eng, n_jet, n_jet_main, n_emit, r['eff'], r['trail']))
    return dict(rows=len(rows), jet=len(jet), by_cls=by_cls, names=names, offlist=offlist, per_ship=per_ship)


def main():
    res = {e: run(e) for e in ('fl', 'ap', 'ap-installed')}
    for e, r in res.items():
        print(f'== {e}: TShips rows {r["rows"]}, JET list {r["jet"]} unique tokens')
        for grp in sorted(r['by_cls'], key=lambda g: (g != 'capital', g)):
            c = r['by_cls'][grp]
            keys = ('rows', 'scene_text', 'scene_binary', 'scene_missing', 'with_engine_parts', 'with_jet_parts',
                    'with_main_jet', 'with_emitters', 'engine_parts', 'jet_parts', 'emitter_parts')
            print(f'  {grp:16s} ' + ' '.join(f'{k}={c[k]}' for k in keys))
        print('  engine bodies referenced by capital scenes (name, on JET list: parts):')
        for (n, on), k in sorted(r['names']['capital'].items(), key=lambda x: -x[1]):
            print(f'    {n} jet={int(on)} parts={k}')
        print('  off-list engine bodies (all classes):', dict(r['offlist'].most_common(12)))
        print('  capital rows: sid scene format source engine/jet/main/emitters eff trail')
        for p in r['per_ship']:
            if p[0] == 'capital':
                print('    ' + ' '.join(str(x) for x in p[1:]))
        print('  big TS rows (TS/TP with >= 4 jet parts or no jet part):')
        for p in r['per_ship']:
            if p[0] in ('OBJ_SHIP_TS', 'OBJ_SHIP_TL') and (p[6] == 0 or p[6] >= 4):
                print('    ' + ' '.join(str(x) for x in p[1:]))


if __name__ == '__main__':
    main()
