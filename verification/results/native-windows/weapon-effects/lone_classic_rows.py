#!/usr/bin/env python3
"""Read-only: the lone classic re-shade's figures from the tracked native D3D record (summary.json): the checks, the
game programs extracted, recognition, the model, the three depths against the batch, the native flag, the refusals
and the cost; beside them the effects lone path's cost row for comparison.

  python3 -I verification/results/native-windows/weapon-effects/lone_classic_rows.py \
    > verification/results/native-windows/weapon-effects/lone_classic_rows_out.txt
"""
import json
import pathlib

r = json.loads((pathlib.Path(__file__).resolve().parent / 'summary.json').read_text())
print('passed', r['passed'], 'commit', r['source']['commit'], 'host', r['host']['name'], 'checks', r['run']['result'])
print('classic checks', {k: v for k, v in dict(r['checks']).items() if 'lone_classic' in k})
print('game programs', {k: v['hash'] for k, v in r['classic_programs']['programs'].items()})
for x in r['metrics']['reshade']:
    c = x.get('case', '')
    if (c.startswith('lone_classic') and c != 'lone_classic_texel') or c == 'lone_cost':
        print(' '.join(f'{k}={v}' for k, v in x.items()))
