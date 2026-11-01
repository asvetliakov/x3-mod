#!/usr/bin/env python3
"""Run 216/217 census of the engine fade-band-state draws (blend on, SRCALPHA/INVSRCALPHA, z write 0).

Per log: (1) per VS, fade_arm, the fade_permille class (0 = arm not reached, <1000, 1000) and the node's alpha13c
(object_evidence: the +0x13c override, 0 = none, so g_AlphaValue = 1): a band row below 1000 with no override can
only come from b0 TRUE (g_EnableFog, the engine's distance-fade branch); a fade-state row at exactly 1000 with no
override is b0 FALSE (material transparency). (2) For VS 37c34a74 per frame and node: model, body, cull-census
distance and the node's colour draws split into fade state / opaque, so a material layer (fade state beside opaque
draws of the same node) would show. Logs are untracked (C:\\X3\\x3m.log, x3m.prev.log).
usage: python3 -I fade_state_census.py <log>...
"""
import collections
import sys

XT = '37c34a7478544c14'


def fields(line):
    return dict(t.split('=', 1) for t in line.split()[1:] if '=' in t)


for path in sys.argv[1:]:
    evidence, context, census, routes = {}, {}, {}, []
    with open(path, errors='replace') as log:
        for line in log:
            kind = line.split(' ', 1)[0]
            if kind not in ('motion_route', 'object_evidence', 'object_context', 'cull_census'):
                continue
            d = fields(line)
            if kind == 'object_evidence':
                evidence[(d['frame'], d['index'])] = d['alpha13c']
            elif kind == 'object_context':
                context[(d['frame'], d['index'])] = d
            elif kind == 'cull_census' and 'frame' in d and 'node' in d:
                census[(d['frame'], d['node'])] = (d.get('body', '?').split('\\')[-1], d.get('d', '?'))
            elif kind == 'motion_route':
                routes.append(d)
    print(path)
    classes = collections.Counter()
    for d in routes:
        if not (d['zwrite'] == '0' and d['blend'] == '1' and d['src'] == '5' and d['dst'] == '6'):
            continue
        p = int(d['fade_permille'])
        band = '0' if p == 0 else '1000' if p == 1000 else '<1000'
        classes[(d['vs'][:8], d['fade_arm'], band, evidence.get((d['frame'], d['index']), '?'))] += 1
    print(' fade-state rows: count vs fade_arm permille alpha13c')
    for k, v in sorted(classes.items()):
        print('  ', v, *k)
    nodes = collections.defaultdict(collections.Counter)
    for d in routes:
        if d['vs'] != XT:
            continue
        c = context.get((d['frame'], d['index']), {})
        key = (d['frame'], c.get('node', '?'), c.get('model', '?'))
        nodes[key]['fade' if d['zwrite'] == '0' and d['blend'] == '1' else 'opaque'] += 1
    print(' VS 37c34a74 per frame/node: frame node model body distance fade opaque')
    for (frame, node, model), n in sorted(nodes.items()):
        body, distance = census.get((frame, node), ('?', '?'))
        print('  ', frame, node, model, body, distance, n['fade'], n['opaque'])
