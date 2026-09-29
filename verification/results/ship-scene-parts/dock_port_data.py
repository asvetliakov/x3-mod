#!/usr/bin/env python3
"""Offline census of carrier dock-port parts (read-only, bottle X3; prints counts, names and hashes only).

1. The Raptor / Ocelot scenes (installed) and the stock Split M1 scene: parts per body reference, the dock-port
   dummy bodies 19026 / 19027 / 19098 / 19099 among them.
2. types/Dummies rows of those bodies and types/CutData rows of their cut ids, installed vs stock layers.
3. The dock cut scenes: member, decoded SHA-256, parts and inline (`L {`) bodies, installed vs stock.
4. Every text ship scene (installed and stock) that references a dock-port dummy.
Inline body groups/draws per model id: verification/results/run341-draw-calls/name_model_id.py <hex id>."""
import collections, pathlib, re, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / 'tools/analysis'))
from sector_fog_census import Assets, STOCK_AP_CATALOGUES  # noqa: E402

ROOT = pathlib.Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
DUMMIES = ('19026', '19027', '19098', '19099')
PART = re.compile(rb'^P (\d+); B ([^;]+);', re.M)


def text(assets, path):
    try:
        data, prov = assets.get(path)
    except FileNotFoundError:
        return None, None
    return data, prov


def main():
    views = {'installed': Assets(ROOT), 'stock': Assets(ROOT, catalogues=STOCK_AP_CATALOGUES)}
    print('1. ship scenes')
    for label, path in (('installed', 'objects/ships/split/split_m1_raptor_scene.bod'),
                        ('installed', 'objects/ships/split/split_m2p_ocelot_scene.bod'),
                        ('stock', 'objects/ships/split/split_m1_raptor_scene.bod'),
                        ('stock', 'objects/ships/split/split_m1_scene.bod'),
                        ('installed', 'objects/ships/split/split_m1_scene.bod')):
        data, prov = text(views[label], path)
        if data is None:
            print(f'  {label:9s} {path}: absent')
            continue
        refs = collections.Counter(m.group(2).decode('latin1') for m in PART.finditer(data))
        dock = {k: refs[k] for k in DUMMIES if refs[k]}
        print(f'  {label:9s} {path}: {prov["source"]} {prov["decoded_sha256"][:12]} parts={sum(refs.values())} '
              f'dock-port dummies={dock}')
    print('2. Dummies / CutData rows')
    for label, assets in views.items():
        dummies, prov = text(assets, 'types/Dummies.txt')
        rows = [l for l in dummies.decode('latin1').splitlines() if l.split(';')[0].strip() in DUMMIES]
        print(f'  {label:9s} Dummies {prov["source"]} {prov["decoded_sha256"][:12]}')
        for row in rows:
            print('    ', row.strip())
        cuts, prov = text(assets, 'types/CutData.txt')
        for row in cuts.decode('latin1').splitlines():
            if row.split(';')[0].strip() in ('9010', '9013', '9014', '9098', '9099'):
                print(f'     CutData {prov["source"]}: {row.strip()}')
    print('3. dock cut scenes')
    for stem in ('dockcarrier_quicklaunch_scene', 'dockcarrier_scene', 'm6dockcarrier_quicklaunch_scene',
                 'm6dockcarrier_scene'):
        for label, assets in views.items():
            data, prov = text(assets, f'objects/stations/docks/{stem}.bod')
            if data is None:
                print(f'  {label:9s} {stem}: absent')
                continue
            parts = [(m.group(1).decode(), m.group(2).decode('latin1')) for m in PART.finditer(data)]
            print(f'  {label:9s} {stem}: {prov["member"]} [{prov["source"]}] {prov["decoded_sha256"][:12]} '
                  f'parts={len(parts)} inline={len(re.findall(rb"^L {", data, re.M))} bodies={[b for _, b in parts]}')
    print('4. text ship scenes referencing dock-port dummies')
    for label, assets in views.items():
        scenes = hits = 0
        per = collections.Counter()
        for key in list(assets.entries):
            if not key.startswith(('objects/ships/', 'addon/objects/ships/')) or not re.search(r'scene\.(bod|pbd)$', key):
                continue
            data, _ = assets.get(key)
            if not PART.search(data):
                continue                       # binary CUT1 scenes are not scanned
            scenes += 1
            refs = collections.Counter(m.group(2).decode('latin1') for m in PART.finditer(data))
            if any(refs[k] for k in DUMMIES):
                hits += 1
                for k in DUMMIES:
                    per[k] += refs[k]
        print(f'  {label:9s} text ship scenes {scenes}, with dock-port dummies {hits}, dummy parts {dict(per)}')


if __name__ == '__main__':
    main()
