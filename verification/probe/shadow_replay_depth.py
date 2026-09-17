#!/usr/bin/env python3
"""Host side of the cascade-0 depth replay fixture (docs/architecture/
shadow-replay-gates.md, "Implemented: cascade-0 depth replay fixture").

Two parts, both without Wine:

* the parser of the DLL's per-frame `shadow_replay_depth` line and its
  refusal samples, with the identities the note states (replayed is either
  draws or 0; every skipped bucket is bounded by draws);
* the CPU projection of the fixture's geometry through the same chain the
  authored vertex program applies (clip rows -> p_view -> world -> sun-space
  NDC) and a rasterizer of the resulting triangles at the D3D9 sample
  positions, so the map the DLL wrote can be compared texel by texel.
"""
import math
import re
import statistics

DEPTH_PREFIX = 'shadow_replay_depth '
REFUSED_PREFIX = 'shadow_replay_depth_refused '
TARGET_PREFIX = 'shadow_replay_depth_target '
DEPTH_FIELDS = ('device', 'frame', 'replayed', 'skipped_lease', 'skipped_state', 'skipped_caps', 'draws', 'us')
# Cascades on (docs/architecture/shadow-cascades.md, section 4): draws<i> per
# configured cascade (issues drawn into it this frame), then these.
CASCADE_FIELDS = ('far_replayed', 'far_frame', 'issues', 'budget')
FIELD = re.compile(r'(\w+)=(\S+)')
DEPTH_CODE = 1.0 / 65536.0   # one 16-bit depth code in normalized sun-space depth (reported, not the gate)
# The gate is the note's 1e-4 in normalized depth (shadow-replay-gates.md,
# section 2): the rasterizer snaps vertices to a sub-texel grid, so the
# interpolated depth moves by the depth gradient per texel over that grid;
# the fixture's narrow cascade (8-unit extent) magnifies it to 1-2 codes.
DEPTH_TOLERANCE = 1e-4
# Sample positions this close to a triangle edge are ambiguous (not compared):
# the rasterizer snaps vertices to a sub-texel grid, so coverage within that
# distance of an edge is its call (measured: one texel per frame at 1e-3).
EDGE_EPSILON_PX = 1.0 / 16.0
COVERAGE_TOLERANCE = 0.02    # covered-texel count of the map versus the CPU rasterization
# D3D9 samples pixel (i, j) at integer screen coordinates (the documented
# half-pixel convention, "Directly Mapping Texels to Pixels"): texel (i, j) of
# the map holds the depth at screen (i, j), not (i + 0.5, j + 0.5). Measured on
# the fixture map: offset 0 gives zero coverage disagreements and <= 1.4e-5
# depth error; 0.5 gives 31 disagreements and 2.3e-4.
SAMPLE_OFFSET = 0.0


class MalformedLine(ValueError):
    pass


def fields(line):
    return dict(FIELD.findall(line))


def parse_depth_line(line):
    body = line[len(DEPTH_PREFIX):].strip()
    pairs = FIELD.findall(body)
    if [k for k, _ in pairs[:len(DEPTH_FIELDS)]] != list(DEPTH_FIELDS) or ' '.join(f'{k}={v}' for k, v in pairs) != body:
        raise MalformedLine(line.strip())
    tail, pairs = pairs[len(DEPTH_FIELDS):], pairs[:len(DEPTH_FIELDS)]
    row = {}
    # The at-rest sun-shadow A/B (comparison-hotkeys.md, "Sun shadows at
    # rest"): shadow_toggle= is the state this frame replayed under. Absent in
    # logs written before the key existed; a frame toggled off has no line.
    if tail and tail[0][0] == 'shadow_toggle':
        if tail[0][1] not in ('0', '1'):
            raise MalformedLine(line.strip())
        row['shadow_toggle'] = int(tail[0][1])
        tail = tail[1:]
    # Live caster retention (shadow-caster-retention.md): after the cascade
    # fields, replayed_live<i> replayed_retained<i> per cascade.
    retained = [(k, v) for k, v in tail if k.startswith('replayed_')]
    if retained:
        tail = tail[:len(tail) - len(retained)]
        count = len(retained) // 2
        if not tail or len(retained) % 2 or [k for k, _ in retained] != [f'replayed_{kind}{i}' for i in range(count) for kind in ('live', 'retained')]:
            raise MalformedLine(line.strip())
        try:
            values = [int(v) for _, v in retained]
        except ValueError as error:
            raise MalformedLine(line.strip()) from error
        if any(v < 0 for v in values) or count != len(tail) - len(CASCADE_FIELDS):
            raise MalformedLine(line.strip())
        row['retention'] = {'live': values[0::2], 'retained': values[1::2]}
    if tail:
        count = len(tail) - len(CASCADE_FIELDS)
        if not 1 <= count <= 4 or [k for k, _ in tail] != [f'draws{i}' for i in range(count)] + list(CASCADE_FIELDS):
            raise MalformedLine(line.strip())
        try:
            values = [int(v) for _, v in tail]
        except ValueError as error:
            raise MalformedLine(line.strip()) from error
        cascades = {'count': count, 'draws': values[:count], **dict(zip(CASCADE_FIELDS, values[count:]))}
        # far_frame is -1 while the far cascade is absent; everything else counts.
        if any(v < 0 for v in cascades['draws']) or cascades['far_replayed'] not in (0, 1) or cascades['far_frame'] < -1 or cascades['issues'] < 0 or cascades['budget'] < 1:
            raise MalformedLine(line.strip())
        if sum(cascades['draws']) > cascades['issues'] or (cascades['far_replayed'] and count > 1 and not cascades['draws'][-1]):
            raise MalformedLine(f'cascade draws inconsistent: {line.strip()}')
        row['cascades'] = cascades
        if 'retention' in row and [a + b for a, b in zip(row['retention']['live'], row['retention']['retained'])] != cascades['draws']:
            raise MalformedLine(f'live + retained issues differ from the cascade draws: {line.strip()}')
    for key, value in pairs:
        try:
            row[key] = float(value) if key == 'us' else int(value)
        except ValueError as error:
            raise MalformedLine(line.strip()) from error
        if row[key] < 0:
            raise MalformedLine(line.strip())
    check_identities(row)
    return row


def check_identities(row):
    """replayed is draws or 0; every skipped bucket <= draws; a replayed frame
    skipped nothing; a frame that skipped anything replayed nothing."""
    draws = row['draws']
    skipped = row['skipped_lease'] + row['skipped_state'] + row['skipped_caps']
    if row['replayed'] not in (0, draws):
        raise MalformedLine(f'replayed {row["replayed"]} is neither 0 nor draws {draws}')
    if any(row[k] > draws for k in ('skipped_lease', 'skipped_state', 'skipped_caps')):
        raise MalformedLine(f'a skipped bucket exceeds draws: {row}')
    if row['replayed'] and skipped:
        raise MalformedLine(f'replayed frame with skipped records: {row}')
    if draws and not row['replayed'] and not skipped:
        raise MalformedLine(f'nothing replayed and nothing skipped: {row}')
    return row


def parse_text(text):
    """(depth rows, refusal rows, target rows) from a capture log."""
    depth, refused, targets = [], [], []
    for line in text.splitlines():
        if line.startswith(DEPTH_PREFIX):
            depth.append(parse_depth_line(line))
        elif line.startswith(REFUSED_PREFIX):
            refused.append(fields(line[len(REFUSED_PREFIX):]))
        elif line.startswith(TARGET_PREFIX):
            targets.append({k: int(v) for k, v in fields(line[len(TARGET_PREFIX):]).items()})
    return depth, refused, targets


def us_summary(rows):
    values = [r['us'] for r in rows if r['replayed']]
    if not values:
        return {'frames': 0}
    return {'frames': len(values), 'min': min(values), 'median': statistics.median(values), 'max': max(values)}


# ---- the projection chain -------------------------------------------------

def shape_vertices(shape, scale=1.0):
    """The fixture's object-space triangles (position (x, y, 0.5, 1)): the
    casters A and B, the large bounds object L (origin 256 units away under
    rows t = 204.8, p = .004; the triangle across the box) and the far object
    F (origin 300 units away, vertices 225 units away: nothing on the map).
    `scale` scales x and y (the pool script's sized casters; z stays 0.5)."""
    tri = {'A': ((-1, 1), (3, 1), (-1, -3)), 'B': ((-.9, .9), (-.3, .9), (-.9, .3)),
           'L': ((-214, 8), (-214, -8), (-195, 0)), 'F': ((-60, 4), (-60, -4), (-56, 0))}[shape]
    return [(x * scale, y * scale, .5) for x, y in tri]


def rows_matrix(t, p, zo):
    """The fixture's clip rows c24-c27 (four dp4 rows)."""
    return ((1, 0, 0, t), (0, 1, 0, 0), (0, 0, 1, zo), (p, 0, 0, 1))


def project_vertex(vertex, rows, camera, basis):
    """Object position -> sun-space (ndc_x, ndc_y, depth) as the vertex
    program computes it: clip = rows . pos; p_view = (clip.x / m00,
    clip.y / m11, clip.w); world = (p_view - t) R^T; sun-space over the
    cascade's half-extent and depth half-range."""
    pos = (vertex[0], vertex[1], vertex[2], 1.0)
    clip = [sum(r[k] * pos[k] for k in range(4)) for r in rows]
    view = (clip[0] / camera['m00'], clip[1] / camera['m11'], clip[3])
    r, t = camera['r'], camera['t']
    world = [sum((view[j] - t[j]) * r[i * 3 + j] for j in range(3)) for i in range(3)]
    d = [world[i] - basis['center'][i] for i in range(3)]
    dot = lambda axis: sum(d[i] * basis[axis][i] for i in range(3))
    # The asymmetric range of a cascade (depth_light towards the light,
    # depth_behind beyond the centre) or the single map's symmetric half range.
    e = basis['extent']
    light, behind = basis.get('depth_light', basis.get('depth_half')), basis.get('depth_behind', basis.get('depth_half'))
    return (dot('right') / e, dot('up') / e, (dot('forward') + light) / (light + behind))


def to_texels(ndc_x, ndc_y, size):
    """Viewport transform: NDC x in [-1, 1] -> screen [0, N], NDC y = +1 at
    screen row 0 (top); the sample position of texel (i, j) is SAMPLE_OFFSET
    texels into it."""
    return ((ndc_x * .5 + .5) * size, (.5 - ndc_y * .5) * size)


def rasterize(triangles, size):
    """Per-texel minimum depth over the triangles (1.0 where uncovered) at
    the D3D9 sample positions, plus the ambiguous mask (a sample within
    EDGE_EPSILON_PX of an edge). `triangles` are lists of three (px, py, depth). numpy."""
    import numpy as np
    depth = np.ones((size, size), dtype=np.float64)
    ambiguous = np.zeros((size, size), dtype=bool)
    for tri in triangles:
        (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = tri
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(area) < 1e-12:
            continue
        lo_x = max(int(math.floor(min(x0, x1, x2))), 0); hi_x = min(int(math.ceil(max(x0, x1, x2))), size - 1)
        lo_y = max(int(math.floor(min(y0, y1, y2))), 0); hi_y = min(int(math.ceil(max(y0, y1, y2))), size - 1)
        if lo_x > hi_x or lo_y > hi_y:
            continue
        xs = np.arange(lo_x, hi_x + 1, dtype=np.float64) + SAMPLE_OFFSET
        ys = np.arange(lo_y, hi_y + 1, dtype=np.float64) + SAMPLE_OFFSET
        px, py = np.meshgrid(xs, ys)
        # Barycentric weights (signed, normalized by the area).
        w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area
        w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area
        w2 = 1.0 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        # Distance to the nearest edge in texels: |w_k| * 2 * area / |edge_k|.
        edges = (math.hypot(x2 - x1, y2 - y1), math.hypot(x0 - x2, y0 - y2), math.hypot(x1 - x0, y1 - y0))
        near = np.zeros(px.shape, dtype=bool)
        for w, edge in zip((w0, w1, w2), edges):
            if edge > 0:
                near |= np.abs(w) * abs(area) / edge < EDGE_EPSILON_PX
        # Pancaking: a caster nearer the light than the near plane is stored at
        # depth 0 (the replay's pixel program writes max(depth, 0)).
        z = np.maximum(w0 * z0 + w1 * z1 + w2 * z2, 0.0)
        window = depth[lo_y:hi_y + 1, lo_x:hi_x + 1]
        window[:] = np.where(inside & (z < window), z, window)
        ambiguous[lo_y:hi_y + 1, lo_x:hi_x + 1] |= near
    return depth, ambiguous


def expected_map(draws, camera, basis, size):
    """The CPU map for one frame's SHADOW_DRAW records."""
    triangles = []
    for d in draws:
        rows = rows_matrix(d['t'], d['p'], d['zo'])
        tri = []
        for v in shape_vertices(d['shape'], d.get('scale', 1.0)):
            # A retained caster (shadow-caster-retention.md) keeps the world place its rows
            # had under the camera of the frame it was recorded on (`camera` of the draw).
            nx, ny, depth = project_vertex(v, rows, d.get('camera', camera), basis)
            px, py = to_texels(nx, ny, size)
            tri.append((px, py, depth))
        triangles.append(tri)
    return rasterize(triangles, size)


def compare_map(actual, draws, camera, basis, size, tolerance=DEPTH_TOLERANCE):
    """`actual`: the R32F map as a flat float sequence (row-major). Returns
    the comparison record; `ok` requires the covered counts within
    COVERAGE_TOLERANCE, no coverage disagreement away from edges and every
    doubly covered unambiguous texel within `tolerance`."""
    import numpy as np
    gpu = np.asarray(actual, dtype=np.float64).reshape(size, size)
    cpu, ambiguous = expected_map(draws, camera, basis, size)
    covered_cpu = cpu < 1.0
    covered_gpu = gpu < 1.0
    clear = ~ambiguous
    both = covered_cpu & covered_gpu & clear
    disagree = (covered_cpu != covered_gpu) & clear
    error = np.abs(gpu[both] - cpu[both]) if both.any() else np.zeros(0)
    cpu_count, gpu_count = int(covered_cpu.sum()), int(covered_gpu.sum())
    within = abs(cpu_count - gpu_count) <= max(1, COVERAGE_TOLERANCE * cpu_count)
    max_error = float(error.max()) if error.size else 0.0
    return {'size': size, 'covered_cpu': cpu_count, 'covered_gpu': gpu_count, 'ambiguous': int(ambiguous.sum()),
            'compared': int(both.sum()), 'coverage_disagreements': int(disagree.sum()), 'max_depth_error': max_error,
            'max_depth_error_codes16': max_error / DEPTH_CODE, 'tolerance': tolerance, 'finite': bool(np.isfinite(gpu).all()),
            'ok': bool(within and both.any() and disagree.sum() == 0 and max_error <= tolerance and np.isfinite(gpu).all())}
