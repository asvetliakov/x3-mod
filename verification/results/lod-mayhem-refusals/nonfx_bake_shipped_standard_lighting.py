#!/usr/bin/env python3
"""How many shipped effect materials name standard_lighting.fx, and with which technique word, flags and
g_CullMode (the template of lod_atlas.classic_material). Read-only over the winning members of every body of the
installed record (all categories).

  python3 nonfx_bake_shipped_standard_lighting.py [--jobs N] > nonfx_bake_shipped_standard_lighting_out.txt
"""
import argparse
import collections
import json
import multiprocessing
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'tools/analysis'), str(ROOT / 'verification/probe')]
import bob1                                          # noqa: E402
import lod_overlay                                   # noqa: E402

_W = {}


def init(game):
    _W['assets'] = lod_overlay.original_assets(Path(game))[0]
    _W['by'] = {f'{e["source"]}:{e["path"]}'.lower(): e for lst in _W['assets'].entries.values() for e in lst}


def scan(row):
    assets, e = _W['assets'], _W['by'].get(row['member'].lower())
    if e is None:
        return None
    try:
        data = assets.read_entry(e)
        tree = (bob1.parse(data, lod_overlay.MAX_TRAILING) if e['path'].lower().endswith(('.pbb', '.bob'))
                else bob1.parse_text(data))
    except Exception:
        return None
    finally:
        assets.cache.clear()
    out = []
    for m in bob1.materials(tree):
        name = m.get('effect', b'').decode('latin1').lower().replace('\\', '/').rsplit('/', 1)[-1]
        if 'params' in m and name == 'standard_lighting.fx':
            p = {n.lower(): v for n, _, v in m['params']}
            out.append((m['technique'], m['flags'], (p.get(b'g_cullmode') or [None])[0]))
    return row['name'], row['cat'], e['source'], out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', type=Path, default=bob1.DEFAULT_GAME)
    ap.add_argument('--jobs', type=int, default=6)
    a = ap.parse_args()
    rows = json.loads((a.game / 'addon' / 'x3m-lod-batch.json').read_text())['bodies']
    with multiprocessing.Pool(a.jobs, init, (str(a.game),)) as pool:
        res = pool.map(scan, rows, chunksize=32)
    read = [r for r in res if r]
    hits = [r for r in read if r[3]]
    mats = [m for r in hits for m in r[3]]
    count = lambda it: dict(sorted(collections.Counter(it).items(), key=lambda x: str(x[0])))
    print(f'bodies of the record {len(rows)}, read {len(read)}; bodies with a standard_lighting.fx effect material'
          f' {len(hits)}; materials {len(mats)}')
    print('  bodies by category: ' + json.dumps(count(r[1] for r in hits)))
    print('  materials by source catalogue: ' + json.dumps(count(r[2] for r in hits for _ in r[3])))
    print('  technique word: ' + json.dumps(count(str(m[0]) for m in mats)))
    print('  flags: ' + json.dumps(count(hex(m[1]) for m in mats)))
    print('  g_CullMode: ' + json.dumps(count(str(m[2]) for m in mats)))
    docks = [r for r in hits if 'dock' in r[0].lower()]
    print(f'  bodies with "dock" in the name: {len(docks)}, materials {sum(len(r[3]) for r in docks)};'
          f' of them in 02.cat: bodies {sum(1 for r in docks if r[2] == "02.cat")}, materials'
          f' {sum(len(r[3]) for r in docks if r[2] == "02.cat")}')


if __name__ == '__main__':
    main()
