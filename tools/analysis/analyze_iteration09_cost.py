#!/usr/bin/env python3
"""Controlled route-on against route-off cost comparison (iteration 9).

The question iteration 8 left open ("the route's own cost is still unmeasured",
docs/verification/iteration-08.md section 6) needs a run of the same save with
the proxy loaded and the route *off*.  The user supplied three same-build
(1d36c29) same-save sessions on 2026-09-12:

* run 1 - route on, TAA on, sampling profiler on, object trace on, telemetry;
* run 2 - as run 1 plus ``--taa-debug`` readbacks, the engine scene hook and
  the mesh-cache request (which did not activate);
* run 3 - proxy loaded, **route off**, telemetry only, no profiler, no object
  trace.

Nothing here is a GPU measurement.  Every number is a CPU-side QPC span or a
``frame_normal`` wall-clock interval between two completed ``Present`` calls, so
it contains application work, pacing and driver blocking
(docs/verification/telemetry.md).

What this tool adds over the existing iteration-8/9 analyzers
--------------------------------------------------------------

``analyze_iteration08_taa.stream_scene_windows`` labels a window "scene" from
the nearest ``motion_output_frame`` with ``routed > 0``.  That marker does not
exist in a route-off run, so a route-off baseline cannot be split that way at
all.  This tool therefore classifies windows structurally, from evidence every
run has:

1. **Loading segmentation.**  A window whose ``frame_normal`` maximum exceeds
   ``--gap-us`` (default 2 s) contains a load; it is dropped and it closes a
   *phase*.  All three sessions segment identically into: main-menu load, main
   menu, save load (the largest gap, 85.6-105.5 s), sector flight, a mid-session
   load, more sector, a return load, main menu again.
2. **Menu against scene.**  The main menu draws an almost fixed number of draws
   every frame; a sector phase sweeps draws/frame over an order of magnitude.  A
   phase is ``menu`` when its median draws/frame is inside ``--menu-draws``
   and its p10-p90 spread is under ``--menu-spread`` of that median, else
   ``scene``.  Where route records exist the classification is *checked*
   against them: a menu phase must have ``routed == 0`` in every
   ``motion_output_frame`` it contains, a scene phase must have a routed one
   (runs 1 and 2 only; ``phase_check`` reports the outcome).
3. **Capture windows are dropped.**  A window holding a capture frame
   (``route_readback``, ``frame_capture``, ``present_capture``, ``capture_cpu``
   or ``snapshot``) carries the readbacks and the per-draw diagnostic logging
   and cannot enter a frame-time median.  Their cost is reported separately.

The menu phase is the load-bearing control.  The route is *enabled* there and
its gate runs on every draw, but nothing is routed (``routed=0``, every draw
rejected at gate 2), no jitter is written, the resolve is skipped - while the
sampling profiler, the object trace, the camera-state reader and every
telemetry stamp are on exactly as in the scene.  The menu therefore measures
the non-route diagnostic overhead of runs 1 and 2 against run 3 at a draw count
the sector also visits, so the scene difference is not attributed to the route
wholesale.

Draw-count normalisation
------------------------

Frame time in these sessions is close to affine in draws per frame, and the
three flight paths are similar but not identical, so a pooled median mostly
reports where each run flew.  Windows are binned on draws/frame
(``--bin-width``); a bin populated in both runs contributes one paired
observation ``(draws, delta_ms)``.  The pairs are fitted with a Theil-Sen
estimator (the median of all pairwise slopes, and the median intercept at that
slope), which needs no error model and is unaffected by a few heavy bins.  The
per-draw slope of the *menu* pairs is subtracted from the per-draw slope of the
*scene* pairs to separate the route from the rest of the instrumentation.

The uncertainty quoted for a slope is the interquartile range of the pairwise
slopes, and beside it the run-1-against-run-2 slope: those two sessions differ
only in the debug readbacks, the scene hook and the mesh-cache request, so their
residual difference at matched draw counts is the scale of the path/session
noise this method cannot remove.

Route metrics
-------------

Per-frame route cost is read two ways.  ``route_costs`` aggregates the
per-window ``route_*``/``taa_*``/``hdr_*`` metric totals divided by the
window's ``frame_normal`` count, and ``frame_records`` reads the exact
per-frame microsecond totals appended to ``motion_output_frame``.  Only the
mutually exclusive group is summed (``route_gate + route_draw + route_fill +
route_jitter + route_lazy_flush``); ``route_set_rt`` is inside ``route_draw``,
the ``taa_*`` phases are inside ``taa_run``, and ``route_readback`` is
capture-only - all per docs/verification/telemetry.md, "Route and boundary
cost".

The log is streamed once and only ``telemetry_*``, ``motion_output_frame``,
``hdr_frame``, ``profile_*`` and a handful of configuration records are parsed;
nothing is held per draw.
"""
import argparse
import json
import math
import statistics
import sys
from collections import Counter, OrderedDict
from pathlib import Path

# Per-second window metrics used for frame time, draw rate and boundary cost.
BASE_METRICS = ('frame_normal', 'present_normal', 'draw_backend', 'stretch_backend',
                'lock_wait', 'log_flush')
# Capture-frame metrics: their presence marks a window as a capture window.
CAPTURE_METRICS = ('route_readback', 'frame_capture', 'present_capture', 'capture_cpu',
                   'snapshot')
# The route's mutually exclusive per-call metrics: these, and only these, add up.
ROUTE_EXCLUSIVE = ('route_gate', 'route_draw', 'route_fill', 'route_jitter',
                   'route_lazy_flush')
# Nested inside route_draw / route_lazy_flush; reported, never added to the above.
ROUTE_NESTED = ('route_set_rt',)
TAA_METRICS = ('taa_run', 'taa_state_capture', 'taa_copy_color', 'taa_copy_depth',
               'taa_resolve_draw', 'taa_state_apply', 'taa_copy_back')
HDR_METRICS = ('hdr_redirect', 'hdr_writeback', 'hdr_writeback_draw',
               'hdr_writeback_stretch', 'hdr_bind', 'hdr_recheck')
METRICS = BASE_METRICS + CAPTURE_METRICS + ROUTE_EXCLUSIVE + ROUTE_NESTED + \
    TAA_METRICS + HDR_METRICS

# Exclusive per-frame microsecond fields of motion_output_frame, and the nested ones.
FRAME_EXCLUSIVE = ('gate_us', 'route_draw_us', 'fill_us', 'jitter_us', 'lazy_flush_us')
FRAME_NESTED = ('set_rt_us',)
FRAME_TAA = ('taa_run_us', 'taa_capture_us', 'taa_copy_color_us', 'taa_copy_depth_us',
             'taa_draw_us', 'taa_apply_us', 'taa_copy_back_us')
FRAME_COUNTS = ('draws', 'routed', 'matched', 'jittered', 'set_rt', 'lazy_flushes',
                'jitter_writes', 'readbacks')

# Cost of one thread suspension by the sampling profiler, measured in
# docs/reverse-engineering/loading-profile-run1.md section 6 (~123 us per
# suspended thread; 2.37% of wall at that run's 192.5 ticks/s).  The share a
# session actually paid follows from its own achieved tick rate.
SUSPEND_US = 123.0
# Upper bound for one QPC pair plus the bucketing of `record`, taken from the
# menu phase's own gate sample: there the gate rejects every draw at gate 2 and
# the whole span is the stamp plus that rejection (see `stamp_cost`).
DEFAULT_STAMP_US = 0.25
# SetRenderTarget calls per routed draw in the per-draw and the lazy RT mode,
# from the motion-output fixture's burst cases (docs/verification/motion-output.md).
PERDRAW_SET_RT, LAZY_SET_RT = 20, 12


class Malformed(Exception):
    """A record that cannot be trusted; the caller counts it rather than guessing."""


def fields(line):
    out = {}
    for token in line.split()[1:]:
        key, _, value = token.partition('=')
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


def quantile(values, q):
    """Linear-interpolated quantile of a sorted-on-demand sample."""
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = q * (len(ordered) - 1)
    low = int(math.floor(position))
    high = min(low + 1, len(ordered) - 1)
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


def describe(values, digits=3):
    if not values:
        return {'n': 0}
    return {'n': len(values),
            'p10': round(quantile(values, 0.10), digits),
            'median': round(quantile(values, 0.50), digits),
            'p90': round(quantile(values, 0.90), digits),
            'min': round(min(values), digits), 'max': round(max(values), digits)}


# ---- log streaming -----------------------------------------------------------------

CONFIG_RECORDS = ('motion_output_mode', 'motion_capture_mode', 'mesh_cache',
                  'application_admission_mode')
STATUS_RECORDS = ('object_trace', 'camera_state', 'scene_hook', 'object_lifetime')


def scan(path, device='1'):
    """One streaming pass: windows, route frame records, profiler reports, config.

    A ``telemetry_metric`` line belongs to the ``telemetry_summary`` that
    precedes it (the proxy writes a summary and then its metrics), so windows
    are built by following that order and keeping only this device's.
    """
    windows = []
    current = None
    frame_records = []
    profiler = []
    config = {}
    status = {}
    clock = None
    rejected = Counter()
    with open(path, 'r', errors='replace') as stream:
        for line in stream:
            event = line.partition(' ')[0]
            if event == 'telemetry_summary':
                f = fields(line)
                if f.get('device') != device:
                    current = None
                    continue
                frame = number(f.get('frame'))
                since = real(f.get('since_start_us'))
                if frame is None or since is None:
                    rejected['telemetry_summary'] += 1
                    current = None
                    continue
                current = {'frame': frame, 'since_us': since,
                           'interval_us': real(f.get('interval_us'), 0.0), 'metrics': {}}
                windows.append(current)
            elif event == 'telemetry_metric':
                if current is None:
                    continue
                f = fields(line)
                if f.get('device') != device:
                    continue
                name = f.get('name')
                if name not in METRICS:
                    continue
                count = number(f.get('count'))
                total = real(f.get('total_us'))
                if not count or total is None or total < 0:
                    rejected['telemetry_metric'] += 1
                    continue
                current['metrics'][name] = {
                    'count': count, 'total_us': total,
                    'min_us': real(f.get('min_us'), 0.0),
                    'max_us': real(f.get('max_us'), 0.0),
                    'failures': number(f.get('failures'), 0)}
            elif event == 'motion_output_frame':
                f = fields(line)
                if f.get('device') != device:
                    continue
                record = {'frame': number(f.get('frame')),
                          'rt_mode': f.get('rt_mode'), 'timing': f.get('timing'),
                          'scene_end_source': f.get('scene_end_source'),
                          'scene_end_check': number(f.get('scene_end_check'), 0),
                          'taa_resolved': number(f.get('taa_resolved'), 0),
                          'taa_skip': number(f.get('taa_skip'), 0)}
                for key in FRAME_COUNTS:
                    record[key] = number(f.get(key), 0)
                for key in FRAME_EXCLUSIVE + FRAME_NESTED + FRAME_TAA + ('readback_us',):
                    record[key] = real(f.get(key), 0.0)
                if record['frame'] is None:
                    rejected['motion_output_frame'] += 1
                    continue
                frame_records.append(record)
            elif event == 'hdr_frame':
                rejected['hdr_frame'] += 1
            elif event == 'profile_report':
                f = fields(line)
                if f.get('scope') != 'delta':
                    continue
                elapsed = real(f.get('elapsed_us'))
                ticks = number(f.get('ticks'))
                since = real(f.get('since_start_us'))
                if not elapsed or ticks is None or since is None:
                    rejected['profile_report'] += 1
                    continue
                profiler.append({'since_us': since, 'elapsed_us': elapsed, 'ticks': ticks,
                                 'threads': number(f.get('threads'), 0),
                                 'samples': number(f.get('samples'), 0)})
            elif event == 'profile_start':
                config['profile'] = fields(line)
            elif event == 'telemetry_start':
                clock = number(fields(line).get('qpc_frequency'))
            elif event in CONFIG_RECORDS:
                # Several of these are re-emitted with a different field set later in
                # the session (`mesh_cache` 205 times in run 2); the announcement is
                # the first one, so a later line must not overwrite it.
                config.setdefault(event, fields(line))
            elif event in STATUS_RECORDS and ' status=' in line and 'device=' not in line:
                status[event] = fields(line)
    return {'path': str(path), 'device': device, 'windows': windows,
            'frame_records': frame_records, 'profiler': profiler,
            'config': config, 'status': status, 'clock_hz': clock,
            'rejected': dict(rejected)}


# ---- per-window derivation ---------------------------------------------------------

def window_row(window):
    """Derived per-window statistics, or None when the window has no frames.

    A window reports count/total/min/max only, so the per-window mean
    (total/count) and the per-window minimum (the fastest frame of that second,
    which no stall can inflate) are the honest statistics; there is no
    session-wide frame-time median in this data.
    """
    metrics = window['metrics']
    frame = metrics.get('frame_normal')
    if not frame:
        return None
    count = frame['count']
    row = {'frame': window['frame'], 'since_s': round(window['since_us'] / 1e6, 3),
           'frames': count,
           'mean_ms': frame['total_us'] / count / 1000.0,
           'min_ms': frame['min_us'] / 1000.0,
           'max_ms': frame['max_us'] / 1000.0,
           'capture': any(name in metrics for name in CAPTURE_METRICS)}
    draws = metrics.get('draw_backend')
    row['draws_per_frame'] = draws['count'] / count if draws else None
    row['draw_backend_us'] = draws['total_us'] / draws['count'] if draws else None
    for name in ('present_normal', 'stretch_backend', 'lock_wait', 'log_flush'):
        entry = metrics.get(name)
        row[name + '_us'] = entry['total_us'] / entry['count'] if entry else None
        row[name + '_per_frame'] = entry['count'] / count if entry else 0.0
    # Per-frame microseconds of every route/TAA/HDR metric present.
    per_frame = {}
    for name in ROUTE_EXCLUSIVE + ROUTE_NESTED + TAA_METRICS + HDR_METRICS + \
            ('route_readback',):
        entry = metrics.get(name)
        if entry:
            per_frame[name] = {'us_per_frame': entry['total_us'] / count,
                               'calls_per_frame': entry['count'] / count,
                               'us_per_call': entry['total_us'] / entry['count']}
    row['route'] = per_frame
    row['route_exclusive_us'] = sum(
        metrics[name]['total_us'] for name in ROUTE_EXCLUSIVE if name in metrics) / count
    row['taa_run_us'] = (metrics['taa_run']['total_us'] / count) if 'taa_run' in metrics else 0.0
    row['route_active'] = row['route_exclusive_us'] > 0.0
    capture_interval = metrics.get('frame_capture')
    row['frame_capture_ms'] = (capture_interval['total_us'] / capture_interval['count'] / 1000.0
                               if capture_interval else None)
    row['frame_capture_frames'] = capture_interval['count'] if capture_interval else 0
    return row


def segment(rows, gap_us=2e6):
    """Split the window sequence into phases at windows containing a load.

    Returns ``(phases, loading)``: phases is a list of lists of rows, loading
    the dropped windows with the gap they held.
    """
    phases = [[]]
    loading = []
    for row in rows:
        if row['max_ms'] * 1000.0 > gap_us:
            loading.append({'frame': row['frame'], 'since_s': row['since_s'],
                            'gap_ms': round(row['max_ms'], 1), 'frames': row['frames']})
            if phases[-1]:
                phases.append([])
            continue
        phases[-1].append(row)
    return [p for p in phases if p], loading


def label_phase(rows, menu_draws=(500.0, 800.0), menu_spread=0.20):
    """``menu`` when draws/frame is high and nearly constant, else ``scene``.

    The main menu submits an almost fixed draw list every frame; a sector phase
    sweeps draws/frame over an order of magnitude.  Both bounds must hold, so a
    short sector window run at a steady draw count cannot be mislabelled unless
    it is also inside the menu's draw band.
    """
    draws = [r['draws_per_frame'] for r in rows if r['draws_per_frame']]
    if not draws:
        return 'unknown', None
    median = quantile(draws, 0.5)
    spread = (quantile(draws, 0.9) - quantile(draws, 0.1)) / median if median else None
    inside = menu_draws[0] <= median <= menu_draws[1]
    flat = spread is not None and spread < menu_spread
    return ('menu' if inside and flat else 'scene'), {
        'draws_median': round(median, 1),
        'draws_spread_fraction': round(spread, 4) if spread is not None else None,
        'inside_menu_band': inside, 'flat': flat}


def check_phases(phases, labels, frame_records):
    """Cross-check the structural labels against the route's own records.

    A menu phase must contain no routed ``motion_output_frame``; a scene phase
    of a route-on run must contain one.  A route-off run has no records and the
    check reports ``unavailable`` rather than passing silently.
    """
    if not frame_records:
        return {'status': 'unavailable', 'reason': 'no motion_output_frame records'}
    out = []
    ok = True
    for rows, label in zip(phases, labels):
        low, high = rows[0]['frame'], rows[-1]['frame']
        inside = [r for r in frame_records if low <= r['frame'] <= high]
        routed = sum(1 for r in inside if r['routed'] > 0)
        agree = (routed == 0) if label == 'menu' else (routed > 0 or not inside)
        ok = ok and agree
        out.append({'frames': [low, high], 'label': label, 'records': len(inside),
                    'routed_records': routed, 'agrees': agree})
    return {'status': 'checked' if ok else 'disagreement', 'phases': out}


def usable(rows, min_frames):
    """Non-capture windows with enough frames for a per-window mean to mean anything."""
    return [r for r in rows if not r['capture'] and r['frames'] >= min_frames]


# ---- regimes and draw bins ---------------------------------------------------------

def regime_split(rows, split_us=45000.0):
    """The iteration-8 bimodal split, on the per-window mean frame time.

    A session that walks between a light and a heavy part of the scene produces
    two clusters of per-window means, and a pooled median mostly reports how
    long the run stayed in each.  The split makes the within-regime comparison
    available beside the occupancy.
    """
    threshold = split_us / 1000.0
    fast = [r for r in rows if r['mean_ms'] < threshold]
    slow = [r for r in rows if r['mean_ms'] >= threshold]
    out = {'threshold_ms': threshold, 'windows': len(rows),
           'fast_occupancy': round(len(fast) / len(rows), 4) if rows else None,
           'histogram_10ms_bins': dict(sorted(Counter(
               int(r['mean_ms'] // 10) * 10 for r in rows).items()))}
    for label, subset in (('fast', fast), ('slow', slow)):
        out[label] = regime_statistics(subset)
    return out


def regime_statistics(rows):
    if not rows:
        return {'windows': 0}
    out = {'windows': len(rows), 'frames': sum(r['frames'] for r in rows),
           'mean_ms': describe([r['mean_ms'] for r in rows]),
           'min_ms': describe([r['min_ms'] for r in rows]),
           'draws_per_frame': describe([r['draws_per_frame'] for r in rows
                                        if r['draws_per_frame']], 1),
           'draw_backend_us': describe([r['draw_backend_us'] for r in rows
                                        if r['draw_backend_us']]),
           'present_normal_us': describe([r['present_normal_us'] for r in rows
                                          if r['present_normal_us']]),
           'stretch_backend_us': describe([r['stretch_backend_us'] for r in rows
                                           if r['stretch_backend_us']]),
           'log_flush_us': describe([r['log_flush_us'] for r in rows if r['log_flush_us']]),
           'log_flush_per_frame': describe([r['log_flush_per_frame'] for r in rows], 3),
           'route_exclusive_us_per_frame': describe(
               [r['route_exclusive_us'] for r in rows], 1),
           'taa_run_us_per_frame': describe([r['taa_run_us'] for r in rows], 1)}
    names = sorted({name for r in rows for name in r['route']})
    out['route_metrics_us_per_frame'] = {
        name: describe([r['route'][name]['us_per_frame'] for r in rows if name in r['route']], 1)
        for name in names}
    out['route_metrics_us_per_call'] = {
        name: describe([r['route'][name]['us_per_call'] for r in rows if name in r['route']], 3)
        for name in names}
    return out


def draw_bins(rows, width=50.0):
    """Median per-window mean frame time and route cost per draws/frame bin."""
    buckets = {}
    for row in rows:
        if not row['draws_per_frame']:
            continue
        key = int(row['draws_per_frame'] // width)
        buckets.setdefault(key, []).append(row)
    out = OrderedDict()
    for key in sorted(buckets):
        subset = buckets[key]
        out[key] = {
            'draws_lo': key * width, 'draws_hi': (key + 1) * width,
            'windows': len(subset), 'frames': sum(r['frames'] for r in subset),
            'draws_median': round(quantile([r['draws_per_frame'] for r in subset], 0.5), 1),
            'mean_ms_median': round(quantile([r['mean_ms'] for r in subset], 0.5), 3),
            'min_ms_median': round(quantile([r['min_ms'] for r in subset], 0.5), 3),
            'route_us_median': round(quantile([r['route_exclusive_us'] for r in subset], 0.5), 1),
            'taa_us_median': round(quantile([r['taa_run_us'] for r in subset], 0.5), 1)}
    return out


def theil_sen(points):
    """Median of pairwise slopes, and the median intercept at that slope.

    Distribution-free and insensitive to a minority of outlying bins, which is
    what a set of draw-count bins from two different flight paths is.  The
    reported uncertainty is the interquartile range of the pairwise slopes: the
    spread the data itself shows, not a fitted standard error.
    """
    slopes = []
    for i in range(len(points)):
        for j in range(i + 1, len(points)):
            dx = points[j][0] - points[i][0]
            if abs(dx) < 1e-9:
                continue
            slopes.append((points[j][1] - points[i][1]) / dx)
    if not slopes:
        return None
    slope = quantile(slopes, 0.5)
    intercept = statistics.median(y - slope * x for x, y in points)
    return {'n_points': len(points), 'n_slopes': len(slopes),
            'slope': slope, 'slope_p25': quantile(slopes, 0.25),
            'slope_p75': quantile(slopes, 0.75), 'intercept': intercept}


def sign_test(deltas):
    """Two-sided exact binomial sign test on the per-bin differences.

    The bins are not independent samples of one distribution and the deltas are
    medians, so this is a consistency check on the *direction* of the
    difference, never a confidence interval on its size.
    """
    positive = sum(1 for d in deltas if d > 0)
    negative = sum(1 for d in deltas if d < 0)
    total = positive + negative
    if total == 0:
        return {'status': 'unavailable', 'bins': 0}
    extreme = min(positive, negative)
    tail = sum(math.comb(total, k) for k in range(extreme + 1)) / (2.0 ** total)
    return {'status': 'evaluated', 'bins': total, 'positive': positive,
            'negative': negative, 'p_two_sided': round(min(1.0, 2.0 * tail), 5)}


def paired_bins(primary, baseline, width=50.0, statistic='mean_ms_median',
                min_windows=2):
    """One (draws, delta_ms) observation per draws/frame bin both runs populate.

    A bin needs ``min_windows`` windows on both sides: a bin holding a single
    one-second window is one second of one flight path and moves the fit
    without carrying any information about a draw count.
    """
    left, right = draw_bins(primary, width), draw_bins(baseline, width)
    pairs = []
    for key in sorted(set(left) & set(right)):
        a, b = left[key], right[key]
        if a['windows'] < min_windows or b['windows'] < min_windows:
            continue
        draws = 0.5 * (a['draws_median'] + b['draws_median'])
        pairs.append({'bin': key, 'draws_lo': a['draws_lo'], 'draws': round(draws, 1),
                      'primary_ms': a[statistic], 'baseline_ms': b[statistic],
                      'delta_ms': round(a[statistic] - b[statistic], 3),
                      'primary_windows': a['windows'], 'baseline_windows': b['windows'],
                      'route_us': a['route_us_median']})
    return pairs


def fit_pairs(pairs):
    """Free-intercept Theil-Sen plus the origin-constrained per-draw estimate.

    The free-intercept slope is the informative one when the bins span a wide
    draw range (a sector phase covers 50-900 draws/frame).  It is
    ill-conditioned when they do not: the main menu submits 630-690 draws every
    frame, so two or three bins inside a 60-draw window can produce any slope at
    all.  ``origin_us_per_draw`` is then the estimate to use - the median of the
    per-bin ``delta / draws`` - which assumes the difference is proportional to
    the draw count and has no leverage problem.
    """
    per_draw = [p['delta_ms'] * 1000.0 / p['draws'] for p in pairs if p['draws']]
    out = {'bins': len(pairs), 'delta_ms': describe([p['delta_ms'] for p in pairs]),
           'draws_span': [min(p['draws'] for p in pairs), max(p['draws'] for p in pairs)]
           if pairs else None,
           'origin_us_per_draw': describe(per_draw, 3),
           'sign_test': sign_test([p['delta_ms'] for p in pairs]), 'pairs': pairs}
    fit = theil_sen([(p['draws'], p['delta_ms']) for p in pairs])
    if fit is None:
        out['status'] = 'origin_only'
        out['reason'] = 'fewer than two populated bins with distinct draw counts'
        return out
    out.update({'status': 'fitted',
                'us_per_draw': round(fit['slope'] * 1000.0, 3),
                'us_per_draw_p25': round(fit['slope_p25'] * 1000.0, 3),
                'us_per_draw_p75': round(fit['slope_p75'] * 1000.0, 3),
                'fixed_ms_per_frame': round(fit['intercept'], 3)})
    return out


# ---- profiler and stamp attribution ------------------------------------------------

def profiler_share(profiler, window_rows, suspend_us=SUSPEND_US):
    """Share of wall a sampled thread loses to the profiler, from its own reports.

    Each tick suspends every sampled thread once, at ~123 us per suspended
    thread (docs/reverse-engineering/loading-profile-run1.md section 6), so the
    render thread's loss is the achieved tick rate times that cost.  The
    achieved rate is far below the 2 ms nominal once the thread count rises,
    which is why the whole-run 2.37% does not apply to the scene phase.
    """
    if not profiler:
        return {'status': 'absent', 'reason': 'no profile_report delta records'}
    if not window_rows:
        return {'status': 'unavailable', 'reason': 'no windows'}
    low = min(r['since_s'] for r in window_rows) * 1e6
    high = max(r['since_s'] for r in window_rows) * 1e6
    inside = [p for p in profiler if low <= p['since_us'] <= high + p['elapsed_us']]
    if not inside:
        return {'status': 'unavailable', 'reason': 'no reports inside the phase'}
    elapsed = sum(p['elapsed_us'] for p in inside)
    ticks = sum(p['ticks'] for p in inside)
    rate = ticks / elapsed * 1e6
    share = rate * suspend_us / 1e6
    threads = quantile([p['threads'] for p in inside], 0.5)
    frame_ms = quantile([r['mean_ms'] for r in window_rows], 0.5)
    return {'status': 'present', 'reports': len(inside),
            'covered_s': round(elapsed / 1e6, 1), 'ticks': ticks,
            'ticks_per_s': round(rate, 1), 'threads_median': threads,
            'suspend_us_per_thread': suspend_us,
            'share_of_wall': round(share, 5),
            'ms_per_frame_at_median': round(share * frame_ms, 3)}


def stamp_cost(menu_rows, scene_rows, stamp_us):
    """Telemetry-stamp overhead: spans per frame times the cost of one stamp.

    Every timed span costs one QPC pair plus the bucketing of ``record``
    (docs/verification/telemetry.md).  The menu phase bounds that cost from
    above: there the gate rejects every draw at gate 2, so its whole sample is
    the stamp plus that rejection.
    """
    observed = None
    gate = [r['route']['route_gate']['us_per_call'] for r in menu_rows
            if 'route_gate' in r['route']]
    if gate:
        observed = round(quantile(gate, 0.5), 4)
    spans = []
    for row in scene_rows:
        spans.append(sum(entry['calls_per_frame'] for entry in row['route'].values()))
    if not spans:
        return {'status': 'unavailable', 'reason': 'no route metrics in the scene phase'}
    per_frame = quantile(spans, 0.5)
    assumed = observed if observed is not None else stamp_us
    return {'status': 'estimated', 'menu_gate_us_per_call': observed,
            'stamp_us_assumed': assumed,
            'spans_per_frame_median': round(per_frame, 1),
            'ms_per_frame': round(per_frame * assumed / 1000.0, 3)}


# ---- route metrics from the per-frame records --------------------------------------

def frame_record_report(records, capture=False):
    """Exact per-frame route cost from ``motion_output_frame``.

    ``capture=False`` keeps the ordinary periodic frames; readbacks only occur
    in capture frames and must stay out of an ordinary-frame estimate.
    """
    subset = [r for r in records if (r['readbacks'] > 0) == capture and r['routed'] > 0]
    if not subset:
        return {'status': 'unavailable', 'records': 0}
    out = {'status': 'evaluated', 'records': len(subset),
           'draws': describe([r['draws'] for r in subset], 1),
           'routed': describe([r['routed'] for r in subset], 1),
           'routed_fraction': describe([r['routed'] / r['draws'] for r in subset
                                        if r['draws']], 4)}
    exclusive = [sum(r[key] for key in FRAME_EXCLUSIVE) for r in subset]
    out['route_exclusive_us'] = describe(exclusive, 1)
    out['route_us_per_draw'] = describe(
        [sum(r[key] for key in FRAME_EXCLUSIVE) / r['draws'] for r in subset if r['draws']], 3)
    out['route_us_per_routed_draw'] = describe(
        [sum(r[key] for key in FRAME_EXCLUSIVE) / r['routed'] for r in subset], 3)
    for key in FRAME_EXCLUSIVE + FRAME_NESTED + FRAME_TAA + ('readback_us',):
        out[key] = describe([r[key] for r in subset], 1)
    out['gate_us_per_draw'] = describe(
        [r['gate_us'] / r['draws'] for r in subset if r['draws']], 3)
    out['route_draw_us_per_routed'] = describe(
        [r['route_draw_us'] / r['routed'] for r in subset], 3)
    out['set_rt_us_per_call'] = describe(
        [r['set_rt_us'] / r['set_rt'] for r in subset if r['set_rt']], 3)
    out['set_rt_per_routed_draw'] = describe(
        [r['set_rt'] / r['routed'] for r in subset], 3)
    out['jitter_us_per_write'] = describe(
        [r['jitter_us'] / r['jitter_writes'] for r in subset if r['jitter_writes']], 4)
    out['jitter_writes_per_routed'] = describe(
        [r['jitter_writes'] / r['routed'] for r in subset], 3)
    if capture:
        out['readbacks'] = describe([r['readbacks'] for r in subset], 1)
        out['readback_us_per_readback'] = describe(
            [r['readback_us'] / r['readbacks'] for r in subset if r['readbacks']], 1)
    out['scene_end_source'] = dict(Counter(r['scene_end_source'] for r in subset))
    out['scene_end_check'] = dict(Counter(r['scene_end_check'] for r in subset))
    out['rt_mode'] = dict(Counter(r['rt_mode'] for r in subset))
    out['timing'] = dict(Counter(r['timing'] for r in subset))
    return out


# ---- per-run assembly --------------------------------------------------------------

def analyze_run(path, label, device='1', gap_us=2e6, min_frames=5, split_us=45000.0,
                bin_width=50.0, stamp_us=DEFAULT_STAMP_US):
    result = scan(path, device)
    rows = [r for r in (window_row(w) for w in result['windows']) if r]
    phases, loading = segment(rows, gap_us)
    labels, details = [], []
    for phase in phases:
        name, detail = label_phase(phase)
        labels.append(name)
        details.append(detail)
    scene_rows, menu_rows = [], []
    phase_table = []
    for index, (phase, name, detail) in enumerate(zip(phases, labels, details)):
        keep = usable(phase, min_frames)
        (scene_rows if name == 'scene' else menu_rows).extend(keep)
        phase_table.append({
            'phase': index, 'label': name, 'windows': len(phase), 'usable': len(keep),
            'capture_windows': sum(1 for r in phase if r['capture']),
            'frames': [phase[0]['frame'], phase[-1]['frame']],
            'since_s': [phase[0]['since_s'], phase[-1]['since_s']],
            'mean_ms_median': round(quantile([r['mean_ms'] for r in keep], 0.5), 3) if keep else None,
            'route_us_per_frame_median': round(
                quantile([r['route_exclusive_us'] for r in keep], 0.5), 1) if keep else None,
            **(detail or {})})
    capture_rows = [r for r in rows if r['capture']]
    mode = result['config'].get('motion_output_mode', {})
    report = {
        'label': label, 'path': str(path), 'device': device,
        'configuration': {
            'motion_output': {key: mode.get(key) for key in
                              ('requested', 'taa', 'taa_debug', 'jitter', 'rt_mode',
                               'temporal_consumer', 'scene_hook', 'state_shadow', 'hdr',
                               'frame_log')},
            'mesh_cache': result['config'].get('mesh_cache', {}),
            'profiler': {key: result['config'].get('profile', {}).get(key) for key in
                         ('enabled', 'interval_us', 'report_s')},
            'status': {name: value.get('status') for name, value in result['status'].items()},
            'clock_hz': result['clock_hz']},
        'rejected': result['rejected'],
        'hdr_metrics_present': sorted({name for r in rows for name in r['route']
                                       if name.startswith('hdr_')}),
        'windows': {'total': len(rows), 'loading': len(loading),
                    'capture': len(capture_rows),
                    'scene_usable': len(scene_rows), 'menu_usable': len(menu_rows),
                    'min_frames': min_frames},
        'loading_gaps': loading,
        'phases': phase_table,
        'phase_check': check_phases(phases, labels, result['frame_records']),
        'scene': regime_split(scene_rows, split_us),
        'menu': regime_statistics(menu_rows),
        'scene_draw_bins': draw_bins(scene_rows, bin_width),
        'menu_draw_bins': draw_bins(menu_rows, bin_width),
        'frame_records': {
            'records': len(result['frame_records']),
            'normal': frame_record_report(result['frame_records'], capture=False),
            'capture': frame_record_report(result['frame_records'], capture=True)},
        'profiler_share': {
            'scene': profiler_share(result['profiler'], scene_rows),
            'menu': profiler_share(result['profiler'], menu_rows)},
        'telemetry_stamps': stamp_cost(menu_rows, scene_rows, stamp_us),
        'capture_windows': [
            {'frame': r['frame'], 'frames': r['frames'], 'mean_ms': round(r['mean_ms'], 2),
             'max_ms': round(r['max_ms'], 2),
             'frame_capture_ms': round(r['frame_capture_ms'], 2) if r['frame_capture_ms'] else None,
             'frame_capture_frames': r['frame_capture_frames'],
             'readback_us_per_frame': round(
                 r['route'].get('route_readback', {}).get('us_per_frame', 0.0), 1)}
            for r in capture_rows],
        'capture_frame_interval_ms': describe(
            [r['frame_capture_ms'] for r in capture_rows if r['frame_capture_ms']]),
    }
    report['route_off'] = not any(r['route_active'] for r in scene_rows) \
        and not result['frame_records']
    report['_rows'] = {'scene': scene_rows, 'menu': menu_rows}
    return report


# ---- comparison --------------------------------------------------------------------

def compare(primary, baseline, bin_width=50.0, split_us=45000.0, min_bin_windows=2):
    """Route-on against route-off: per regime, per draw bin, and normalised.

    ``controlled`` is true only when the baseline actually has the route off.
    Two route-on runs are still compared - that pair is the path/session noise
    scale - but no route cost is attributed from it.
    """
    controlled = baseline['route_off'] and not primary['route_off']
    out = {'primary': primary['label'], 'baseline': baseline['label'],
           'controlled': controlled,
           'role': ('route cost' if controlled else 'path and configuration noise')}
    regimes = {}
    for regime in ('fast', 'slow'):
        a = primary['scene'].get(regime, {})
        b = baseline['scene'].get(regime, {})
        if not a.get('windows') or not b.get('windows'):
            regimes[regime] = {'status': 'unavailable',
                               'primary_windows': a.get('windows', 0),
                               'baseline_windows': b.get('windows', 0)}
            continue
        pm, bm = a['mean_ms']['median'], b['mean_ms']['median']
        pn, bn = a['min_ms']['median'], b['min_ms']['median']
        route = a['route_exclusive_us_per_frame']['median'] / 1000.0
        regimes[regime] = {
            'status': 'compared',
            'primary': {'windows': a['windows'], 'mean_ms': pm, 'min_ms': pn,
                        'draws_per_frame': a['draws_per_frame'].get('median')},
            'baseline': {'windows': b['windows'], 'mean_ms': bm, 'min_ms': bn,
                         'draws_per_frame': b['draws_per_frame'].get('median')},
            'delta_mean_ms': round(pm - bm, 3),
            'delta_mean_percent': round((pm / bm - 1) * 100.0, 2) if bm else None,
            'delta_min_ms': round(pn - bn, 3),
            'delta_min_percent': round((pn / bn - 1) * 100.0, 2) if bn else None,
            'route_exclusive_ms': round(route, 3),
            'route_share_of_delta': (round(route / (pm - bm), 3)
                                     if controlled and abs(pm - bm) > 1e-9 else None),
            'draws_per_frame_ratio': round(
                a['draws_per_frame']['median'] / b['draws_per_frame']['median'], 3)
            if a['draws_per_frame'].get('median') and b['draws_per_frame'].get('median') else None,
            'note': 'the two draw counts differ; see normalised',
        }
    out['regimes'] = regimes
    out['occupancy'] = {
        'primary_fast_fraction': primary['scene'].get('fast_occupancy'),
        'baseline_fast_fraction': baseline['scene'].get('fast_occupancy')}
    scene_pairs = paired_bins(primary['_rows']['scene'], baseline['_rows']['scene'],
                              bin_width, 'mean_ms_median', min_bin_windows)
    menu_pairs = paired_bins(primary['_rows']['menu'], baseline['_rows']['menu'],
                             bin_width, 'mean_ms_median', min_bin_windows)
    out['normalised'] = {
        'bin_width': bin_width, 'min_bin_windows': min_bin_windows,
        'scene': fit_pairs(scene_pairs),
        'menu': fit_pairs(menu_pairs),
        'scene_min': fit_pairs(paired_bins(primary['_rows']['scene'],
                                           baseline['_rows']['scene'], bin_width,
                                           'min_ms_median', min_bin_windows)),
    }
    scene_fit, menu_fit = out['normalised']['scene'], out['normalised']['menu']
    if controlled and scene_fit.get('status') == 'fitted':
        rows = primary['_rows']['scene']
        route_metric = quantile([r['route_exclusive_us'] for r in rows], 0.5)
        draws_median = quantile([r['draws_per_frame'] for r in rows if r['draws_per_frame']], 0.5)
        # The sector phase spans 50-900 draws/frame, so its free-intercept slope
        # is the total per-draw difference.  The menu phase spans 60 draws, so
        # only its origin-constrained estimate is usable as the overhead term.
        total = scene_fit['us_per_draw']
        overhead = menu_fit['origin_us_per_draw'].get('median')
        attributed = total - overhead if overhead is not None else None
        metric_per_draw = route_metric / draws_median if draws_median else None
        profiler = primary['profiler_share']['scene']
        out['attribution'] = {
            'scene_total_us_per_draw': total,
            'scene_total_us_per_draw_iqr': [scene_fit['us_per_draw_p25'],
                                            scene_fit['us_per_draw_p75']],
            'scene_fixed_ms_per_frame': scene_fit['fixed_ms_per_frame'],
            'menu_overhead_us_per_draw': overhead,
            'menu_overhead_bins': menu_fit['bins'],
            'menu_delta_ms': menu_fit['delta_ms'].get('median'),
            'profiler_share_of_wall': profiler.get('share_of_wall'),
            'profiler_ms_per_frame': profiler.get('ms_per_frame_at_median'),
            'telemetry_stamp_ms_per_frame': primary['telemetry_stamps'].get('ms_per_frame'),
            'route_attributed_us_per_draw': round(attributed, 3) if attributed is not None else None,
            'route_metric_us_per_frame_median': round(route_metric, 1),
            'draws_per_frame_median': round(draws_median, 1) if draws_median else None,
            'route_metric_us_per_draw': round(metric_per_draw, 3) if metric_per_draw else None,
            'route_attributed_ms_per_frame_at_median': round(
                attributed * draws_median / 1000.0, 3)
            if attributed is not None and draws_median else None,
            'unexplained_us_per_draw': round(attributed - metric_per_draw, 3)
            if attributed is not None and metric_per_draw else None,
            'note': ('the sector slope minus the menu overhead is what the route adds per '
                     'draw; the menu measures the profiler, object trace and per-draw '
                     'telemetry with the route enabled and nothing routed, so it is not '
                     'charged to the route. The profiler is a share of wall time and '
                     'therefore lands mostly in the fixed term, not the slope.'),
        }
        # The headline: the attributed per-draw cost carried to each regime's own
        # draw count, beside what the route's own metrics measured there.
        per_regime = {}
        for regime in ('fast', 'slow'):
            entry = regimes.get(regime, {})
            if entry.get('status') != 'compared' or attributed is None:
                continue
            draws = entry['primary'].get('draws_per_frame')
            frame_ms = entry['primary']['mean_ms']
            if not draws or not frame_ms:
                continue
            cost = attributed * draws / 1000.0
            metric = entry['route_exclusive_ms']
            per_regime[regime] = {
                'draws_per_frame': draws, 'frame_ms': frame_ms,
                'route_attributed_ms': round(cost, 3),
                'route_attributed_percent': round(cost / frame_ms * 100.0, 2),
                'route_metric_ms': metric,
                'route_metric_percent': round(metric / frame_ms * 100.0, 2),
                'frame_ms_without_route': round(frame_ms - cost, 3)}
        out['route_cost_per_regime'] = per_regime
    return out


def levers(run, comparison=None):
    """Ranked cost levers with the saving each would take off a frame."""
    normal = run['frame_records']['normal']
    if normal.get('status') != 'evaluated':
        return {'status': 'unavailable', 'reason': 'no routed ordinary frame records'}
    draws = normal['draws']['median']
    routed = normal['routed']['median']
    gate = normal['gate_us']['median']
    route_draw = normal['route_draw_us']['median']
    set_rt = normal['set_rt_us']['median']
    jitter = normal['jitter_us']['median']
    fill = normal['fill_us']['median']
    taa = normal['taa_run_us']['median']
    total = gate + route_draw + jitter + fill
    stamps = run['telemetry_stamps']
    items = [
        {'lever': 'gate: per-draw selector/gate/history evaluation',
         'metric': 'route_gate', 'now_us_per_frame': round(gate, 1),
         'now_us_per_draw': normal['gate_us_per_draw']['median'],
         'share_of_route': round(gate / total, 3) if total else None,
         'saving_us_per_frame': round(gate * 0.5, 1),
         'basis': ('the gate is the single largest route item; the menu phase shows an '
                   'early-rejected gate costs ' +
                   str(stamps.get('menu_gate_us_per_call')) +
                   ' us, so a cached per-(shader,buffer) decision that reaches the '
                   'early-out should recover a large fraction. 50% assumed, unmeasured.')},
        {'lever': 'lazy RT mode (X3M_MOTION_RT_MODE=lazy)',
         'metric': 'route_set_rt', 'now_us_per_frame': round(set_rt, 1),
         'now_us_per_draw': round(set_rt / draws, 3) if draws else None,
         'share_of_route': round(set_rt / total, 3) if total else None,
         'saving_us_per_frame': round(set_rt * (PERDRAW_SET_RT - LAZY_SET_RT) / PERDRAW_SET_RT, 1),
         'basis': ('the motion-output fixture burst drops SetRenderTarget from '
                   f'{PERDRAW_SET_RT} to {LAZY_SET_RT} ({PERDRAW_SET_RT - LAZY_SET_RT} '
                   'fewer of 20 = 40%); set_rt_us is inside route_draw_us, so the saving '
                   'is a share of route_draw, not additional to it')},
        {'lever': 'telemetry stamps (one QPC pair + record per span)',
         'metric': 'all route_*/taa_* spans',
         'now_us_per_frame': round(stamps.get('ms_per_frame', 0.0) * 1000.0, 1),
         'now_us_per_draw': None,
         'share_of_route': round(stamps.get('ms_per_frame', 0.0) * 1000.0 / total, 3)
         if total else None,
         'saving_us_per_frame': round(stamps.get('ms_per_frame', 0.0) * 1000.0, 1),
         'basis': (f"{stamps.get('spans_per_frame_median')} spans per frame at "
                   f"{stamps.get('stamp_us_assumed')} us; this is diagnostic-only cost "
                   'and disappears with X3M_TELEMETRY off')},
        {'lever': 'jitter constant writes (two per jittered draw)',
         'metric': 'route_jitter', 'now_us_per_frame': round(jitter, 1),
         'now_us_per_draw': round(jitter / draws, 3) if draws else None,
         'share_of_route': round(jitter / total, 3) if total else None,
         'saving_us_per_frame': round(jitter * 0.5, 1),
         'basis': ('the restore half of each pair exists because the engine may read the '
                   'rows back; writing once per frame and restoring once would halve the '
                   'writes if that can be shown safe')},
        {'lever': 'sentinel fill (one fullscreen quad per frame)',
         'metric': 'route_fill', 'now_us_per_frame': round(fill, 1),
         'now_us_per_draw': None,
         'share_of_route': round(fill / total, 3) if total else None,
         'saving_us_per_frame': 0.0,
         'basis': 'already a per-frame constant; no lever, listed for completeness'},
        {'lever': 'temporal resolve (TemporalPass::run, inclusive)',
         'metric': 'taa_run', 'now_us_per_frame': round(taa, 1),
         'now_us_per_draw': None,
         'share_of_route': round(taa / total, 3) if total else None,
         'saving_us_per_frame': 0.0,
         'basis': ('the resolve is not a lever: it is already under 0.5 ms per frame, '
                   'consistent with the iteration-8 +0.2% TAA-on/off result')},
    ]
    items.sort(key=lambda item: -item['saving_us_per_frame'])
    for rank, item in enumerate(items, 1):
        item['rank'] = rank
    return {'status': 'ranked', 'draws_per_frame_median': draws,
            'routed_per_frame_median': routed,
            'route_exclusive_us_per_frame_median': round(total, 1),
            'items': items}


# ---- text rendering ----------------------------------------------------------------

def fmt(value, digits=2):
    if value is None:
        return '-'
    if isinstance(value, float):
        return f'{value:.{digits}f}'
    return str(value)


def render_text(report):
    lines = []
    lines.append('iteration 9: controlled route-on against route-off cost')
    lines.append('=' * 72)
    lines.append('CPU-side wall clock only; frame_normal is a Present-to-Present interval')
    lines.append('and contains application work, pacing and driver blocking.')
    for run in report['runs']:
        lines.append('')
        lines.append(f"-- run {run['label']}  {run['path']}")
        mo = run['configuration']['motion_output']
        lines.append('   route=%s taa=%s taa_debug=%s jitter=%s rt_mode=%s scene_hook=%s hdr=%s'
                     % (mo.get('requested'), mo.get('taa'), mo.get('taa_debug'),
                        mo.get('jitter'), mo.get('rt_mode'), mo.get('scene_hook'),
                        mo.get('hdr')))
        lines.append('   profiler=%s object_trace=%s mesh_cache_enabled=%s  hdr metrics: %s'
                     % (run['configuration']['profiler'].get('enabled'),
                        run['configuration']['status'].get('object_trace'),
                        run['configuration']['mesh_cache'].get('enabled'),
                        run['hdr_metrics_present'] or 'none (expected)'))
        w = run['windows']
        lines.append('   windows total=%d loading=%d capture=%d scene_usable=%d menu_usable=%d'
                     % (w['total'], w['loading'], w['capture'], w['scene_usable'],
                        w['menu_usable']))
        lines.append('   phases: ' + ', '.join(
            '%d=%s[%d..%d] n=%d %s ms' % (p['phase'], p['label'], p['frames'][0],
                                          p['frames'][1], p['usable'],
                                          fmt(p['mean_ms_median'])) for p in run['phases']))
        lines.append('   phase check: ' + run['phase_check']['status'])
        lines.append('   %-6s %8s %8s %8s %9s %9s %9s' % (
            'regime', 'windows', 'mean_ms', 'min_ms', 'draws/f', 'route_ms', 'taa_us'))
        for regime in ('fast', 'slow'):
            r = run['scene'].get(regime, {})
            if not r.get('windows'):
                continue
            lines.append('   %-6s %8d %8s %8s %9s %9s %9s' % (
                regime, r['windows'], fmt(r['mean_ms']['median']), fmt(r['min_ms']['median']),
                fmt(r['draws_per_frame'].get('median'), 1),
                fmt(r['route_exclusive_us_per_frame']['median'] / 1000.0, 3),
                fmt(r['taa_run_us_per_frame']['median'], 1)))
        menu = run['menu']
        if menu.get('windows'):
            lines.append('   %-6s %8d %8s %8s %9s %9s %9s' % (
                'menu', menu['windows'], fmt(menu['mean_ms']['median']),
                fmt(menu['min_ms']['median']), fmt(menu['draws_per_frame'].get('median'), 1),
                fmt(menu['route_exclusive_us_per_frame']['median'] / 1000.0, 3),
                fmt(menu['taa_run_us_per_frame']['median'], 1)))
        prof = run['profiler_share']['scene']
        if prof.get('status') == 'present':
            lines.append('   profiler: %.1f ticks/s, %d threads -> %.2f%% of wall = %s ms/frame'
                         % (prof['ticks_per_s'], prof['threads_median'],
                            prof['share_of_wall'] * 100.0, fmt(prof['ms_per_frame_at_median'], 3)))
        else:
            lines.append('   profiler: ' + prof.get('status', '?'))
        stamps = run['telemetry_stamps']
        if stamps.get('status') == 'estimated':
            lines.append('   telemetry stamps: %s spans/frame x %s us = %s ms/frame'
                         % (fmt(stamps['spans_per_frame_median'], 1),
                            stamps['stamp_us_assumed'], fmt(stamps['ms_per_frame'], 3)))
        normal = run['frame_records']['normal']
        if normal.get('status') == 'evaluated':
            lines.append('   per-frame records (ordinary frames, n=%d): draws %s routed %s'
                         % (normal['records'], fmt(normal['draws']['median'], 0),
                            fmt(normal['routed']['median'], 0)))
            lines.append('      route exclusive %s us/frame = %s us/draw (%s us/routed draw)'
                         % (fmt(normal['route_exclusive_us']['median'], 1),
                            fmt(normal['route_us_per_draw']['median'], 2),
                            fmt(normal['route_us_per_routed_draw']['median'], 2)))
            lines.append('      gate %s (%s us/draw)  route_draw %s (%s us/routed)  '
                         'set_rt %s (%s us/call)' % (
                             fmt(normal['gate_us']['median'], 1),
                             fmt(normal['gate_us_per_draw']['median'], 2),
                             fmt(normal['route_draw_us']['median'], 1),
                             fmt(normal['route_draw_us_per_routed']['median'], 2),
                             fmt(normal['set_rt_us']['median'], 1),
                             fmt(normal['set_rt_us_per_call']['median'], 3)))
            lines.append('      jitter %s (%s us/write)  fill %s  taa_run %s  '
                         'taa_resolve_draw %s' % (
                             fmt(normal['jitter_us']['median'], 1),
                             fmt(normal['jitter_us_per_write']['median'], 3),
                             fmt(normal['fill_us']['median'], 1),
                             fmt(normal['taa_run_us']['median'], 1),
                             fmt(normal['taa_draw_us']['median'], 1)))
            lines.append('      scene_end_source=%s' % normal['scene_end_source'])
        cap = run['frame_records']['capture']
        if cap.get('status') == 'evaluated':
            interval = run['capture_frame_interval_ms']
            lines.append('   capture frames (n=%d): %s readbacks, readback %s us/frame '
                         '(%s us each); frame_capture interval median %s ms' % (
                             cap['records'], fmt(cap['readbacks']['median'], 0),
                             fmt(cap['readback_us']['median'], 0),
                             fmt(cap['readback_us_per_readback']['median'], 0),
                             fmt(interval.get('median'), 1)))
        elif run['capture_frame_interval_ms'].get('n'):
            lines.append('   capture frames: frame_capture interval median %s ms'
                         % fmt(run['capture_frame_interval_ms']['median'], 1))
    for entry in report['comparisons']:
        lines.append('')
        lines.append('-- %s against %s  (%s)' % (entry['primary'], entry['baseline'],
                                                 entry['role']))
        for regime in ('fast', 'slow'):
            r = entry['regimes'].get(regime, {})
            if r.get('status') != 'compared':
                lines.append('   %-5s unavailable' % regime)
                continue
            lines.append('   %-5s %s ms (n=%d, %s draws/f) vs %s ms (n=%d, %s draws/f) '
                         '-> %+.2f ms (%+.1f%%)' % (
                             regime, fmt(r['primary']['mean_ms']), r['primary']['windows'],
                             fmt(r['primary']['draws_per_frame'], 0),
                             fmt(r['baseline']['mean_ms']), r['baseline']['windows'],
                             fmt(r['baseline']['draws_per_frame'], 0),
                             r['delta_mean_ms'], r['delta_mean_percent']))
            if r['route_share_of_delta'] is not None:
                lines.append('         route metrics %s ms/frame = %s of the difference'
                             % (fmt(r['route_exclusive_ms'], 2),
                                fmt(r['route_share_of_delta'])))
        norm = entry['normalised']
        for key in ('scene', 'menu', 'scene_min'):
            fit = norm.get(key, {})
            if not fit.get('bins'):
                lines.append('   %-10s no shared draw bins' % key)
                continue
            origin = fit['origin_us_per_draw']
            slope = ('%+.2f us/draw (IQR %+.2f..%+.2f) %+.2f ms fixed' % (
                fit['us_per_draw'], fit['us_per_draw_p25'], fit['us_per_draw_p75'],
                fit['fixed_ms_per_frame'])) if fit.get('status') == 'fitted' \
                else 'slope ill-conditioned'
            sign = fit['sign_test']
            lines.append('   %-10s %2d bins over %.0f-%.0f draws: %s | origin %+.2f us/draw'
                         ' | sign %d+/%d- p=%s'
                         % (key, fit['bins'], fit['draws_span'][0], fit['draws_span'][1],
                            slope, origin['median'], sign.get('positive', 0),
                            sign.get('negative', 0), fmt(sign.get('p_two_sided'), 4)))
        att = entry.get('attribution')
        if att:
            lines.append('   attribution: sector %s us/draw - menu overhead %s us/draw '
                         '= route %s us/draw' % (
                             fmt(att['scene_total_us_per_draw']),
                             fmt(att['menu_overhead_us_per_draw']),
                             fmt(att['route_attributed_us_per_draw'])))
            lines.append('                route metrics say %s us/draw; unexplained %s us/draw; '
                         'route %s ms/frame at %s draws' % (
                             fmt(att['route_metric_us_per_draw']),
                             fmt(att['unexplained_us_per_draw']),
                             fmt(att['route_attributed_ms_per_frame_at_median']),
                             fmt(att['draws_per_frame_median'], 0)))
            lines.append('                not charged to the route: profiler %s ms/frame '
                         '(%.2f%% of wall), telemetry stamps %s ms/frame' % (
                             fmt(att['profiler_ms_per_frame'], 3),
                             (att['profiler_share_of_wall'] or 0.0) * 100.0,
                             fmt(att['telemetry_stamp_ms_per_frame'], 3)))
        for regime, cost in (entry.get('route_cost_per_regime') or {}).items():
            lines.append('   route cost %-5s %s ms of %s ms = %s%% (metrics %s ms = %s%%) '
                         'at %s draws/frame' % (
                             regime, fmt(cost['route_attributed_ms']), fmt(cost['frame_ms']),
                             fmt(cost['route_attributed_percent'], 1),
                             fmt(cost['route_metric_ms']),
                             fmt(cost['route_metric_percent'], 1),
                             fmt(cost['draws_per_frame'], 0)))
    lev = report.get('levers', {})
    if lev.get('status') == 'ranked':
        lines.append('')
        lines.append('-- cost levers (%s, %s draws/frame, route %s us/frame)' % (
            lev['label'], fmt(lev['draws_per_frame_median'], 0),
            fmt(lev['route_exclusive_us_per_frame_median'], 0)))
        for item in lev['items']:
            lines.append('   %d. %-52s now %8s us  save %8s us' % (
                item['rank'], item['lever'][:52], fmt(item['now_us_per_frame'], 1),
                fmt(item['saving_us_per_frame'], 1)))
    return '\n'.join(lines) + '\n'


# ---- entry point -------------------------------------------------------------------

def build(runs, baseline_label, device='1', gap_us=2e6, min_frames=5, split_us=45000.0,
          bin_width=50.0, stamp_us=DEFAULT_STAMP_US, min_bin_windows=2):
    analyzed = [analyze_run(path, label, device, gap_us, min_frames, split_us, bin_width,
                            stamp_us) for label, path in runs]
    index = {run['label']: run for run in analyzed}
    if baseline_label not in index:
        raise Malformed(f'baseline {baseline_label!r} is not one of the runs')
    baseline = index[baseline_label]
    comparisons = [compare(run, baseline, bin_width, split_us, min_bin_windows)
                   for run in analyzed if run['label'] != baseline_label]
    # The two route-on runs against each other: the scale of path/session noise.
    route_on = [run for run in analyzed if run['label'] != baseline_label]
    if len(route_on) == 2:
        comparisons.append(compare(route_on[0], route_on[1], bin_width, split_us,
                                   min_bin_windows))
    lever_run = route_on[0] if route_on else baseline
    report = {'runs': analyzed, 'comparisons': comparisons,
              'levers': dict(levers(lever_run), label=lever_run['label']),
              'parameters': {'device': device, 'gap_us': gap_us, 'min_frames': min_frames,
                             'split_us': split_us, 'bin_width': bin_width,
                             'min_bin_windows': min_bin_windows,
                             'stamp_us': stamp_us, 'suspend_us': SUSPEND_US,
                             'baseline': baseline_label},
              'limits': [
                  'CPU-side QPC spans and Present-to-Present intervals; never GPU time.',
                  'Route metrics nest: only route_gate+route_draw+route_fill+route_jitter'
                  '+route_lazy_flush are summed; route_set_rt is inside route_draw and the'
                  ' taa_* phases are inside taa_run.',
                  'The three sessions are the same build and save but not the same flight'
                  ' path, so pooled medians report occupancy; the draw-count normalisation'
                  ' and the run-1-against-run-2 slope carry the uncertainty.',
                  'Runs 1 and 2 also carry the sampling profiler and the object trace; the'
                  ' menu phase measures that overhead with the route enabled and nothing'
                  ' routed, and it is subtracted before any cost is attributed to the route.',
                  'Capture windows are excluded from every frame-time median; their'
                  ' readback cost is reported on its own.',
                  'A frame-time difference is not proof of causation: only the route'
                  ' metrics are direct measurements of route work.']}
    return report


def strip_rows(report):
    for run in report['runs']:
        run.pop('_rows', None)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--run', action='append', default=[], metavar='LABEL=PATH',
                        help='a session log to analyze; repeat for each run')
    parser.add_argument('--baseline', default='run3',
                        help='label of the route-off run (default run3)')
    parser.add_argument('--device', default='1')
    parser.add_argument('--gap-us', type=float, default=2e6,
                        help='a frame interval above this marks a loading window (default 2 s)')
    parser.add_argument('--min-frames', type=int, default=5,
                        help='windows with fewer frames are dropped from medians')
    parser.add_argument('--window-mode-split-us', type=float, default=45000.0,
                        help='the iteration-8 fast/slow regime split (default 45 ms)')
    parser.add_argument('--bin-width', type=float, default=50.0,
                        help='draws-per-frame bin width for the normalised comparison')
    parser.add_argument('--min-bin-windows', type=int, default=2,
                        help='a paired draw bin needs this many windows on both sides')
    parser.add_argument('--stamp-us', type=float, default=DEFAULT_STAMP_US,
                        help='assumed cost of one telemetry stamp when the menu phase '
                             'gives no gate sample')
    parser.add_argument('--json', type=Path)
    parser.add_argument('--text', type=Path)
    args = parser.parse_args(argv)
    runs = []
    for entry in args.run:
        label, _, path = entry.partition('=')
        if not path:
            parser.error(f'--run expects LABEL=PATH, got {entry!r}')
        runs.append((label, Path(path)))
    if not runs:
        parser.error('at least one --run is required')
    report = build(runs, args.baseline, args.device, args.gap_us, args.min_frames,
                   args.window_mode_split_us, args.bin_width, args.stamp_us,
                   args.min_bin_windows)
    text = render_text(report)
    strip_rows(report)
    if args.json:
        args.json.write_text(json.dumps(report, indent=1, sort_keys=False) + '\n')
    if args.text:
        args.text.write_text(text)
    sys.stdout.write(text)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
