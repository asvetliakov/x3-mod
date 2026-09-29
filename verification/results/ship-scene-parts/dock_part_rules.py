#!/usr/bin/env python3
"""Replay candidate cull rules for carrier dock-port parts on the first frame of each run385 burst.

Joins `cull_census` rows (model id, s, verdict, radius, d) with the drawn `object_context` rows (draws per node) of
the same frame. A model id >= 100000 is a cut-scene inline body (id = local + (cut - 1) * 100000, EXE 0x004920f1 /
0x00491521), so cut = id // 100000. Dock-port interior scenes: CutData 9013 DockCarrier_quicklaunch_scene,
9014 DockCarrier_scene, 9098 / 9099 the M6 variants. The hull's LOD per root comes from the kept hull rows.

The implemented stub rule (X3M_CULL_DOCK_PARTS_PX, 2026-09-29; docs/verification/cull-small-parts.md, "Dock ports")
is replayed last: for each --px, the frame's own cull_small_parts_frame row (px, m00, width, focus) gives the
small-parts threshold and the dock threshold through the stub's conversion (verify_cull_small_parts_site.threshold_for,
the twin of cull_small_parts_core.h threshold_for), upper = max of the two, and a kept row is removed when
dock_model(id) (the stub's two range compares) and s < upper.
Prints counts only. Usage: dock_part_rules.py <session.log> [frame ...] [--px PX ...]
(default frames 4827 8142 10210, px 8 12)."""
import argparse, collections, re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'verification/probe'))
from verify_cull_small_parts_site import dock_model, parse_frame_line, threshold_for  # noqa: E402

DOCK_CUTS = {9013, 9014, 9098, 9099}
FIELDS = re.compile(r' (frame|node|model|s|d|radius|limit|verdict|lod|body)=(\S+)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log')
    parser.add_argument('frames', nargs='*', type=int)
    parser.add_argument('--px', type=float, action='append')
    args = parser.parse_args()
    log = args.log
    frames = args.frames or [4827, 8142, 10210]
    pxs = args.px or [8.0, 12.0]
    want = {str(f) for f in frames}
    census = collections.defaultdict(list)
    draws = collections.defaultdict(collections.Counter)
    small = {}
    with open(log, errors='replace') as stream:
        for line in stream:
            if line.startswith('cull_small_parts_frame '):
                row = parse_frame_line(line)
                if row and str(row['frame']) in want:
                    small[row['frame']] = row
            elif line.startswith('cull_census device=') or line.startswith('object_context device='):
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
    print('stub rule (X3M_CULL_DOCK_PARTS_PX, the frame\'s cull_small_parts_frame projection):')
    totals = collections.defaultdict(list)
    for frame in frames:
        f = small.get(frame)
        if not f:
            print(f'  frame {frame}: no cull_small_parts_frame row')
            continue
        focus = f['focus'] or 0x4000
        t_small = threshold_for(f['px'], f['m00'], f['width'], focus)
        in_range = [r for r in census[frame] if dock_model(int(r['model'], 16))]
        assert {int(r['model'], 16) // 100000 for r in in_range} <= DOCK_CUTS
        kept = [r for r in in_range if r['verdict'] == 'kept']
        for px in pxs:
            t_dock = threshold_for(px, f['m00'], f['width'], focus)
            upper = max(t_small, t_dock) if t_small > 0 else 0
            gone = [r for r in kept if int(r['s']) < upper]
            removed = sum(draws[frame][r['node']] for r in gone)
            totals[px].append(removed)
            print(f'  frame {frame}: px {f["px"]:g} -> threshold {t_small}, dock px {px:g} -> dock threshold {t_dock} '
                  f'(m00 {f["m00"]:.9g}, width {f["width"]}, focus 0x{focus:04x}); dock-port rows {len(in_range)}, '
                  f'kept {len(kept)}; culled by the dock rule: nodes {len(gone)}, draws removed {removed}')
    for px in pxs:
        print(f'  dock px {px:g}: draws removed per frame {"/".join(str(n) for n in totals[px])}')


if __name__ == '__main__':
    main()
