#!/usr/bin/env python3
"""Offline analysis of live motion-route readbacks (``motion_<device>_<frame>.rgba32f``).

Reads a capture session log once, streaming, and the per-frame RGBA32F readback
files the route writes beside it. No game geometry, shader bytes or renderer
code are needed: every check uses only the log's per-draw ``motion_route``
decisions, the submitted rows ``c24-27`` recorded by the capture, the
``motion_output_frame`` counters and the readback pixels.

Pixel ABI (src/temporal/README.md, src/temporal/rigid_motion_ps.hlsl):

* alpha ``1``: RG hold the previous absolute texture UV *including* the
  half-texel that converts raw D3D9 raster coordinates to texture centres,
  B holds the previous clip Z/W; alpha ``0``: camera reprojection valid
  (never written by this producer); alpha ``-1``: sentinel, no correspondence.
* Raster convention: the D3D9 sample of pixel ``(px, py)`` is at integer raster
  coordinates, i.e. current NDC ``(2*px/W - 1, 1 - 2*py/H)``.  The shader
  writes ``uv = ndc_prev.xy * (0.5, -0.5) + 0.5 + 0.5/size - prior_jitter``.
  With no jitter (checkpoint B1) a static object therefore reports exactly the
  texture-centre UV ``((px + 0.5)/W, (py + 0.5)/H)`` of its own pixel.
* Rows ``c24-27`` are the four submitted rows of the position matrix and are
  applied as ``clip_i = dot(row_i, position)``; the translation column
  ``(c24.w, c25.w, c26.w, c27.w)`` is the clip-space position of the object
  origin.

Checks (details in docs/verification/motion-readback.md):

1. readback integrity and counter cross-checks per captured frame;
2. static consistency for frames whose matched draws reuse bit-identical rows;
3. displacement statistics, suspicious-displacement flags and the row-pair
   consistency test (each valid pixel's previous UV/depth mapped through
   ``M_current * inverse(M_previous)`` of some matched draw must land on the
   pixel itself), which also attributes pixels to draws without geometry;
4. temporal cross-check of consecutive captured frames;
5. RT2 depth-image integrity (``depth_<device>_<frame>.r32f``, R32F device
   depth in [0,1] where a routed draw covered the pixel, ``-1`` elsewhere:
   finiteness, range and sentinel fraction) and the previous-depth comparison:
   frame N+1's previous-depth channel at the previous UV (offset by frame N's
   raster jitter, which the route logs as ``jitter_previous_x/y``) against
   frame N's depth image, sampled nearest (default) or bilinear over the
   non-sentinel taps, with the error distribution reported. Sentinel taps are
   excluded from the comparison and counted;
6. the resolved image written with ``X3M_TAA_DEBUG`` (``taa_<device>_<frame>.rgba16f``,
   the FP16 output of the temporal resolve) against the pre-resolve 8-bit main
   target (``color_<device>_<frame>.bgra8``): finiteness of every value and the
   fraction of pixels whose RGB differs from the current color by more than a
   threshold. A sanity signal only: it says the resolve produced finite values
   and how much of the image history changed, not whether the blend is right.

The log is never loaded whole; only bounded per-draw metadata is kept.
"""
import argparse
import array
from collections import Counter, OrderedDict
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys

FIELDS = re.compile(r'(\S+?)=(\S*)')
# The logged parts of the DLL's RigidDrawKey (motion_output.cpp `motion_route`):
# object/camera identity and epochs, geometry bindings, the position program
# and its declaration offset/type, and the draw arguments.
KEY_FIELDS = ('node', 'camera', 'node_handle', 'camera_handle', 'node_serial', 'camera_serial',
              'load_epoch', 'registry_epoch', 'model', 'lod', 'vb', 'ib', 'declaration', 'offset',
              'stride', 'vs', 'position_offset', 'position_type', 'topology', 'first', 'primitives',
              'base_vertex', 'min_vertex', 'vertex_count', 'indexed')
ROW_REGISTERS = (24, 25, 26, 27)
# Gates that reach the history table: 0 matched, 6 lookup miss but recorded.
RECORDING_GATES = (0, 6)
GATE_NAMES = {0: 'matched', 1: 'gate1', 2: 'gate2', 3: 'gate3', 4: 'gate4', 5: 'gate5', 6: 'gate6'}
DISPLACEMENT_BINS = (0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0)
DEPTH_ERROR_BINS = (1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1)
PIXEL_BYTES = 16


class MalformedInput(Exception):
    """Input that cannot be analysed at all (as opposed to a failing check)."""


def parse_fields(line):
    return dict(FIELDS.findall(line))


def number(text, default=None):
    try:
        return int(text, 10)
    except (TypeError, ValueError):
        return default


def real(text, default=None):
    """Float field (jitter, cut statistics); None/default when absent or malformed."""
    try:
        value = float(text)
    except (TypeError, ValueError):
        return default
    return value if math.isfinite(value) else default


def hresult_ok(text):
    try:
        return int(text, 16) == 0
    except (TypeError, ValueError):
        return False


def bits_to_float(hex_word):
    return struct.unpack('<f', struct.pack('<I', int(hex_word, 16) & 0xffffffff))[0]


def decode_rows(row_bits):
    return tuple(tuple(bits_to_float(word) for word in row.split(',')) for row in row_bits)


class Draw:
    __slots__ = ('device', 'frame', 'index', 'vs', 'ps', 'viewport', 'rows_bits', 'rows', 'rows_hash',
                 'gate', 'routed', 'matched', 'key', 'key_fields', 'draw_result', 'route_result',
                 'previous_bits', 'previous', 'previous_index', 'predicted', 'history_note')

    def __init__(self, device, frame, index):
        self.device, self.frame, self.index = device, frame, index
        self.vs = self.ps = None
        self.viewport = None
        self.rows_bits = self.rows = None
        self.rows_hash = None
        self.gate = self.routed = self.matched = None
        self.key = self.key_fields = None
        self.draw_result = self.route_result = None
        self.previous_bits = self.previous = None
        self.previous_index = None
        self.predicted = None
        self.history_note = None


class Frame:
    def __init__(self, device, frame):
        self.device, self.frame = device, frame
        self.captured = False
        self.draw_count = None
        self.draws = []          # scene draws with a motion_route line, in order
        self.summary = None      # motion_output_frame fields
        self.readback = None     # motion_output_readback fields
        self.depth_readback = None  # motion_output_depth_readback fields (RT2, step 1)
        self.taa_readback = None    # motion_output_taa_readback fields (resolved FP16, step 3, X3M_TAA_DEBUG)
        self.color_readback = None  # motion_output_color_readback fields (pre-resolve 8-bit main target)
        self.cut = None          # motion_output_cut fields (data-only cut detector)
        self.reset_at = None     # route draws seen before a motion_output_reset logged in this frame
        self.notes = []


def parse_log(lines):
    """Stream the session log; keep frames, route decisions, rows and counters only."""
    frames = OrderedDict()
    devices = {}
    current = {}       # device -> Frame currently being recorded
    pending = None     # Draw record between `draw` and `draw_result`
    vs_float_header = None
    rows_seen = {}

    def frame_for(fields):
        device, frame = number(fields.get('device')), number(fields.get('frame'))
        if device is None or frame is None:
            return None
        record = frames.get((device, frame))
        if record is None:
            record = frames[(device, frame)] = Frame(device, frame)
        return record

    for line in lines:
        if not line or line[0] == ' ':
            continue
        space = line.find(' ')
        tag = line[:space] if space > 0 else line.rstrip('\n')
        if tag == 'constant':
            if pending is None or vs_float_header is None:
                continue
            fields = parse_fields(line)
            if fields.get('kind') == 'vs' and fields.get('type') == 'f':
                reg = number(fields.get('reg'))
                if reg in ROW_REGISTERS:
                    rows_seen[reg] = fields.get('bits', '')
            continue
        if tag in ('sampler', 'state', 'stream', 'transform', 'texture', 'texture_desc',
                   'vertex_element', 'surface', 'capture_event', 'telemetry_metric'):
            continue
        fields = parse_fields(line)
        if tag == 'draw':
            pending = Draw(number(fields.get('device')), number(fields.get('frame')), number(fields.get('index')))
            pending.vs, pending.ps = fields.get('vs'), fields.get('ps')
            vs_float_header = None
            rows_seen = {}
        elif tag == 'constants':
            if fields.get('kind') == 'vs' and fields.get('type') == 'f':
                vs_float_header = fields
            else:
                vs_float_header = None
        elif tag == 'viewport':
            if pending is not None and pending.viewport is None:
                pending.viewport = tuple(number(fields.get(name)) for name in ('x', 'y', 'w', 'h'))
        elif tag == 'motion_route':
            device, frame, index = number(fields.get('device')), number(fields.get('frame')), number(fields.get('index'))
            draw = pending
            if draw is None or (draw.device, draw.frame, draw.index) != (device, frame, index):
                draw = Draw(device, frame, index)
                draw.history_note = 'motion_route without matching draw record'
                record = frame_for(fields)
                if record is not None:
                    record.draws.append(draw)
            draw.gate = number(fields.get('gate'))
            draw.routed = number(fields.get('routed'))
            draw.matched = number(fields.get('matched'))
            draw.rows_hash = fields.get('rows_hash')
            draw.route_result = fields.get('result')
            draw.vs = fields.get('vs', draw.vs)
            draw.ps = fields.get('ps', draw.ps)
            draw.key_fields = {name: fields.get(name) for name in KEY_FIELDS}
            draw.key = tuple(draw.key_fields[name] for name in KEY_FIELDS)
        elif tag == 'draw_result':
            if pending is not None:
                pending.draw_result = fields.get('result')
                if pending.gate is not None:
                    complete = all(reg in rows_seen for reg in ROW_REGISTERS)
                    if not complete and vs_float_header is not None \
                            and vs_float_header.get('encoding') == 'sparse_zero' \
                            and hresult_ok(vs_float_header.get('result')):
                        for reg in ROW_REGISTERS:
                            rows_seen.setdefault(reg, '00000000,00000000,00000000,00000000')
                        complete = True
                    if complete:
                        pending.rows_bits = tuple(rows_seen[reg] for reg in ROW_REGISTERS)
                        try:
                            pending.rows = decode_rows(pending.rows_bits)
                        except ValueError:
                            pending.rows_bits = pending.rows = None
                    record = frame_for({'device': str(pending.device), 'frame': str(pending.frame)})
                    if record is not None:
                        record.draws.append(pending)
            pending = None
            vs_float_header = None
            rows_seen = {}
        elif tag == 'frame_begin':
            record = frame_for(fields)
            if record is not None:
                current[record.device] = record
        elif tag == 'frame_end':
            record = frame_for(fields)
            if record is not None:
                record.captured = number(fields.get('capture')) == 1
                record.draw_count = number(fields.get('draws'))
        elif tag == 'motion_output_frame':
            record = frame_for(fields)
            if record is not None:
                record.summary = fields
        elif tag == 'motion_output_readback':
            record = frame_for(fields)
            if record is not None:
                record.readback = fields
        elif tag == 'motion_output_depth_readback':
            record = frame_for(fields)
            if record is not None:
                record.depth_readback = fields
        elif tag == 'motion_output_taa_readback':
            record = frame_for(fields)
            if record is not None:
                record.taa_readback = fields
        elif tag == 'motion_output_color_readback':
            record = frame_for(fields)
            if record is not None:
                record.color_readback = fields
        elif tag == 'motion_output_cut':
            record = frame_for(fields)
            if record is not None:
                record.cut = fields
        elif tag == 'motion_output_reset':
            device = number(fields.get('device'))
            record = current.get(device)
            if record is not None and record.reset_at is None:
                record.reset_at = len(record.draws)
        elif tag == 'motion_output_device':
            devices[fields.get('device')] = fields
    return frames, devices


# ---- history pairing ----------------------------------------------------------

def pair_history(frames):
    """Reconstruct the DLL's previous-frame table from the log to obtain previous rows.

    Returns per-frame notes.  Prediction is possible only when frame N-1 of the
    same device was captured and committed and no Reset intervened.
    """
    for (device, number_), frame in frames.items():
        previous = frames.get((device, number_ - 1))
        note = None
        table = None
        if previous is None or not previous.captured:
            note = 'previous frame not captured: previous rows unknown'
        elif previous.summary is not None and previous.summary.get('committed') != '1':
            note = 'previous frame not committed: previous rows unknown'
        elif previous.reset_at is not None:
            table = {}
            note = 'history invalidated by Reset in the previous frame'
        else:
            table = {}
            for draw in previous.draws:
                if draw.routed == 1 and draw.gate in RECORDING_GATES:
                    table.setdefault(draw.key, []).append(draw)
        if frame.reset_at is not None:
            frame.notes.append(f'Reset logged after {frame.reset_at} route decisions: later lookups see an empty table')
        if note:
            frame.notes.append(note)
        consumed = set()
        for position, draw in enumerate(frame.draws):
            if draw.routed != 1 or draw.gate not in RECORDING_GATES:
                continue
            if table is None:
                draw.predicted = None
                continue
            if frame.reset_at is not None and position >= frame.reset_at:
                table, consumed = {}, set()
            entries = table.get(draw.key)
            if not entries:
                draw.predicted = False
            elif len(entries) > 1:
                draw.predicted = False
                draw.history_note = 'previous frame duplicate key (poisoned)'
            elif draw.key in consumed:
                draw.predicted = False
                draw.history_note = 'current frame duplicate key (consumed once)'
            else:
                draw.predicted = True
                consumed.add(draw.key)
                if draw.gate == 0:
                    source = entries[0]
                    draw.previous_index = source.index
                    draw.previous_bits = source.rows_bits
                    draw.previous = source.rows
                    if source.rows_bits is None and source.rows_hash is not None:
                        draw.history_note = 'previous rows only known by hash'


def frame_static_state(frame, frames):
    """'static', 'moving', 'unknown' or 'no_matched' from the log alone."""
    matched = [draw for draw in frame.draws if draw.gate == 0]
    if not matched:
        return 'no_matched'
    previous = frames.get((frame.device, frame.frame - 1))
    identical = []
    for draw in matched:
        if draw.previous_bits is not None and draw.rows_bits is not None:
            identical.append(draw.previous_bits == draw.rows_bits)
        elif draw.previous_index is not None and previous is not None and draw.rows_hash is not None:
            source = next((d for d in previous.draws if d.index == draw.previous_index), None)
            if source is None or source.rows_hash is None:
                return 'unknown'
            identical.append(source.rows_hash == draw.rows_hash)
        else:
            return 'unknown'
    return 'static' if all(identical) else 'moving'


# ---- readback ---------------------------------------------------------------------

class Readback:
    """One RGBA32F readback: validity map plus packed arrays of the valid pixels."""

    def __init__(self, width, height):
        self.width, self.height = width, height
        self.validity = array.array('b', bytes(width * height))  # 1, 0, -1, 2=other
        self.counts = Counter()
        self.nonfinite = 0
        self.index = array.array('I')
        self.u = array.array('f')
        self.v = array.array('f')
        self.z = array.array('f')


def load_readback(path, width, height):
    size = path.stat().st_size
    expected = width * height * PIXEL_BYTES
    if size != expected:
        raise MalformedInput(f'{path.name}: size {size} != {width}x{height}x16 = {expected}')
    result = Readback(width, height)
    validity = result.validity
    counts = Counter()
    isfinite = math.isfinite
    with path.open('rb') as stream:
        for py in range(height):
            row = array.array('f')
            row.fromfile(stream, width * 4)
            if sys.byteorder != 'little':
                row.byteswap()
            base = py * width
            for px in range(width):
                offset = px * 4
                u, v, z, a = row[offset], row[offset + 1], row[offset + 2], row[offset + 3]
                finite = isfinite(u) and isfinite(v) and isfinite(z) and isfinite(a)
                if not finite:
                    result.nonfinite += 1
                if a == 1.0 and finite:
                    validity[base + px] = 1
                    counts['valid'] += 1
                    result.index.append(base + px)
                    result.u.append(u)
                    result.v.append(v)
                    result.z.append(z)
                elif a == -1.0:
                    validity[base + px] = -1
                    counts['sentinel'] += 1
                elif a == 0.0:
                    validity[base + px] = 0
                    counts['zero'] += 1
                else:
                    validity[base + px] = 2
                    counts['other'] += 1
    result.counts = counts
    return result


DEPTH_SENTINEL = -1.0


def load_r32f(path, width, height):
    size = path.stat().st_size
    if size != width * height * 4:
        raise MalformedInput(f'{path.name}: size {size} != {width}x{height}x4')
    data = array.array('f')
    with path.open('rb') as stream:
        data.fromfile(stream, width * height)
    if sys.byteorder != 'little':
        data.byteswap()
    return data


def depth_image_stats(data):
    """RT2 contract: finite, device depth in [0,1] where written, -1 sentinel elsewhere."""
    isfinite = math.isfinite
    nonfinite = sentinel = written = out_of_range = 0
    low, high = None, None
    for value in data:
        if not isfinite(value):
            nonfinite += 1
        elif value == DEPTH_SENTINEL:
            sentinel += 1
        elif 0.0 <= value <= 1.0:
            written += 1
            low = value if low is None or value < low else low
            high = value if high is None or value > high else high
        else:
            out_of_range += 1
    total = len(data)
    return {
        'pixels': total, 'nonfinite': nonfinite, 'sentinel': sentinel, 'written': written,
        'out_of_range': out_of_range,
        'sentinel_fraction': (sentinel / total) if total else None,
        'written_fraction': (written / total) if total else None,
        'written_range': [low, high] if written else None,
        'clean': nonfinite == 0 and out_of_range == 0,
    }


def half_to_float(bits):
    """IEEE binary16 -> float (subnormals, infinities and NaN included)."""
    sign = -1.0 if bits & 0x8000 else 1.0
    exponent = (bits >> 10) & 31
    mantissa = bits & 1023
    if exponent == 31:
        return sign * (math.nan if mantissa else math.inf)
    if exponent == 0:
        return sign * mantissa * 2.0 ** -24
    return sign * (1024 + mantissa) * 2.0 ** (exponent - 25)


HALF_TABLE = [half_to_float(bits) for bits in range(65536)]


def load_rgba16f(path, width, height):
    """Row-major RGBA FP16 image (the resolved output) as RGB float triples."""
    size = path.stat().st_size
    if size != width * height * 8:
        raise MalformedInput(f'{path.name}: size {size} != {width}x{height}x8')
    data = array.array('H')
    with path.open('rb') as stream:
        data.fromfile(stream, width * height * 4)
    if sys.byteorder != 'little':
        data.byteswap()
    table = HALF_TABLE
    return [(table[data[i]], table[data[i + 1]], table[data[i + 2]]) for i in range(0, len(data), 4)]


def load_bgra8(path, width, height):
    """Row-major A8R8G8B8 image (the pre-resolve main target) as RGB triples in [0,1]."""
    size = path.stat().st_size
    if size != width * height * 4:
        raise MalformedInput(f'{path.name}: size {size} != {width}x{height}x4')
    data = path.read_bytes()
    return [(data[i + 2] / 255.0, data[i + 1] / 255.0, data[i] / 255.0) for i in range(0, len(data), 4)]


def taa_image_stats(resolved, current, threshold):
    """Finiteness of the resolved RGB and how many pixels moved away from the
    current color by more than `threshold` in any channel (history contribution)."""
    isfinite = math.isfinite
    nonfinite = differing = 0
    largest = 0.0
    total_difference = 0.0
    for (r, g, b), (cr, cg, cb) in zip(resolved, current):
        if not (isfinite(r) and isfinite(g) and isfinite(b)):
            nonfinite += 1
            continue
        difference = max(abs(r - cr), abs(g - cg), abs(b - cb))
        total_difference += difference
        if difference > largest:
            largest = difference
        if difference > threshold:
            differing += 1
    total = len(resolved)
    return {
        'pixels': total, 'nonfinite': nonfinite, 'differing': differing,
        'differing_fraction': (differing / total) if total else None,
        'max_difference': largest, 'mean_difference': (total_difference / (total - nonfinite)) if total > nonfinite else None,
        'threshold': threshold, 'clean': nonfinite == 0,
    }


# ---- statistics helpers ------------------------------------------------------------

def percentile(sorted_values, fraction):
    if not sorted_values:
        return None
    position = fraction * (len(sorted_values) - 1)
    low = int(math.floor(position))
    high = min(low + 1, len(sorted_values) - 1)
    weight = position - low
    return sorted_values[low] * (1 - weight) + sorted_values[high] * weight


def describe(values):
    if not values:
        return {'count': 0}
    ordered = sorted(values)
    return {
        'count': len(ordered),
        'min': ordered[0],
        'max': ordered[-1],
        'mean': sum(ordered) / len(ordered),
        'median': percentile(ordered, 0.5),
        'p95': percentile(ordered, 0.95),
        'p99': percentile(ordered, 0.99),
    }


def histogram(values, edges):
    labels = []
    previous = 0.0
    for edge in edges:
        labels.append(f'[{previous:g},{edge:g})')
        previous = edge
    labels.append(f'[{previous:g},inf)')
    counts = [0] * (len(edges) + 1)
    for value in values:
        slot = len(edges)
        for position, edge in enumerate(edges):
            if value < edge:
                slot = position
                break
        counts[slot] += 1
    return OrderedDict(zip(labels, counts))


# ---- matrices ----------------------------------------------------------------------

def matrix_multiply(a, b):
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)) for i in range(4))


def matrix_inverse(m):
    """Gauss-Jordan with partial pivoting; returns None when singular."""
    rows = [list(m[i]) + [1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
    scale = max(abs(value) for row in m for value in row) or 1.0
    for column in range(4):
        pivot = max(range(column, 4), key=lambda r: abs(rows[r][column]))
        if abs(rows[pivot][column]) <= 1e-12 * scale:
            return None
        rows[column], rows[pivot] = rows[pivot], rows[column]
        factor = rows[column][column]
        rows[column] = [value / factor for value in rows[column]]
        for r in range(4):
            if r != column and rows[r][column] != 0.0:
                f = rows[r][column]
                rows[r] = [value - f * pivot_value for value, pivot_value in zip(rows[r], rows[column])]
    return tuple(tuple(row[4:]) for row in rows)


def project_origin(rows, width, height):
    """Raster position of the object origin (translation column) or None if w <= 0."""
    x, y, z, w = rows[0][3], rows[1][3], rows[2][3], rows[3][3]
    if not (w > 1e-6) or not all(math.isfinite(value) for value in (x, y, z, w)):
        return None
    return ((x / w * 0.5 + 0.5) * width, (-y / w * 0.5 + 0.5) * height, z / w)


# ---- per-frame analysis ------------------------------------------------------------

def analyze_pixels(frame, readback, options):
    """Displacement statistics, static test and row-pair consistency for one frame."""
    width, height = readback.width, readback.height
    jitter_u, jitter_v = options['jitter_uv']
    raster_jx, raster_jy = raster_jitter_px(frame, options)
    count = len(readback.index)
    magnitudes = array.array('f')
    dx_values = array.array('f')
    dy_values = array.array('f')
    u_min = v_min = z_min = math.inf
    u_max = v_max = z_max = -math.inf
    outside = 0
    bound = options['displacement_bound_px']
    suspicious = 0
    static_max = 0.0
    static_violations = 0
    static_tolerance = options['static_tolerance_px']
    for n in range(count):
        idx = readback.index[n]
        px, py = idx % width, idx // width
        u, v, z = readback.u[n], readback.v[n], readback.z[n]
        if u < u_min: u_min = u
        if u > u_max: u_max = u
        if v < v_min: v_min = v
        if v > v_max: v_max = v
        if z < z_min: z_min = z
        if z > z_max: z_max = z
        if not (0.0 <= u <= 1.0 and 0.0 <= v <= 1.0):
            outside += 1
        # Previous raster position minus current raster position, both in the
        # texture-centre convention (the +0.5 cancels in the difference).
        dx = (u + jitter_u) * width - (px - raster_jx + 0.5)
        dy = (v + jitter_v) * height - (py - raster_jy + 0.5)
        dx_values.append(dx)
        dy_values.append(dy)
        magnitude = math.hypot(dx, dy)
        magnitudes.append(magnitude)
        if magnitude > bound:
            suspicious += 1
        error = max(abs(dx), abs(dy))
        if error > static_max:
            static_max = error
        if error > static_tolerance:
            static_violations += 1
    stats = {
        'valid_pixels': count,
        'uv_range': None if count == 0 else {'u': [u_min, u_max], 'v': [v_min, v_max]},
        'depth_range': None if count == 0 else [z_min, z_max],
        'previous_uv_outside_unit': outside,
        'displacement_px': describe(magnitudes),
        'displacement_x_px': describe(dx_values) if count else {'count': 0},
        'displacement_y_px': describe(dy_values) if count else {'count': 0},
        'displacement_histogram': histogram(magnitudes, DISPLACEMENT_BINS),
        'suspicious_over_bound': suspicious,
        'suspicious_fraction': (suspicious / count) if count else None,
        'static_max_error_px': static_max if count else None,
        'static_violations': static_violations if count else None,
        'raster_jitter_px': [raster_jx, raster_jy],
    }
    return stats, dx_values, dy_values


def row_consistency(frame, readback, dx_values, dy_values, options):
    """Map each sampled valid pixel back through every matched draw's row pair."""
    width, height = readback.width, readback.height
    jitter_u, jitter_v = options['jitter_uv']
    raster_jx, raster_jy = raster_jitter_px(frame, options)
    tolerance = options['consistency_tolerance_px']
    candidates = []
    skipped = []
    for draw in frame.draws:
        if draw.gate != 0:
            continue
        if draw.rows is None or draw.previous is None:
            skipped.append({'index': draw.index, 'reason': 'rows or previous rows unknown'})
            continue
        inverse = matrix_inverse(draw.previous)
        if inverse is None:
            skipped.append({'index': draw.index, 'reason': 'previous rows singular'})
            continue
        candidates.append((draw, matrix_multiply(draw.rows, inverse)))
    result = {
        'status': 'unavailable',
        'matched_draws': sum(1 for draw in frame.draws if draw.gate == 0),
        'draws_with_row_pairs': len(candidates),
        'skipped_draws': skipped,
    }
    if not candidates:
        result['reason'] = 'no matched draw with known current and previous rows'
        return result
    # Group draws that share the same reprojection map (static objects under one
    # camera share `P*V_cur*inverse(V_prev)*inverse(P)`).
    groups = []
    group_of_draw = {}
    for draw, matrix in candidates:
        signature = tuple(round(value, 9) for row in matrix for value in row)
        for group in groups:
            if group['signature'] == signature:
                group['draws'].append(draw.index)
                group_of_draw[draw.index] = group
                break
        else:
            group = {'signature': signature, 'matrix': matrix, 'draws': [draw.index], 'attributed': 0,
                     'dx': [], 'dy': [], 'errors': []}
            groups.append(group)
            group_of_draw[draw.index] = group
    count = len(readback.index)
    if count == 0:
        result.update({'status': 'unavailable', 'reason': 'no valid pixels'})
        return result
    stride = max(1, int(math.ceil(count / options['max_consistency_pixels'])))
    sampled = explained = ambiguous = 0
    best_errors = []
    unexplained_examples = []
    half_u, half_v = 0.5 / width, 0.5 / height
    for n in range(0, count, stride):
        sampled += 1
        idx = readback.index[n]
        px, py = idx % width, idx // width
        # Undo the half texel and the (zero) jitter to recover the previous NDC.
        xp = 2.0 * (readback.u[n] + jitter_u - half_u) - 1.0
        yp = -(2.0 * (readback.v[n] + jitter_v - half_v) - 1.0)
        zp = readback.z[n]
        best = None
        best_group = None
        hits = 0
        for group in groups:
            m = group['matrix']
            cw = m[3][0] * xp + m[3][1] * yp + m[3][2] * zp + m[3][3]
            if abs(cw) < 1e-12:
                continue
            cx = (m[0][0] * xp + m[0][1] * yp + m[0][2] * zp + m[0][3]) / cw
            cy = (m[1][0] * xp + m[1][1] * yp + m[1][2] * zp + m[1][3]) / cw
            error = max(abs((cx * 0.5 + 0.5) * width - (px - raster_jx)),
                        abs((-cy * 0.5 + 0.5) * height - (py - raster_jy)))
            if error <= tolerance:
                hits += 1
            if best is None or error < best:
                best, best_group = error, group
        if hits >= 1:
            explained += 1
            best_errors.append(best)
            best_group['attributed'] += 1
            best_group['dx'].append(dx_values[n])
            best_group['dy'].append(dy_values[n])
            best_group['errors'].append(best)
            if hits > 1:
                ambiguous += 1
        elif len(unexplained_examples) < 8:
            unexplained_examples.append({'pixel': [px, py], 'previous_uv': [readback.u[n], readback.v[n]],
                                         'previous_depth': zp, 'best_error_px': best})
    per_draw = []
    for draw, _ in candidates:
        group = group_of_draw[draw.index]
        current_origin = project_origin(draw.rows, width, height)
        previous_origin = project_origin(draw.previous, width, height)
        expected = None
        if current_origin and previous_origin:
            expected = [previous_origin[0] - current_origin[0], previous_origin[1] - current_origin[1]]
        median = None
        if group['dx']:
            median = [percentile(sorted(group['dx']), 0.5), percentile(sorted(group['dy']), 0.5)]
        entry = {
            'index': draw.index,
            'previous_index': draw.previous_index,
            'group': groups.index(group),
            'origin_current_raster': None if current_origin is None else list(current_origin[:2]),
            'origin_previous_raster': None if previous_origin is None else list(previous_origin[:2]),
            'expected_origin_displacement_px': expected,
            'group_attributed_pixels': group['attributed'],
            'group_median_displacement_px': median,
            'origin_vs_median_difference_px': None if (expected is None or median is None)
            else math.hypot(expected[0] - median[0], expected[1] - median[1]),
            'group_max_error_px': max(group['errors']) if group['errors'] else None,
        }
        per_draw.append(entry)
    result.update({
        'status': 'evaluated',
        'raster_jitter_px': [raster_jx, raster_jy],
        'sample_stride': stride,
        'sampled_pixels': sampled,
        'explained_pixels': explained,
        'unexplained_pixels': sampled - explained,
        'unexplained_fraction': (sampled - explained) / sampled if sampled else None,
        'ambiguous_pixels': ambiguous,
        'max_error_px': max(best_errors) if best_errors else None,
        'groups': [{'draws': group['draws'], 'attributed_pixels': group['attributed']} for group in groups],
        'per_draw': per_draw,
        'unexplained_examples': unexplained_examples,
    })
    return result


def temporal_cross_check(previous_frame, previous_readback, frame, readback, min_matched_ratio,
                         previous_jitter=(0.0, 0.0)):
    if previous_frame is None or previous_readback is None:
        return {'status': 'unavailable', 'reason': 'previous captured frame readback not available'}
    if previous_frame.device != frame.device or previous_frame.frame != frame.frame - 1:
        return {'status': 'unavailable', 'reason': 'previous captured frame is not frame N-1 of the same device'}
    if (previous_readback.width, previous_readback.height) != (readback.width, readback.height):
        return {'status': 'unavailable', 'reason': 'dimensions differ between frames'}
    width, height = readback.width, readback.height
    validity = previous_readback.validity
    # Frame N-1 was rasterized with its own jitter, so the previous UV (which is
    # unjittered) lands on the mask offset by that jitter.
    jx, jy = previous_jitter
    covered = uncovered = offscreen = 0
    for n in range(len(readback.index)):
        u, v = readback.u[n], readback.v[n]
        px, py = int(math.floor(u * width + jx)), int(math.floor(v * height + jy))
        if px < 0 or py < 0 or px >= width or py >= height:
            offscreen += 1
        elif validity[py * width + px] == 1:
            covered += 1
        else:
            uncovered += 1
    in_range = covered + uncovered
    summary = previous_frame.summary or {}
    routed, matched = number(summary.get('routed')), number(summary.get('matched'))
    full = routed is not None and matched is not None and matched > 0 and matched >= min_matched_ratio * routed
    return {
        'status': 'evaluated' if in_range else 'unavailable',
        'reason': None if in_range else 'no in-range valid pixels',
        'previous_frame': previous_frame.frame,
        'previous_jitter_px': [jx, jy],
        'valid_pixels': len(readback.index),
        'previous_offscreen': offscreen,
        'previous_covered': covered,
        'previous_uncovered': uncovered,
        'covered_fraction': (covered / in_range) if in_range else None,
        # The sentinel marks both unrouted and routed-but-unmatched draws, so the
        # coverage criterion is only meaningful when frame N matched (nearly) every routed draw.
        'criterion_applies': bool(full and in_range),
        'criterion_min_matched_ratio': min_matched_ratio,
        'previous_frame_routed': routed,
        'previous_frame_matched': matched,
    }


def sample_depth(depth, width, height, u, v, bilinear):
    """Depth of frame N at texture UV (u, v); None off-screen or when every tap is
    the sentinel. Nearest takes the texel whose centre is closest; bilinear
    weights the four surrounding texel centres and drops sentinel taps,
    renormalizing the rest (a depth edge then leans on its written side)."""
    if not bilinear:
        px, py = int(math.floor(u * width)), int(math.floor(v * height))
        if px < 0 or py < 0 or px >= width or py >= height:
            return None, 'offscreen'
        value = depth[py * width + px]
        return (None, 'sentinel') if value == DEPTH_SENTINEL else (value, None)
    fx, fy = u * width - 0.5, v * height - 0.5
    x0, y0 = int(math.floor(fx)), int(math.floor(fy))
    tx, ty = fx - x0, fy - y0
    if x0 < -1 or y0 < -1 or x0 >= width or y0 >= height:
        return None, 'offscreen'
    total = accumulated = 0.0
    for (px, py, weight) in ((x0, y0, (1 - tx) * (1 - ty)), (x0 + 1, y0, tx * (1 - ty)),
                             (x0, y0 + 1, (1 - tx) * ty), (x0 + 1, y0 + 1, tx * ty)):
        if weight <= 0.0 or px < 0 or py < 0 or px >= width or py >= height:
            continue
        value = depth[py * width + px]
        if value == DEPTH_SENTINEL:
            continue
        total += weight
        accumulated += weight * value
    if total <= 0.0:
        return None, 'sentinel'
    return accumulated / total, None


def raster_jitter_px(frame, options):
    """Frame N's own raster jitter in pixels (`motion_output_frame jitter_x/y`,
    +X right, +Y down), or (0, 0) unless `--jitter-from-log` is requested.

    With `--motion-jitter`/`--taa` the route offsets the projection of every
    scene draw, so a stationary object is rasterized at `p + j` while the
    producer still writes the **unjittered** previous UV (`c216 = (1/W, 1/H,
    0, 0)`: zero prior jitter). Comparing that UV against the jittered raster
    pixel therefore reports a constant `-j` displacement for a static scene.
    Subtracting the frame's own jitter from the raster pixel removes it.
    """
    if not options.get('jitter_from_log'):
        return (0.0, 0.0)
    summary = frame.summary or {}
    if number(summary.get('jitter'), 0) != 1:
        return (0.0, 0.0)
    return (real(summary.get('jitter_x'), 0.0), real(summary.get('jitter_y'), 0.0))


def previous_jitter_px(frame):
    """Frame N's raster jitter as logged by frame N+1 (`jitter_previous_x/y`, pixels,
    +X right, +Y down); zero when the route ran without jitter or the field is absent."""
    summary = frame.summary or {}
    return (real(summary.get('jitter_previous_x'), 0.0), real(summary.get('jitter_previous_y'), 0.0))


def depth_cross_check(previous_frame, previous_depth, frame, readback, tolerance, bilinear=False):
    """Previous-depth channel (B) of frame N+1's valid pixels against frame N's RT2
    image at the previous UV. The producer's RG is the previous *unjittered*
    texture-centre UV while frame N was rasterized with its own jitter, so the
    sample position is RG plus frame N's jitter in UV units."""
    if previous_depth is None:
        return {'status': 'unavailable',
                'reason': 'no R32F depth image for frame N (depth_<device>_<frame>.r32f absent)'}
    if previous_frame is None or previous_frame.frame != frame.frame - 1:
        return {'status': 'unavailable', 'reason': 'previous captured frame is not frame N-1'}
    width, height = readback.width, readback.height
    jx, jy = previous_jitter_px(frame)
    du, dv = jx / width, jy / height
    errors = []
    offscreen = sentinel = 0
    for n in range(len(readback.index)):
        value, reason = sample_depth(previous_depth, width, height, readback.u[n] + du, readback.v[n] + dv, bilinear)
        if value is None:
            if reason == 'offscreen':
                offscreen += 1
            else:
                sentinel += 1
            continue
        errors.append(abs(readback.z[n] - value))
    within = sum(1 for error in errors if error <= tolerance)
    return {
        'status': 'evaluated' if errors else 'unavailable',
        'reason': None if errors else 'no valid pixel lands on written previous depth',
        'previous_frame': previous_frame.frame,
        'sampling': 'bilinear' if bilinear else 'nearest',
        'previous_jitter_px': [jx, jy],
        'compared_pixels': len(errors),
        'previous_offscreen': offscreen,
        'previous_sentinel': sentinel,
        'tolerance': tolerance,
        'within_tolerance': within,
        'within_fraction': (within / len(errors)) if errors else None,
        'error': describe(errors),
        'error_histogram': histogram(errors, DEPTH_ERROR_BINS),
    }


def counter_check(frame):
    """Cross-check motion_route lines against the motion_output_frame counters."""
    gates = Counter(draw.gate for draw in frame.draws)
    routed = sum(1 for draw in frame.draws if draw.routed == 1)
    matched = sum(1 for draw in frame.draws if draw.matched == 1)
    observed = {'routed': routed, 'matched': matched}
    for gate in range(1, 7):
        observed[f'gate{gate}'] = gates.get(gate, 0)
    mismatches = []
    summary = frame.summary
    if summary is None:
        return {'status': 'unavailable', 'reason': 'no motion_output_frame line', 'observed': observed}
    non_scene = 0
    for name, value in observed.items():
        logged = number(summary.get(name))
        if name in ('gate1', 'gate2'):
            # Feature/Scene gates also count draws outside the scene phase, which
            # write no motion_route line; the counter may only exceed the lines.
            if logged is None or logged < value:
                mismatches.append({'field': name, 'route_lines': value, 'counter': logged})
            else:
                non_scene += logged - value
        elif logged != value:
            mismatches.append({'field': name, 'route_lines': value, 'counter': logged})
    if gates.get(0, 0) != matched:
        mismatches.append({'field': 'gate0_equals_matched', 'route_lines': gates.get(0, 0), 'counter': matched})
    return {'status': 'pass' if not mismatches else 'fail', 'observed': observed, 'mismatches': mismatches,
            'non_scene_draws_in_counters': non_scene,
            'gate_histogram': {GATE_NAMES.get(gate, str(gate)): count for gate, count in sorted(gates.items())}}


def draw_details(frame):
    details = []
    for draw in frame.draws:
        details.append({
            'index': draw.index,
            'gate': draw.gate,
            'routed': draw.routed,
            'matched': draw.matched,
            'predicted_match': draw.predicted,
            'vs': draw.vs,
            'ps': draw.ps,
            'viewport': draw.viewport,
            'key': draw.key_fields,
            'rows_hash': draw.rows_hash,
            'rows_c24_27': draw.rows_bits,
            'previous_rows_c24_27': draw.previous_bits,
            'previous_index': draw.previous_index,
            'note': draw.history_note,
            'draw_result': draw.draw_result,
        })
    return details


# ---- driver ------------------------------------------------------------------------

def sha256_stream(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def analyze(log_path, readback_dir, options, depth_pattern=None):
    with Path(log_path).open('r', encoding='utf-8', errors='replace') as stream:
        frames, devices = parse_log(stream)
    captured = [frame for frame in frames.values() if frame.captured or frame.readback is not None]
    with_readback = [frame for frame in captured if frame.readback is not None]
    if not with_readback:
        raise MalformedInput('log contains no motion_output_readback line')
    pair_history(frames)
    readback_dir = Path(readback_dir)
    frame_reports = []
    previous_state = {}   # device -> (frame, readback, depth)
    hard_errors = []
    static_frames = []
    temporal_pairs = []
    consistency_frames = []
    depth_frames = []
    depth_images = []
    taa_frames = []
    for frame in sorted(captured, key=lambda f: (f.device, f.frame)):
        report = OrderedDict()
        report['device'] = frame.device
        report['frame'] = frame.frame
        report['captured'] = frame.captured
        report['notes'] = list(frame.notes)
        report['counters'] = counter_check(frame)
        report['static_state'] = frame_static_state(frame, frames)
        report['history_pairing'] = {
            'predictable_draws': sum(1 for d in frame.draws if d.predicted is not None),
            'disagreements': [{'index': d.index, 'gate': d.gate, 'predicted': d.predicted, 'note': d.history_note}
                              for d in frame.draws if d.predicted is not None and d.predicted != (d.gate == 0)],
        }
        viewports = {d.viewport for d in frame.draws if d.routed == 1 and d.viewport is not None}
        report['routed_viewports'] = [list(v) for v in sorted(viewports)]
        if options['draw_details']:
            report['draws'] = draw_details(frame)
        rb = frame.readback
        if rb is None:
            report['readback'] = {'status': 'missing', 'reason': 'no motion_output_readback line'}
            frame_reports.append(report)
            previous_state[frame.device] = (frame, None, None)
            continue
        width, height = number(rb.get('width')), number(rb.get('height'))
        path = readback_dir / rb.get('file', '')
        entry = {'file': rb.get('file'), 'width': width, 'height': height, 'result': rb.get('result'),
                 'bytes_logged': number(rb.get('bytes'))}
        readback = None
        try:
            if not hresult_ok(rb.get('result')):
                raise MalformedInput(f'{rb.get("file")}: readback result {rb.get("result")}')
            if not width or not height:
                raise MalformedInput(f'{rb.get("file")}: missing dimensions')
            if not path.is_file():
                raise MalformedInput(f'{path.name}: file missing')
            readback = load_readback(path, width, height)
        except MalformedInput as error:
            entry.update({'status': 'malformed', 'reason': str(error)})
            hard_errors.append(str(error))
            report['readback'] = entry
            frame_reports.append(report)
            previous_state[frame.device] = (frame, None, None)
            continue
        entry['status'] = 'ok'
        entry['sha256'] = sha256_stream(path)
        total = width * height
        entry['validity_counts'] = {name: readback.counts.get(name, 0) for name in ('valid', 'zero', 'sentinel', 'other')}
        entry['validity_fractions'] = {name: readback.counts.get(name, 0) / total for name in ('valid', 'zero', 'sentinel', 'other')}
        entry['nonfinite_values'] = readback.nonfinite
        entry['abi_clean'] = readback.nonfinite == 0 and readback.counts.get('other', 0) == 0
        if not entry['abi_clean']:
            hard_errors.append(f'{path.name}: {readback.nonfinite} nonfinite values, {readback.counts.get("other", 0)} pixels outside the validity ABI')
        report['readback'] = entry
        if any(v != (0, 0, width, height) for v in viewports):
            report['notes'].append('routed draw viewport differs from the full target; pixel-centre convention assumes a full viewport')
        summary_matched = number((frame.summary or {}).get('matched'))
        valid = readback.counts.get('valid', 0)
        if summary_matched == 0 and valid > 0:
            report['notes'].append(f'{valid} valid pixels although no draw matched')
            hard_errors.append(f'frame {frame.device}:{frame.frame}: valid pixels without matched draws')
        if summary_matched and valid == 0:
            report['notes'].append('matched draws but no valid pixels (off-screen or fully occluded?)')
        stats, dx_values, dy_values = analyze_pixels(frame, readback, options)
        report['pixels'] = stats
        static_state = report['static_state']
        report['static_test'] = {
            'status': ('evaluated' if static_state == 'static' and valid else
                       'not_applicable' if static_state in ('moving', 'no_matched') else 'unknown'),
            'tolerance_px': options['static_tolerance_px'],
            'max_error_px': stats['static_max_error_px'] if static_state == 'static' else None,
            'violations': stats['static_violations'] if static_state == 'static' else None,
        }
        if report['static_test']['status'] == 'evaluated':
            static_frames.append(report)
        report['row_consistency'] = row_consistency(frame, readback, dx_values, dy_values, options)
        if report['row_consistency']['status'] == 'evaluated':
            consistency_frames.append(report)
        prev_frame, prev_readback, prev_depth = previous_state.get(frame.device, (None, None, None))
        report['temporal'] = temporal_cross_check(
            prev_frame, prev_readback, frame, readback,
            options['temporal_criterion_min_matched_ratio'],
            previous_jitter_px(frame) if options.get('jitter_from_log') else (0.0, 0.0))
        if report['temporal']['status'] == 'evaluated':
            temporal_pairs.append(report)
        report['depth'] = depth_cross_check(prev_frame, prev_depth, frame, readback, options['depth_tolerance'],
                                            options['depth_sampling'] == 'bilinear')
        if report['depth']['status'] == 'evaluated':
            depth_frames.append(report)
        report['cut'] = cut_report(frame)
        # RT2 of this frame: the logged depth readback names the file; the
        # pattern is the fallback for captures without the log line.
        depth = None
        depth_name = (frame.depth_readback or {}).get('file') or (depth_pattern.format(device=frame.device, frame=frame.frame) if depth_pattern else None)
        if depth_name:
            depth_path = readback_dir / depth_name
            if frame.depth_readback is not None and not hresult_ok(frame.depth_readback.get('result')):
                report['depth_image'] = {'file': depth_name, 'status': 'malformed',
                                         'reason': f"depth readback result {frame.depth_readback.get('result')}"}
                hard_errors.append(f'{depth_name}: depth readback result {frame.depth_readback.get("result")}')
            elif depth_path.is_file():
                try:
                    depth = load_r32f(depth_path, width, height)
                    stats = depth_image_stats(depth)
                    report['depth_image'] = {'file': depth_path.name, 'status': 'loaded', 'sha256': sha256_stream(depth_path), **stats}
                    depth_images.append(report)
                    if not stats['clean']:
                        hard_errors.append(f"{depth_path.name}: {stats['nonfinite']} nonfinite, {stats['out_of_range']} outside [0,1] and the -1 sentinel")
                    # Every motion-valid pixel of this frame was written by a routed
                    # draw whose row carries the depth output, so RT2 must be written there.
                    missing = sum(1 for index in readback.index if depth[index] == DEPTH_SENTINEL)
                    report['depth_image']['valid_motion_without_depth'] = missing
                    if missing:
                        report['notes'].append(f'{missing} motion-valid pixels carry the depth sentinel (motion-only rows or a write-mask difference)')
                except MalformedInput as error:
                    report['depth_image'] = {'file': depth_path.name, 'status': 'malformed', 'reason': str(error)}
                    hard_errors.append(str(error))
            elif frame.depth_readback is not None:
                report['depth_image'] = {'file': depth_name, 'status': 'missing', 'reason': 'logged depth readback file absent'}
                hard_errors.append(f'{depth_name}: file missing')
            else:
                report['depth_image'] = {'file': depth_name, 'status': 'absent'}
        # Resolved image (X3M_TAA_DEBUG): finite, and its distance from the
        # pre-resolve color as a sanity signal of the history contribution.
        if frame.taa_readback is not None:
            taa_name = frame.taa_readback.get('file', '')
            color_name = (frame.color_readback or {}).get('file', '')
            taa_path, color_path = readback_dir / taa_name, readback_dir / color_name
            try:
                if not hresult_ok(frame.taa_readback.get('result')) or (frame.color_readback is not None and not hresult_ok(frame.color_readback.get('result'))):
                    raise MalformedInput(f'{taa_name}: resolved/color readback result '
                                         f'{frame.taa_readback.get("result")}/{(frame.color_readback or {}).get("result")}')
                if not taa_path.is_file():
                    raise MalformedInput(f'{taa_name}: file missing')
                if frame.color_readback is None or not color_path.is_file():
                    raise MalformedInput(f'{taa_name}: pre-resolve color image {color_name or "(unlogged)"} missing')
                stats = taa_image_stats(load_rgba16f(taa_path, width, height), load_bgra8(color_path, width, height),
                                        options['taa_threshold'])
                report['taa'] = {'file': taa_name, 'color_file': color_name, 'status': 'loaded',
                                 'sha256': sha256_stream(taa_path), 'color_sha256': sha256_stream(color_path), **stats}
                if not stats['clean']:
                    hard_errors.append(f"{taa_name}: {stats['nonfinite']} nonfinite resolved pixels")
                taa_frames.append(report)
            except MalformedInput as error:
                report['taa'] = {'file': taa_name, 'status': 'malformed', 'reason': str(error)}
                hard_errors.append(str(error))
        previous_state[frame.device] = (frame, readback, depth)
        frame_reports.append(report)

    checks = build_checks(frame_reports, hard_errors, static_frames, consistency_frames, temporal_pairs, depth_frames, options,
                          depth_images, taa_frames)
    failed = [name for name, check in checks.items() if check['status'] == 'fail']
    return OrderedDict([
        ('tool', 'tools/analysis/analyze_motion_readback.py'),
        ('log', str(log_path)),
        ('log_sha256', sha256_stream(Path(log_path))),
        ('readback_dir', str(readback_dir)),
        ('devices', devices),
        ('parameters', {k: v for k, v in options.items()}),
        ('captured_frames', len(captured)),
        ('frames_with_readback', len(with_readback)),
        ('status', 'FAIL' if failed else 'PASS'),
        ('failed_checks', failed),
        ('hard_errors', hard_errors),
        ('checks', checks),
        ('frames', frame_reports),
    ])


def build_checks(frame_reports, hard_errors, static_frames, consistency_frames, temporal_pairs, depth_frames, options,
                 depth_images=(), taa_frames=()):
    checks = OrderedDict()
    readbacks = [r for r in frame_reports if 'readback' in r]
    clean = [r for r in readbacks if r['readback'].get('status') == 'ok' and r['readback'].get('abi_clean')]
    checks['readback_integrity'] = {
        'status': 'pass' if readbacks and len(clean) == len(readbacks) and not hard_errors else 'fail',
        'frames': len(readbacks), 'clean_frames': len(clean), 'errors': hard_errors,
    }
    counter_fail = [r['frame'] for r in frame_reports if r['counters']['status'] == 'fail']
    counter_eval = [r for r in frame_reports if r['counters']['status'] != 'unavailable']
    checks['counter_consistency'] = {
        'status': 'unavailable' if not counter_eval else ('fail' if counter_fail else 'pass'),
        'frames': len(counter_eval), 'failing_frames': counter_fail,
    }
    predictable = sum(r['history_pairing']['predictable_draws'] for r in frame_reports)
    disagreements = sum(len(r['history_pairing']['disagreements']) for r in frame_reports)
    checks['history_pairing'] = {
        'status': 'unavailable' if predictable == 0 else ('pass' if disagreements == 0 else 'fail'),
        'predictable_draws': predictable, 'disagreements': disagreements,
        'note': 'compares the log-reconstructed previous-frame table with the DLL matched flag; needs consecutive captured frames',
    }
    static_states = Counter(r['static_state'] for r in frame_reports)
    violations = sum(r['static_test']['violations'] or 0 for r in static_frames)
    checks['static_consistency'] = {
        'status': 'unavailable' if not static_frames else ('pass' if violations == 0 else 'fail'),
        'frames_evaluated': [r['frame'] for r in static_frames],
        'frame_states': dict(static_states),
        'tolerance_px': options['static_tolerance_px'],
        'max_error_px': max((r['static_test']['max_error_px'] for r in static_frames), default=None),
        'violations': violations,
    }
    valid_total = sum(r['pixels']['valid_pixels'] for r in frame_reports if 'pixels' in r)
    suspicious = sum(r['pixels']['suspicious_over_bound'] for r in frame_reports if 'pixels' in r)
    fraction = (suspicious / valid_total) if valid_total else None
    checks['displacement'] = {
        'status': 'unavailable' if not valid_total else ('pass' if fraction <= options['max_suspicious_fraction'] else 'fail'),
        'bound_px': options['displacement_bound_px'], 'valid_pixels': valid_total,
        'suspicious_pixels': suspicious, 'suspicious_fraction': fraction,
        'max_displacement_px': max((r['pixels']['displacement_px'].get('max', 0) for r in frame_reports if 'pixels' in r), default=None),
    }
    sampled = sum(r['row_consistency']['sampled_pixels'] for r in consistency_frames)
    unexplained = sum(r['row_consistency']['unexplained_pixels'] for r in consistency_frames)
    unexplained_fraction = (unexplained / sampled) if sampled else None
    checks['row_consistency'] = {
        'status': 'unavailable' if not sampled else
        ('pass' if unexplained_fraction <= options['max_unexplained_fraction'] else 'fail'),
        'tolerance_px': options['consistency_tolerance_px'],
        'frames_evaluated': [r['frame'] for r in consistency_frames],
        'sampled_pixels': sampled, 'unexplained_pixels': unexplained, 'unexplained_fraction': unexplained_fraction,
        'max_error_px': max((r['row_consistency']['max_error_px'] or 0 for r in consistency_frames), default=None),
    }
    applicable = [r for r in temporal_pairs if r['temporal']['criterion_applies']]
    worst = min((r['temporal']['covered_fraction'] for r in applicable), default=None)
    checks['temporal_coverage'] = {
        'status': ('unavailable' if not temporal_pairs else 'informative' if not applicable else
                   'pass' if worst >= options['temporal_coverage_min'] else 'fail'),
        'minimum_fraction': options['temporal_coverage_min'],
        'criterion_min_matched_ratio': options['temporal_criterion_min_matched_ratio'],
        'pairs_evaluated': [[r['temporal']['previous_frame'], r['frame']] for r in temporal_pairs],
        'pairs_with_criterion': [[r['temporal']['previous_frame'], r['frame']] for r in applicable],
        'worst_covered_fraction': worst,
        'covered_fractions': {f"{r['temporal']['previous_frame']}->{r['frame']}": r['temporal']['covered_fraction'] for r in temporal_pairs},
    }
    if depth_images:
        unclean = [r['frame'] for r in depth_images if not r['depth_image']['clean']]
        without = sum(r['depth_image'].get('valid_motion_without_depth', 0) for r in depth_images)
        checks['depth_image_integrity'] = {
            'status': 'pass' if not unclean else 'fail',
            'frames': [r['frame'] for r in depth_images], 'unclean_frames': unclean,
            'sentinel_fraction': {r['frame']: r['depth_image']['sentinel_fraction'] for r in depth_images},
            'written_fraction': {r['frame']: r['depth_image']['written_fraction'] for r in depth_images},
            'written_range': {r['frame']: r['depth_image']['written_range'] for r in depth_images},
            'valid_motion_without_depth': without,
            'note': 'R32F device depth in [0,1] where a routed depth row covered the pixel, -1 elsewhere; nonfinite or out-of-range values fail',
        }
    else:
        checks['depth_image_integrity'] = {'status': 'unavailable', 'reason': 'no depth_<device>_<frame>.r32f image beside the readbacks'}
    if depth_frames:
        worst_depth = min(r['depth']['within_fraction'] for r in depth_frames)
        checks['depth'] = {'status': 'pass' if worst_depth >= options['depth_within_min'] else 'fail',
                           'sampling': options['depth_sampling'],
                           'frames_evaluated': [r['frame'] for r in depth_frames], 'worst_within_fraction': worst_depth,
                           'compared_pixels': sum(r['depth']['compared_pixels'] for r in depth_frames),
                           'previous_sentinel': sum(r['depth']['previous_sentinel'] for r in depth_frames),
                           'max_error': max((r['depth']['error'].get('max') or 0) for r in depth_frames),
                           'tolerance': options['depth_tolerance'], 'minimum_within_fraction': options['depth_within_min']}
    else:
        checks['depth'] = {'status': 'unavailable',
                           'reason': 'no consecutive captured frame pair with a readable depth image of frame N; see docs/verification/motion-readback.md'}
    if taa_frames:
        unclean = [r['frame'] for r in taa_frames if not r['taa']['clean']]
        checks['taa_image'] = {
            'status': 'pass' if not unclean else 'fail',
            'frames': [r['frame'] for r in taa_frames], 'unclean_frames': unclean,
            'threshold': options['taa_threshold'],
            'differing_fraction': {r['frame']: r['taa']['differing_fraction'] for r in taa_frames},
            'max_difference': {r['frame']: r['taa']['max_difference'] for r in taa_frames},
            'note': 'sanity signal: the resolved FP16 image is finite; the fraction of pixels whose RGB moved away from the pre-resolve color by more than the threshold is reported, not judged',
        }
    else:
        checks['taa_image'] = {'status': 'unavailable', 'reason': 'no motion_output_taa_readback line (X3M_TAA_DEBUG off or TAA not resolved)'}
    return checks


def cut_report(frame):
    """The route's cut verdict for the frame (motion_output_cut line and the
    per-frame summary): reported, not judged; the resolve rejects history for a
    frame whose verdict is set."""
    summary = frame.summary or {}
    cut = frame.cut or {}
    if not cut and 'cut' not in summary:
        return {'status': 'unavailable'}
    return {
        'status': 'reported',
        'cut': number(cut.get('cut', summary.get('cut'))),
        'median_px': real(cut.get('median_px', summary.get('cut_median_px'))),
        'missing_fraction': real(cut.get('missing_fraction', summary.get('cut_missing'))),
        'samples': number(cut.get('samples', summary.get('cut_samples'))),
        'keyed': number(cut.get('keyed')), 'missing': number(cut.get('missing')),
        'bound_px': real(cut.get('bound_px')), 'bound_missing': real(cut.get('bound_missing')),
        'jitter_px': [real(summary.get('jitter_x'), 0.0), real(summary.get('jitter_y'), 0.0)],
        'jitter_previous_px': list(previous_jitter_px(frame)),
    }


def render_report(summary):
    lines = []
    lines.append(f"motion readback analysis: {summary['status']}")
    lines.append(f"log: {summary['log']}")
    lines.append(f"log sha256: {summary['log_sha256']}")
    lines.append(f"captured frames: {summary['captured_frames']}, with readback: {summary['frames_with_readback']}")
    lines.append('')
    lines.append('checks:')
    for name, check in summary['checks'].items():
        detail = {k: v for k, v in check.items() if k not in ('status', 'errors', 'covered_fractions')}
        lines.append(f"  {name}: {check['status']} {json.dumps(detail, default=str)}")
    for error in summary['hard_errors']:
        lines.append(f'  error: {error}')
    lines.append('')
    for report in summary['frames']:
        lines.append(f"frame {report['device']}:{report['frame']} captured={report['captured']} static={report['static_state']}")
        counters = report['counters']
        lines.append(f"  counters: {counters['status']} {json.dumps(counters.get('observed'))} gates={json.dumps(counters.get('gate_histogram'))}")
        if counters.get('mismatches'):
            lines.append(f"  counter mismatches: {json.dumps(counters['mismatches'])}")
        pairing = report['history_pairing']
        lines.append(f"  history pairing: predictable={pairing['predictable_draws']} disagreements={len(pairing['disagreements'])}")
        for note in report['notes']:
            lines.append(f'  note: {note}')
        rb = report.get('readback', {})
        if rb.get('status') != 'ok':
            lines.append(f"  readback: {rb.get('status')} {rb.get('reason', '')}")
            lines.append('')
            continue
        lines.append(f"  readback: {rb['file']} {rb['width']}x{rb['height']} validity={json.dumps(rb['validity_counts'])} nonfinite={rb['nonfinite_values']}")
        pixels = report['pixels']
        if pixels['valid_pixels']:
            d = pixels['displacement_px']
            lines.append(f"  valid uv range u={pixels['uv_range']['u']} v={pixels['uv_range']['v']} depth={pixels['depth_range']} outside_unit={pixels['previous_uv_outside_unit']}")
            lines.append(f"  displacement px: median={d['median']:.4f} p95={d['p95']:.4f} p99={d['p99']:.4f} max={d['max']:.4f} suspicious={pixels['suspicious_over_bound']}")
            lines.append(f"  displacement histogram: {json.dumps(pixels['displacement_histogram'])}")
        static = report['static_test']
        lines.append(f"  static test: {static['status']} max_error_px={static['max_error_px']} violations={static['violations']}")
        rc = report['row_consistency']
        if rc['status'] == 'evaluated':
            lines.append(f"  row consistency: sampled={rc['sampled_pixels']} (stride {rc['sample_stride']}) explained={rc['explained_pixels']} unexplained={rc['unexplained_pixels']} ambiguous={rc['ambiguous_pixels']} max_error_px={rc['max_error_px']}")
            for entry in rc['per_draw']:
                lines.append(f"    draw {entry['index']} (prev {entry['previous_index']}, group {entry['group']}): origin displacement={entry['expected_origin_displacement_px']} group pixels={entry['group_attributed_pixels']} median displacement={entry['group_median_displacement_px']} difference={entry['origin_vs_median_difference_px']}")
        else:
            lines.append(f"  row consistency: {rc['status']} {rc.get('reason', '')}")
        t = report['temporal']
        if t['status'] == 'evaluated':
            lines.append(f"  temporal {t['previous_frame']}->{report['frame']}: covered={t['previous_covered']} uncovered={t['previous_uncovered']} offscreen={t['previous_offscreen']} fraction={t['covered_fraction']:.4f} criterion_applies={t['criterion_applies']}")
        else:
            lines.append(f"  temporal: {t['status']} {t.get('reason', '')}")
        dp = report['depth']
        if dp['status'] == 'evaluated':
            lines.append(f"  depth {dp['previous_frame']}->{report['frame']} ({dp['sampling']}, previous jitter {dp['previous_jitter_px']} px): compared={dp['compared_pixels']} sentinel={dp['previous_sentinel']} offscreen={dp['previous_offscreen']} within={dp['within_fraction']:.4f} error={json.dumps(dp['error'])}")
            lines.append(f"  depth error histogram: {json.dumps(dp['error_histogram'])}")
        else:
            lines.append(f"  depth: {dp['status']} {dp.get('reason', '')}")
        di = report.get('depth_image')
        if di and di.get('status') == 'loaded':
            lines.append(f"  depth image: {di['file']} written={di['written']} ({di['written_fraction']:.4f}) sentinel={di['sentinel']} ({di['sentinel_fraction']:.4f}) nonfinite={di['nonfinite']} out_of_range={di['out_of_range']} range={di['written_range']} valid_motion_without_depth={di.get('valid_motion_without_depth')}")
        elif di:
            lines.append(f"  depth image: {di.get('status')} {di.get('reason', '')}")
        taa = report.get('taa')
        if taa and taa.get('status') == 'loaded':
            lines.append(f"  resolved image: {taa['file']} vs {taa['color_file']} nonfinite={taa['nonfinite']} differing={taa['differing']} ({taa['differing_fraction']:.4f} over {taa['threshold']}) max_difference={taa['max_difference']:.6f} mean_difference={taa['mean_difference']}")
        elif taa:
            lines.append(f"  resolved image: {taa.get('status')} {taa.get('reason', '')}")
        cut = report.get('cut', {})
        if cut.get('status') == 'reported':
            lines.append(f"  cut detector: cut={cut['cut']} median_px={cut['median_px']} missing_fraction={cut['missing_fraction']} samples={cut['samples']} bounds=({cut['bound_px']} px, {cut['bound_missing']}) jitter={cut['jitter_px']} previous={cut['jitter_previous_px']}")
        lines.append('')
    return '\n'.join(lines) + '\n'


def default_options(**overrides):
    """Analysis parameters with the CLI defaults; tests and callers override by name."""
    options = {
        'static_tolerance_px': 0.01,
        'displacement_bound_px': 64.0,
        'max_suspicious_fraction': 0.01,
        'consistency_tolerance_px': 0.5,
        'max_unexplained_fraction': 0.01,
        'max_consistency_pixels': 50000,
        'temporal_coverage_min': 0.9,
        'temporal_criterion_min_matched_ratio': 0.99,
        'depth_tolerance': 1e-4,
        'depth_within_min': 0.99,
        'depth_sampling': 'nearest',
        'taa_threshold': 2.0 / 255.0,
        'jitter_uv': (0.0, 0.0),
        'jitter_from_log': False,
        'draw_details': True,
    }
    unknown = set(overrides) - set(options)
    if unknown:
        raise KeyError(f'unknown options: {sorted(unknown)}')
    options.update(overrides)
    return options


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', type=Path, help='capture session log (streamed)')
    parser.add_argument('--readback-dir', type=Path, help='directory with motion_<device>_<frame>.rgba32f (default: log directory)')
    parser.add_argument('--label', required=True, help='output name: motion-readback-<label>-summary.json/.txt')
    parser.add_argument('--results-dir', type=Path, default=Path('verification/results'))
    parser.add_argument('--static-tolerance-px', type=float, default=0.01)
    parser.add_argument('--displacement-bound-px', type=float, default=64.0)
    parser.add_argument('--max-suspicious-fraction', type=float, default=0.01)
    parser.add_argument('--consistency-tolerance-px', type=float, default=0.5)
    parser.add_argument('--max-unexplained-fraction', type=float, default=0.01)
    parser.add_argument('--max-consistency-pixels', type=int, default=50000)
    parser.add_argument('--temporal-coverage-min', type=float, default=0.9)
    parser.add_argument('--temporal-criterion-min-matched-ratio', type=float, default=0.99,
                        help='the coverage criterion applies only when frame N matched at least this fraction of its routed draws')
    parser.add_argument('--depth-pattern', default='depth_{device}_{frame}.r32f',
                        help='row-major R32F depth image of frame N beside the readbacks (RT2; the logged '
                             'motion_output_depth_readback file name takes precedence)')
    parser.add_argument('--depth-tolerance', type=float, default=1e-4)
    parser.add_argument('--depth-within-min', type=float, default=0.99)
    parser.add_argument('--depth-sampling', choices=('nearest', 'bilinear'), default='nearest',
                        help='how frame N depth is sampled at the previous UV (bilinear drops sentinel taps)')
    parser.add_argument('--taa-threshold', type=float, default=2.0 / 255.0,
                        help='resolved-vs-current RGB difference above which a pixel counts as changed by history (X3M_TAA_DEBUG images)')
    parser.add_argument('--jitter-uv', type=float, nargs=2, default=(0.0, 0.0), metavar=('U', 'V'),
                        help='prior jitter UV subtracted by the producer (zero at checkpoint B1)')
    parser.add_argument('--jitter-from-log', action='store_true',
                        help='take each captured frame\'s own raster jitter from '
                             'motion_output_frame (jitter_x/jitter_y, pixels) and unjitter the '
                             'raster pixel before comparing it with the producer\'s unjittered '
                             'previous UV; also offsets the previous-frame coverage lookup by '
                             'jitter_previous_x/y. Required for --motion-jitter/--taa captures.')
    parser.add_argument('--no-draw-details', action='store_true', help='omit per-draw keys and rows from the JSON')
    args = parser.parse_args(argv)
    options = default_options(
        static_tolerance_px=args.static_tolerance_px,
        displacement_bound_px=args.displacement_bound_px,
        max_suspicious_fraction=args.max_suspicious_fraction,
        consistency_tolerance_px=args.consistency_tolerance_px,
        max_unexplained_fraction=args.max_unexplained_fraction,
        max_consistency_pixels=max(1, args.max_consistency_pixels),
        temporal_coverage_min=args.temporal_coverage_min,
        temporal_criterion_min_matched_ratio=args.temporal_criterion_min_matched_ratio,
        depth_tolerance=args.depth_tolerance,
        depth_within_min=args.depth_within_min,
        depth_sampling=args.depth_sampling,
        taa_threshold=args.taa_threshold,
        jitter_uv=tuple(args.jitter_uv),
        jitter_from_log=args.jitter_from_log,
        draw_details=not args.no_draw_details,
    )
    readback_dir = args.readback_dir or args.log.parent
    try:
        summary = analyze(args.log, readback_dir, options, args.depth_pattern)
    except MalformedInput as error:
        print(f'malformed input: {error}', file=sys.stderr)
        return 2
    args.results_dir.mkdir(parents=True, exist_ok=True)
    json_path = args.results_dir / f'motion-readback-{args.label}-summary.json'
    text_path = args.results_dir / f'motion-readback-{args.label}.txt'
    with json_path.open('w', encoding='utf-8') as stream:
        json.dump(summary, stream, indent=2, default=str)
        stream.write('\n')
    text = render_report(summary)
    text_path.write_text(text, encoding='utf-8')
    sys.stdout.write(text)
    print(f'summary: {json_path}')
    return 0 if summary['status'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
