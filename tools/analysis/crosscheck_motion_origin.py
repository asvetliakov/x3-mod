#!/usr/bin/env python3
"""Independent cross-check of a motion readback against projected object origins.

This deliberately shares no code with ``analyze_motion_readback.py``. For two
consecutive captured frames it pairs the matched routed draws by their logged
``RigidDrawKey`` fields, projects each draw's *object origin* with the current
and the previous submitted rows, and compares the resulting screen displacement
with what the readback actually stores at that pixel.

Why the object origin: rows ``c24-27`` are applied as ``clip_i = dot(row_i, p)``,
so for ``p = (0, 0, 0, 1)`` the clip position is exactly the translation column
``(c24.w, c25.w, c26.w, c27.w)``. Dividing by ``w`` and mapping to the viewport
gives a screen position that needs no geometry, no shader and no renderer code.
The readback's own displacement at that pixel is ``uv_prev * size - (px+0.5,
py+0.5)`` (the producer writes texture-centre UV with no jitter at checkpoint
B1), so the two numbers are computed from disjoint evidence.

Caveat, reported rather than hidden: a pixel is attributed to a draw only by
position. For a closed hull the origin pixel is covered by the object itself,
but under perspective and rotation the origin's displacement differs from the
surface displacement at that pixel, so the comparison is a sign-and-magnitude
agreement test, not an equality test. A 3x3 median around the pixel damps
single-pixel disagreements at silhouettes.

Usage::

    python3 tools/analysis/crosscheck_motion_origin.py <session.log> \
        --readback-dir <captures> --previous 2793 --current 2794
"""
import argparse
import re
import statistics
import struct
import sys

FIELDS = re.compile(r'(\S+?)=(\S*)')
KEY_FIELDS = ('node', 'camera', 'node_handle', 'camera_handle', 'node_serial', 'camera_serial',
              'load_epoch', 'registry_epoch', 'model', 'lod', 'vb', 'ib', 'declaration', 'offset',
              'stride', 'vs', 'position_offset', 'position_type', 'topology', 'first', 'primitives',
              'base_vertex', 'min_vertex', 'vertex_count', 'indexed')


def bits_to_float(word):
    return struct.unpack('<f', struct.pack('<I', int(word, 16) & 0xffffffff))[0]


def collect(log_path, device, wanted):
    """Matched draws of the wanted frames: key -> (draw index, four rows c24-27).

    Registers the capture omitted are all-zero (``encoding=sparse_zero``).
    """
    out = {frame: {} for frame in wanted}
    frame = index = None
    rows = None
    with open(log_path, 'r', errors='replace') as handle:
        for line in handle:
            if line.startswith(f'frame_begin device={device} '):
                value = int(dict(FIELDS.findall(line))['frame'])
                frame = value if value in wanted else None
            elif frame is None:
                continue
            elif line.startswith(f'draw device={device} '):
                index = int(dict(FIELDS.findall(line))['index'])
                rows = {}
            elif line.startswith('constant kind=vs type=f reg=2') and rows is not None:
                f = dict(FIELDS.findall(line))
                register = int(f['reg'])
                if 24 <= register <= 27:
                    rows[register] = tuple(bits_to_float(b) for b in f['bits'].split(','))
            elif line.startswith(f'motion_route device={device} '):
                f = dict(FIELDS.findall(line))
                if int(f['gate']) == 0 and int(f['index']) == index:
                    matrix = [rows.get(r, (0.0, 0.0, 0.0, 0.0)) for r in (24, 25, 26, 27)]
                    out[frame][tuple(f[k] for k in KEY_FIELDS)] = (index, matrix)
            elif line.startswith(f'frame_end device={device} '):
                frame = None
    return out


def origin_pixel(matrix, width, height):
    x, y, z, w = matrix[0][3], matrix[1][3], matrix[2][3], matrix[3][3]
    if w <= 0:
        return None
    return ((x / w + 1.0) * 0.5 * width, (1.0 - y / w) * 0.5 * height, z / w)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log')
    parser.add_argument('--readback-dir', required=True)
    parser.add_argument('--device', type=int, default=1)
    parser.add_argument('--previous', type=int, required=True)
    parser.add_argument('--current', type=int, required=True)
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--height', type=int, default=768)
    parser.add_argument('--min-predicted-px', type=float, default=0.5,
                        help='draws below this predicted displacement are not scored')
    parser.add_argument('--rows', type=int, default=25, help='table rows to print')
    args = parser.parse_args(argv)

    width, height = args.width, args.height
    tables = collect(args.log, args.device, {args.previous, args.current})
    previous, current = tables[args.previous], tables[args.current]
    shared = set(previous) & set(current)
    print(f'matched draws: previous {len(previous)}, current {len(current)}, '
          f'shared keys {len(shared)}')

    path = f'{args.readback_dir}/motion_{args.device}_{args.current}.rgba32f'
    with open(path, 'rb') as handle:
        data = handle.read()
    if len(data) != width * height * 16:
        print(f'unexpected readback size {len(data)} for {width}x{height}', file=sys.stderr)
        return 2

    def pixel(px, py):
        offset = (py * width + px) * 16
        return struct.unpack('<4f', data[offset:offset + 16])

    scored = []
    for key in shared:
        current_index, current_rows = current[key]
        previous_index, previous_rows = previous[key]
        a = origin_pixel(current_rows, width, height)
        b = origin_pixel(previous_rows, width, height)
        if a is None or b is None:
            continue
        px, py = int(round(a[0] - 0.5)), int(round(a[1] - 0.5))
        if not (1 <= px < width - 1 and 1 <= py < height - 1):
            continue
        predicted = (b[0] - a[0], b[1] - a[1])
        samples = []
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                r, g, _, alpha = pixel(px + dx, py + dy)
                if alpha == 1.0:
                    samples.append((r * width - (px + dx + 0.5), g * height - (py + dy + 0.5)))
        if not samples:
            continue
        mx = statistics.median(s[0] for s in samples)
        my = statistics.median(s[1] for s in samples)
        scored.append(dict(index=current_index, previous_index=previous_index, px=px, py=py,
                           valid=len(samples), predicted=predicted, measured=(mx, my),
                           predicted_magnitude=(predicted[0] ** 2 + predicted[1] ** 2) ** 0.5,
                           measured_magnitude=(mx * mx + my * my) ** 0.5))
    scored.sort(key=lambda row: -row['predicted_magnitude'])
    print('%-6s %-6s %-6s %-4s  %-19s %-19s %-8s %-8s %-7s' %
          ('draw', 'px', 'py', 'n', 'predicted dx,dy', 'measured dx,dy', '|pred|', '|meas|', 'ratio'))
    for row in scored[:args.rows]:
        print('%-6d %-6d %-6d %-4d  %8.3f,%8.3f %8.3f,%8.3f %8.3f %8.3f %7.4f' % (
            row['index'], row['px'], row['py'], row['valid'],
            row['predicted'][0], row['predicted'][1], row['measured'][0], row['measured'][1],
            row['predicted_magnitude'], row['measured_magnitude'],
            row['measured_magnitude'] / row['predicted_magnitude'] if row['predicted_magnitude'] else float('nan')))
    moving = [r for r in scored if r['predicted_magnitude'] > args.min_predicted_px]
    agreeing = [r for r in moving
                if r['predicted'][0] * r['measured'][0] >= 0 and r['predicted'][1] * r['measured'][1] >= 0]
    print(f'\ndraws with |predicted| > {args.min_predicted_px} px: {len(moving)}; '
          f'sign agreement on both axes: {len(agreeing)}')
    if moving:
        ratios = [r['measured_magnitude'] / r['predicted_magnitude'] for r in moving]
        errors = [abs(r['measured_magnitude'] - r['predicted_magnitude']) for r in moving]
        print('magnitude ratio measured/predicted: median %.4f, min %.4f, max %.4f'
              % (statistics.median(ratios), min(ratios), max(ratios)))
        print('absolute magnitude difference px: median %.4f, max %.4f'
              % (statistics.median(errors), max(errors)))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
