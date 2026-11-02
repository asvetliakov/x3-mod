#!/usr/bin/env python3
"""The recogniser's effects pair (engine_effects_core.h effect_vs_a/effect_vs_b/effect_ps) against the FL offline shader
inventory (verification/results/fl-shader-coverage/inventory.py --output <inv.json>: every program X3FL.exe can create
from root + addon2 in its mount order, keyed by the proxy's FNV-1a 64). Reports, for each of the three ids, whether FL
can create it (and whether AP shares it), and every FL pixel shader whose effect path names engine or effects, so a
different FL engine/effects pixel shader would show up. Prints one JSON line; exit 1 when an id is not an FL member.

usage: python3 -I verification/results/fl-engine-effects/effect_pair_check.py <inv.json>
"""
import json
import sys

PAIR = {'effect_vs_a': 'vs_d5e1c75351ed3f04', 'effect_vs_b': 'vs_89193868c61c3846', 'effect_ps': 'ps_8360f422de08b5bd'}


def main():
    inv = json.load(open(sys.argv[1]))
    programs = {p['id']: p for p in inv['programs']}
    result = {'programs': len(programs), 'pair': {}, 'fl_engine_effects_ps': []}
    for name, pid in PAIR.items():
        p = programs.get(pid)
        result['pair'][name] = None if p is None else {
            'id': pid, 'model': p['model'], 'shared_with_ap': p['shared_with_ap'], 'catalogues': p['catalogues'],
            'effect_path': p.get('effect_path'), 'effect_path_count': p.get('effect_path_count')}
    for p in inv['programs']:
        path = (p.get('effect_path') or '').lower()
        if p['kind'] == 'ps' and ('engine' in path or '/effects' in path):
            result['fl_engine_effects_ps'].append({'id': p['id'], 'model': p['model'], 'shared_with_ap': p['shared_with_ap'],
                                                   'effect_path': p.get('effect_path'),
                                                   'effect_path_count': p.get('effect_path_count')})
    result['present'] = all(result['pair'].values())
    print(json.dumps(result))
    return 0 if result['present'] else 1


if __name__ == '__main__':
    sys.exit(main())
