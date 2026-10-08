#!/usr/bin/env python3
"""Distinct per-draw pipeline keys from capture-frame `draw` snapshots in x3m session logs.

Usage: families.py <session.log> [...]  -> prints JSON summary per run and combined.
Keys come from capture.cpp snapshot(): draw (topology, vs, ps = application bindings,
restored before the snapshot), surface rt0..3/depth (format), state id=value,
vertex_element rows. Twin kind per draw is joined from *_draw rows carrying frame/index.
"""
import json, re, sys, statistics
from collections import Counter, defaultdict

BLEND = (27, 19, 20, 171, 206, 207, 208, 209, 168, 193)   # ABE src dst op sepA srcA dstA opA colorwrite blendfactor
DEPTH = (7, 14, 23, 52, 56, 57, 58, 59, 60, 185, 186, 187, 188, 189)  # z, zwrite, zfunc, stencil*
RASTER = (22,)          # cull (FILLMODE not logged)
OTHER = (28, 15, 24, 25, 194)  # fog, alphatest, aref, afunc, srgbwrite
TWIN_ROWS = ("hull_lightmap_widen_draw", "engine_draw", "text_density_draw", "hull_emission_draw")
num = re.compile(r'(\w+)=(\S+)')

def parse(path):
    draws, cur, twin = [], None, defaultdict(set)
    with open(path, 'rb') as f:
        for raw in f:
            if not raw[:1] in b'dsvhte': continue
            line = raw.decode('latin-1').rstrip('\n')
            head = line.split(' ', 1)[0]
            if head == 'draw':
                kv = dict(num.findall(line))
                cur = dict(frame=int(kv['frame']), index=int(kv['index']), topo=kv['topology'],
                           vs=kv['vs'], ps=kv['ps'], kind=kv['kind'], st={}, rt={}, decl=[])
                draws.append(cur)
            elif cur is None: continue
            elif head == 'state':
                kv = dict(num.findall(line)); cur['st'][int(kv['id'])] = kv['value']
            elif head == 'surface':
                kv = dict(num.findall(line))
                if kv.get('role', '').startswith(('rt', 'depth')) and kv['role'] != 'clear_depth':
                    cur['rt'][kv['role']] = kv['format']
            elif head == 'vertex_element':
                kv = dict(num.findall(line))
                if kv['stream'] != '255':
                    cur['decl'].append((kv['stream'], kv['offset'], kv['type'], kv['usage'], kv['index']))
            elif head == 'draw_result':
                cur = None
            elif head in TWIN_ROWS:
                kv = dict(num.findall(line))
                if 'frame' in kv and 'index' in kv:
                    twin[(int(kv['frame']), int(kv['index']))].add(head.replace('_draw', ''))
    return draws, twin

def key(d):
    g = lambda ids: tuple(d['st'].get(i) for i in ids)
    return dict(vs=d['vs'], ps=d['ps'], blend=g(BLEND), depth=g(DEPTH), raster=g(RASTER), other=g(OTHER),
                decl=tuple(d['decl']), topo=d['topo'],
                targets=tuple(sorted(d['rt'].items())))

def summarise(paths):
    out, allkeys = {}, Counter()
    fam = defaultdict(set)
    for p in paths:
        draws, twin = parse(p)
        keys, first, perprog, twins = Counter(), {}, defaultdict(set), Counter()
        for d in draws:
            k = key(d); t = tuple(sorted(twin.get((d['frame'], d['index']), {'none'})))
            kk = json.dumps(k, sort_keys=True); kt = kk + '|' + ','.join(t)
            keys[kt] += 1; first.setdefault(kt, d['frame']); allkeys[kt] += 1
            perprog[(d['vs'], d['ps'])].add(kt); twins[t] += 1
            for f in ('blend', 'depth', 'raster', 'decl', 'topo', 'targets', 'other'):
                fam[f].add(json.dumps(k[f]))
        sizes = [len(v) for v in perprog.values()]
        out[p] = dict(draws=len(draws), frames=sorted({d['frame'] for d in draws}), distinct=len(keys),
                      programs=len(perprog), per_program_median=statistics.median(sizes) if sizes else 0,
                      per_program_max=max(sizes) if sizes else 0, twin_draws={','.join(k): v for k, v in twins.items()},
                      table=[dict(key=json.loads(k.split('|')[0]), twin=k.split('|')[1], count=c, first_frame=first[k])
                             for k, c in keys.most_common()])
    return out, allkeys, fam

if __name__ == '__main__':
    out, allkeys, fam = summarise(sys.argv[1:])
    progs = defaultdict(set); B=set(); D=set(); R=set(); DE=set(); T=set(); TP=set(); PR=set()
    for k in allkeys:
        kk = json.loads(k.split('|')[0])
        progs[(kk['vs'], kk['ps'])].add(k)
        B.add(json.dumps(kk['blend'])); D.add(json.dumps(kk['depth'])); R.add(json.dumps(kk['raster']))
        DE.add(json.dumps(kk['decl'])); T.add(json.dumps(kk['targets'])); TP.add(kk['topo'])
    sizes = sorted(len(v) for v in progs.values())
    comb = dict(distinct=len(allkeys), program_pairs=len(progs), vs=len({p[0] for p in progs}), ps=len({p[1] for p in progs}),
                per_program_median=statistics.median(sizes), per_program_max=sizes[-1],
                blend=len(B), depth=len(D), raster=len(R), decl=len(DE), targets=len(T), topology=len(TP),
                cross_product_pairs_x_families=len(progs)*len(B)*len(D)*len(R)*len(DE)*len(T)*len(TP))
    json.dump(dict(runs=out, combined=comb), sys.stdout, indent=1, default=str)
