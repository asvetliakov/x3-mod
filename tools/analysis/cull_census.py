#!/usr/bin/env python3
"""Bucket the engine's own cull/LOD census (`cull_census` rows, X3M_CULL_CENSUS=1).

Per capture frame the pass logs one row per evaluated node with the engine's
LOD metric `s = r*640/D`, its small-object measure `r*W/D`, the per-node
thresholds, the verdict and the selected LOD (docs/reverse-engineering/
lod-selection.md, "Cull census sites"). This script buckets the nodes of the
main view (the view with the most rows, or --view) by `s` in the engine's
640-reference units and in pixels (`px = s * m00 * width / 1280`, m00 from the
frame's projection row, width from the rt0 surface row; both overridable),
joins the frame's `object_context`/`draw` rows on the node pointer to count
draws and triangles per bucket, and prices the buckets at --us-per-draw
(docs/architecture/engine-frame-time.md 2.3). Read-only; streams the log.
"""
import argparse
from collections import defaultdict
import json
import re
import struct
import sys

EDGES = (1, 2, 4, 8, 16)
BUCKETS = ('<1', '1-2', '2-4', '4-8', '8-16', '>16')
FRAME_RE = re.compile(r'\bcull_census_frame device=(\d+) frame=(\d+) entries=(\d+) overflow=(\d+) unmeasured=(\d+) exited=(\d+) ring=(\d+)')
ROW_RE = re.compile(r'\bcull_census device=\d+ frame=(\d+) view=([0-9a-f]{8}) node=([0-9a-f]{8}) model=([0-9a-f]{8}) s=(-?\d+) measure=(-?\d+) d=(-?\d+) '
                    r'radius=(-?\d+) thr_1dc=(-?\d+) thr_1d8=(-?\d+) limit=(-?\d+) flags_in=[0-9a-f]{8} flags_out=[0-9a-f]{8} lod=(-?\d+) verdict=(\w+)')
CONTEXT_RE = re.compile(r'\bobject_context device=\d+ frame=(\d+) index=(\d+) .*?\bnode=([0-9a-f]{8}) .*?\bmodel=([0-9a-f]{8}) lod=([0-9a-f]{8})')
DRAW_RE = re.compile(r'\bdraw device=\d+ frame=(\d+) index=(\d+) kind=\w+ topology=\d+ primitives=(\d+)')
SURFACE_RE = re.compile(r'\bsurface role=rt0 .*?\bwidth=(\d+) height=(\d+)')
PROJECTION_RE = re.compile(r'\bobject_matrix role=projection row=0 bits=([0-9a-f]{8}),')


def bucket_of(value):
    for i, edge in enumerate(EDGES):
        if value < edge:
            return i
    return len(EDGES)


def bits_to_float(text):
    return struct.unpack('<f', struct.pack('<I', int(text, 16)))[0]


def parse(lines):
    """Collect census frames, the per-frame draw join and the frame geometry from an iterable of lines."""
    frames = {}
    rows = defaultdict(list)
    context = {}          # (frame, index) -> node
    primitives = {}       # (frame, index) -> primitives
    width, m00 = None, None
    m00_by_frame = {}
    current_frame = None
    for line in lines:
        if 'cull_census' in line:
            m = ROW_RE.search(line)
            if m:
                frame = int(m.group(1))
                rows[frame].append({'view': int(m.group(2), 16), 'node': int(m.group(3), 16), 'model': int(m.group(4), 16), 's': int(m.group(5)),
                                    'measure': int(m.group(6)), 'd': int(m.group(7)), 'radius': int(m.group(8)), 'thr_1dc': int(m.group(9)),
                                    'thr_1d8': int(m.group(10)), 'limit': int(m.group(11)), 'lod': int(m.group(12)), 'verdict': m.group(13)})
                continue
            m = FRAME_RE.search(line)
            if m:
                frames[int(m.group(2))] = {'entries': int(m.group(3)), 'overflow': int(m.group(4)), 'unmeasured': int(m.group(5)), 'exited': int(m.group(6)), 'ring': int(m.group(7))}
                continue
        elif 'object_context' in line:
            m = CONTEXT_RE.search(line)
            if m:
                current_frame = int(m.group(1))
                context[(current_frame, int(m.group(2)))] = int(m.group(3), 16)
        elif line.lstrip().startswith('draw ') or ' draw device=' in line:
            m = DRAW_RE.search(line)
            if m:
                primitives[(int(m.group(1)), int(m.group(2)))] = int(m.group(3))
        elif 'surface role=rt0' in line:
            m = SURFACE_RE.search(line)
            if m and width is None:
                width = int(m.group(1))
        elif 'object_matrix role=projection row=0' in line:
            m = PROJECTION_RE.search(line)
            if m:
                value = bits_to_float(m.group(1))
                if m00 is None:
                    m00 = value
                if current_frame is not None and current_frame not in m00_by_frame:
                    m00_by_frame[current_frame] = value
    return {'frames': frames, 'rows': dict(rows), 'context': context, 'primitives': primitives, 'width': width, 'm00': m00, 'm00_by_frame': m00_by_frame}


def summarize(parsed, frames=None, view=None, us_per_draw=23.7, width=None, m00=None):
    """Bucket table per frame and the per-frame average over the selected frames."""
    width = width or parsed['width'] or 1280
    selected = sorted(f for f in parsed['rows'] if frames is None or frames[0] <= f <= frames[1])
    result = {'width': width, 'us_per_draw': us_per_draw, 'frames': {}, 'average': None}
    for frame in selected:
        rows = parsed['rows'][frame]
        by_view = defaultdict(int)
        for r in rows:
            by_view[r['view']] += 1
        main_view = view if view is not None else max(by_view, key=by_view.get)
        frame_m00 = m00 or parsed['m00_by_frame'].get(frame) or parsed['m00'] or 1.0
        px_per_s = frame_m00 * width / 1280.0
        draws_by_node = defaultdict(list)
        for (f, index), node in parsed['context'].items():
            if f == frame:
                draws_by_node[node].append(parsed['primitives'].get((f, index), 0))
        table = [{'bucket': name, 'nodes': 0, 'kept': 0, 'culled': 0, 'draws': 0, 'tris': 0} for name in BUCKETS]
        px_table = [{'bucket': name, 'nodes': 0, 'kept': 0, 'draws': 0, 'tris': 0} for name in BUCKETS]
        joined_nodes = set()
        for r in rows:
            if r['view'] != main_view:
                continue
            draws = draws_by_node.get(r['node'], [])
            if draws:
                joined_nodes.add(r['node'])
            for t, value in ((table, r['s']), (px_table, r['s'] * px_per_s)):
                cell = t[bucket_of(value)]
                cell['nodes'] += 1
                if r['verdict'] == 'kept':
                    cell['kept'] += 1
                elif 'culled' in cell:
                    cell['culled'] += 1
                cell['draws'] += len(draws)
                cell['tris'] += sum(draws)
        for t in (table, px_table):
            for cell in t:
                cell['ms'] = cell['draws'] * us_per_draw / 1000.0
        frame_draws = sum(len(v) for v in draws_by_node.values())
        joined_draws = sum(len(draws_by_node[n]) for n in joined_nodes)
        savings = {}
        for edge in EDGES[1:]:
            under = sum(c['draws'] for c in px_table[:bucket_of(edge - 0.5) + 1])
            savings[f'under_{edge}px'] = {'draws': under, 'ms': under * us_per_draw / 1000.0}
        result['frames'][frame] = {'view': main_view, 'views': dict(by_view), 'm00': frame_m00, 'px_per_s': px_per_s,
                                   'nodes': by_view[main_view], 'frame': parsed['frames'].get(frame),
                                   'draws': frame_draws, 'joined_draws': joined_draws, 'table': table, 'px_table': px_table, 'savings': savings}
    if result['frames']:
        n = len(result['frames'])
        average = [{'bucket': name, 'nodes': 0.0, 'kept': 0.0, 'draws': 0.0, 'tris': 0.0, 'ms': 0.0} for name in BUCKETS]
        for fr in result['frames'].values():
            for cell, src in zip(average, fr['px_table']):
                for key in ('nodes', 'kept', 'draws', 'tris', 'ms'):
                    cell[key] += src[key] / n
        result['average'] = {'frames': n, 'px_table': average,
                             'savings': {k: {'draws': sum(fr['savings'][k]['draws'] for fr in result['frames'].values()) / n,
                                             'ms': sum(fr['savings'][k]['ms'] for fr in result['frames'].values()) / n} for k in next(iter(result['frames'].values()))['savings']}}
    return result


def render(result):
    out = []
    for frame, fr in result['frames'].items():
        meta = fr['frame'] or {}
        out.append(f"frame {frame}: view={fr['view']:08x} nodes={fr['nodes']} entries={meta.get('entries')} overflow={meta.get('overflow')} "
                   f"unmeasured={meta.get('unmeasured')} draws={fr['draws']} joined_draws={fr['joined_draws']} m00={fr['m00']:.4f} px_per_s={fr['px_per_s']:.4f}")
        out.append('| bucket (s units) | nodes | kept | culled | draws | tris | ms |')
        out.append('|---|---|---|---|---|---|---|')
        for c in fr['table']:
            out.append(f"| {c['bucket']} | {c['nodes']} | {c['kept']} | {c['culled']} | {c['draws']} | {c['tris']} | {c['ms']:.3f} |")
        out.append('| bucket (px) | nodes | kept | draws | tris | ms |')
        out.append('|---|---|---|---|---|---|')
        for c in fr['px_table']:
            out.append(f"| {c['bucket']} | {c['nodes']} | {c['kept']} | {c['draws']} | {c['tris']} | {c['ms']:.3f} |")
        out.append('savings: ' + ', '.join(f"{k}: {v['draws']} draws ({v['ms']:.3f} ms)" for k, v in fr['savings'].items()))
        out.append('')
    if result['average']:
        avg = result['average']
        out.append(f"average over {avg['frames']} frames (px buckets, per frame, {result['us_per_draw']} us/draw)")
        out.append('| bucket (px) | nodes | kept | draws | tris | ms |')
        out.append('|---|---|---|---|---|---|')
        for c in avg['px_table']:
            out.append(f"| {c['bucket']} | {c['nodes']:.1f} | {c['kept']:.1f} | {c['draws']:.1f} | {c['tris']:.0f} | {c['ms']:.3f} |")
        out.append('savings: ' + ', '.join(f"{k}: {v['draws']:.1f} draws ({v['ms']:.3f} ms)" for k, v in avg['savings'].items()))
    return '\n'.join(out)


def parse_frames(text):
    if not text:
        return None
    a, _, b = text.partition('-')
    return (int(a), int(b or a))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log')
    parser.add_argument('--frames', type=parse_frames, default=None, help='frame or A-B range (default: every census frame)')
    parser.add_argument('--view', type=lambda t: int(t, 16), default=None, help='view pointer (hex) to bucket; default: the view with the most rows')
    parser.add_argument('--us-per-draw', type=float, default=23.7)
    parser.add_argument('--width', type=int, default=None, help='viewport width (default: the rt0 surface row, else 1280)')
    parser.add_argument('--m00', type=float, default=None, help='projection m00 (default: the frame\'s projection row, else 1.0)')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    with open(args.log, errors='replace') as handle:
        parsed = parse(handle)
    result = summarize(parsed, frames=args.frames, view=args.view, us_per_draw=args.us_per_draw, width=args.width, m00=args.m00)
    if args.json:
        json.dump(result, sys.stdout, indent=1)
        sys.stdout.write('\n')
    else:
        print(render(result) if result['frames'] else 'no cull_census rows')
    return 0


if __name__ == '__main__':
    sys.exit(main())
