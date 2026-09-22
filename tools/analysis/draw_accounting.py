#!/usr/bin/env python3
"""Bucket one capture frame's draws into offscreen / occluded / tiny / visible.

Input is a preserved run directory (session log plus its readbacks). The frame's
`object_bounds` rows (`--object-bounds-log`, F8 frames only) give the projected,
viewport-clipped screen box, device depth range and frustum corner count of every
routed draw whose object box the caster-candidate route computed; the frame's
`draw` rows give primitives, `object_context` gives node and model, and the
frame's `depth_<device>_<frame>.r32f`/`.rgba32f` readback (R32F, or RGBA32F with
device depth in .r, -1 where no routed draw covered the pixel) answers what is in
front of each box. Buckets, in this priority:

* `offscreen`   - the clipped screen box is empty (`offscreen=1`): drawn outside the view.
* `occluded`    - every sampled pixel of the box carries routed depth closer than
                  the box's `zmin` by `--margin`: the object cannot be visible.
* `tiny`        - on screen, box area below `--tiny-px` square pixels.
* `partial`     - some but not all sampled pixels are covered by nearer depth.
* `visible`     - no sampled pixel is covered.
* `no_box`      - a `draw` row of the frame with no `object_bounds` row (unrouted,
                  or routed without a known object box): the remainder that keeps
                  the accounting equal to the frame's own draw count.

Per bucket: draws, primitives and milliseconds at the frame's measured per-draw
cost (`frame_timing` nearest the frame: `dt_p50_us / draws_p50`, overridable with
`--us-per-draw`), plus a per-node table. A box straddling the eye plane (`near=1`)
has an unbounded projection; its row carries the whole viewport, so it is only
ever `partial` or `visible`. A row marked `alpha_tested=1` (an alpha-tested
routed draw, whose box is logged but never used for a caster verdict) is bucketed
like any other; the header counts them as `alpha_tested=`, and logs written
before the field existed count 0. `stale=1` after it marks a box that is an
earlier buffer revision's (the draw's current vertices may lie elsewhere): the
header's `stale=` counts them, so a bucket they land in can be discounted. Read-only; the log is streamed, the depth image is
read once and sampled on a bounded stride.
"""
import argparse
import array
from collections import defaultdict
import json
from pathlib import Path
import re
import sys

BOUNDS_RE = re.compile(r'\bobject_bounds device=(\d+) frame=(\d+) index=(\d+) node=([0-9a-fA-F]+) model=([0-9a-fA-F]+) '
                       r'sx0=(-?[\d.]+) sy0=(-?[\d.]+) sx1=(-?[\d.]+) sy1=(-?[\d.]+) zmin=(-?[\d.eE+-]+) zmax=(-?[\d.eE+-]+) inside=(\d+)'
                       r'(?P<offscreen> offscreen=1)?(?P<near> near=1)?(?P<alpha> alpha_tested=1)?(?P<stale> stale=1)?')
DRAW_RE = re.compile(r'\bdraw device=(\d+) frame=(\d+) index=(\d+) kind=\w+ topology=\d+ primitives=(\d+)')
CONTEXT_RE = re.compile(r'\bobject_context device=(\d+) frame=(\d+) index=(\d+) .*?\bnode=([0-9a-fA-F]+) .*?\bmodel=([0-9a-fA-F]+) lod=([0-9a-fA-F]+)')
DEPTH_RE = re.compile(r'\bmotion_output_depth_readback device=(\d+) frame=(\d+) file=(\S+) width=(\d+) height=(\d+) format=(\S+) result=([0-9a-fA-F]+)')
TIMING_RE = re.compile(r'\bframe_timing qpc=\d+ frame=(\d+) frames=\d+ dt_p50_us=(\d+) .*?\bdraws_p50=(\d+)')
SESSION_RE = re.compile(r'session-\d{8}-\d{6}-\d+\.log\Z')
DEPTH_LANES = {'r32f_row_major': 1, 'rg32f_row_major': 2, 'rgba32f_row_major': 4}
DEPTH_SENTINEL = -1.0
BUCKETS = ('offscreen', 'occluded', 'tiny', 'partial', 'visible', 'no_box')


class MalformedInput(Exception):
    pass


def find_log(run_dir, explicit=None):
    if explicit:
        return Path(explicit)
    candidates = sorted(p for p in Path(run_dir).iterdir() if SESSION_RE.search(p.name))
    if not candidates:
        candidates = sorted(Path(run_dir).glob('*.log'))
    if not candidates:
        raise MalformedInput(f'no session log in {run_dir}')
    return candidates[-1]


def parse(lines, device=None):
    """Collect the per-draw rows, the depth readback records and the timing windows."""
    bounds = {}       # (frame, index) -> row
    primitives = {}   # (frame, index) -> primitives
    context = {}      # (frame, index) -> (node, model, lod)
    depth = {}        # frame -> readback fields
    timing = []       # (frame, us_per_draw)
    for line in lines:
        if 'object_bounds ' in line:
            m = BOUNDS_RE.search(line)
            if m and (device is None or int(m.group(1)) == device):
                frame, index = int(m.group(2)), int(m.group(3))
                bounds[(frame, index)] = {
                    'device': int(m.group(1)), 'frame': frame, 'index': index,
                    'node': m.group(4).lower(), 'model': m.group(5).lower(),
                    'x0': float(m.group(6)), 'y0': float(m.group(7)), 'x1': float(m.group(8)), 'y1': float(m.group(9)),
                    'zmin': float(m.group(10)), 'zmax': float(m.group(11)), 'inside': int(m.group(12)),
                    'offscreen': m.group('offscreen') is not None, 'near': m.group('near') is not None,
                    'alpha_tested': m.group('alpha') is not None, 'stale': m.group('stale') is not None}
        elif 'object_context ' in line:
            m = CONTEXT_RE.search(line)
            if m and (device is None or int(m.group(1)) == device):
                context[(int(m.group(2)), int(m.group(3)))] = (m.group(4).lower(), m.group(5).lower(), m.group(6).lower())
        elif ' draw device=' in line or line.startswith('draw device='):
            m = DRAW_RE.search(line)
            if m and (device is None or int(m.group(1)) == device):
                primitives[(int(m.group(2)), int(m.group(3)))] = int(m.group(4))
        elif 'motion_output_depth_readback ' in line:
            m = DEPTH_RE.search(line)
            if m and (device is None or int(m.group(1)) == device):
                depth[int(m.group(2))] = {'file': m.group(3), 'width': int(m.group(4)), 'height': int(m.group(5)),
                                          'format': m.group(6), 'result': int(m.group(7), 16)}
        elif 'frame_timing qpc=' in line:
            m = TIMING_RE.search(line)
            if m and int(m.group(3)):
                timing.append((int(m.group(1)), int(m.group(2)) / int(m.group(3))))
    return {'bounds': bounds, 'primitives': primitives, 'context': context, 'depth': depth, 'timing': timing}


def per_draw_us(timing, frame):
    """The per-draw cost of the frame_timing window closest to this frame."""
    if not timing:
        return None
    return min(timing, key=lambda row: abs(row[0] - frame))[1]


def load_depth(path, width, height, lanes):
    size = path.stat().st_size
    if size != width * height * 4 * lanes:
        raise MalformedInput(f'{path.name}: size {size} != {width}x{height}x{4 * lanes}')
    data = array.array('f')
    with path.open('rb') as stream:
        data.fromfile(stream, width * height * lanes)
    if sys.byteorder != 'little':
        data.byteswap()
    return data[::lanes] if lanes > 1 else data


def coverage(row, depth, width, height, margin, max_samples):
    """Sampled pixels of the clipped box and how many carry nearer routed depth.

    Sky and never-covered pixels (the -1 sentinel) count as not covering. The
    stride keeps at most max_samples samples per box and is deterministic."""
    x0 = max(0, int(row['x0']))
    y0 = max(0, int(row['y0']))
    x1 = min(width, max(x0 + 1, int(row['x1']) + 1))
    y1 = min(height, max(y0 + 1, int(row['y1']) + 1))
    if x0 >= width or y0 >= height or x1 <= x0 or y1 <= y0:
        return 0, 0
    span_x, span_y = x1 - x0, y1 - y0
    step = 1
    while (span_x // step + 1) * (span_y // step + 1) > max_samples:
        step += 1
    limit = row['zmin'] - margin
    sampled = covered = 0
    for y in range(y0, y1, step):
        base = y * width
        for x in range(x0, x1, step):
            sampled += 1
            value = depth[base + x]
            if value != DEPTH_SENTINEL and value < limit:
                covered += 1
    return sampled, covered


def bucket_of(row, sampled, covered, tiny_px):
    if row['offscreen']:
        return 'offscreen'
    if sampled and covered == sampled:
        return 'occluded'
    if max(0.0, row['x1'] - row['x0']) * max(0.0, row['y1'] - row['y0']) < tiny_px:
        return 'tiny'
    if covered:
        return 'partial'
    return 'visible'


def account(parsed, frame, run_dir, margin=1e-5, tiny_px=16.0, max_samples=4096, us_per_draw=None):
    readback = parsed['depth'].get(frame)
    if readback is None:
        raise MalformedInput(f'frame {frame}: no motion_output_depth_readback line')
    if readback['format'] not in DEPTH_LANES:
        raise MalformedInput(f'unknown depth readback format {readback["format"]}')
    path = Path(run_dir) / readback['file']
    if not path.is_file():
        raise MalformedInput(f'frame {frame}: depth readback {readback["file"]} is not in {run_dir}')
    width, height = readback['width'], readback['height']
    depth = load_depth(path, width, height, DEPTH_LANES[readback['format']])
    cost = us_per_draw if us_per_draw is not None else per_draw_us(parsed['timing'], frame)
    table = {name: {'draws': 0, 'primitives': 0, 'covered_sum': 0.0} for name in BUCKETS}
    nodes = defaultdict(lambda: {'node': None, 'model': None, 'draws': 0, 'primitives': 0,
                                 **{name: 0 for name in BUCKETS}})
    tiny_total = 0
    rows = []
    for (f, index), row in sorted(parsed['bounds'].items()):
        if f != frame:
            continue
        sampled, covered = (0, 0) if row['offscreen'] else coverage(row, depth, width, height, margin, max_samples)
        name = bucket_of(row, sampled, covered, tiny_px)
        prims = parsed['primitives'].get((f, index), 0)
        fraction = covered / sampled if sampled else 0.0
        table[name]['draws'] += 1
        table[name]['primitives'] += prims
        table[name]['covered_sum'] += fraction
        if not row['offscreen'] and max(0.0, row['x1'] - row['x0']) * max(0.0, row['y1'] - row['y0']) < tiny_px:
            tiny_total += 1
        key = row['node'] or (parsed['context'].get((f, index)) or ('?',))[0]
        entry = nodes[key]
        entry['node'] = key
        entry['model'] = row['model'] or (parsed['context'].get((f, index)) or ('', '?'))[1]
        entry['draws'] += 1
        entry['primitives'] += prims
        entry[name] += 1
        rows.append({**row, 'bucket': name, 'primitives': prims, 'sampled': sampled, 'covered': covered,
                     'covered_fraction': fraction})
    # The frame's remaining draws: every draw row without an object_bounds row.
    for (f, index), prims in parsed['primitives'].items():
        if f == frame and (f, index) not in parsed['bounds']:
            table['no_box']['draws'] += 1
            table['no_box']['primitives'] += prims
    for entry in table.values():
        entry['ms'] = None if cost is None else entry['draws'] * cost / 1000.0
        entry['covered_fraction'] = (entry.pop('covered_sum') / entry['draws']) if entry['draws'] else 0.0
    for entry in nodes.values():
        entry['ms'] = None if cost is None else entry['draws'] * cost / 1000.0
    return {'frame': frame, 'width': width, 'height': height, 'depth_file': readback['file'],
            'us_per_draw': cost, 'margin': margin, 'tiny_px': tiny_px,
            'draws_in_frame': sum(1 for (f, _) in parsed['primitives'] if f == frame),
            'draws_with_box': sum(1 for (f, _) in parsed['bounds'] if f == frame),
            'alpha_tested_with_box': sum(1 for (f, _), row in parsed['bounds'].items() if f == frame and row['alpha_tested']),
            'stale_with_box': sum(1 for (f, _), row in parsed['bounds'].items() if f == frame and row['stale']),
            'tiny_total': tiny_total, 'buckets': table,
            'nodes': sorted(nodes.values(), key=lambda e: -e['draws']), 'rows': rows}


def default_frame(parsed):
    counts = defaultdict(int)
    for (frame, _) in parsed['bounds']:
        counts[frame] += 1
    if not counts:
        raise MalformedInput('no object_bounds rows: was the run flown with --object-bounds-log?')
    return max(sorted(counts), key=lambda f: counts[f])


def report(result, nodes=20):
    out = [f'frame {result["frame"]} {result["width"]}x{result["height"]} depth={result["depth_file"]} '
           f'us_per_draw={"n/a" if result["us_per_draw"] is None else format(result["us_per_draw"], ".2f")} '
           f'draws={result["draws_in_frame"]} with_box={result["draws_with_box"]} alpha_tested={result["alpha_tested_with_box"]} '
           f'stale={result["stale_with_box"]} '
           f'tiny_on_screen={result["tiny_total"]}',
           f'{"bucket":<10}{"draws":>7}{"prims":>10}{"ms":>8}{"covered":>9}']
    for name in BUCKETS:
        entry = result['buckets'][name]
        ms = 'n/a' if entry['ms'] is None else format(entry['ms'], '.2f')
        out.append(f'{name:<10}{entry["draws"]:>7}{entry["primitives"]:>10}{ms:>8}{entry["covered_fraction"]:>9.2f}')
    out.append(f'{"node":<10}{"model":<10}{"draws":>7}{"prims":>10}{"ms":>8}  buckets')
    for entry in result['nodes'][:nodes]:
        ms = 'n/a' if entry['ms'] is None else format(entry['ms'], '.2f')
        mix = ' '.join(f'{name}={entry[name]}' for name in BUCKETS if entry[name])
        out.append(f'{entry["node"]:<10}{entry["model"]:<10}{entry["draws"]:>7}{entry["primitives"]:>10}{ms:>8}  {mix}')
    return '\n'.join(out)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('run_dir', help='preserved run directory (session log and its readbacks)')
    parser.add_argument('--log', default=None, help='session log to read instead of the newest one in the run directory')
    parser.add_argument('--frame', type=int, default=None, help='capture frame (default: the frame with the most object_bounds rows)')
    parser.add_argument('--device', type=int, default=None, help='device id filter (default: every device)')
    parser.add_argument('--margin', type=float, default=1e-5, help='device-depth margin a covering pixel must beat (default 1e-5)')
    parser.add_argument('--tiny-px', type=float, default=16.0, help='box area below which a draw is tiny, square pixels (default 16)')
    parser.add_argument('--max-samples', type=int, default=4096, help='depth samples per box (default 4096)')
    parser.add_argument('--us-per-draw', type=float, default=None, help='override the per-draw cost taken from frame_timing')
    parser.add_argument('--nodes', type=int, default=20, help='rows of the per-node table (default 20)')
    parser.add_argument('--json', action='store_true', help='machine-readable output (without the per-draw rows)')
    args = parser.parse_args(argv)
    try:
        log = find_log(args.run_dir, args.log)
        with log.open('r', errors='replace') as stream:
            parsed = parse(stream, args.device)
        frame = args.frame if args.frame is not None else default_frame(parsed)
        result = account(parsed, frame, args.run_dir, margin=args.margin, tiny_px=args.tiny_px,
                         max_samples=args.max_samples, us_per_draw=args.us_per_draw)
    except (MalformedInput, OSError) as error:
        print(f'draw_accounting: {error}', file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps({k: v for k, v in result.items() if k != 'rows'}, indent=1, sort_keys=True))
    else:
        print(report(result, args.nodes))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
