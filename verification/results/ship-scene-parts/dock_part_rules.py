#!/usr/bin/env python3
"""Replay candidate cull rules for carrier dock-port parts on the first frame of each run385 burst.

Joins `cull_census` rows (model id, s, verdict, radius, d) with the drawn `object_context` rows (draws per node) of
the same frame. A model id >= 100000 is a cut-scene inline body (id = local + (cut - 1) * 100000, EXE 0x004920f1 /
0x00491521), so cut = id // 100000. Dock-port interior scenes: CutData 9013 DockCarrier_quicklaunch_scene,
9014 DockCarrier_scene, 9098 / 9099 the M6 variants. The hull's LOD per root comes from the kept hull rows.
Prints counts only. Usage: dock_part_rules.py <session.log> [frame ...]  (default 4827 8142 10210)."""
import collections, re, sys

DOCK_CUTS = {9013, 9014, 9098, 9099}
FIELDS = re.compile(r' (frame|node|model|s|d|radius|limit|verdict|lod|body)=(\S+)')


def main():
    log = sys.argv[1]
    frames = [int(f) for f in sys.argv[2:]] or [4827, 8142, 10210]
    want = {str(f) for f in frames}
    census = collections.defaultdict(list)
    draws = collections.defaultdict(collections.Counter)
    with open(log, errors='replace') as stream:
        for line in stream:
            if line.startswith('cull_census device=') or line.startswith('object_context device='):
                m = re.search(r' frame=(\d+) ', line)
                if not m or m.group(1) not in want:
                    continue
                row = dict(FIELDS.findall(line))
                if line.startswith('cull_census'):
                    census[int(row['frame'])].append(row)
                else:
                    draws[int(row['frame'])][row['node']] += 1
    for frame in frames:
        rows = census[frame]
        parts = [r for r in rows if int(r['model'], 16) >= 100000]
        dock = [r for r in parts if int(r['model'], 16) // 100000 in DOCK_CUTS]
        kept = [r for r in dock if r['verdict'] == 'kept']
        drawn = sum(draws[frame][r['node']] for r in kept)
        by_model = collections.Counter()
        for r in kept:
            by_model[(r['model'], int(r['model'], 16) // 100000, r['radius'])] += draws[frame][r['node']]
        print(f'frame {frame}: census rows {len(rows)}, cut-inline rows {len(parts)}, dock-port rows {len(dock)}, '
              f'kept {len(kept)}, their draws {drawn}')
        print('  kept draws by model (model, cut, radius):', dict(sorted(by_model.items())))
        print('  verdicts of dock-port rows:', dict(collections.Counter(r['verdict'] for r in dock)))
        for t in (4, 6, 8, 10, 12, 16, 24):
            gone = [r for r in kept if int(r['s']) < t]
            print(f'  rule s < {t:2d} on dock-port parts: nodes {len(gone):3d}, draws removed '
                  f'{sum(draws[frame][r["node"]] for r in gone):4d}')


if __name__ == '__main__':
    main()
