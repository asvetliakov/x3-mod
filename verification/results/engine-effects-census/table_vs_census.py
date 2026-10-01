#!/usr/bin/env python3
"""Cross-check of the shipped engine body table (tools/effects/engine_bodies.py output) against the census
(engine_bodies.csv beside this script): which glow bodies placed on ships are absent from the table (not on the
JET/SMALLJET lists), how v/00114 resolves, and the LOD-0 value ratio stock/installed of the xtc bodies present in both
views. Facts for docs/architecture/mod-compatibility.md, "Engine bodies".

  python3 engine_bodies.py --game-root <X3> --out /tmp/eb_installed.json
  python3 engine_bodies.py --game-root <X3> --stock-only --out /tmp/eb_stock.json
  python3 table_vs_census.py /tmp/eb_installed.json /tmp/eb_stock.json
"""
import csv
import json
import statistics
import sys
from pathlib import Path


def stem(key):
    return key.replace('\\', '/').split('/')[-1].lower() if not key.lower().startswith('v\\') else key.replace('\\', '/').lower()


def main(installed_path, stock_path):
    tables = {'mayhem': json.loads(Path(installed_path).read_text()), 'stock': json.loads(Path(stock_path).read_text())}
    rows = list(csv.DictReader(open(Path(__file__).with_name('engine_bodies.csv'))))
    for view, table in tables.items():
        listed = {stem(k): k for k in table['bodies']} | {stem(m['name']): m['name'] for m in table['missing']}
        placed = [r for r in rows if r['view'] == view]
        off = sorted((r['body'], r['ships'], r['parts']) for r in placed if r['body'].lower() not in listed)
        print(f'{view}: census bodies {len(placed)}, table bodies {len(table["bodies"])}, missing {len(table["missing"])}; '
              f'placed but not on the lists: {len(off)} {off}')
        v114 = table['bodies'].get('v\\00114')
        miss114 = [m for m in table['missing'] if m['name'] == 'v\\00114']
        print(f'  v\\00114: ' + (f"entry kind={v114['material']['kind']} cluster={v114['cluster']} value={v114['value']}" if v114
                                 else f'missing {miss114}' if miss114 else 'not listed'))
    m, s = ({stem(k): b for k, b in tables[v]['bodies'].items()} for v in ('mayhem', 'stock'))
    ratios = sorted((s[k]['value'] / m[k]['value'], k) for k in m.keys() & s.keys()
                    if 'xtc' in k and m[k]['value'] and s[k]['value'])
    if ratios:
        r = [x for x, _ in ratios]
        print(f'xtc bodies in both views: {len(r)}; stock/installed value ratio min {min(r):.3f} median '
              f'{statistics.median(r):.3f} max {max(r):.3f}; within 1 % of 2: {sum(1 for x in r if abs(x - 2) <= 0.02)}; '
              f'other: {[(k, round(x, 3)) for x, k in ratios if abs(x - 2) > 0.02]}')


if __name__ == '__main__':
    main(*sys.argv[1:3])
