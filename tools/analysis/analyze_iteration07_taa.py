#!/usr/bin/env python3
"""Iteration 7 (first TAA gameplay run): temporal-resolve status, jitter, route
counters, telemetry and an image measurement of the trembling/blur the user saw.

The capture log is streamed once; nothing is loaded whole. Readback images are
loaded one at a time and only for the bursts selected for image analysis.

Three independent bodies of evidence are produced.

1. Log evidence: per-frame `motion_output_frame` TAA status (attempted /
   resolved / skipped with the skip code, history validity, `taa_result`,
   `taa_references`), the `motion_output_cut` verdicts (median origin
   displacement and missing fraction per frame and per burst), the logged
   per-frame raster jitter, the route's gate counters cross-checked against the
   per-draw `motion_route` records, every readback record, and any failure,
   unsupported, reset or shutdown record.

2. Telemetry: the fixed-size `telemetry_metric` counters aggregated per name,
   for this run and optionally for a baseline log (iteration 6).  These are
   CPU-side wall-clock spans, never GPU time; see docs/verification/telemetry.md.
   The boundary `StretchRect` is *not* a telemetry metric in this build, so the
   only in-game figure available is the QPC bracket between the last
   `capture_event op=color_fill` and the `op=stretch_rect` event of the same
   captured frame, which the ordered-boundary hooks record.  With
   `--taa-debug` that bracket also contains the pre-resolve colour readback and
   the resolved FP16 readback, so it bounds the resolve from above only.

3. Image evidence (``--captures``): for a burst the log certifies as stationary
   (`motion_output_cut median_px` ~ 0) the sub-pixel shift between consecutive
   frames is measured twice, independently, on the pre-resolve colour readbacks
   (`color_<device>_<frame>.bgra8`) and on the resolved images
   (`taa_<device>_<frame>.rgba16f`):

   * ``lucas_kanade`` - iterated gradient least squares for one global
     translation, on a 2x binomial-blurred luminance image, over the
     highest-gradient pixels.  Returns ``d`` with ``I2(x) ~= I1(x - d)``, i.e.
     the displacement of image content from the first frame to the second, in
     pixels, +X right and +Y down.
   * ``phase_correlation`` - normalized cross-power spectrum of one Hann-
     windowed square tile (the tile with the most gradient energy), Gaussian
     low-passed so the whitened peak is wide enough for a parabolic fit.  Same
     sign convention.  Its tile cannot be masked, so it is a cross-check of the
     sign and magnitude, not a precise estimator.

   Samples are restricted to pixels the route marked valid (alpha == 1) in
   `motion_<device>_<frame>.rgba32f` in both frames, eroded by two pixels.
   Only those pixels carry routed, jittered scene geometry: background, particle
   and unknown-program draws are not jittered, and including them dilutes the
   measured shift towards zero.

   The expectation the measurement tests.  The render is jittered, so the raw
   current colour of a stationary scene is displaced by the frame's own jitter
   ``j_n`` and moves by exactly the logged step ``j_n - j_(n-1)``.  Write the
   resolved image's displacement as ``d_n`` and the history weight as ``w``.

   * History on the unjittered grid (the correct convention):
     ``d_n = (1-w) j_n + w d_(n-1)`` - a running mean of a zero-mean sequence,
     so ``d`` stays near zero and the resolved image is nearly still.
   * History sampled at the previous position *plus the previous jitter* (the
     suspected defect): ``d_n = (1-w) j_n + w (d_(n-1) + j_n - j_(n-1))``,
     whence ``d_n - j_n = w (d_(n-1) - j_(n-1))``.  The deviation decays by
     ``w`` per frame and ``d_n -> j_n``: the resolved image ends up displaced
     by the current jitter, exactly like the unresolved render, and its
     inter-frame shift is the full jitter step.

   Both predictions are reported next to the measurement, per consecutive pair
   and - independently of any pairing - per frame as the shift between that
   frame's pre-resolve colour and its resolved image (zero under the defect,
   ``-j_n`` under the correct convention).

   Blur is reported as the mean squared luminance gradient over the same mask
   (resolved versus pre-resolve colour of the same frame) and as the fraction
   of tile spectral energy above half Nyquist.  The 8-bit quantization of the
   pre-resolve readback adds a flat noise floor to both; the floor is estimated
   and subtracted in a second, corrected figure so the ratio can be read
   honestly.
"""
import argparse
import array
import cmath
import hashlib
import json
import math
import statistics
import sys
from collections import Counter, OrderedDict
from pathlib import Path


class MalformedInput(Exception):
    pass


# ---- log streaming -----------------------------------------------------------------

def fields(line):
    """`key=value` pairs of one record; the leading event name is dropped."""
    out = {}
    for token in line.split()[1:]:
        key, sep, value = token.partition('=')
        if sep:
            out[key] = value
    return out


def number(text, default=None):
    try:
        return int(text)
    except (TypeError, ValueError):
        return default


def real(text, default=None):
    try:
        value = float(text)
    except (TypeError, ValueError):
        return default
    return value if math.isfinite(value) else default


def hresult_ok(text):
    return text is not None and int(text, 16) == 0


FAILURE_TOKENS = ('_failed', 'unsupported', '_error', 'fallback')


def stream_records(path):
    """Yield (event, line) for every record; the file is read line by line."""
    with path.open('r', encoding='utf-8', errors='replace') as stream:
        for line in stream:
            line = line.rstrip('\n')
            if not line:
                continue
            yield line.partition(' ')[0], line


# ---- telemetry ---------------------------------------------------------------------

BUCKET_EDGES = ['<=10us', '<=100us', '<=1ms', '<=10ms', '<=100ms', '>100ms']


def telemetry_accumulate(store, f):
    """Fold one `telemetry_metric` window into the per (device, name) aggregate.

    The build reports fixed-size counters per summary window: count, failures,
    total/min/max and six duration buckets.  Individual durations are not
    recorded, so only counts, extremes, means and bucket shares can be stated -
    never a median or a percentile of frame time.
    """
    count = number(f.get('count'))
    buckets = [number(x, 0) for x in (f.get('buckets') or '').split(',') if x != '']
    total, low, high = (real(f.get(k)) for k in ('total_us', 'min_us', 'max_us'))
    if count is None or count <= 0 or len(buckets) != 6 or None in (total, low, high):
        return False
    if sum(buckets) != count or low > high or total + 1e-3 < high:
        return False
    key = f.get('device', '?') + ':' + f.get('name', '?')
    entry = store.setdefault(key, dict(device=f.get('device'), name=f.get('name'), count=0,
                                       failures=0, total_us=0.0, min_us=low, max_us=0.0,
                                       windows=0, buckets=[0] * 6))
    entry['count'] += count
    entry['failures'] += number(f.get('failures'), 0)
    entry['total_us'] += total
    entry['min_us'] = min(entry['min_us'], low)
    entry['max_us'] = max(entry['max_us'], high)
    entry['windows'] += 1
    entry['buckets'] = [a + b for a, b in zip(entry['buckets'], buckets)]
    return True


def telemetry_finish(store):
    for entry in store.values():
        entry['mean_us'] = entry['total_us'] / entry['count'] if entry['count'] else None
        entry['bucket_labels'] = BUCKET_EDGES
        entry['bucket_fraction'] = [b / entry['count'] if entry['count'] else None
                                    for b in entry['buckets']]
    return store


WINDOW_METRICS = ('frame_normal', 'present_normal', 'draw_backend', 'lock_wait')


def describe_windows(windows):
    """Distribution *across one-second summary windows*.

    A window reports only count/total/min/max, so the honest statistics are the
    per-window mean (total/count) and the per-window minimum (the fastest frame
    of that second, which no load stall can inflate).  A median frame time over
    the whole session does not exist in this data.
    """
    if not windows:
        return {'status': 'unavailable', 'windows': 0}
    means = sorted(w['total_us'] / w['count'] for w in windows)
    minima = sorted(w['min_us'] for w in windows)

    def quantile(values, q):
        position = q * (len(values) - 1)
        low = int(math.floor(position))
        high = min(low + 1, len(values) - 1)
        return values[low] + (values[high] - values[low]) * (position - low)

    return {
        'status': 'evaluated',
        'windows': len(windows),
        'samples': sum(w['count'] for w in windows),
        'window_mean_us': {'p10': quantile(means, 0.1), 'median': quantile(means, 0.5),
                           'p90': quantile(means, 0.9), 'min': means[0], 'max': means[-1]},
        'window_min_us': {'p10': quantile(minima, 0.1), 'median': quantile(minima, 0.5),
                          'p90': quantile(minima, 0.9), 'min': minima[0], 'max': minima[-1]},
    }


def scan_telemetry(path, device='1'):
    """Scan of a log used as a timing baseline: telemetry metrics, the
    per-window distributions, and the boundary `StretchRect` bracket."""
    metrics = {}
    rejected = 0
    brackets = []
    previous = None
    clock = None
    windows = {}
    scene = {}
    summary_frame = None
    for event, line in stream_records(path):
        if event == 'telemetry_metric':
            f = fields(line)
            if not telemetry_accumulate(metrics, f):
                rejected += 1
                continue
            if f.get('name') in WINDOW_METRICS:
                total, low, high = (real(f.get(k)) for k in ('total_us', 'min_us', 'max_us'))
                windows.setdefault(f.get('device', '?') + ':' + f['name'], []).append(
                    {'frame': summary_frame, 'count': number(f.get('count')), 'total_us': total,
                     'min_us': low, 'max_us': high})
        elif event == 'telemetry_summary':
            f = fields(line)
            if f.get('device') == device:
                summary_frame = number(f.get('frame'))
        elif event == 'motion_output_frame':
            f = fields(line)
            scene[number(f.get('frame'))] = {
                'latched': number(f.get('latched'), 0),
                'routed': number(f.get('routed'), 0),
                'taa_resolved': number(f.get('taa_resolved'), 0),
            }
        elif event == 'telemetry_start':
            clock = number(fields(line).get('qpc_frequency'))
        elif event == 'capture_event':
            f = fields(line)
            if f.get('op') == 'stretch_rect' and previous and previous[0] == f.get('frame'):
                begin, end = number(previous[2]), number(f.get('qpc'))
                if begin is not None and end is not None and end >= begin:
                    brackets.append((f.get('frame'), previous[1], end - begin))
            previous = (f.get('frame'), f.get('op'), f.get('qpc'))
    return {'metrics': telemetry_finish(metrics), 'rejected_records': rejected,
            'brackets': brackets, 'clock_hz': clock,
            'windows': split_windows(windows, scene)}


def split_windows(windows, scene):
    """Per-window distributions, and the same split by whether the nearest
    preceding periodic `motion_output_frame` observed a routed scene.

    The split is *not* a measurement of the route or the resolve: the
    non-scene parts of a session are menus, maps and loads with different
    content, and the feature was enabled throughout.  It only says what the
    session's two regimes cost.
    """
    keys = sorted(k for k in scene if k is not None)

    def regime(frame):
        if frame is None or not keys:
            return 'unknown'
        low, high = 0, len(keys)
        while low < high:
            mid = (low + high) // 2
            if keys[mid] <= frame:
                low = mid + 1
            else:
                high = mid
        if low == 0:
            return 'unknown'
        entry = scene[keys[low - 1]]
        return 'scene' if entry['routed'] else 'other'

    out = {}
    for name, items in windows.items():
        out[name] = {'all': describe_windows(items)}
        for label in ('scene', 'other'):
            subset = [w for w in items if regime(w['frame']) == label]
            if subset:
                out[name][label] = describe_windows(subset)
    return out


def bracket_report(brackets, clock, note):
    if not brackets or not clock:
        return {'status': 'unavailable', 'reason': 'no stretch_rect capture_event bracket', 'note': note}
    values = sorted(v * 1e6 / clock for _, _, v in brackets)
    return {
        'status': 'measured',
        'samples': len(values),
        'preceding_events': sorted({b[1] for b in brackets}),
        'median_us': statistics.median(values),
        'mean_us': statistics.fmean(values),
        'min_us': values[0],
        'max_us': values[-1],
        'note': note,
    }


# ---- image helpers -----------------------------------------------------------------

def _half_to_float(bits):
    sign = -1.0 if bits & 0x8000 else 1.0
    exponent = (bits >> 10) & 31
    mantissa = bits & 1023
    if exponent == 31:
        return sign * (math.nan if mantissa else math.inf)
    if exponent == 0:
        return sign * mantissa * 2.0 ** -24
    return sign * (1024 + mantissa) * 2.0 ** (exponent - 25)


HALF_TABLE = [_half_to_float(bits) for bits in range(65536)]
# Rec.709 luma; the readbacks store whatever the game wrote, so this is a fixed
# deterministic projection of RGB to one channel, not a colorimetric claim.
LUMA = (0.2126, 0.7152, 0.0722)


def load_luma_bgra8(path, width, height):
    """Row-major A8R8G8B8 (the pre-resolve main target) -> luminance in [0,1]."""
    data = path.read_bytes()
    if len(data) != width * height * 4:
        raise MalformedInput(f'{path.name}: {len(data)} bytes != {width}x{height}x4')
    r, g, b = LUMA
    scale = 1.0 / 255.0
    return array.array('f', [(r * data[i + 2] + g * data[i + 1] + b * data[i]) * scale
                             for i in range(0, len(data), 4)])


def load_luma_rgba16f(path, width, height):
    """Row-major RGBA binary16 (the resolved image) -> luminance."""
    if path.stat().st_size != width * height * 8:
        raise MalformedInput(f'{path.name}: size != {width}x{height}x8')
    raw = array.array('H')
    with path.open('rb') as stream:
        raw.fromfile(stream, width * height * 4)
    if sys.byteorder != 'little':
        raw.byteswap()
    table = HALF_TABLE
    r, g, b = LUMA
    return array.array('f', [r * table[raw[i]] + g * table[raw[i + 1]] + b * table[raw[i + 2]]
                             for i in range(0, len(raw), 4)])


def load_validity_mask(path, width, height):
    """1 where the route's RGBA32F readback has alpha == 1 (a matched, routed,
    therefore jittered scene pixel), 0 elsewhere (sentinel)."""
    if path.stat().st_size != width * height * 16:
        raise MalformedInput(f'{path.name}: size != {width}x{height}x16')
    raw = array.array('f')
    with path.open('rb') as stream:
        raw.fromfile(stream, width * height * 4)
    if sys.byteorder != 'little':
        raw.byteswap()
    return bytearray(1 if raw[i] == 1.0 else 0 for i in range(3, len(raw), 4))


def erode(mask, width, height, radius=2):
    """Keep only pixels whose whole (2r+1) square neighbourhood is set, so a
    bilinear sample and a central difference stay inside the mask."""
    out = bytearray(width * height)
    for y in range(radius, height - radius):
        row = y * width
        for x in range(radius, width - radius):
            if not mask[row + x]:
                continue
            keep = 1
            for dy in range(-radius, radius + 1):
                base = row + dy * width
                for dx in range(-radius, radius + 1):
                    if not mask[base + x + dx]:
                        keep = 0
                        break
                if not keep:
                    break
            out[row + x] = keep
    return out


def binomial_blur(image, width, height, passes=2):
    """Separable [1,2,1]/4, edge-clamped.  Applied identically to both frames,
    so it cannot bias the estimated shift; it makes the first-order gradient
    constraint valid for sub-pixel shifts of aliased content."""
    current = image
    for _ in range(passes):
        tmp = array.array('f', bytes(4 * width * height))
        for y in range(height):
            row = y * width
            tmp[row] = (current[row] * 3.0 + current[row + 1]) * 0.25
            for x in range(1, width - 1):
                tmp[row + x] = (current[row + x - 1] + 2.0 * current[row + x] + current[row + x + 1]) * 0.25
            tmp[row + width - 1] = (current[row + width - 2] + current[row + width - 1] * 3.0) * 0.25
        out = array.array('f', bytes(4 * width * height))
        for y in range(height):
            row = y * width
            above = row - width if y > 0 else row
            below = row + width if y < height - 1 else row
            for x in range(width):
                out[row + x] = (tmp[above + x] + 2.0 * tmp[row + x] + tmp[below + x]) * 0.25
        current = out
    return current


def central_gradients(image, width, height):
    gx = array.array('f', bytes(4 * width * height))
    gy = array.array('f', bytes(4 * width * height))
    for y in range(1, height - 1):
        row = y * width
        for x in range(1, width - 1):
            gx[row + x] = (image[row + x + 1] - image[row + x - 1]) * 0.5
            gy[row + x] = (image[row + width + x] - image[row - width + x]) * 0.5
    return gx, gy


def bilinear(image, width, height, x, y):
    x0 = math.floor(x)
    y0 = math.floor(y)
    if x0 < 0 or y0 < 0 or x0 >= width - 1 or y0 >= height - 1:
        return None
    fx = x - x0
    fy = y - y0
    i = int(y0) * width + int(x0)
    top = image[i] * (1.0 - fx) + image[i + 1] * fx
    bottom = image[i + width] * (1.0 - fx) + image[i + width + 1] * fx
    return top * (1.0 - fy) + bottom * fy


def lucas_kanade_shift(first, second, width, height, mask=None, margin=12,
                       max_samples=40000, iterations=8, blur_passes=2):
    """Global translation d with ``second(x) ~= first(x - d)``, in pixels.

    Iterated gradient least squares: with the current estimate, sample the first
    image and its gradient bilinearly at ``x - d`` and solve the 2x2 normal
    equations for the correction.  Deterministic: samples are the highest
    gradient-magnitude pixels of the (blurred) first image inside the mask,
    ordered by magnitude then by index.
    """
    blurred_first = binomial_blur(first, width, height, blur_passes)
    blurred_second = binomial_blur(second, width, height, blur_passes)
    gx, gy = central_gradients(blurred_first, width, height)
    candidates = []
    for y in range(margin, height - margin):
        row = y * width
        for x in range(margin, width - margin):
            i = row + x
            if mask is not None and not mask[i]:
                continue
            energy = gx[i] * gx[i] + gy[i] * gy[i]
            if energy > 0.0:
                candidates.append((-energy, i))
    candidates.sort()
    selected = [i for _, i in candidates[:max_samples]]
    dx = dy = 0.0
    used = 0
    residual = None
    for _ in range(iterations):
        a11 = a12 = a22 = b1 = b2 = 0.0
        used = 0
        squared = 0.0
        for i in selected:
            py, px = divmod(i, width)
            sx = px - dx
            sy = py - dy
            value = bilinear(blurred_first, width, height, sx, sy)
            if value is None:
                continue
            jx = bilinear(gx, width, height, sx, sy)
            jy = bilinear(gy, width, height, sx, sy)
            r = blurred_second[i] - value
            a11 += jx * jx
            a12 += jx * jy
            a22 += jy * jy
            b1 -= jx * r
            b2 -= jy * r
            squared += r * r
            used += 1
        determinant = a11 * a22 - a12 * a12
        residual = math.sqrt(squared / used) if used else None
        if determinant == 0.0 or used == 0:
            break
        step_x = (b1 * a22 - b2 * a12) / determinant
        step_y = (a11 * b2 - a12 * b1) / determinant
        dx += step_x
        dy += step_y
        if abs(step_x) < 1e-6 and abs(step_y) < 1e-6:
            break
    return {'method': 'lucas_kanade', 'dx_px': dx, 'dy_px': dy,
            'samples': len(selected), 'used': used, 'residual_rms': residual}


# ---- pure-Python FFT and phase correlation -----------------------------------------

def _fft(values):
    """In-place iterative radix-2 FFT of a list of complex numbers."""
    n = len(values)
    if n & (n - 1):
        raise MalformedInput('FFT length must be a power of two')
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            values[i], values[j] = values[j], values[i]
    length = 2
    while length <= n:
        step = -2.0 * math.pi / length
        root = cmath.exp(complex(0.0, step))
        half = length >> 1
        for start in range(0, n, length):
            twiddle = 1.0 + 0j
            for k in range(half):
                a = values[start + k]
                b = values[start + k + half] * twiddle
                values[start + k] = a + b
                values[start + k + half] = a - b
                twiddle *= root
        length <<= 1
    return values


def fft2(tile, size):
    """Row-major complex tile -> its 2-D DFT, row-major."""
    data = [list(tile[r * size:(r + 1) * size]) for r in range(size)]
    for row in data:
        _fft(row)
    for c in range(size):
        column = [data[r][c] for r in range(size)]
        _fft(column)
        for r in range(size):
            data[r][c] = column[r]
    return [v for row in data for v in row]


def ifft2(spectrum, size):
    conjugated = [v.conjugate() for v in spectrum]
    transformed = fft2(conjugated, size)
    scale = 1.0 / (size * size)
    return [v.conjugate() * scale for v in transformed]


def hann_tile(image, width, origin_x, origin_y, size):
    """Hann-windowed, mean-removed square tile as complex values."""
    window = [0.5 - 0.5 * math.cos(2.0 * math.pi * i / (size - 1)) for i in range(size)]
    raw = []
    for y in range(size):
        row = (origin_y + y) * width + origin_x
        raw.extend(image[row:row + size])
    mean = sum(raw) / len(raw)
    tile = []
    for y in range(size):
        wy = window[y]
        base = y * size
        for x in range(size):
            tile.append(complex((raw[base + x] - mean) * window[x] * wy, 0.0))
    return tile


def best_tile_origin(image, width, height, size, mask=None, step=32, margin=8):
    """Deterministic tile choice: the origin whose tile has the most gradient
    energy (and, inside a mask, the most masked pixels as the first key)."""
    gx, gy = central_gradients(image, width, height)
    best = None
    for oy in range(margin, height - size - margin + 1, step):
        for ox in range(margin, width - size - margin + 1, step):
            energy = 0.0
            covered = 0
            for y in range(oy, oy + size, 4):
                row = y * width
                for x in range(ox, ox + size, 4):
                    if mask is not None and not mask[row + x]:
                        continue
                    covered += 1
                    energy += gx[row + x] ** 2 + gy[row + x] ** 2
            key = (covered, energy)
            if best is None or key > best[0]:
                best = (key, ox, oy)
    if best is None:
        return None
    return best[1], best[2]


def _parabolic(previous, centre, following):
    denominator = previous - 2.0 * centre + following
    if denominator == 0.0:
        return 0.0
    offset = 0.5 * (previous - following) / denominator
    return offset if -1.0 <= offset <= 1.0 else 0.0


def tile_mask_fraction(mask, width, origin, size):
    if mask is None:
        return None
    ox, oy = origin
    covered = 0
    for y in range(oy, oy + size):
        row = y * width
        covered += sum(mask[row + ox:row + ox + size])
    return covered / (size * size)


PHASE_LOWPASS = 0.06   # Gaussian sigma as a fraction of the tile size


def phase_correlation_shift(first, second, width, height, origin, size,
                            lowpass=PHASE_LOWPASS):
    """Global translation d with ``second(x) ~= first(x - d)``, from the
    normalized cross-power spectrum of one tile, with a parabolic peak fit.

    Whitening makes the correlation peak a one-sample spike, which a parabolic
    fit cannot localize below a pixel, so the whitened spectrum is multiplied by
    a Gaussian of sigma `lowpass * size`.  This broadens the peak into a smooth
    blob the fit can follow.  The method stays an *independent cross-check* of
    the Lucas-Kanade estimate: on a 64-pixel tile it reads a few percent low,
    and its tile cannot be masked, so unjittered content inside it pulls the
    estimate towards zero (`tile_masked_fraction` states how much).
    """
    ox, oy = origin
    f1 = fft2(hann_tile(first, width, ox, oy, size), size)
    f2 = fft2(hann_tile(second, width, ox, oy, size), size)
    sigma = max(1e-6, lowpass * size)
    cross = []
    for index, (a, b) in enumerate(zip(f2, f1)):
        ky, kx = divmod(index, size)
        fy = ky - size if ky > size // 2 else ky
        fx = kx - size if kx > size // 2 else kx
        value = a * b.conjugate()
        magnitude = abs(value)
        weight = math.exp(-0.5 * (fx * fx + fy * fy) / (sigma * sigma))
        cross.append((value / magnitude) * weight if magnitude > 1e-30 else 0.0 + 0j)
    correlation = [v.real for v in ifft2(cross, size)]
    peak = max(range(len(correlation)), key=lambda i: correlation[i])
    py, px = divmod(peak, size)
    left = correlation[py * size + (px - 1) % size]
    right = correlation[py * size + (px + 1) % size]
    up = correlation[((py - 1) % size) * size + px]
    down = correlation[((py + 1) % size) * size + px]
    dx = px + _parabolic(left, correlation[peak], right)
    dy = py + _parabolic(up, correlation[peak], down)
    if dx > size / 2:
        dx -= size
    if dy > size / 2:
        dy -= size
    return {'method': 'phase_correlation', 'dx_px': dx, 'dy_px': dy,
            'tile': [ox, oy, size], 'peak': correlation[peak], 'lowpass_sigma_fraction': lowpass}


def spectral_high_fraction(image, width, origin, size, quantization_variance=0.0):
    """Fraction of windowed tile energy above half Nyquist (radius > size/4).

    `quantization_variance` is the per-pixel variance of a flat noise floor in
    the source (an 8-bit readback has one; an FP16 one does not).  White noise
    spreads over the whole plane, so in a smooth image it can dominate the high
    band; the estimated floor is reported and a corrected fraction with it
    removed from both bands is given beside the raw one.
    """
    ox, oy = origin
    spectrum = fft2(hann_tile(image, width, ox, oy, size), size)
    total = high = 0.0
    bins = high_bins = 0
    limit = size / 4.0
    for index, value in enumerate(spectrum):
        ky, kx = divmod(index, size)
        fy = ky - size if ky > size // 2 else ky
        fx = kx - size if kx > size // 2 else kx
        if fx == 0 and fy == 0:
            continue
        power = value.real * value.real + value.imag * value.imag
        total += power
        bins += 1
        if math.hypot(fx, fy) > limit:
            high += power
            high_bins += 1
    result = {'total_energy': total, 'high_energy': high,
              'high_fraction': high / total if total else None,
              'band': 'radius > size/4 (above half Nyquist)'}
    if quantization_variance > 0.0 and bins:
        # Parseval over the windowed tile: sum |F|^2 = N^2 * sum w(x)^2 w(y)^2 * var.
        window = [0.5 - 0.5 * math.cos(2.0 * math.pi * i / (size - 1)) for i in range(size)]
        weight = sum(w * w for w in window) ** 2
        floor_total = size * size * weight * quantization_variance
        floor_high = floor_total * (high_bins / bins)
        result['quantization_floor_energy'] = floor_total
        result['quantization_share_of_high_band'] = floor_high / high if high else None
        corrected_total = total - floor_total
        corrected_high = high - floor_high
        result['high_fraction_noise_corrected'] = (corrected_high / corrected_total
                                                   if corrected_total > 0 and corrected_high > 0 else None)
    return result


def gradient_energy(image, width, height, mask=None, margin=12):
    total = 0.0
    count = 0
    for y in range(margin, height - margin):
        row = y * width
        for x in range(margin, width - margin):
            if mask is not None and not mask[row + x]:
                continue
            gx = (image[row + x + 1] - image[row + x - 1]) * 0.5
            gy = (image[row + width + x] - image[row - width + x]) * 0.5
            total += gx * gx + gy * gy
            count += 1
    return {'mean_squared_gradient': total / count if count else None, 'pixels': count}


# ---- log analysis ------------------------------------------------------------------

CUT_BURST_GAP = 1


def bursts_of(frames):
    """Group consecutive captured frame numbers into bursts."""
    groups = []
    for number_ in sorted(frames):
        if groups and number_ == groups[-1][-1] + CUT_BURST_GAP:
            groups[-1].append(number_)
        else:
            groups.append([number_])
    return groups


def analyze_log(path, device='1'):
    report = OrderedDict()
    windows = {}
    scene = {}
    summary_frame = None
    configuration = {}
    frame_records = []
    cut_records = {}
    readbacks = {}
    route_gates = Counter()
    route_per_frame = Counter()
    route_matched = Counter()
    route_routed = Counter()
    diagnostics = {'failure_records': [], 'reset_records': [], 'shutdown_records': [],
                   'nonzero_results': [], 'rejected_telemetry': 0}
    telemetry = {}
    brackets = []
    previous_event = None
    clock = None
    captured_frames = set()
    for event, line in stream_records(path):
        if event in ('motion_output_mode', 'motion_output_device', 'motion_output_taa',
                     'motion_output_target', 'adapter', 'x3-modern-renderer', 'ownership_mode',
                     'ownership_factory', 'object_trace', 'object_lifetime', 'capture_caps',
                     'motion_capture_mode', 'motion_output_variant'):
            configuration.setdefault(event, []).append(fields(line))
        elif event == 'telemetry_presentation':
            configuration.setdefault(event, []).append(fields(line))
        elif event == 'telemetry_start':
            clock = number(fields(line).get('qpc_frequency'))
        elif event == 'motion_output_frame':
            f = fields(line)
            frame_records.append(f)
            scene[number(f.get('frame'))] = {'latched': number(f.get('latched'), 0),
                                             'routed': number(f.get('routed'), 0),
                                             'taa_resolved': number(f.get('taa_resolved'), 0)}
        elif event == 'motion_output_cut':
            f = fields(line)
            cut_records[number(f.get('frame'))] = f
        elif event.startswith('motion_output_') and event.endswith('readback'):
            f = fields(line)
            kind = event[len('motion_output_'):-len('_readback')] or 'motion'
            readbacks.setdefault(kind, []).append(f)
            if not hresult_ok(f.get('result')):
                diagnostics['nonzero_results'].append({'event': event, 'record': f})
            captured_frames.add(number(f.get('frame')))
        elif event == 'motion_route':
            f = fields(line)
            gate = number(f.get('gate'))
            route_gates[gate] += 1
            frame_number = number(f.get('frame'))
            route_per_frame[frame_number] += 1
            route_routed[frame_number] += number(f.get('routed'), 0)
            route_matched[frame_number] += number(f.get('matched'), 0)
        elif event == 'telemetry_metric':
            f = fields(line)
            if not telemetry_accumulate(telemetry, f):
                diagnostics['rejected_telemetry'] += 1
            elif f.get('name') in WINDOW_METRICS:
                windows.setdefault(f.get('device', '?') + ':' + f['name'], []).append({
                    'frame': summary_frame, 'count': number(f.get('count')),
                    'total_us': real(f.get('total_us')), 'min_us': real(f.get('min_us')),
                    'max_us': real(f.get('max_us'))})
        elif event == 'telemetry_summary':
            f = fields(line)
            if f.get('device') == device:
                summary_frame = number(f.get('frame'))
        elif event == 'capture_event':
            f = fields(line)
            if f.get('op') == 'stretch_rect' and previous_event and previous_event[0] == f.get('frame'):
                begin, end = number(previous_event[2]), number(f.get('qpc'))
                if begin is not None and end is not None and end >= begin:
                    brackets.append((f.get('frame'), previous_event[1], end - begin))
            previous_event = (f.get('frame'), f.get('op'), f.get('qpc'))
        elif event in ('motion_output_reset', 'device_reset'):
            diagnostics['reset_records'].append(fields(line))
        elif event in ('motion_output_release', 'device_destroy'):
            diagnostics['shutdown_records'].append({'event': event, **fields(line)})
        if any(token in line for token in FAILURE_TOKENS) and not line.startswith('telemetry_'):
            f = fields(line)
            if any(k.endswith(('_failed', '_errors')) and number(v, 0) for k, v in f.items()) \
                    or event.endswith('_failed') or 'unsupported' in line:
                diagnostics['failure_records'].append({'event': event, 'record': f})
    telemetry_finish(telemetry)

    # --- TAA status and jitter -------------------------------------------------------
    taa_frames = []
    skip_histogram = Counter()
    resolved = attempted = history = 0
    for f in frame_records:
        frame_number = number(f.get('frame'))
        entry = {
            'frame': frame_number,
            'captured': frame_number in captured_frames,
            'draws': number(f.get('draws')),
            'routed': number(f.get('routed')),
            'matched': number(f.get('matched')),
            'latched': number(f.get('latched')),
            'committed': number(f.get('committed')),
            'selector_state': number(f.get('selector_state')),
            'taa_attempted': number(f.get('taa_attempted')),
            'taa_resolved': number(f.get('taa_resolved')),
            'taa_history': number(f.get('taa_history')),
            'taa_skip': number(f.get('taa_skip')),
            'taa_result': f.get('taa_result'),
            'taa_copy': f.get('taa_copy'),
            'taa_restore': f.get('taa_restore'),
            'taa_references': number(f.get('taa_references')),
            'jitter': number(f.get('jitter')),
            'jitter_index': number(f.get('jitter_index')),
            'jitter_px': [real(f.get('jitter_x'), 0.0), real(f.get('jitter_y'), 0.0)],
            'jitter_previous_px': [real(f.get('jitter_previous_x'), 0.0),
                                   real(f.get('jitter_previous_y'), 0.0)],
            'jittered_draws': number(f.get('jittered')),
            'cut': number(f.get('cut')),
            'cut_median_px': real(f.get('cut_median_px')),
            'cut_missing_fraction': real(f.get('cut_missing')),
            'cut_samples': number(f.get('cut_samples')),
            'gates': {g: number(f.get(g), 0) for g in
                      ('gate1', 'gate2', 'gate3', 'gate4', 'gate5', 'gate6')},
            'apply_failures': number(f.get('apply_failures'), 0),
            'restore_failures': number(f.get('restore_failures'), 0),
            'scene_open': number(f.get('scene_open')),
            'active_queries': number(f.get('active_queries')),
        }
        taa_frames.append(entry)
        skip_histogram[entry['taa_skip']] += 1
        resolved += entry['taa_resolved'] or 0
        attempted += entry['taa_attempted'] or 0
        history += entry['taa_history'] or 0

    groups = bursts_of(captured_frames - {None})
    burst_reports = []
    for index, group in enumerate(groups):
        cuts = [cut_records.get(n) for n in group]
        medians = [real(c.get('median_px')) for c in cuts if c]
        missing = [real(c.get('missing_fraction')) for c in cuts if c]
        samples = [number(c.get('samples')) for c in cuts if c]
        verdicts = [number(c.get('cut')) for c in cuts if c]
        burst_reports.append({
            'index': index,
            'frames': group,
            'cut_median_px': medians,
            'cut_missing_fraction': missing,
            'cut_samples': samples,
            'cut_verdicts': verdicts,
            'max_cut_median_px': max(medians) if medians else None,
            'max_cut_missing_fraction': max(missing) if missing else None,
            'any_cut': any(verdicts),
            # The log's own stationarity criterion, restated in pixels as
            # iteration 6 anomaly 1 required.
            'stationary': bool(medians) and max(medians) <= 0.01,
            'jitter_px': [next((e['jitter_px'] for e in taa_frames if e['frame'] == n), None)
                          for n in group],
        })

    captured = [e for e in taa_frames if e['captured']]
    counter_consistency = []
    for entry in captured:
        logged = route_per_frame.get(entry['frame'], 0)
        expected = (entry['routed'] or 0) + entry['gates']['gate3'] + entry['gates']['gate4'] \
            + entry['gates']['gate5']
        counter_consistency.append({
            'frame': entry['frame'],
            'motion_route_lines': logged,
            'routed_plus_gate345': expected,
            'routed_sum': route_routed.get(entry['frame'], 0),
            'matched_sum': route_matched.get(entry['frame'], 0),
            'agrees': logged == expected and route_routed.get(entry['frame'], 0) == entry['routed']
            and route_matched.get(entry['frame'], 0) == entry['matched'],
        })

    report['configuration'] = configuration
    report['taa'] = {
        'frames': taa_frames,
        'records': len(taa_frames),
        'attempted': attempted,
        'resolved': resolved,
        'failed': sum(1 for e in taa_frames if e['taa_attempted'] and not e['taa_resolved']),
        'history_valid': history,
        'skip_histogram': {str(k): v for k, v in sorted(skip_histogram.items(),
                                                        key=lambda kv: (kv[0] is None, kv[0]))},
        'skip_reasons': {'0': 'no skip (resolved)', '2': 'selector never reached the copy',
                         '3': 'no jitter', '6': 'open application query'},
        'result_histogram': dict(Counter(e['taa_result'] for e in taa_frames)),
        'references_range': [min((e['taa_references'] for e in taa_frames if e['taa_references'] is not None), default=None),
                             max((e['taa_references'] for e in taa_frames if e['taa_references'] is not None), default=None)],
        'captured_frames_all_resolved': all(e['taa_resolved'] == 1 and e['taa_history'] == 1
                                            for e in captured),
    }
    report['bursts'] = burst_reports
    report['route'] = {
        'totals': {
            'draws': sum(e['draws'] or 0 for e in taa_frames),
            'routed': sum(e['routed'] or 0 for e in taa_frames),
            'matched': sum(e['matched'] or 0 for e in taa_frames),
            'gates': {g: sum(e['gates'][g] for e in taa_frames) for g in
                      ('gate1', 'gate2', 'gate3', 'gate4', 'gate5', 'gate6')},
            'apply_failures': sum(e['apply_failures'] for e in taa_frames),
            'restore_failures': sum(e['restore_failures'] for e in taa_frames),
        },
        'captured': {
            'frames': len(captured),
            'draws': sum(e['draws'] or 0 for e in captured),
            'routed': sum(e['routed'] or 0 for e in captured),
            'matched': sum(e['matched'] or 0 for e in captured),
            'gates': {g: sum(e['gates'][g] for e in captured) for g in
                      ('gate1', 'gate2', 'gate3', 'gate4', 'gate5', 'gate6')},
        },
        'per_draw_gate_histogram': {str(k): v for k, v in sorted(route_gates.items(),
                                                                 key=lambda kv: (kv[0] is None, kv[0]))},
        'counter_consistency': counter_consistency,
        'counter_consistency_pass': all(c['agrees'] for c in counter_consistency),
    }
    report['readbacks'] = {kind: {'records': len(items),
                                  'all_zero_result': all(hresult_ok(i.get('result')) for i in items),
                                  'frames': [number(i.get('frame')) for i in items],
                                  'bytes': sorted({number(i.get('bytes')) for i in items}),
                                  'dimensions': sorted({(i.get('width'), i.get('height')) for i in items})}
                           for kind, items in sorted(readbacks.items())}
    report['diagnostics'] = diagnostics
    report['telemetry'] = {'metrics': telemetry, 'clock_hz': clock,
                           'windows': split_windows(windows, scene)}
    report['_brackets'] = brackets
    report['_captured_frames'] = sorted(captured_frames - {None})
    return report


# ---- image analysis ----------------------------------------------------------------

HISTORY_WEIGHT = 0.9  # src/renderer/temporal_pass.h: TemporalPass::Input::weight


def analyze_burst_images(burst, captures, device, width, height, options):
    """Sub-pixel shift and blur for one burst of consecutive captured frames."""
    frames = burst['frames']
    jitter = {n: j for n, j in zip(frames, burst['jitter_px'])}
    colour = {}
    resolved = {}
    masks = {}
    missing = []
    for n in frames:
        paths = {
            'colour': captures / f'color_{device}_{n}.bgra8',
            'resolved': captures / f'taa_{device}_{n}.rgba16f',
            'motion': captures / f'motion_{device}_{n}.rgba32f',
        }
        absent = [k for k, p in paths.items() if not p.is_file()]
        if absent:
            missing.append({'frame': n, 'absent': absent})
            continue
        colour[n] = load_luma_bgra8(paths['colour'], width, height)
        resolved[n] = load_luma_rgba16f(paths['resolved'], width, height)
        masks[n] = load_validity_mask(paths['motion'], width, height)
    usable = [n for n in frames if n in colour]
    if len(usable) < 2:
        return {'status': 'unavailable', 'reason': 'fewer than two complete frames', 'missing': missing}

    size = options['tile']
    pairs = []
    for first, second in zip(usable, usable[1:]):
        if second != first + 1:
            continue
        shared = bytearray(a & b for a, b in zip(masks[first], masks[second]))
        shared = erode(shared, width, height, 2)
        covered = sum(shared)
        origin = best_tile_origin(colour[first], width, height, size, shared,
                                  step=options['tile_step'])
        step = [jitter[second][0] - jitter[first][0], jitter[second][1] - jitter[first][1]]
        entry = {
            'frames': [first, second],
            'jitter_px': [jitter[first], jitter[second]],
            'jitter_step_px': step,
            'mask_pixels': covered,
            'mask_fraction': covered / (width * height),
        }
        for label, images in (('current_colour', colour), ('resolved', resolved)):
            lk = lucas_kanade_shift(images[first], images[second], width, height, shared,
                                    max_samples=options['max_samples'])
            entry[label] = {'lucas_kanade': lk}
            if origin is not None:
                correlation = phase_correlation_shift(images[first], images[second],
                                                      width, height, origin, size)
                # The tile cannot be masked (the transform needs a full square),
                # so unjittered background inside it pulls the estimate towards
                # zero.  The covered fraction states how much.
                correlation['tile_masked_fraction'] = tile_mask_fraction(shared, width, origin, size)
                entry[label]['phase_correlation'] = correlation
        # Model predictions for the resolved image.  Both use the same history
        # weight w and differ only in the jitter convention.  Write the resolved
        # image of frame n as the scene displaced by d_n.
        #
        #   history at previous position + previous jitter (the suspected defect):
        #     d_n = (1-w) j_n + w (d_(n-1) + j_n - j_(n-1)), so (d_n - j_n) =
        #     w (d_(n-1) - j_(n-1)): the deviation decays by w each frame and
        #     d_n -> j_n.  The resolved image is then displaced by the *current*
        #     jitter, and its inter-frame shift is the full jitter step.
        #   history on the unjittered grid (the correct convention):
        #     d_n = (1-w) j_n + w d_(n-1), a (1-w)-weighted running mean of a
        #     zero-mean jitter sequence, so d stays near zero and the whole
        #     inter-frame shift is the (1-w) term below.
        w = options['history_weight']
        reach = max(abs(v) for j in jitter.values() for v in j)
        entry['model'] = {
            'history_weight': w,
            'jitter_tracking_resolve_dx': step[0],
            'jitter_tracking_resolve_dy': step[1],
            'stable_resolve_bound_px': (1.0 - w) * 2.0 * reach,
            'note': ('jitter_tracking: d_n -> j_n, inter-frame shift = the jitter step '
                     '(a deviation from it decays by w per frame); stable: d_n stays near 0, '
                     'inter-frame shift = (1-w)(j_n - d_(n-1)), bounded by stable_resolve_bound_px'),
        }
        for label in ('current_colour', 'resolved'):
            lk = entry[label]['lucas_kanade']
            entry[label]['shift_vs_jitter_step'] = [
                lk['dx_px'] - step[0], lk['dy_px'] - step[1]]
            entry[label]['ratio_to_jitter_step'] = [
                lk['dx_px'] / step[0] if step[0] else None,
                lk['dy_px'] / step[1] if step[1] else None]
        base = entry['current_colour']['lucas_kanade']
        res = entry['resolved']['lucas_kanade']
        entry['resolved_over_colour_ratio'] = [
            res['dx_px'] / base['dx_px'] if base['dx_px'] else None,
            res['dy_px'] / base['dy_px'] if base['dy_px'] else None]
        tracking = math.hypot(res['dx_px'] - entry['model']['jitter_tracking_resolve_dx'],
                              res['dy_px'] - entry['model']['jitter_tracking_resolve_dy'])
        stable = math.hypot(res['dx_px'], res['dy_px'])
        entry['model']['residual_to_jitter_tracking_px'] = tracking
        entry['model']['residual_to_stable_px'] = stable
        entry['model']['verdict'] = 'tracks_jitter' if tracking < stable else 'stable'
        entry['colour_vs_logged_jitter_error_px'] = math.hypot(
            base['dx_px'] - step[0], base['dy_px'] - step[1])
        pairs.append(entry)

    # A flat 8-bit quantization floor exists in the pre-resolve readback only:
    # var = (1/255)^2/12 per channel, projected through the luma weights.  A
    # central difference of white noise gives var/2 per axis, so the two axes
    # together contribute exactly `quantization` to the mean squared gradient.
    quantization = sum(c * c for c in LUMA) * (1.0 / 255.0) ** 2 / 12.0

    blur = []
    for n in usable:
        shared = erode(masks[n], width, height, 2)
        origin = best_tile_origin(colour[n], width, height, size, shared, step=options['tile_step'])
        item = {'frame': n, 'mask_pixels': sum(shared)}
        for label, images, noise in (('current_colour', colour, quantization),
                                     ('resolved', resolved, 0.0)):
            item[label] = gradient_energy(images[n], width, height, shared)
            if origin is not None:
                item[label].update(spectral_high_fraction(images[n], width, origin, size, noise))
        current = item['current_colour']['mean_squared_gradient']
        item['gradient_energy_ratio'] = (item['resolved']['mean_squared_gradient'] / current
                                         if current else None)
        if item['current_colour'].get('high_fraction'):
            item['high_frequency_fraction_ratio'] = (item['resolved']['high_fraction']
                                                     / item['current_colour']['high_fraction'])
            corrected = item['current_colour'].get('high_fraction_noise_corrected')
            item['high_frequency_fraction_ratio_noise_corrected'] = (
                item['resolved']['high_fraction'] / corrected if corrected else None)
        item['colour_quantization_gradient_floor'] = quantization
        item['colour_quantization_share'] = quantization / current if current else None
        # Same-frame discriminator, independent of the frame pairing: if the
        # resolved image ends up displaced by the current jitter (the defect), it
        # sits on top of the current colour and the shift between them is zero;
        # if the history stays on the unjittered grid, the resolved image sits at
        # -j_n relative to the current colour.
        shift = lucas_kanade_shift(colour[n], resolved[n], width, height, shared,
                                   max_samples=options['max_samples'])
        jx, jy = jitter[n]
        model = {'jitter_tracking_prediction_px': [0.0, 0.0],
                 'stable_prediction_px': [-jx, -jy],
                 'residual_to_jitter_tracking_px': math.hypot(shift['dx_px'], shift['dy_px']),
                 'residual_to_stable_px': math.hypot(shift['dx_px'] + jx, shift['dy_px'] + jy)}
        model['verdict'] = ('tracks_jitter' if model['residual_to_jitter_tracking_px']
                            < model['residual_to_stable_px'] else 'stable')
        item['colour_to_resolved_shift'] = shift
        item['same_frame_model'] = model
        blur.append(item)

    verdicts = [p['model']['verdict'] for p in pairs]
    return {
        'status': 'evaluated',
        'frames': usable,
        'missing': missing,
        'width': width,
        'height': height,
        'tile': size,
        'pairs': pairs,
        'blur': blur,
        'verdict': {
            'pairs': len(pairs),
            'tracks_jitter': verdicts.count('tracks_jitter'),
            'stable': verdicts.count('stable'),
            'colour_max_error_vs_logged_jitter_px':
                max((p['colour_vs_logged_jitter_error_px'] for p in pairs), default=None),
            'resolved_over_colour_ratio_range':
                [min(v for p in pairs for v in p['resolved_over_colour_ratio'] if v is not None),
                 max(v for p in pairs for v in p['resolved_over_colour_ratio'] if v is not None)]
                if pairs else None,
            'gradient_energy_ratio_range':
                [min(b['gradient_energy_ratio'] for b in blur),
                 max(b['gradient_energy_ratio'] for b in blur)] if blur else None,
        },
        'method': {
            'mask': ('pixels with alpha == 1 in motion_<device>_<frame>.rgba32f in both frames, '
                     'eroded by 2 px: routed scene geometry, the only content the route jitters'),
            'lucas_kanade': ('iterated gradient least squares for one global translation on '
                             '2x [1,2,1]/4-blurred Rec.709 luminance, highest-gradient masked '
                             'pixels; returns d with I2(x) = I1(x - d), +X right, +Y down'),
            'phase_correlation': ('normalized cross-power spectrum of one Hann-windowed tile '
                                  '(largest masked gradient energy), parabolic peak fit; same sign'),
            'blur': ('mean squared central-difference luminance gradient over the mask, and the '
                     'fraction of windowed tile spectral energy above half Nyquist'),
        },
    }


# ---- report ------------------------------------------------------------------------

def render_text(summary):
    lines = []
    source = summary['source']
    lines.append(f"iteration-07 TAA analysis of {source['log']} "
                 f"({source['bytes']} bytes, sha256 {source['sha256'][:16]}…)")
    taa = summary['taa']
    lines.append(f"TAA: {taa['records']} motion_output_frame records, attempted {taa['attempted']}, "
                 f"resolved {taa['resolved']}, failed {taa['failed']}, history valid {taa['history_valid']}")
    lines.append(f"  skip histogram {taa['skip_histogram']} ; results {taa['result_histogram']}")
    lines.append(f"  every captured frame resolved with history: {taa['captured_frames_all_resolved']}")
    route = summary['route']
    lines.append(f"route totals: draws {route['totals']['draws']} routed {route['totals']['routed']} "
                 f"matched {route['totals']['matched']} gates {route['totals']['gates']}")
    lines.append(f"  per-draw gate histogram {route['per_draw_gate_histogram']}; "
                 f"counter consistency {route['counter_consistency_pass']}")
    for burst in summary['bursts']:
        lines.append(f"burst {burst['index']} frames {burst['frames'][0]}-{burst['frames'][-1]}: "
                     f"cut median {burst['cut_median_px']} px, missing {burst['cut_missing_fraction']}, "
                     f"verdicts {burst['cut_verdicts']}, stationary={burst['stationary']}")
    diag = summary['diagnostics']
    lines.append(f"diagnostics: {len(diag['failure_records'])} failure records, "
                 f"{len(diag['reset_records'])} resets, {len(diag['shutdown_records'])} shutdown records, "
                 f"{len(diag['nonzero_results'])} nonzero readback results")
    for name, entry in sorted(summary['telemetry']['metrics'].items()):
        lines.append(f"  telemetry {name}: n={entry['count']} mean={entry['mean_us']:.1f}us "
                     f"max={entry['max_us']:.1f}us buckets={entry['buckets']}")
    for label, holder in (('run', summary['telemetry']),
                          ('baseline', summary['telemetry'].get('baseline') or {})):
        for name, split in sorted((holder.get('windows') or {}).items()):
            for regime, stats in sorted(split.items()):
                if stats.get('status') != 'evaluated':
                    continue
                lines.append(f"  window {label}/{name}/{regime}: {stats['windows']} windows, "
                             f"per-window mean median {stats['window_mean_us']['median']:.1f}us, "
                             f"per-window min median {stats['window_min_us']['median']:.1f}us")
    boundary = summary['boundary_stretchrect']
    for label, entry in boundary.items():
        if isinstance(entry, dict) and entry.get('status') == 'measured':
            lines.append(f"  boundary StretchRect bracket [{label}]: n={entry['samples']} "
                         f"median={entry['median_us']:.1f}us max={entry['max_us']:.1f}us")
    images = summary.get('image_analysis') or {}
    for burst in images.get('bursts', []):
        analysis = burst['analysis']
        if analysis.get('status') != 'evaluated':
            lines.append(f"image burst {burst['frames']}: {analysis.get('reason')}")
            continue
        for pair in analysis['pairs']:
            colour = pair['current_colour']['lucas_kanade']
            res = pair['resolved']['lucas_kanade']
            lines.append(
                f"image {pair['frames'][0]}->{pair['frames'][1]}: jitter step "
                f"({pair['jitter_step_px'][0]:+.4f},{pair['jitter_step_px'][1]:+.4f}) "
                f"colour ({colour['dx_px']:+.4f},{colour['dy_px']:+.4f}) "
                f"resolved ({res['dx_px']:+.4f},{res['dy_px']:+.4f}) "
                f"resolved/colour ({pair['resolved_over_colour_ratio'][0]:.3f},"
                f"{pair['resolved_over_colour_ratio'][1]:.3f})")
        for item in analysis['blur']:
            lines.append(f"blur frame {item['frame']}: gradient energy ratio "
                         f"{item['gradient_energy_ratio']:.4f}, high-frequency fraction ratio "
                         f"{item.get('high_frequency_fraction_ratio', float('nan')):.4f}")
    return '\n'.join(lines) + '\n'


def sha256_of(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', type=Path, help='TAA run capture session log (streamed)')
    parser.add_argument('--output', type=Path, required=True, help='summary JSON path')
    parser.add_argument('--text', type=Path, help='optional text report path')
    parser.add_argument('--captures', type=Path, help='readback directory (default: log directory)')
    parser.add_argument('--baseline-log', type=Path,
                        help='earlier run whose telemetry is compared (iteration 6)')
    parser.add_argument('--baseline-label', default='baseline')
    parser.add_argument('--device', default='1', help='device index in the readback file names')
    parser.add_argument('--no-images', action='store_true', help='log evidence only')
    parser.add_argument('--image-burst', type=int, action='append', default=None,
                        metavar='FIRST_FRAME',
                        help='analyse the burst starting at this frame (default: every '
                             'burst the log certifies stationary)')
    parser.add_argument('--tile', type=int, default=256, help='phase-correlation tile size (power of two)')
    parser.add_argument('--tile-step', type=int, default=64, help='tile search stride')
    parser.add_argument('--max-shift-samples', type=int, default=40000)
    parser.add_argument('--history-weight', type=float, default=HISTORY_WEIGHT)
    args = parser.parse_args(argv)

    try:
        report = analyze_log(args.log)
    except MalformedInput as error:
        print(f'malformed input: {error}', file=sys.stderr)
        return 2
    brackets = report.pop('_brackets')
    captured = report.pop('_captured_frames')
    clock = report['telemetry']['clock_hz']

    summary = OrderedDict()
    summary['source'] = {'log': args.log.name, 'bytes': args.log.stat().st_size,
                         'sha256': sha256_of(args.log), 'captured_frames': captured}
    summary.update(report)
    summary['boundary_stretchrect'] = {
        'run': bracket_report(brackets, clock,
                              'QPC between the last capture_event op=color_fill and op=stretch_rect '
                              'of the same captured frame. Telemetry has no StretchRect metric in '
                              'this build. With --taa-debug this bracket also contains the '
                              'pre-resolve colour readback and the resolved FP16 readback, so it is '
                              'an upper bound on the resolve, not a measurement of it.'),
        'telemetry_metric_present': any(name.endswith(':stretch_rect') or name.endswith(':stretch')
                                        for name in summary['telemetry']['metrics']),
    }
    if args.baseline_log:
        base = scan_telemetry(args.baseline_log, args.device)
        baseline_metrics = base['metrics']
        summary['telemetry']['baseline'] = {
            'label': args.baseline_label, 'log': args.baseline_log.name,
            'bytes': args.baseline_log.stat().st_size,
            'metrics': baseline_metrics, 'rejected_records': base['rejected_records'],
            'clock_hz': base['clock_hz'], 'windows': base['windows'],
        }
        summary['boundary_stretchrect']['baseline'] = bracket_report(
            base['brackets'], base['clock_hz'],
            'same bracket in the baseline run, which had no resolve and no --taa-debug readbacks')
        comparison = {}
        for key, entry in summary['telemetry']['metrics'].items():
            other = baseline_metrics.get(key)
            if not other:
                continue
            comparison[key] = {
                'run': {k: entry[k] for k in ('count', 'mean_us', 'min_us', 'max_us', 'buckets')},
                'baseline': {k: other[k] for k in ('count', 'mean_us', 'min_us', 'max_us', 'buckets')},
                'mean_ratio': (entry['mean_us'] / other['mean_us']) if other['mean_us'] else None,
            }
        summary['telemetry']['comparison'] = comparison
    summary['telemetry']['limits'] = [
        'CPU-side wall-clock spans, never GPU execution time.',
        'frame_normal is the interval between completed Presents: application work, pacing, '
        'resource loading and diagnostics are all inside it.',
        'Only per-window count/failures/min/max/total and six duration buckets exist, so no '
        'median or percentile of frame time can be derived.',
        'Intervals adjacent to a captured frame are classified as capture intervals.',
        'Metrics can nest or overlap; never add totals across names.',
        'Two runs of different scenes are not a controlled comparison of the resolve cost.',
    ]

    summary['image_analysis'] = {'status': 'skipped'} if args.no_images else None
    if not args.no_images:
        captures = args.captures or args.log.parent
        presentation = (summary['configuration'].get('telemetry_presentation') or [{}])[-1]
        width = number(presentation.get('width'), 0)
        height = number(presentation.get('height'), 0)
        readback = (summary['readbacks'].get('taa') or {}).get('dimensions') or []
        if readback and readback[0][0]:
            width, height = int(readback[0][0]), int(readback[0][1])
        selected = []
        for burst in summary['bursts']:
            if args.image_burst is None:
                if burst['stationary']:
                    selected.append(burst)
            elif burst['frames'][0] in args.image_burst:
                selected.append(burst)
        options = {'tile': args.tile, 'tile_step': args.tile_step,
                   'max_samples': args.max_shift_samples, 'history_weight': args.history_weight}
        results = []
        for burst in selected:
            results.append({'frames': burst['frames'],
                            'cut_median_px': burst['cut_median_px'],
                            'analysis': analyze_burst_images(burst, captures, args.device,
                                                             width, height, options)})
        summary['image_analysis'] = {
            'status': 'evaluated' if results else 'unavailable',
            'reason': None if results else 'no burst selected (none certified stationary by the log)',
            'captures': str(captures),
            'selection': ('bursts whose motion_output_cut median_px <= 0.01 in every frame'
                          if args.image_burst is None else 'explicit --image-burst'),
            'bursts': results,
        }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('w', encoding='utf-8') as stream:
        json.dump(summary, stream, indent=2, allow_nan=False, default=str)
        stream.write('\n')
    text = render_text(summary)
    if args.text:
        args.text.write_text(text, encoding='utf-8')
    sys.stdout.write(text)
    print(f'summary: {args.output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
