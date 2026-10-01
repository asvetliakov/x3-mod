#!/usr/bin/env python3
"""Counts of engine_bodies.{mayhem,stock}.json (tools/effects/engine_bodies.py output, untracked) and their overlap
with the bodies ship scenes reference (engine_bodies.csv of the census, tracked).

  python3 tools/effects/engine_bodies.py --out verification/results/engine-effects-census/engine_bodies.mayhem.json
  python3 tools/effects/engine_bodies.py --stock-only --out verification/results/engine-effects-census/engine_bodies.stock.json
  python3 verification/results/engine-effects-census/engine_bodies_counts.py
"""
import collections
import csv
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent


def census_key(body):
    """Census body column -> the generator's key, lower-cased (census strips effects/engines/ and uses /)."""
    b = body if body.startswith(('v/', 'effects/')) else 'effects/engines/' + body
    return b.replace('/', '\\').lower()


def main():
    rows = list(csv.DictReader(open(HERE / 'engine_bodies.csv')))
    for view in ('mayhem', 'stock'):
        t = json.loads((HERE / f'engine_bodies.{view}.json').read_text())
        bodies = {k.lower(): v for k, v in t['bodies'].items()}
        missing = {m['name'].lower(): m for m in t['missing']}
        scene = {census_key(r['body']) for r in rows if r['view'] == view}
        used = {k: v for k, v in bodies.items() if k in scene}
        print(view, json.dumps(dict(
            lists=t['generated_from']['lists'], loaded=len(bodies), missing=len(missing),
            missing_reasons=dict(collections.Counter(m['reason'].split(':')[0] for m in missing.values())),
            effect=dict(collections.Counter(str(v['effect']) for v in bodies.values())),
            cluster=dict(collections.Counter(str(v['cluster']) for v in bodies.values())),
            scene_referenced=len(scene), scene_referenced_loaded=len(used),
            scene_referenced_missing=sorted(k for k in scene if k in missing),
            scene_referenced_not_on_list=sorted(k for k in scene if k not in bodies and k not in missing),
            scene_loaded_by_effect=dict(collections.Counter(str(v['effect']) for v in used.values())),
            scene_loaded_by_cluster=dict(collections.Counter(str(v['cluster']) for v in used.values())),
            xtc_engine_fx=sum(1 for k, v in bodies.items() if '_xtc_' in k and v['effect'] == 'engine.fx'),
            v00566={k: bodies['v\\00566'][k] for k in ('lists', 'effect', 'cluster', 'z_extent', 'value')}
            if 'v\\00566' in bodies else None), sort_keys=True))


if __name__ == '__main__':
    main()
