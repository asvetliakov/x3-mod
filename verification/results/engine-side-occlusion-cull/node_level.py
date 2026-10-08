#!/usr/bin/env python3
"""Engine-side occlusion cull sizing from the run14/run15 occlusion estimate (no game, no Wine).

Inputs: the extract JSON (verification/results/occlusion-cull-estimate/extract.py <log> <out.json>) and the
per-draw rows (analyze.py ... <prefix>_draws.jsonl, tol 0.01 key). Both are local, untracked.
Prints per capture frame:
  join     draws whose object_context node (args[1] of 0x004c0150) has a cull_census row (EDI of 0x0047cfe0)
           in the same frame / all scoped draws: the identity check;
  visits   census rows per sub-part node in the frame (pass visits per frame) and the distinct views;
  hidden   sub-part draws fully hidden (every footprint pixel, tol 0.01) and the nodes all of whose draws are
           fully hidden (the engine-side skip unit), with their draw count.
Usage: node_level.py <extract.json> <draws.jsonl> [tol] [--classes]
"""
import collections, json, sys

def main(extract, draws_path, tol='0.01', classes=''):
    R = json.load(open(extract))
    rows = collections.defaultdict(list)
    for line in open(draws_path):
        r = json.loads(line)
        rows[r['frame']].append(r)
    print('frame draws joined/scoped | census_rows views part_nodes visits_per_part_node(min,max) | '
          'part_draws hidden_draws | nodes_with_hidden nodes_all_hidden draws_on_all_hidden_nodes')
    for fr_s, F in sorted(R['frames'].items(), key=lambda x: int(x[0])):
        fr = int(fr_s)
        census = F['census']
        cen_nodes = collections.Counter(c['node'] for c in census)
        views = {c['view'] for c in census}
        node_views = collections.defaultdict(set)
        for c in census:
            node_views[c['node']].add(c['view'])
        D = rows.get(fr, [])
        scoped = [r for r in D if r.get('node')]
        joined = sum(1 for r in scoped if r['node'] in cen_nodes)
        parts = [r for r in D if r['class'] in ('turret', 'dock')]
        by_node = collections.defaultdict(list)
        for r in parts:
            by_node[r['node']].append(r)
        hid = lambda r: r.get('frac') is not None and r['frac'].get(tol, 0.0) >= 1.0
        hidden = [r for r in parts if hid(r)]
        nodes_h = {r['node'] for r in hidden}
        all_h = [n for n, rs in by_node.items() if all(hid(r) for r in rs)]
        visits = [cen_nodes[n] for n in by_node if n in cen_nodes]
        print(f'{fr} {len(D)} {joined}/{len(scoped)} | {len(census)} {len(views)} {len(by_node)} '
              f'({min(visits) if visits else "-"},{max(visits) if visits else "-"}) | {len(parts)} {len(hidden)} | '
              f'{len(nodes_h)} {len(all_h)} {sum(len(by_node[n]) for n in all_h)}'
              + (' | by class (nodes/draws): ' + ' '.join(
                  f"{c}={sum(1 for n in all_h if by_node[n][0]['class'] == c)}/"
                  f"{sum(len(by_node[n]) for n in all_h if by_node[n][0]['class'] == c)}"
                  for c in ('turret', 'dock')) + ' bodies: ' + ','.join(sorted(
                  {str(by_node[n][0]['body']).rsplit(chr(92), 1)[-1] for n in all_h}))[:160]
                 if classes else ''))

if __name__ == '__main__':
    main(*sys.argv[1:])
