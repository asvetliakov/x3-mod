#!/usr/bin/env python3
"""The ledger's figures from summary.json (run_shadow_penumbra.py): host, near-map identity, the hole-share table per
ref / distance / cascade (tree share / host prediction, bias, base share), prediction agreement, partial holes and the
replay's GPU time per transaction. usage: table.py [summary.json]"""
import json
import sys
from pathlib import Path

d = json.loads(Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).with_name('summary.json')).read_text())
adapter = d['host']['adapter']
print('source', d['commit'], 'base', d['base'][:12], 'result', d['result'], 'wall', d['wall_seconds'], 's')
print('host', adapter.get('description'), adapter.get('driver_version'), 'windows', d['host']['windows'].get('version'))
print('near maps identical', sum(v['identical'] for v in d['near_maps'].values()), 'of', len(d['near_maps']))
rows = d['penumbra_maps']
print('max |share - predicted|', round(max(abs(r['share'] - r['predicted']) for r in rows), 5),
      'min texel agreement', min(r['texel_agreement'] for r in rows))
print('identical to base', sorted((r['ref'], r['distance'], r['cascade']) for r in rows if r['identical_to_base']))
for ref in sorted({r['ref'] for r in rows}):
    for dist in sorted({r['distance'] for r in rows}):
        rr = sorted((r for r in rows if r['ref'] == ref and r['distance'] == dist), key=lambda r: r['cascade'])
        print(f'ref {ref:3} {dist:6} u', ' | '.join(f"c{r['cascade']} {r['share']:.4f}/{r['predicted']:.4f} bias {r['bias']:.2f} base {r['share_base']:.4f}" for r in rr))
print('partial', json.dumps({k: v for k, v in d['partial_holes'].items() if v['regions']}))
print('gpu', json.dumps(d['gpu_us_per_transaction']))
print('calls', d['calls_tree'], 'texture open share', d['texture']['open_share'])
