#!/usr/bin/env python3
"""Iteration 8 (TAA rerun with the fixed history convention): run health, the
iteration-7 tremble/blur measurement re-run, and a per-pixel *flicker
classification* that says where the residual shimmer comes from.

The log evidence, the telemetry aggregation, the sub-pixel shift estimators and
the blur measurement are imported from ``analyze_iteration07_taa`` unchanged, so
the tremble and blur numbers of iteration 7 and iteration 8 are produced by the
same code and are directly comparable.  Only the new evidence lives here.

What is new.

1. **Flicker classification** (``flicker``).  On a burst the route certifies
   stationary, every pixel is assigned one class from the readbacks of the four
   frames, and the temporal variance of the resolved image is compared with the
   temporal variance of the pre-resolve colour at the same pixel:

   * ``sentinel`` - the RT2 current depth is negative in every frame.  The
     resolve returns the current colour unconditionally for these
     (``resolve.hlsl``: ``if (options.w > 0.5 && depth < 0) return ...``), so
     background, effects, particles and every unrouted draw get no temporal
     filtering at all.  Expected ratio 1.
   * ``routed_interior`` - RT2 depth valid in every frame, motion alpha 1 in
     every frame, and the depth image equal across the burst within
     ``--depth-tolerance`` (the resolve's own absolute rejection tolerance,
     1e-4).  These are the pixels the resolve can accumulate.  Expected
     ratio well below 1.
   * ``routed_edge`` - everything else that is not pure sentinel: RT2 valid in
     some frames only, motion alpha not constant, or the depth spread across
     the burst exceeds the tolerance.  Silhouettes and thin geometry land here,
     and the shader's per-tap depth test rejects history exactly where the
     expected depth and the history depth disagree by more than the tolerance.
   * ``thin_feature`` - an *overlay*, not a fourth partition: pixels whose raw
     colour is a high-contrast feature no more than two pixels wide, found by a
     morphological top-hat with a 3x3 structuring element (an opening with a
     3x3 square removes every bright feature narrower than 3 px, and the
     closing does the same for dark ones), gated on the 3x3 morphological
     gradient.  Its intersection with the three classes is reported.

   The ratio is reported as a distribution over pixels whose raw temporal
   variance exceeds ``--variance-floor`` (below that the raw signal is at the
   8-bit quantization floor and the ratio is meaningless), as an aggregate
   energy ratio, and as each class's share of the total resolved temporal
   variance - the last is the "where does the shimmer come from" number.

2. **Guide-line probe** (``yellow_lines``).  The user reports the station's thin
   yellow guide lines shimmering while standing still.  Bright yellow pixels
   (``min(R,G)`` high, ``min(R,G) - B`` high, ``R`` and ``G`` close) are located
   in the pre-resolve colour of every frame of the burst; their class, their RT2
   depth availability, their raw and resolved temporal variance and how much
   their coverage flips between frames (union against intersection) are
   reported.

3. **Run B hook** (``--run-b-log``).  The timing in this report is *not* a
   controlled comparison: one session, feature on throughout.  The user will
   supply a TAA-off run of the same scene.  ``--run-b-log`` streams that second
   session's telemetry and emits ``timing_comparison``: the per-window scene
   mean and per-window scene minimum of both runs and their ratio, per metric.
   Without it the section records ``status=pending`` and says what is missing.

Nothing is loaded whole: the log is streamed once by the iteration-7 reader and
the readbacks are loaded one image at a time, per burst.
"""
import argparse
import array
import json
import math
import statistics
import sys
from collections import Counter, OrderedDict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze_iteration07_taa as it07  # noqa: E402
# LUMA is re-exported: it is the luminance convention every measurement here
# shares with iteration 7, and the fixture builds its expectations from it.
from analyze_iteration07_taa import (  # noqa: E402
    HISTORY_WEIGHT, LUMA, MalformedInput, WINDOW_METRICS, analyze_burst_images,
    analyze_log, bracket_report, load_luma_bgra8, load_luma_rgba16f, number,
    scan_telemetry, sha256_of,
)

# The resolve's absolute device-depth rejection tolerance, src/temporal/resolve.h
# (`rejection.x`); a history tap whose previous depth differs from the expected
# depth by more than this is dropped.
DEPTH_TOLERANCE = 1e-4
# Luma temporal variance below which the raw signal is at the 8-bit floor: the
# per-channel quantization variance q^2/12 projected through the luma weights is
# 7.2e-7, so 1e-5 (a standard deviation of about 0.8/255) is an order above it.
VARIANCE_FLOOR = 1e-5
# A temporal standard deviation a viewer can see, in 8-bit levels.
VISIBLE_LEVELS = 2.0

CLASSES = ('sentinel', 'routed_interior', 'routed_edge')
SENTINEL, INTERIOR, EDGE = 0, 1, 2


# ---- readback loading --------------------------------------------------------------

def load_depth_r32f(path, width, height):
    """Row-major R32F RT2 readback (device depth, negative sentinel)."""
    if path.stat().st_size != width * height * 4:
        raise MalformedInput(f'{path.name}: size != {width}x{height}x4')
    out = array.array('f')
    with path.open('rb') as stream:
        out.fromfile(stream, width * height)
    if sys.byteorder != 'little':
        out.byteswap()
    return out


def load_motion_alpha(path, width, height):
    """Alpha channel of the route's RGBA32F readback: 1 valid, -1 sentinel."""
    if path.stat().st_size != width * height * 16:
        raise MalformedInput(f'{path.name}: size != {width}x{height}x16')
    raw = array.array('f')
    with path.open('rb') as stream:
        raw.fromfile(stream, width * height * 4)
    if sys.byteorder != 'little':
        raw.byteswap()
    return array.array('f', raw[3::4])


def yellow_mask_bgra8(path, width, height, minimum=60, gap=40, balance=0.45,
                      balance_floor=40):
    """1 where the 8-bit colour is a bright yellow: both R and G above
    `minimum`, at least `gap` above B, and R and G within a tolerance of each
    other.  Works on the raw bytes; nothing else of the image is kept."""
    data = path.read_bytes()
    if len(data) != width * height * 4:
        raise MalformedInput(f'{path.name}: {len(data)} bytes != {width}x{height}x4')
    out = bytearray(width * height)
    for index in range(0, len(data), 4):
        blue, green, red = data[index], data[index + 1], data[index + 2]
        low = red if red < green else green
        if low > minimum and low - blue > gap and abs(red - green) <= max(balance_floor,
                                                                         balance * low):
            out[index >> 2] = 1
    return out


# ---- morphology and per-pixel statistics -------------------------------------------

def separable_extreme(image, width, height, largest, radius=1):
    """Separable 3x3 (radius r) min or max, edge-clamped."""
    pick = max if largest else min
    tmp = array.array('f', bytes(4 * width * height))
    for y in range(height):
        row = y * width
        for x in range(width):
            low = x - radius if x - radius > 0 else 0
            high = x + radius + 1 if x + radius + 1 < width else width
            tmp[row + x] = pick(image[row + low:row + high])
    out = array.array('f', bytes(4 * width * height))
    for y in range(height):
        row = y * width
        low = y - radius if y - radius > 0 else 0
        high = y + radius + 1 if y + radius + 1 < height else height
        for x in range(width):
            out[row + x] = pick(tmp[i * width + x] for i in range(low, high))
    return out


def thin_feature_mask(image, width, height, tophat_min, contrast_min, radius=1):
    """High-contrast features at most 2*radius pixels wide.

    An opening with a (2r+1) square erases every bright structure narrower than
    2r+1 pixels, so the white top-hat `image - opening` is large exactly on
    those; the black top-hat `closing - image` does the same for dark ones.  The
    3x3 morphological gradient `dilate - erode` is the local contrast gate the
    specification asks for.  Both are computed on the raw colour luminance.
    """
    eroded = separable_extreme(image, width, height, False, radius)
    dilated = separable_extreme(image, width, height, True, radius)
    opening = separable_extreme(eroded, width, height, True, radius)
    closing = separable_extreme(dilated, width, height, False, radius)
    out = bytearray(width * height)
    for i in range(width * height):
        value = image[i]
        tophat = value - opening[i]
        bottomhat = closing[i] - value
        peak = tophat if tophat > bottomhat else bottomhat
        if peak >= tophat_min and (dilated[i] - eroded[i]) >= contrast_min:
            out[i] = 1
    return out


def temporal_variance(images):
    """Population variance across the frames, per pixel."""
    count = len(images)
    out = array.array('f', bytes(4 * len(images[0])))
    for index, values in enumerate(zip(*images)):
        mean = sum(values) / count
        out[index] = sum((v - mean) ** 2 for v in values) / count
    return out


def classify_pixels(depths, alphas, tolerance=DEPTH_TOLERANCE):
    """One class per pixel from the burst's RT2 depth and motion alpha images."""
    pixels = len(depths[0])
    out = bytearray(pixels)
    for index in range(pixels):
        values = [d[index] for d in depths]
        negative = sum(1 for v in values if v < 0)
        if negative == len(values):
            out[index] = SENTINEL
            continue
        if negative:
            out[index] = EDGE
            continue
        if any(a[index] != 1.0 for a in alphas):
            out[index] = EDGE
            continue
        if max(values) - min(values) > tolerance or any(v != v for v in values):
            out[index] = EDGE
            continue
        out[index] = INTERIOR
    return out


def quantile(values, q):
    if not values:
        return None
    position = q * (len(values) - 1)
    low = int(math.floor(position))
    high = min(low + 1, len(values) - 1)
    return values[low] + (values[high] - values[low]) * (position - low)


def variance_statistics(indices, raw, resolved, floor, total_resolved_energy):
    """Raw/resolved temporal variance of one pixel set and their ratio."""
    if not indices:
        return {'pixels': 0}
    raw_values = sorted(raw[i] for i in indices)
    resolved_values = sorted(resolved[i] for i in indices)
    raw_sum = math.fsum(raw_values)
    resolved_sum = math.fsum(resolved_values)
    ratios = sorted(resolved[i] / raw[i] for i in indices if raw[i] > floor)
    visible = (VISIBLE_LEVELS / 255.0) ** 2
    return {
        'pixels': len(indices),
        'raw_variance': {
            'mean': raw_sum / len(indices),
            'rms_levels': math.sqrt(raw_sum / len(indices)) * 255.0,
            'p50': quantile(raw_values, 0.5), 'p90': quantile(raw_values, 0.9),
            'p99': quantile(raw_values, 0.99),
        },
        'resolved_variance': {
            'mean': resolved_sum / len(indices),
            'rms_levels': math.sqrt(resolved_sum / len(indices)) * 255.0,
            'p50': quantile(resolved_values, 0.5), 'p90': quantile(resolved_values, 0.9),
            'p99': quantile(resolved_values, 0.99),
        },
        'aggregate_ratio': resolved_sum / raw_sum if raw_sum > 0 else None,
        'ratio_distribution': {
            'gated_pixels': len(ratios),
            'gate': f'raw temporal variance > {floor:g}',
            'p10': quantile(ratios, 0.1), 'p25': quantile(ratios, 0.25),
            'p50': quantile(ratios, 0.5), 'p75': quantile(ratios, 0.75),
            'p90': quantile(ratios, 0.9),
            'mean': statistics.fmean(ratios) if ratios else None,
        },
        'flicker_energy_share': (resolved_sum / total_resolved_energy
                                 if total_resolved_energy > 0 else None),
        'visible_flicker_pixels': {
            'raw': sum(1 for v in raw_values if v > visible),
            'resolved': sum(1 for v in resolved_values if v > visible),
            'threshold_levels': VISIBLE_LEVELS,
        },
    }


# ---- flicker analysis --------------------------------------------------------------

def analyze_burst_flicker(burst, captures, device, width, height, options):
    """Per-pixel flicker classification for one burst of consecutive frames."""
    frames = burst['frames']
    colour, resolved, depths, alphas, yellows = {}, {}, {}, {}, {}
    missing = []
    for frame in frames:
        paths = {
            'colour': captures / f'color_{device}_{frame}.bgra8',
            'resolved': captures / f'taa_{device}_{frame}.rgba16f',
            'motion': captures / f'motion_{device}_{frame}.rgba32f',
            'depth': captures / f'depth_{device}_{frame}.r32f',
        }
        absent = [k for k, p in paths.items() if not p.is_file()]
        if absent:
            missing.append({'frame': frame, 'absent': absent})
            continue
        colour[frame] = load_luma_bgra8(paths['colour'], width, height)
        resolved[frame] = load_luma_rgba16f(paths['resolved'], width, height)
        depths[frame] = load_depth_r32f(paths['depth'], width, height)
        alphas[frame] = load_motion_alpha(paths['motion'], width, height)
        yellows[frame] = yellow_mask_bgra8(paths['colour'], width, height,
                                           options['yellow_minimum'], options['yellow_gap'])
    usable = [f for f in frames if f in colour]
    if len(usable) < 2:
        return {'status': 'unavailable', 'reason': 'fewer than two complete frames',
                'missing': missing}

    pixels = width * height
    classes = classify_pixels([depths[f] for f in usable], [alphas[f] for f in usable],
                              options['depth_tolerance'])
    raw_variance = temporal_variance([colour[f] for f in usable])
    resolved_variance = temporal_variance([resolved[f] for f in usable])
    # The thin-feature overlay is the union over the burst: a feature whose
    # coverage flips is thin in some frames only, and those are the pixels of
    # interest.
    thin = bytearray(pixels)
    for frame in usable:
        mask = thin_feature_mask(colour[frame], width, height, options['tophat_min'],
                                 options['contrast_min'])
        for index, value in enumerate(mask):
            if value:
                thin[index] = 1

    total_energy = math.fsum(resolved_variance)
    buckets = {name: [] for name in CLASSES}
    for index, code in enumerate(classes):
        buckets[CLASSES[code]].append(index)
    report = OrderedDict()
    report['status'] = 'evaluated'
    report['frames'] = usable
    report['missing'] = missing
    report['pixels'] = pixels
    report['depth_tolerance'] = options['depth_tolerance']
    report['variance_floor'] = options['variance_floor']
    report['total_resolved_variance_energy'] = total_energy
    report['total_raw_variance_energy'] = math.fsum(raw_variance)
    report['classes'] = OrderedDict()
    for name in CLASSES:
        entry = variance_statistics(buckets[name], raw_variance, resolved_variance,
                                    options['variance_floor'], total_energy)
        entry['share_of_screen'] = entry['pixels'] / pixels
        report['classes'][name] = entry
    thin_indices = [i for i in range(pixels) if thin[i]]
    thin_entry = variance_statistics(thin_indices, raw_variance, resolved_variance,
                                     options['variance_floor'], total_energy)
    thin_entry['share_of_screen'] = thin_entry['pixels'] / pixels if pixels else None
    thin_entry['overlay'] = True
    thin_entry['by_class'] = {}
    for code, name in enumerate(CLASSES):
        subset = [i for i in thin_indices if classes[i] == code]
        stats = variance_statistics(subset, raw_variance, resolved_variance,
                                    options['variance_floor'], total_energy)
        thin_entry['by_class'][name] = stats
    thin_entry['thresholds'] = {'tophat_min': options['tophat_min'],
                                'contrast_min': options['contrast_min'],
                                'structuring_element': '3x3 square (width <= 2 px)'}
    report['classes']['thin_feature'] = thin_entry
    report['yellow_lines'] = yellow_report(yellows, usable, classes, depths, raw_variance,
                                           resolved_variance, width, height, options,
                                           total_energy)
    report['method'] = {
        'classes': ('sentinel: RT2 current depth < 0 in every frame (the resolve returns '
                    'the current colour); routed_interior: depth valid and motion alpha 1 '
                    'in every frame and the depth spread across the burst <= the tolerance; '
                    'routed_edge: everything else (validity, alpha or depth changes across '
                    'the burst)'),
        'thin_feature': ('overlay, not a partition: morphological white/black top-hat with a '
                         '3x3 square (features of width <= 2 px) gated on the 3x3 '
                         'morphological gradient, on the raw colour luminance, union over the '
                         'burst'),
        'variance': ('population temporal variance of Rec.709 luminance across the burst, per '
                     'pixel, for the pre-resolve 8-bit colour and for the resolved FP16 image; '
                     'a ratio near 1 means the resolve changes nothing there'),
    }
    return report


def yellow_report(yellows, usable, classes, depths, raw_variance, resolved_variance,
                  width, height, options, total_energy):
    """Bright thin yellow features (the station guide lines the user reports)."""
    per_frame = {str(f): sum(yellows[f]) for f in usable}
    union = [i for i in range(width * height) if any(yellows[f][i] for f in usable)]
    intersection = [i for i in union if all(yellows[f][i] for f in usable)]
    if not union:
        return {'status': 'absent', 'per_frame_pixels': per_frame,
                'criterion': options['yellow_criterion']}
    rows = [i // width for i in union]
    columns = [i % width for i in union]
    class_counts = Counter(CLASSES[classes[i]] for i in union)
    depth_images = [depths[f] for f in usable]
    depth_in_all = sum(1 for i in union if all(d[i] >= 0 for d in depth_images))
    entry = {
        'status': 'present',
        'criterion': options['yellow_criterion'],
        'per_frame_pixels': per_frame,
        'union_pixels': len(union),
        'stable_pixels': len(intersection),
        'coverage_flip_fraction': 1.0 - len(intersection) / len(union),
        'bounding_box': {'x': [min(columns), max(columns)], 'y': [min(rows), max(rows)]},
        'centroid': [statistics.fmean(columns), statistics.fmean(rows)],
        'class_counts': dict(class_counts),
        'pixels_with_rt2_depth_in_all_frames': depth_in_all,
        'union': variance_statistics(union, raw_variance, resolved_variance,
                                     options['variance_floor'], total_energy),
        'stable_subset': variance_statistics(intersection, raw_variance, resolved_variance,
                                             options['variance_floor'], total_energy),
    }
    for code, name in enumerate(CLASSES):
        subset = [i for i in union if classes[i] == code]
        if subset:
            entry.setdefault('by_class', {})[name] = variance_statistics(
                subset, raw_variance, resolved_variance, options['variance_floor'],
                total_energy)
    return entry


# ---- Run B timing hook -------------------------------------------------------------

def compare_window_timings(primary, secondary, primary_label, secondary_label):
    """Per-window scene timing of two sessions, side by side.

    Both sides are the honest per-window statistics of
    `analyze_iteration07_taa.describe_windows`: the per-window mean
    (total/count) and the per-window minimum (the fastest frame of that second,
    which no load stall can inflate).  A ratio above 1 means the primary run is
    slower.  This is a comparison of two sessions, and is only a controlled
    measurement of the resolve when the two runs are the same save, the same
    route and the same diagnostics with only `--taa` differing.
    """
    out = OrderedDict()
    for key in sorted(set(primary) | set(secondary)):
        name = key.split(':', 1)[-1]
        if name not in WINDOW_METRICS:
            continue
        left, right = primary.get(key, {}), secondary.get(key, {})
        for regime in ('scene', 'other', 'all'):
            a, b = left.get(regime), right.get(regime)
            if not a or not b or a.get('status') != 'evaluated' or b.get('status') != 'evaluated':
                continue
            row = {
                primary_label: {'windows': a['windows'],
                                'window_mean_us_median': a['window_mean_us']['median'],
                                'window_min_us_median': a['window_min_us']['median']},
                secondary_label: {'windows': b['windows'],
                                  'window_mean_us_median': b['window_mean_us']['median'],
                                  'window_min_us_median': b['window_min_us']['median']},
            }
            base_mean = b['window_mean_us']['median']
            base_min = b['window_min_us']['median']
            row['mean_ratio'] = (a['window_mean_us']['median'] / base_mean) if base_mean else None
            row['min_ratio'] = (a['window_min_us']['median'] / base_min) if base_min else None
            out[f'{key}/{regime}'] = row
    return out


def stream_scene_windows(path, device='1'):
    """Raw per-window telemetry entries with their scene/other regime.

    `analyze_iteration07_taa.split_windows` keeps only the summarized
    distribution, which cannot show that a session contains two *regimes*.  A
    per-window mean is one second of wall clock and the sessions here are
    bimodal, so the raw windows are needed before any two runs are compared.
    """
    windows = {}
    scene = {}
    summary_frame = None
    for event, line in it07.stream_records(path):
        if event == 'telemetry_summary':
            f = it07.fields(line)
            if f.get('device') == device:
                summary_frame = number(f.get('frame'))
        elif event == 'motion_output_frame':
            f = it07.fields(line)
            scene[number(f.get('frame'))] = number(f.get('routed'), 0)
        elif event == 'telemetry_metric':
            f = it07.fields(line)
            name = f.get('name')
            if name in WINDOW_METRICS:
                count = number(f.get('count'))
                total = it07.real(f.get('total_us'))
                if not count or total is None:
                    continue
                windows.setdefault(f.get('device', '?') + ':' + name, []).append(
                    {'frame': summary_frame, 'count': count, 'total_us': total,
                     'min_us': it07.real(f.get('min_us')), 'max_us': it07.real(f.get('max_us'))})
    keys = sorted(k for k in scene if k is not None)

    def regime(frame):
        if frame is None or not keys:
            return 'unknown'
        low, high = 0, len(keys)
        while low < high:
            middle = (low + high) // 2
            if keys[middle] <= frame:
                low = middle + 1
            else:
                high = middle
        if low == 0:
            return 'unknown'
        return 'scene' if scene[keys[low - 1]] else 'other'

    for entries in windows.values():
        for entry in entries:
            entry['regime'] = regime(entry['frame'])
    return windows


def mode_split(entries, threshold_us):
    """Split one metric's scene windows into a fast and a slow regime.

    A session that walks between a light and a heavy part of the scene produces
    two clusters of per-window means; comparing the medians of two such sessions
    compares how long each spent in each cluster, not what a frame costs.  The
    split makes the within-regime comparison available beside it.
    """
    scene = [e for e in entries if e['regime'] == 'scene']
    fast = [e for e in scene if e['total_us'] / e['count'] < threshold_us]
    slow = [e for e in scene if e['total_us'] / e['count'] >= threshold_us]
    return {
        'threshold_us': threshold_us,
        'scene_windows': len(scene),
        'fast': it07.describe_windows(fast),
        'slow': it07.describe_windows(slow),
        'fast_fraction': len(fast) / len(scene) if scene else None,
        'histogram_10ms_bins': dict(sorted(Counter(
            int(e['total_us'] / e['count'] // 10000) * 10 for e in scene).items())),
    }


def compare_mode_splits(primary, secondary, primary_label, secondary_label, threshold_us):
    """Within-regime comparison of two sessions, per metric."""
    out = OrderedDict()
    for key in sorted(set(primary) & set(secondary)):
        left = mode_split(primary[key], threshold_us)
        right = mode_split(secondary[key], threshold_us)
        row = {primary_label: left, secondary_label: right}
        for mode in ('fast', 'slow'):
            a, b = left[mode], right[mode]
            if a.get('status') == 'evaluated' and b.get('status') == 'evaluated':
                base_mean = b['window_mean_us']['median']
                base_min = b['window_min_us']['median']
                row[f'{mode}_mean_ratio'] = (a['window_mean_us']['median'] / base_mean
                                             if base_mean else None)
                row[f'{mode}_min_ratio'] = (a['window_min_us']['median'] / base_min
                                            if base_min else None)
        out[key] = row
    return out


MODE_KEYS = ('taa', 'taa_debug', 'jitter', 'jitter_samples', 'temporal_consumer', 'scope')


def session_profile(report):
    """What a session had enabled and what its route did, from its own records.

    Used to state what actually differed between two sessions before their
    timings are compared: a timing ratio means nothing until the configuration
    delta is on the table.
    """
    mode = (report['configuration'].get('motion_output_mode') or [{}])[-1]
    frames = report['taa']['frames']
    return {
        'mode': {key: mode.get(key) for key in MODE_KEYS},
        'motion_output_frame_records': report['taa']['records'],
        'route_totals': report['route']['totals'],
        'route_captured': report['route']['captured'],
        'taa': {'attempted': report['taa']['attempted'], 'resolved': report['taa']['resolved'],
                'history_valid': report['taa']['history_valid'],
                'skip_histogram': report['taa']['skip_histogram']},
        'frames_with_jitter': sum(1 for e in frames if e['jitter']),
        'jittered_draws': sum(e['jittered_draws'] or 0 for e in frames),
        'frames_with_routed_draws': sum(1 for e in frames if e['routed']),
        'captured_frames': [e['frame'] for e in frames if e['captured']],
    }


# ---- report ------------------------------------------------------------------------

def select_bursts(bursts, explicit):
    selected = []
    for burst in bursts:
        if explicit is None:
            if burst['stationary']:
                selected.append(burst)
        elif burst['frames'][0] in explicit:
            selected.append(burst)
    return selected


def format_ratio(value, digits=3):
    return 'n/a' if value is None else f'{value:.{digits}f}'


def render_text(summary):
    lines = []
    source = summary['source']
    lines.append(f"iteration-08 TAA analysis of {source['log']} "
                 f"({source['bytes']} bytes, sha256 {source['sha256'][:16]}…)")
    taa = summary['taa']
    lines.append(f"TAA: {taa['records']} motion_output_frame records, attempted "
                 f"{taa['attempted']}, resolved {taa['resolved']}, failed {taa['failed']}, "
                 f"history valid {taa['history_valid']}")
    lines.append(f"  skip histogram {taa['skip_histogram']}; results {taa['result_histogram']}; "
                 f"every captured frame resolved with history: "
                 f"{taa['captured_frames_all_resolved']}")
    route = summary['route']
    lines.append(f"route totals: draws {route['totals']['draws']} routed {route['totals']['routed']} "
                 f"matched {route['totals']['matched']} gates {route['totals']['gates']}")
    lines.append(f"  per-draw gate histogram {route['per_draw_gate_histogram']}; "
                 f"counter consistency {route['counter_consistency_pass']}")
    for burst in summary['bursts']:
        lines.append(f"burst {burst['index']} frames {burst['frames'][0]}-{burst['frames'][-1]}: "
                     f"cut median {burst['cut_median_px']} px, verdicts {burst['cut_verdicts']}, "
                     f"stationary={burst['stationary']}")
    diag = summary['diagnostics']
    lines.append(f"diagnostics: {len(diag['failure_records'])} failure records, "
                 f"{len(diag['reset_records'])} resets, {len(diag['shutdown_records'])} shutdown "
                 f"records, {len(diag['nonzero_results'])} nonzero readback results")
    for label, holder in (('run', summary['telemetry']),
                          ('baseline', summary['telemetry'].get('baseline') or {})):
        for name, split in sorted((holder.get('windows') or {}).items()):
            for regime, stats in sorted(split.items()):
                if stats.get('status') != 'evaluated':
                    continue
                lines.append(f"  window {label}/{name}/{regime}: {stats['windows']} windows, "
                             f"per-window mean median {stats['window_mean_us']['median']:.1f}us, "
                             f"per-window min median {stats['window_min_us']['median']:.1f}us")
    comparison = summary.get('timing_comparison') or {}
    if comparison.get('status') == 'evaluated':
        for side in ('primary', 'secondary'):
            entry = comparison[side]
            profile = entry['profile']
            lines.append(f"  session {entry['label']} ({entry['log']}): mode {profile['mode']}, "
                         f"routed {profile['route_totals']['routed']}/"
                         f"{profile['route_totals']['draws']} draws, matched "
                         f"{profile['route_totals']['matched']}, jittered draws "
                         f"{profile['jittered_draws']}, resolved {profile['taa']['resolved']}")
        for key, row in comparison['windows'].items():
            lines.append(f"  timing {key}: mean ratio {format_ratio(row['mean_ratio'])}, "
                         f"min ratio {format_ratio(row['min_ratio'])}")
        for key, row in (comparison.get('scene_window_modes') or {}).items():
            labels = [k for k in row if isinstance(row[k], dict)]
            # Only a bimodal metric has something to say here; the microsecond
            # metrics put every window in the fast bucket by construction.
            if not any(row[label]['slow'].get('windows') for label in labels):
                continue
            parts = []
            for label in labels:
                piece = []
                for mode in ('fast', 'slow'):
                    stats = row[label][mode]
                    if stats.get('status') == 'evaluated':
                        piece.append(f"{mode} {stats['windows']}w "
                                     f"{stats['window_mean_us']['median']:.0f}us")
                parts.append(f"{label} " + ' / '.join(piece))
            lines.append(f"  regimes {key}: {'; '.join(parts)}; fast ratio "
                         f"{format_ratio(row.get('fast_mean_ratio'))}, slow ratio "
                         f"{format_ratio(row.get('slow_mean_ratio'))}")
    else:
        lines.append(f"  timing comparison: {comparison.get('status')} - {comparison.get('note')}")
    for burst in (summary.get('image_analysis') or {}).get('bursts', []):
        analysis = burst['analysis']
        if analysis.get('status') != 'evaluated':
            lines.append(f"image burst {burst['frames']}: {analysis.get('reason')}")
            continue
        for pair in analysis['pairs']:
            colour = pair['current_colour']['lucas_kanade']
            res = pair['resolved']['lucas_kanade']
            model = pair['model']
            lines.append(
                f"tremble {pair['frames'][0]}->{pair['frames'][1]}: jitter step "
                f"({pair['jitter_step_px'][0]:+.4f},{pair['jitter_step_px'][1]:+.4f}) colour "
                f"({colour['dx_px']:+.4f},{colour['dy_px']:+.4f}) resolved "
                f"({res['dx_px']:+.4f},{res['dy_px']:+.4f}) residual tracking "
                f"{model['residual_to_jitter_tracking_px']:.4f} stable "
                f"{model['residual_to_stable_px']:.4f} bound "
                f"{model['stable_resolve_bound_px']:.4f} -> {model['verdict']}")
        for item in analysis['blur']:
            lines.append(f"blur frame {item['frame']}: gradient energy ratio "
                         f"{item['gradient_energy_ratio']:.4f}, high-frequency fraction ratio "
                         f"{item.get('high_frequency_fraction_ratio', float('nan')):.4f}")
    for burst in (summary.get('flicker') or {}).get('bursts', []):
        analysis = burst['analysis']
        if analysis.get('status') != 'evaluated':
            lines.append(f"flicker burst {burst['frames']}: {analysis.get('reason')}")
            continue
        lines.append(f"flicker burst {analysis['frames'][0]}-{analysis['frames'][-1]}:")
        for name, entry in analysis['classes'].items():
            if not entry.get('pixels'):
                lines.append(f"  {name}: 0 pixels")
                continue
            ratio = entry['ratio_distribution']
            lines.append(
                f"  {name}: {entry['pixels']} px ({entry['share_of_screen']*100:.1f}%), raw rms "
                f"{entry['raw_variance']['rms_levels']:.2f} levels, resolved rms "
                f"{entry['resolved_variance']['rms_levels']:.2f}, aggregate ratio "
                f"{format_ratio(entry['aggregate_ratio'])}, ratio p25/p50/p75 "
                f"{format_ratio(ratio['p25'])}/{format_ratio(ratio['p50'])}/"
                f"{format_ratio(ratio['p75'])}, flicker energy share "
                f"{format_ratio(entry['flicker_energy_share'])}")
        yellow = analysis['yellow_lines']
        if yellow.get('status') == 'present':
            lines.append(
                f"  yellow guide lines: {yellow['union_pixels']} px union, "
                f"{yellow['stable_pixels']} in every frame (coverage flip "
                f"{yellow['coverage_flip_fraction']:.2f}), classes {yellow['class_counts']}, "
                f"{yellow['pixels_with_rt2_depth_in_all_frames']} with RT2 depth in all frames, "
                f"raw rms {yellow['union']['raw_variance']['rms_levels']:.2f} levels, resolved "
                f"rms {yellow['union']['resolved_variance']['rms_levels']:.2f}, aggregate ratio "
                f"{format_ratio(yellow['union']['aggregate_ratio'])}")
        else:
            lines.append(f"  yellow guide lines: {yellow.get('status')}")
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', type=Path, help='TAA rerun capture session log (streamed)')
    parser.add_argument('--output', type=Path, required=True, help='summary JSON path')
    parser.add_argument('--text', type=Path, help='optional text report path')
    parser.add_argument('--captures', type=Path, help='readback directory (default: log directory)')
    parser.add_argument('--device', default='1', help='device index in the readback file names')
    parser.add_argument('--baseline-log', type=Path,
                        help='earlier run whose telemetry metrics are aggregated beside this one')
    parser.add_argument('--baseline-label', default='baseline')
    parser.add_argument('--run-b-log', type=Path,
                        help='second session (the TAA-off Run B of the same scene) whose '
                             'per-window scene times are compared with this run')
    parser.add_argument('--run-b-label', default='run_b')
    parser.add_argument('--window-mode-split-us', type=float, default=45000.0,
                        help='per-window mean that separates the fast and slow scene regime '
                             'of a bimodal session (default 45 ms)')
    parser.add_argument('--no-images', action='store_true',
                        help='skip the iteration-7 tremble/blur measurement')
    parser.add_argument('--no-flicker', action='store_true',
                        help='skip the per-pixel flicker classification')
    parser.add_argument('--image-burst', type=int, action='append', default=None,
                        metavar='FIRST_FRAME',
                        help='burst to measure (default: every burst the log certifies '
                             'stationary); applies to both image sections')
    parser.add_argument('--tile', type=int, default=256, help='phase-correlation tile size')
    parser.add_argument('--tile-step', type=int, default=64, help='tile search stride')
    parser.add_argument('--max-shift-samples', type=int, default=40000)
    parser.add_argument('--history-weight', type=float, default=HISTORY_WEIGHT)
    parser.add_argument('--depth-tolerance', type=float, default=DEPTH_TOLERANCE,
                        help="the resolve's absolute depth rejection tolerance")
    parser.add_argument('--variance-floor', type=float, default=VARIANCE_FLOOR,
                        help='raw temporal variance below which the ratio is not reported')
    parser.add_argument('--thin-tophat', type=float, default=0.06,
                        help='minimum morphological top-hat (luma) for a thin feature')
    parser.add_argument('--thin-contrast', type=float, default=0.10,
                        help='minimum 3x3 morphological gradient (luma) for a thin feature')
    parser.add_argument('--yellow-minimum', type=int, default=60,
                        help='minimum of R and G (8-bit) for a guide-line pixel')
    parser.add_argument('--yellow-gap', type=int, default=40,
                        help='minimum min(R,G) - B (8-bit) for a guide-line pixel')
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
                         'sha256': sha256_of(args.log), 'captured_frames': captured,
                         'iteration07_module': 'tools/analysis/analyze_iteration07_taa.py'}
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
        summary['telemetry']['baseline'] = {
            'label': args.baseline_label, 'log': args.baseline_log.name,
            'bytes': args.baseline_log.stat().st_size, 'metrics': base['metrics'],
            'rejected_records': base['rejected_records'], 'clock_hz': base['clock_hz'],
            'windows': base['windows'],
        }
    summary['telemetry']['limits'] = [
        'CPU-side wall-clock spans, never GPU execution time.',
        'frame_normal is the interval between completed Presents: application work, pacing, '
        'resource loading and diagnostics are all inside it.',
        'Only per-window count/failures/min/max/total and six duration buckets exist, so no '
        'median or percentile of frame time can be derived.',
        'One session with the feature on throughout is not a controlled measurement of the '
        'resolve; use --run-b-log with a TAA-off run of the same scene.',
    ]
    if args.run_b_log:
        other = analyze_log(args.run_b_log, args.device)
        other.pop('_brackets', None)
        other.pop('_captured_frames', None)
        summary['timing_comparison'] = {
            'status': 'evaluated',
            'primary': {'label': 'run_a', 'log': args.log.name,
                        'profile': session_profile(summary)},
            'secondary': {'label': args.run_b_label, 'log': args.run_b_log.name,
                          'bytes': args.run_b_log.stat().st_size,
                          'sha256': sha256_of(args.run_b_log),
                          'profile': session_profile(other)},
            'windows': compare_window_timings(summary['telemetry']['windows'],
                                              other['telemetry']['windows'],
                                              'run_a', args.run_b_label),
            'metrics': {
                key: {'run_a': {k: entry[k] for k in ('count', 'mean_us', 'min_us', 'max_us',
                                                      'buckets')},
                      args.run_b_label: {k: other['telemetry']['metrics'][key][k]
                                         for k in ('count', 'mean_us', 'min_us', 'max_us',
                                                   'buckets')},
                      'mean_ratio': (entry['mean_us'] / other['telemetry']['metrics'][key]['mean_us']
                                     if other['telemetry']['metrics'][key]['mean_us'] else None)}
                for key, entry in summary['telemetry']['metrics'].items()
                if key in other['telemetry']['metrics']
            },
            'scene_window_modes': compare_mode_splits(
                stream_scene_windows(args.log, args.device),
                stream_scene_windows(args.run_b_log, args.device),
                'run_a', args.run_b_label, args.window_mode_split_us),
            'note': ('per-window scene mean and per-window scene minimum of both sessions; a '
                     'ratio above 1 means this run is slower. The profiles state what actually '
                     'differed; anything both runs share (the route itself) is not isolated by '
                     'this comparison. scene_window_modes splits each session\'s scene windows '
                     'into a fast and a slow regime first, because a session that visits a light '
                     'and a heavy part of the scene is bimodal and its median only reports how '
                     'long it stayed in each.'),
        }
    else:
        summary['timing_comparison'] = {
            'status': 'pending',
            'note': ('no --run-b-log: this report has one session with TAA on throughout, so the '
                     'timing is not a controlled comparison. Re-run with the TAA-off capture of '
                     'the same scene to populate this section.'),
        }

    captures = args.captures or args.log.parent
    presentation = (summary['configuration'].get('telemetry_presentation') or [{}])[-1]
    width = number(presentation.get('width'), 0)
    height = number(presentation.get('height'), 0)
    readback = (summary['readbacks'].get('taa') or {}).get('dimensions') or []
    if readback and readback[0][0]:
        width, height = int(readback[0][0]), int(readback[0][1])
    selected = select_bursts(summary['bursts'], args.image_burst)
    selection_note = ('bursts whose motion_output_cut median_px <= 0.01 in every frame'
                      if args.image_burst is None else 'explicit --image-burst')

    if args.no_images:
        summary['image_analysis'] = {'status': 'skipped'}
    else:
        options = {'tile': args.tile, 'tile_step': args.tile_step,
                   'max_samples': args.max_shift_samples, 'history_weight': args.history_weight}
        results = [{'frames': b['frames'], 'cut_median_px': b['cut_median_px'],
                    'analysis': analyze_burst_images(b, captures, args.device, width, height,
                                                     options)}
                   for b in selected]
        summary['image_analysis'] = {
            'status': 'evaluated' if results else 'unavailable',
            'reason': None if results else 'no burst selected (none certified stationary)',
            'captures': str(captures), 'selection': selection_note,
            'method_source': 'analyze_iteration07_taa.analyze_burst_images (unchanged)',
            'bursts': results,
        }

    if args.no_flicker:
        summary['flicker'] = {'status': 'skipped'}
    else:
        options = {'depth_tolerance': args.depth_tolerance, 'variance_floor': args.variance_floor,
                   'tophat_min': args.thin_tophat, 'contrast_min': args.thin_contrast,
                   'yellow_minimum': args.yellow_minimum, 'yellow_gap': args.yellow_gap,
                   'yellow_criterion': (f'min(R,G) > {args.yellow_minimum} and min(R,G) - B > '
                                        f'{args.yellow_gap} and |R-G| <= max(40, 0.45 min(R,G)), '
                                        f'8-bit pre-resolve colour')}
        results = [{'frames': b['frames'], 'cut_median_px': b['cut_median_px'],
                    'analysis': analyze_burst_flicker(b, captures, args.device, width, height,
                                                      options)}
                   for b in selected]
        summary['flicker'] = {
            'status': 'evaluated' if results else 'unavailable',
            'reason': None if results else 'no burst selected (none certified stationary)',
            'captures': str(captures), 'selection': selection_note, 'bursts': results,
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
