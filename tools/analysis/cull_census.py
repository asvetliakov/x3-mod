#!/usr/bin/env python3
"""Bucket the engine's own cull/LOD census (`cull_census` rows, X3M_CULL_CENSUS=1).

Per capture frame the pass logs one row per evaluated node with the engine's
LOD metric `s = r*640/D`, its small-object measure `r*W/D`, the per-node
thresholds, the verdict and the selected LOD (docs/reverse-engineering/
lod-selection.md, "Cull census sites"). This script buckets the nodes of the
main view (the view with the most rows, or --view) by `s` in the engine's
640-reference units and in pixels (`px = s * m00 * width / 1280 * F / 0x4000`,
m00 from the frame's projection row, width from the rt0 surface row, F the
view's binary-angle FOV: the engine's s uses D' = D*F/0x4000 at 0x0047d1ce, so
the factor is exactly the one cull_small_parts applies. F comes from the
frame's projection rows (cot(F/2) = max(0.75*m11, m00), zoom included), else
the last `cull_small_parts_value ... focus=` row, else 0x4000; --focus
overrides. All of m00, width and F are overridable),
joins the frame's `object_context`/`draw` rows on the node pointer to count
draws and triangles per bucket, and prices the buckets at --us-per-draw
(docs/architecture/engine-frame-time.md 2.3). It also names what sits under
--bodies-px: the repository has no model-id -> object-type table (the game's
type files are not extracted), so each model under the threshold is listed by
its model id with the node's body flags (`flags_in & 0x09000000`: 0x1000000 =
body tree built, 0x8000000 = body cache early-out, loading-profile-run1.md),
a radius class, distance range, draws and, on `culled_small` rows, the
cull_small_parts scope. The rows carry no parent link, so "body" here is the
flag class, not the stub's `[node+0x18] == 0` test. Read-only; streams the log.
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
                    r'radius=(-?\d+) thr_1dc=(-?\d+) thr_1d8=(-?\d+) limit=(-?\d+) flags_in=([0-9a-f]{8}) flags_out=[0-9a-f]{8} lod=(-?\d+) verdict=(\w+)(?: scope=(\w+))?')
BODY_FLAGS = 0x09000000
RADIUS_EDGES = (1000, 5000, 20000)
RADIUS_CLASSES = ('<1k', '1k-5k', '5k-20k', '>20k')
CONTEXT_RE = re.compile(r'\bobject_context device=\d+ frame=(\d+) index=(\d+) .*?\bnode=([0-9a-f]{8}) .*?\bmodel=([0-9a-f]{8}) lod=([0-9a-f]{8})')
DRAW_RE = re.compile(r'\bdraw device=\d+ frame=(\d+) index=(\d+) kind=\w+ topology=\d+ primitives=(\d+)')
SURFACE_RE = re.compile(r'\bsurface role=rt0 .*?\bwidth=(\d+) height=(\d+)')
PROJECTION_RE = re.compile(r'\bobject_matrix role=projection row=0 bits=([0-9a-f]{8}),')
PROJECTION1_RE = re.compile(r'\bobject_matrix role=projection row=1 bits=[0-9a-f]{8},([0-9a-f]{8}),')
VALUE_FOCUS_RE = re.compile(r'\bcull_small_parts_value .*?\bfocus=0x([0-9a-f]+)')
FOCUS_DEFAULT, FOCUS_MIN, FOCUS_MAX = 0x4000, 0x106, 0x8000


def bucket_of(value):
    for i, edge in enumerate(EDGES):
        if value < edge:
            return i
    return len(EDGES)


def bits_to_float(text):
    return struct.unpack('<f', struct.pack('<I', int(text, 16)))[0]


def focus_from_projection(m00, m11):
    """The view's binary-angle FOV from P[0] and P[5] (cull_small_parts_core.h focus_from_projection); None when unusable."""
    import math
    if not (m00 and m11 and math.isfinite(m00) and math.isfinite(m11) and m00 > 0 and m11 > 0):
        return None
    focus = math.floor(65536 / math.pi * math.atan(1 / max(0.75 * m11, m00)) + 0.5)
    return focus if FOCUS_MIN <= focus <= FOCUS_MAX else None


def parse(lines):
    """Collect census frames, the per-frame draw join and the frame geometry from an iterable of lines."""
    frames = {}
    rows = defaultdict(list)
    context = {}          # (frame, index) -> node
    primitives = {}       # (frame, index) -> primitives
    width, m00, m11, focus = None, None, None, None
    m00_by_frame, m11_by_frame = {}, {}
    current_frame = None
    for line in lines:
        if 'cull_census' in line:
            m = ROW_RE.search(line)
            if m:
                frame = int(m.group(1))
                rows[frame].append({'view': int(m.group(2), 16), 'node': int(m.group(3), 16), 'model': int(m.group(4), 16), 's': int(m.group(5)),
                                    'measure': int(m.group(6)), 'd': int(m.group(7)), 'radius': int(m.group(8)), 'thr_1dc': int(m.group(9)),
                                    'thr_1d8': int(m.group(10)), 'limit': int(m.group(11)), 'flags_in': int(m.group(12), 16), 'lod': int(m.group(13)), 'verdict': m.group(14), 'scope': m.group(15)})
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
        elif 'cull_small_parts_value' in line:
            m = VALUE_FOCUS_RE.search(line)
            if m:
                focus = int(m.group(1), 16)
        elif 'object_matrix role=projection row=1' in line:
            m = PROJECTION1_RE.search(line)
            if m:
                value = bits_to_float(m.group(1))
                if m11 is None:
                    m11 = value
                if current_frame is not None and current_frame not in m11_by_frame:
                    m11_by_frame[current_frame] = value
        elif 'object_matrix role=projection row=0' in line:
            m = PROJECTION_RE.search(line)
            if m:
                value = bits_to_float(m.group(1))
                if m00 is None:
                    m00 = value
                if current_frame is not None and current_frame not in m00_by_frame:
                    m00_by_frame[current_frame] = value
    return {'frames': frames, 'rows': dict(rows), 'context': context, 'primitives': primitives, 'width': width, 'm00': m00, 'm00_by_frame': m00_by_frame,
            'm11': m11, 'm11_by_frame': m11_by_frame, 'focus': focus}


def summarize(parsed, frames=None, view=None, us_per_draw=23.7, width=None, m00=None, bodies_px=4.0, focus=None):
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
        frame_focus = (focus or focus_from_projection(parsed['m00_by_frame'].get(frame) or parsed['m00'],
                                                      parsed.get('m11_by_frame', {}).get(frame) or parsed.get('m11'))
                       or parsed.get('focus') or FOCUS_DEFAULT)
        px_per_s = frame_m00 * width / 1280.0 * (frame_focus / FOCUS_DEFAULT)
        draws_by_node = defaultdict(list)
        for (f, index), node in parsed['context'].items():
            if f == frame:
                draws_by_node[node].append(parsed['primitives'].get((f, index), 0))
        table = [{'bucket': name, 'nodes': 0, 'kept': 0, 'culled': 0, 'draws': 0, 'tris': 0} for name in BUCKETS]
        px_table = [{'bucket': name, 'nodes': 0, 'kept': 0, 'draws': 0, 'tris': 0} for name in BUCKETS]
        joined_nodes = set()
        models = {}
        for r in rows:
            if r['view'] != main_view:
                continue
            draws = draws_by_node.get(r['node'], [])
            # The model join: nodes that would draw (kept) or that the small-parts stub culled, under bodies_px.
            if r['verdict'] in ('kept', 'culled_small') and r['s'] * px_per_s < bodies_px:
                body = r.get('flags_in', 0) & BODY_FLAGS
                key = (r['model'], body, r['verdict'], r.get('scope'))
                m = models.setdefault(key, {'model': r['model'], 'body_flags': body, 'verdict': r['verdict'], 'scope': r.get('scope'), 'nodes': 0, 'draws': 0,
                                            'radius_max': 0, 'd_min': r['d'], 'd_max': r['d']})
                m['nodes'] += 1
                m['draws'] += len(draws)
                m['radius_max'] = max(m['radius_max'], r['radius'])
                m['d_min'], m['d_max'] = min(m['d_min'], r['d']), max(m['d_max'], r['d'])
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
        result['frames'][frame] = {'view': main_view, 'views': dict(by_view), 'm00': frame_m00, 'focus': frame_focus, 'px_per_s': px_per_s,
                                   'nodes': by_view[main_view], 'frame': parsed['frames'].get(frame),
                                   'draws': frame_draws, 'joined_draws': joined_draws, 'table': table, 'px_table': px_table, 'savings': savings,
                                   'bodies_px': bodies_px,
                                   'models': sorted(({**m, 'radius_class': RADIUS_CLASSES[sum(m['radius_max'] >= e for e in RADIUS_EDGES)]} for m in models.values()),
                                                    key=lambda m: (-m['draws'], -m['nodes'], m['model']))}
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
                   f"unmeasured={meta.get('unmeasured')} draws={fr['draws']} joined_draws={fr['joined_draws']} m00={fr['m00']:.4f} focus=0x{fr['focus']:04x} px_per_s={fr['px_per_s']:.4f}")
        out.append('| bucket (s units) | nodes | kept | culled | draws | tris | ms |')
        out.append('|---|---|---|---|---|---|---|')
        for c in fr['table']:
            out.append(f"| {c['bucket']} | {c['nodes']} | {c['kept']} | {c['culled']} | {c['draws']} | {c['tris']} | {c['ms']:.3f} |")
        out.append('| bucket (px) | nodes | kept | draws | tris | ms |')
        out.append('|---|---|---|---|---|---|')
        for c in fr['px_table']:
            out.append(f"| {c['bucket']} | {c['nodes']} | {c['kept']} | {c['draws']} | {c['tris']} | {c['ms']:.3f} |")
        out.append('savings: ' + ', '.join(f"{k}: {v['draws']} draws ({v['ms']:.3f} ms)" for k, v in fr['savings'].items()))
        models = fr.get('models') or []
        if models:
            flagged = [m for m in models if m['body_flags']]
            out.append(f"models under {fr['bodies_px']:g} px (kept or culled_small): {len(models)} groups, body-flagged {sum(m['nodes'] for m in flagged)} nodes / "
                       f"{sum(m['draws'] for m in flagged)} draws, unflagged {sum(m['nodes'] for m in models if not m['body_flags'])} nodes / "
                       f"{sum(m['draws'] for m in models if not m['body_flags'])} draws (no model -> object-type table in the repository; no parent link in the rows)")
            out.append('| model | body flags | radius class | max radius | D range | nodes | draws | verdict | scope |')
            out.append('|---|---|---|---|---|---|---|---|---|')
            for m in models:
                out.append(f"| {m['model']:08x} | {m['body_flags']:08x} | {m['radius_class']} | {m['radius_max']} | {m['d_min']}-{m['d_max']} | {m['nodes']} | {m['draws']} | {m['verdict']} | {m['scope'] or '-'} |")
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
    parser.add_argument('--focus', type=lambda v: int(v, 0), default=None,
                        help='the view\'s binary-angle FOV, e.g. 0x3470 (default: from the frame\'s projection rows, else the cull_small_parts_value focus, else 0x4000)')
    parser.add_argument('--bodies-px', type=float, default=4.0, help='list the models of kept/culled_small nodes under this many pixels (default 4)')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    with open(args.log, errors='replace') as handle:
        parsed = parse(handle)
    result = summarize(parsed, frames=args.frames, view=args.view, us_per_draw=args.us_per_draw, width=args.width, m00=args.m00, bodies_px=args.bodies_px, focus=args.focus)
    if args.json:
        json.dump(result, sys.stdout, indent=1)
        sys.stdout.write('\n')
    else:
        print(render(result) if result['frames'] else 'no cull_census rows')
    return 0


if __name__ == '__main__':
    sys.exit(main())
