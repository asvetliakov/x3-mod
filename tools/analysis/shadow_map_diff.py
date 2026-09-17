#!/usr/bin/env python3
"""Compare consecutive F8 cascade depth maps after aligning them by the cascade
basis (docs/verification/directional-shadows.md, "Run 40 A (run117) diagnosis").

Raw same-texel differencing of two frames of a cascade map measures the camera,
not the map: `shadow_replay_basis` (src/renderer/shadow_replay_projection.h)
re-centres every cascade on the camera each frame, snapping the centre to the
texel grid in x/y only, so a moving camera slides the box by whole texels and
drags the depth origin along the sun axis by an unsnapped amount. Both are
recoverable from the `shadow_replay_map_basis` telemetry line:

  texel      = 2 * extent / size
  d          = centre(B) - centre(A)                       (world)
  shift_x    = -(d . right) / texel   shift_y = (d . up) / texel   (whole texels)
  depth_off  =  (d . forward) / R,    R = depth_light + depth_behind

with sun-space NDC x = (p . right - c . right) / extent, y likewise, and the
stored depth z = (p . f - c . f + L) / R (`shadow_replay_light_rows`). A world
point at map A texel (u, v) therefore sits at map B texel (u + shift_x,
v + shift_y) holding z_A - depth_off. Aligned, run117 burst 14780 C3 goes from
83 % of texels changed to a few.

Inputs: a run directory of F8 dumps `shadow_map<k>_<device>_<frame>.r32f`
(`size` x `size` R32F, cleared texels hold `--empty`, default 1.0) and the
session log, which is streamed for the basis lines of the requested frames only
(it is hundreds of MB; it is never loaded whole). The log carries no per-cascade
depth epsilon, so `--eps` defaults to 1e-3 (the diagnosis' "> 1e-3" tier).

Per cascade and per consecutive frame pair the report gives the box shift in
texels, the depth offset, the occupied texel counts, and - over the texels the
two frames have in common - the occupied<->empty flips, the texels with
|dz| > eps and their mean |dz|, aligned and, for contrast, unaligned.
"""
import argparse
import json
import sys
from pathlib import Path

BASIS_PREFIX = 'shadow_replay_map_basis '
VECTOR_FIELDS = ('right', 'up', 'forward', 'center', 'sun')
SCALAR_FIELDS = ('extent', 'depth_light', 'depth_behind')
DEFAULT_EPS = 1e-3
DEFAULT_EMPTY = 1.0


class MalformedInput(Exception):
    """A telemetry line or a dump file that does not match the contract."""


def _unpack_map(path, size):
    """The F8 map reader the apply twin uses (verification/probe/sun_shadow_apply.py)."""
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'verification/probe'))
    from sun_shadow_apply import unpack_map
    path = Path(path)
    if not path.is_file():
        raise MalformedInput('no map dump %s' % path)
    data = path.read_bytes()
    if len(data) != size * size * 4:
        raise MalformedInput('%s: size %d != %d x %d x 4' % (Path(path).name, len(data), size, size))
    return unpack_map(data, size)


def parse_basis_line(line):
    """`shadow_replay_map_basis ...` -> dict. Raises MalformedInput."""
    body = line.strip()
    if not body.startswith(BASIS_PREFIX):
        raise MalformedInput('not a map basis line: %r' % body[:40])
    out = {}
    for token in body[len(BASIS_PREFIX):].split():
        key, sep, value = token.partition('=')
        if not sep:
            raise MalformedInput('malformed token %r' % token)
        out[key] = value
    try:
        basis = {
            'device': int(out['device']), 'frame': int(out['frame']), 'cascade': int(out['cascade']),
            'size': int(out['size']), 'valid': int(out['valid']),
        }
        for field in SCALAR_FIELDS:
            basis[field] = float(out[field])
        for field in VECTOR_FIELDS:
            parts = out[field].split(',')
            if len(parts) != 3:
                raise MalformedInput('%s wants 3 components, got %r' % (field, out[field]))
            basis[field] = [float(p) for p in parts]
    except KeyError as exc:
        raise MalformedInput('map basis line lacks %s' % exc)
    except ValueError as exc:
        raise MalformedInput('map basis line: %s' % exc)
    basis['texel'] = 2.0 * basis['extent'] / basis['size'] if basis['size'] else 0.0
    basis['depth_range'] = basis['depth_light'] + basis['depth_behind']
    return basis


def read_bases(log, frames, device, cascades):
    """Stream `log` and collect the basis lines of `frames` x `cascades`.

    Returns {(cascade, frame): basis}. The log is read line by line; nothing
    but the matching lines is retained.
    """
    frames, cascades = set(frames), set(cascades)
    found = {}
    with open(log, 'r', errors='replace') as stream:
        for line in stream:
            if not line.startswith(BASIS_PREFIX):
                continue
            basis = parse_basis_line(line)
            if basis['device'] != device or basis['frame'] not in frames or basis['cascade'] not in cascades:
                continue
            key = (basis['cascade'], basis['frame'])
            previous = found.get(key)
            if previous is None:
                found[key] = basis
            elif any(previous[f] != basis[f] for f in ('size', 'valid') + SCALAR_FIELDS + VECTOR_FIELDS):
                raise MalformedInput('conflicting map basis lines for cascade %d frame %d' % key)
    return found


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def alignment(basis_a, basis_b):
    """Box shift in whole texels and the depth origin drift between two frames.

    `shift_x`/`shift_y`: map B texel (u + shift_x, v + shift_y) holds the world
    point that map A holds at (u, v). `depth_offset`: what to add to a B depth
    to put it on A's origin. `shift_residual` is the distance of the raw shift
    from a whole texel (the snap makes it ~0; a non-zero value means the grids
    do not share a phase and the alignment is approximate). `basis_turn` is the
    largest axis component change: a re-derived sun direction turns the basis
    and no whole-texel shift can align the two maps.
    """
    for basis in (basis_a, basis_b):
        if not basis['valid']:
            raise MalformedInput('cascade %d frame %d basis is not valid' % (basis['cascade'], basis['frame']))
    if basis_a['size'] != basis_b['size'] or basis_a['extent'] != basis_b['extent']:
        raise MalformedInput('cascade %d geometry changed between frames %d and %d'
                             % (basis_a['cascade'], basis_a['frame'], basis_b['frame']))
    delta = [basis_b['center'][i] - basis_a['center'][i] for i in range(3)]
    texel = basis_b['texel']
    raw_x = -_dot(delta, basis_b['right']) / texel
    raw_y = _dot(delta, basis_b['up']) / texel
    shift_x, shift_y = int(round(raw_x)), int(round(raw_y))
    turn = max(abs(basis_b[f][i] - basis_a[f][i]) for f in ('right', 'up', 'forward') for i in range(3))
    return {
        'shift_x': shift_x, 'shift_y': shift_y,
        'shift_raw_x': raw_x, 'shift_raw_y': raw_y,
        'shift_residual': max(abs(raw_x - shift_x), abs(raw_y - shift_y)),
        'depth_offset': _dot(delta, basis_b['forward']) / basis_b['depth_range'],
        'center_delta': delta,
        'center_delta_len': _dot(delta, delta) ** .5,
        'basis_turn': turn,
        'texel': texel, 'depth_range': basis_b['depth_range'], 'extent': basis_b['extent'], 'size': basis_b['size'],
    }


def _overlap(size, shift_x, shift_y):
    """Slices (a_rows, a_cols, b_rows, b_cols) of the texels the two share."""
    a_x0, b_x0 = max(0, -shift_x), max(0, shift_x)
    a_y0, b_y0 = max(0, -shift_y), max(0, shift_y)
    width, height = size - abs(shift_x), size - abs(shift_y)
    if width <= 0 or height <= 0:
        raise MalformedInput('shift (%d, %d) leaves no overlap at size %d' % (shift_x, shift_y, size))
    return (slice(a_y0, a_y0 + height), slice(a_x0, a_x0 + width),
            slice(b_y0, b_y0 + height), slice(b_x0, b_x0 + width))


def _compare(map_a, map_b, shift_x, shift_y, depth_offset, eps, empty):
    """Flip / change counts of map B against map A under one alignment."""
    import numpy as np
    ay, ax, by, bx = _overlap(map_a.shape[0], shift_x, shift_y)
    a, b = map_a[ay, ax], map_b[by, bx]
    occ_a, occ_b = a < empty, b < empty
    both = occ_a & occ_b
    delta = np.abs((b + depth_offset) - a)
    delta = np.where(both, delta, 0.0)
    changed = both & (delta > eps)
    changed_count = int(changed.sum())
    both_count = int(both.sum())
    return {
        'overlap_texels': int(a.size),
        'occupied_a': int(occ_a.sum()), 'occupied_b': int(occ_b.sum()), 'occupied_both': both_count,
        'flips': int(int(occ_a.sum()) + int(occ_b.sum()) - 2 * both_count),
        'changed': changed_count,
        # of the reference frame's occupied texels, as the diagnosis counts them
        'changed_fraction': (changed_count / int(occ_a.sum())) if occ_a.any() else 0.0,
        'changed_fraction_both': (changed_count / both_count) if both_count else 0.0,
        'mean_abs_delta_changed': float(delta[changed].mean()) if changed_count else 0.0,
        'p50_abs_delta_both': float(np.median(delta[both])) if both_count else 0.0,
    }


def diff_pair(directory, device, cascade, frame_a, frame_b, basis_a, basis_b, eps=DEFAULT_EPS, empty=DEFAULT_EMPTY,
              maps=None):
    """One consecutive pair of one cascade: alignment, aligned and unaligned churn.

    `maps` optionally supplies {frame: array} so a burst reads each map once.
    """
    align = alignment(basis_a, basis_b)
    size = align['size']
    load = (lambda frame: _unpack_map(Path(directory) / ('shadow_map%d_%d_%d.r32f' % (cascade, device, frame)), size))
    map_a = maps[frame_a] if maps and frame_a in maps else load(frame_a)
    map_b = maps[frame_b] if maps and frame_b in maps else load(frame_b)
    record = {
        'cascade': cascade, 'device': device, 'frame_a': frame_a, 'frame_b': frame_b, 'eps': eps, 'empty': empty,
    }
    record.update(align)
    record['aligned'] = _compare(map_a, map_b, align['shift_x'], align['shift_y'], align['depth_offset'], eps, empty)
    record['unaligned'] = _compare(map_a, map_b, 0, 0, 0.0, eps, empty)
    return record


def diff_burst(directory, log, frames, cascades, device=1, eps=DEFAULT_EPS, empty=DEFAULT_EMPTY):
    """Every consecutive pair of `frames` for every cascade in `cascades`."""
    frames = sorted(frames)
    if len(frames) < 2:
        raise MalformedInput('need at least two frames')
    bases = read_bases(log, frames, device, cascades)
    records = []
    for cascade in sorted(cascades):
        maps = {}
        for frame_a, frame_b in zip(frames, frames[1:]):
            for frame in (frame_a, frame_b):
                if (cascade, frame) not in bases:
                    raise MalformedInput('no map basis line for cascade %d frame %d device %d' % (cascade, frame, device))
            size = bases[(cascade, frame_b)]['size']
            for frame in (frame_a, frame_b):
                if frame not in maps:
                    maps[frame] = _unpack_map(
                        Path(directory) / ('shadow_map%d_%d_%d.r32f' % (cascade, device, frame)), size)
            records.append(diff_pair(directory, device, cascade, frame_a, frame_b,
                                     bases[(cascade, frame_a)], bases[(cascade, frame_b)], eps, empty, maps))
            maps.pop(frame_a, None)
    return records


def format_record(record):
    lines = ['c%d %d->%d shift=(%+d, %+d) texels residual=%.3f depth_offset=%+.3e turn=%.1e centre_delta=%.1f u'
             % (record['cascade'], record['frame_a'], record['frame_b'], record['shift_x'], record['shift_y'],
                record['shift_residual'], record['depth_offset'], record['basis_turn'], record['center_delta_len'])]
    for name in ('aligned', 'unaligned'):
        part = record[name]
        lines.append('    %-9s occupied %d/%d (both %d) flips %d  |dz|>%g on %d (%.1f %%) mean %.3e p50 %.3e'
                     % (name, part['occupied_a'], part['occupied_b'], part['occupied_both'], part['flips'],
                        record['eps'], part['changed'], 100.0 * part['changed_fraction'],
                        part['mean_abs_delta_changed'], part['p50_abs_delta_both']))
    return '\n'.join(lines)


def parse_frames(text):
    frames = []
    for piece in text.replace(',', ' ').split():
        if '-' in piece[1:]:
            first, _, last = piece.partition('-')
            frames.extend(range(int(first), int(last) + 1))
        else:
            frames.append(int(piece))
    return sorted(set(frames))


def find_log(directory):
    candidates = sorted(Path(directory).glob('session-*.log'))
    if len(candidates) != 1:
        raise MalformedInput('%d session-*.log in %s; pass --log' % (len(candidates), directory))
    return candidates[0]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--run', required=True, help='run directory with the F8 shadow_map dumps')
    parser.add_argument('--log', help='session log (default: the single session-*.log of --run)')
    parser.add_argument('--frames', required=True, help='frames, e.g. 14780-14787 or 14780,14781')
    parser.add_argument('--cascade', type=int, action='append', dest='cascades', required=True,
                        help='cascade index, repeatable')
    parser.add_argument('--device', type=int, default=1)
    parser.add_argument('--eps', type=float, default=DEFAULT_EPS, help='|dz| threshold (default %g)' % DEFAULT_EPS)
    parser.add_argument('--empty', type=float, default=DEFAULT_EMPTY, help='cleared texel value (default %g)' % DEFAULT_EMPTY)
    parser.add_argument('--json', help='write the records as JSON here ("-" for stdout)')
    args = parser.parse_args(argv)
    log = args.log or find_log(args.run)
    records = diff_burst(args.run, log, parse_frames(args.frames), args.cascades, args.device, args.eps, args.empty)
    if args.json == '-':
        json.dump(records, sys.stdout, indent=1, sort_keys=True)
        sys.stdout.write('\n')
    else:
        for record in records:
            print(format_record(record))
        if args.json:
            Path(args.json).write_text(json.dumps(records, indent=1, sort_keys=True) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
