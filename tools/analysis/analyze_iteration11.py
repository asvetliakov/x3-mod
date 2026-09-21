#!/usr/bin/env python3
"""Iteration 11: the review-25 build (validated direct engine reads) on the X3 bottle.

Run 9 is the first gameplay run of the review-25 build with the route on, the
engine scene-end hook on and **`X3M_TELEMETRY_DRAW` unset**, so the per-draw
telemetry stamps that every earlier route-on run carried are gone.  That single
configuration change removes two things the iteration-9/10 tools depend on:

* the ``draw_backend`` metric, which is how ``analyze_iteration09_cost`` learns
  draws per frame per report window - without it every window's
  ``draws_per_frame`` is ``None``, no phase can be labelled, and the paired
  draw-count bins that are the only path-insensitive frame-time comparison are
  empty; and
* ``gate_us`` / ``route_draw_us`` / ``set_rt_us`` / ``jitter_us`` in the
  ``motion_output_frame`` record, which are how the route's own per-draw cost
  was measured (16.33 us/draw in run 6).  They are logged as ``0.0``.

What this tool adds, and nothing else - everything else is imported from the
iteration-9/10 tools so the numbers stay produced by the same code:

1. ``draws_surrogate`` - a **draws-per-frame surrogate** for a ``per_draw=0``
   run.  ``motion_output_frame`` carries the frame's own ``draws`` count every
   ``frame_log`` frames (60 here), which is one or two samples per one-second
   report window.  The median of the samples inside a window is injected as a
   synthetic ``draw_backend`` metric (count only, no time), after which the
   whole iteration-9 cost machinery - phase labelling, regimes, draw bins,
   Theil-Sen paired fits, the sign test - runs unchanged.  The surrogate is
   validated on a run that has both (run 6: median relative error 1.3 %) and
   that cross-check is part of the report, not a claim in prose.
   Injection is explicit: ``draw_backend_us`` is left at zero so no per-call
   draw time can be read out of a synthetic window, and the report records
   which windows were injected.

2. ``route_cost_availability`` - what the missing stamps mean for the cost
   question, stated as data: the ``per_draw`` flag, the frame fields that are
   structurally zero, and the frame fields that still measure (``fill_us``,
   the ``taa_*`` family, ``readback_us``) because those brackets are per frame,
   not per draw.  A zero here is *unmeasured*, never *free*.

3. ``route_recovery`` - the answer to "did the direct-read change recover the
   31 %".  Two controlled comparisons against the same route-off baseline
   (run 5), each a paired-bin Theil-Sen slope in us/draw, are put side by side
   and the older run's diagnostic overhead (its sampling profiler and its own
   per-draw stamps, both measured in its log) is subtracted, because run 9
   carries neither.  The residual is the change attributable to the read path.

4. ``scene_hook`` - the ``motion_output_frame`` hook fields decoded
   (``scene_end_check`` 0 None / 1 Agree / 2 HookOnly / 3 StretchOnly /
   4 Disagree, ``src/proxy/motion_output.h``), the resolve attribution, the
   state-shadow resync counters and every hook field that can report a failure
   or an unwind, with the ``motion_output_scene_hook_disagreement`` records.

5. ``engine_reads`` - whether the log says anything at all about the read path.
   ``engine_memory``'s ``Stats`` (reads, VirtualQuery queries, rejected spans)
   is not emitted to the session log by the build that produced this log, so the
   report states the absence rather than leaving it to be inferred from an
   empty grep.

6. ``gz_buffer`` - the ``gz_buffer_file`` per-file counters, the served/real
   read amplification, and the cumulative ``loading_metric`` totals of the zlib
   entry points, split by the loading gap they fall in.

7. ``loads`` - the loading gaps with a label each.  A gap is bounded by the
   ``frame_normal`` window maximum (``analyze_iteration09_cost.segment``); the
   label comes from the route's own records on either side of it (a phase whose
   ``motion_output_frame`` records show ``routed=0`` is a menu), so "menu load",
   "save load", "sector change" and "return to menu" are read off the data.

Usage
-----

    python3 tools/analysis/analyze_iteration11.py \
        --run run9=<run 9 log> --run run6=<run 6 log> --run run5=<run 5 log> \
        --primary run9 --route-off run5 --previous run6 \
        --output verification/results/iteration-11.json \
        --text verification/results/iteration-11.txt
"""

import argparse
import collections
import json
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import analyze_iteration09_cost as it09cost     # noqa: E402
import analyze_iteration10 as it10              # noqa: E402

# scene_end_check enumeration, src/proxy/motion_output.h.
CHECK_NAMES = {0: 'None', 1: 'Agree', 2: 'HookOnly', 3: 'StretchOnly', 4: 'Disagree'}

# motion_output_frame fields that only a per-draw stamp can fill.
PER_DRAW_FIELDS = ('gate_us', 'route_draw_us', 'set_rt_us', 'lazy_flush_us', 'jitter_us')
# ... and the ones bracketed once per frame, which survive per_draw=0.
PER_FRAME_FIELDS = ('fill_us', 'taa_run_us', 'taa_capture_us', 'taa_copy_color_us',
                    'taa_copy_depth_us', 'taa_draw_us', 'taa_apply_us', 'taa_copy_back_us',
                    'readback_us')
# telemetry_metric names that X3M_TELEMETRY_DRAW gates (telemetry.cpp).
PER_DRAW_METRICS = ('draw_backend', 'route_gate', 'route_draw', 'route_set_rt',
                    'route_jitter', 'route_lazy_flush')

ZLIB_OPS = ('gzopen', 'gzread', 'gzgetc', 'gzseek', 'gztell', 'gzclose', 'gzwrite', 'inflate')

GZ_FILE_FIELDS = ('slot', 'capacity', 'calls', 'small_calls', 'served_bytes', 'real_reads',
                  'real_bytes', 'direct_reads', 'getcs', 'tells', 'seeks', 'seeks_served',
                  'seeks_real', 'error', 'end', 'close')

# Session-log line kinds that would carry engine_memory::Stats if the build
# emitted them.  None of these exist in review 25; the report says so.
ENGINE_READ_KINDS = ('engine_memory', 'engine_reads', 'engine_read', 'engine_memory_metric')


class Malformed(Exception):
    pass


def fields(line):
    out = {}
    for token in line.split()[1:]:
        if '=' in token:
            key, value = token.split('=', 1)
            out[key] = value
    return out


def number(text, default=None):
    try:
        return int(text)
    except (TypeError, ValueError):
        return default


def real(text, default=None):
    try:
        return float(text)
    except (TypeError, ValueError):
        return default


def quantiles(values, digits=3):
    return it09cost.describe(values, digits)


# ---- 1. the draws-per-frame surrogate ----------------------------------------------

def telemetry_per_draw(path):
    """The ``per_draw`` flag of the run's ``telemetry_start`` announcement.

    ``None`` means the line predates the flag (every pre-review-25 build), which
    is the same configuration as ``per_draw=1``: those runs stamped every draw.
    """
    with open(path, errors='replace') as handle:
        for line in handle:
            if line.startswith('telemetry_start'):
                return fields(line).get('per_draw', 'absent')
            if line.startswith(('frame_begin', 'draw ', 'telemetry_summary')):
                break
    return None


def route_draw_samples(scan):
    """``(frame, draws)`` of every ``motion_output_frame`` that counted draws."""
    samples = [(r['frame'], r['draws']) for r in scan['frame_records']
               if r['frame'] is not None and r['draws']]
    samples.sort()
    return samples


def inject_surrogate_draws(scan):
    """Fill the missing ``draw_backend`` count of each window from the route records.

    A report window ends at the ``telemetry_summary``'s frame; it therefore
    covers ``(previous summary frame, this frame]``.  Every route record in that
    half-open span is a draw-count sample of a frame inside the window, and the
    median of the samples is the window's draws per frame.  ``total_us`` stays
    zero so that no per-call draw time can be read from an injected window.
    """
    samples = route_draw_samples(scan)
    if not samples:
        return {'status': 'unavailable', 'reason': 'no motion_output_frame draw counts',
                'windows_injected': 0}
    frames = [f for f, _ in samples]
    draws = [d for _, d in samples]
    injected = kept = 0
    previous = None
    cursor = 0
    for window in scan['windows']:
        frame = window['frame']
        if 'draw_backend' in window['metrics']:
            kept += 1
            previous = frame
            continue
        low = -1 if previous is None else previous
        # samples are sorted; advance a cursor instead of rescanning per window
        while cursor < len(frames) and frames[cursor] <= low:
            cursor += 1
        end = cursor
        while end < len(frames) and frames[end] <= frame:
            end += 1
        inside = draws[cursor:end]
        previous = frame
        if not inside:
            continue
        count = window['metrics'].get('frame_normal', {}).get('count')
        if not count:
            continue
        window['metrics']['draw_backend'] = {
            'count': int(round(statistics.median(inside) * count)), 'total_us': 0.0,
            'min_us': 0.0, 'max_us': 0.0, 'failures': 0, 'surrogate': True}
        injected += 1
    return {'status': 'injected' if injected else 'not_needed',
            'windows_injected': injected, 'windows_measured': kept,
            'route_samples': len(samples),
            'note': 'draws/frame of an injected window is the median motion_output_frame '
                    'draw count of the frames it covers; draw_backend_us stays zero'}


def surrogate_cross_check(path, device='1'):
    """Surrogate against measurement on a run that logged both.

    Returns the distribution of ``|surrogate - measured| / measured`` over the
    windows of a ``per_draw=1`` run, which is the error the paired-bin
    comparison of a ``per_draw=0`` run inherits.
    """
    scan = it09cost.scan(path, device)
    measured = {}
    for window in scan['windows']:
        entry = window['metrics'].get('draw_backend')
        count = window['metrics'].get('frame_normal', {}).get('count')
        if entry and count:
            measured[window['frame']] = entry['count'] / count
    if not measured:
        return {'status': 'unavailable', 'reason': 'run has no draw_backend metric'}
    for window in scan['windows']:
        window['metrics'].pop('draw_backend', None)
    inject_surrogate_draws(scan)
    errors = []
    for window in scan['windows']:
        entry = window['metrics'].get('draw_backend')
        count = window['metrics'].get('frame_normal', {}).get('count')
        if not entry or not count or window['frame'] not in measured:
            continue
        truth = measured[window['frame']]
        if truth:
            errors.append(abs(entry['count'] / count - truth) / truth)
    return {'status': 'evaluated', 'windows': len(errors),
            'relative_error': quantiles(errors, 4),
            'within_10_percent': round(sum(1 for e in errors if e <= 0.10) / len(errors), 4)
            if errors else None}


def analyze_run(path, label, device='1', bin_width=50.0, min_bin_windows=2, min_frames=5):
    """``analyze_iteration09_cost.analyze_run`` with the surrogate in front of it.

    The cost tool reads its windows through the module-level ``scan``; the
    surrogate is applied by wrapping that one function, so the analysis itself
    is the tracked iteration-9 code, unmodified.
    """
    original = it09cost.scan
    record = {}

    def scan_with_surrogate(target, dev='1'):
        scan = original(target, dev)
        record.update(inject_surrogate_draws(scan))
        return scan

    it09cost.scan = scan_with_surrogate
    try:
        report = it09cost.analyze_run(path, label, device=device, bin_width=bin_width,
                                      min_frames=min_frames)
    finally:
        it09cost.scan = original
    record['per_draw'] = telemetry_per_draw(path)
    report['draws_surrogate'] = record
    return report


# ---- 2. what the missing stamps cost the measurement --------------------------------

def route_cost_availability(path, device='1'):
    """Which route cost fields this run can and cannot report, from the log."""
    per_draw = None
    metrics = collections.Counter()
    frame_totals = collections.defaultdict(list)
    records = 0
    with open(path, errors='replace') as handle:
        for line in handle:
            head = line.split(' ', 1)[0]
            if head == 'telemetry_start':
                per_draw = fields(line).get('per_draw')
            elif head == 'telemetry_metric':
                f = fields(line)
                if f.get('name') in PER_DRAW_METRICS:
                    metrics[f['name']] += number(f.get('count'), 0) or 0
            elif head == 'motion_output_frame':
                f = fields(line)
                if f.get('device') != device or number(f.get('routed'), 0) == 0:
                    continue
                records += 1
                for key in PER_DRAW_FIELDS + PER_FRAME_FIELDS:
                    frame_totals[key].append(real(f.get(key), 0.0))
    per_draw_zero = {key: max(frame_totals.get(key) or [0.0]) for key in PER_DRAW_FIELDS}
    return {
        'telemetry_per_draw': per_draw,
        'per_draw_metrics_present': {name: metrics[name] for name in PER_DRAW_METRICS
                                     if metrics[name]},
        'per_draw_metrics_absent': [name for name in PER_DRAW_METRICS if not metrics[name]],
        'routed_frame_records': records,
        'per_draw_frame_fields_max_us': per_draw_zero,
        'per_draw_fields_unmeasured': all(v == 0.0 for v in per_draw_zero.values()),
        'per_frame_fields_us': {key: quantiles(values, 1)
                                for key, values in sorted(frame_totals.items())
                                if key in PER_FRAME_FIELDS},
        'note': 'a zero per-draw field on a per_draw=0 run is unmeasured, not free; the '
                'route own cost of such a run can only be bounded by the frame time at a '
                'matched draw count',
    }


# ---- 3. did the read path recover the route's share --------------------------------

def _slope(comparison, phase='scene'):
    """The paired-bin fit of one phase of an ``analyze_iteration09_cost.compare``."""
    return ((comparison or {}).get('normalised', {}) or {}).get(phase, {}) or {}


def route_recovery(new_comparison, old_comparison, new_report, old_report, phase='scene'):
    """The route's per-draw overhead now against then, diagnostics removed.

    Both inputs are ``analyze_iteration09_cost.compare`` reports against the
    *same* route-off baseline, so each contributes a paired-bin Theil-Sen slope
    in microseconds per draw.  The older run also carried a sampling profiler
    and per-draw telemetry stamps, whose per-frame costs its own log measures;
    they are converted to us/draw at that run's own draws per frame and
    subtracted, because the new run carries neither.
    """
    out = {'phase': phase, 'comparisons': {}}
    for name, comparison in (('new', new_comparison), ('old', old_comparison)):
        fit = _slope(comparison, phase)
        out['comparisons'][name] = {
            'primary': (comparison or {}).get('primary'),
            'baseline': (comparison or {}).get('baseline'),
            'controlled': (comparison or {}).get('controlled'),
            'status': fit.get('status'),
            'bins': fit.get('bins'),
            'draws_span': fit.get('draws_span'),
            'us_per_draw': fit.get('us_per_draw'),
            'us_per_draw_iqr': [fit.get('us_per_draw_p25'), fit.get('us_per_draw_p75')],
            'us_per_draw_origin': (fit.get('origin_us_per_draw') or {}).get('median'),
            'fixed_ms_per_frame': fit.get('fixed_ms_per_frame'),
            'sign_test': fit.get('sign_test'),
            'attribution': (comparison or {}).get('attribution'),
        }
    diagnostics = {}
    for name, report in (('new', new_report), ('old', old_report)):
        scene = (report.get('scene') or {}).get('fast') or {}
        draws = ((scene.get('draws_per_frame') or {}).get('median'))
        profiler = ((report.get('profiler_share') or {}).get('scene') or {})
        stamps = report.get('telemetry_stamps') or {}
        profiler_ms = profiler.get('ms_per_frame_at_median') or 0.0
        stamp_ms = stamps.get('ms_per_frame') or 0.0
        diagnostics[name] = {
            'draws_per_frame': draws,
            'profiler_ms_per_frame': profiler_ms,
            'stamp_ms_per_frame': stamp_ms,
            'stamp_us_per_draw': round(stamp_ms * 1000.0 / draws, 3) if draws else None,
            'note': 'the sampling profiler is a share of wall time and lands in the fixed '
                    'term of the fit, not in the slope, so only the per-draw stamp cost is '
                    'subtracted from a per-draw slope',
        }
    out['diagnostics'] = diagnostics
    new_slope = ((new_comparison or {}).get('attribution') or {}).get(
        'route_attributed_us_per_draw')
    old_slope = ((old_comparison or {}).get('attribution') or {}).get(
        'route_attributed_us_per_draw')
    out['attributed_us_per_draw'] = {'new': new_slope, 'old': old_slope}
    old_diag = diagnostics['old']['stamp_us_per_draw'] or 0.0
    new_diag = diagnostics['new']['stamp_us_per_draw'] or 0.0
    if new_slope is not None and old_slope is not None:
        old_net = old_slope - old_diag
        new_net = new_slope - new_diag
        out['net_us_per_draw'] = {'old': round(old_net, 3), 'new': round(new_net, 3)}
        out['recovered_us_per_draw'] = round(old_net - new_net, 3)
        out['recovered_fraction'] = round((old_net - new_net) / old_net, 4) if old_net else None
        out['note'] = (
            'difference of the attributed per-draw cost (sector slope minus the run\'s own '
            'menu overhead) against the same route-off baseline, with each run\'s own '
            'measured per-draw telemetry stamp cost removed as well; the residual is the '
            'read-path and per-draw-work change together, not a single-variable experiment')
    return out


def route_share(report, comparison, phase='scene'):
    """The route's share of the frame at the run's own draw count.

    ``route_attributed_*`` is the cost tool's own attribution (the sector slope
    minus the menu overhead, which is the diagnostics the route is not charged
    for) and is the quantity iteration 10 published; the raw slope is kept
    beside it, and ``route_metrics_share`` is what the route's own spans
    measured - meaningless on a ``per_draw=0`` run, where they measure only the
    per-frame brackets.
    """
    scene = (report.get('scene') or {}).get('fast') or {}
    frame_ms = (scene.get('mean_ms') or {}).get('median')
    draws = (scene.get('draws_per_frame') or {}).get('median')
    fit = _slope(comparison, phase)
    slope = fit.get('us_per_draw')
    regime = ((comparison or {}).get('route_cost_per_regime') or {}).get('fast') or {}
    metrics_us = (scene.get('route_exclusive_us_per_frame') or {}).get('median')
    out = {'frame_ms_median': frame_ms, 'draws_per_frame_median': draws,
           'route_metrics_us_per_frame': metrics_us,
           'route_metrics_share': round(metrics_us / 1000.0 / frame_ms, 4)
           if metrics_us and frame_ms else None,
           'route_attributed_us_per_draw': ((comparison or {}).get('attribution') or {})
           .get('route_attributed_us_per_draw'),
           'route_attributed_ms': regime.get('route_attributed_ms'),
           'route_attributed_percent': regime.get('route_attributed_percent'),
           'frame_ms_without_route': regime.get('frame_ms_without_route')}
    if slope is not None and draws and frame_ms:
        out['route_ms_from_slope'] = round(slope * draws / 1000.0, 3)
        out['route_share_from_slope'] = round(slope * draws / 1000.0 / frame_ms, 4)
    return out


# ---- 4. the scene-end hook ---------------------------------------------------------

def scene_hook(path, device='1'):
    """Every hook field of ``motion_output_frame``, plus the disagreement records."""
    check = collections.Counter()
    source = collections.Counter()
    resolved = collections.Counter()
    signals = collections.Counter()
    outside = collections.Counter()
    state = collections.Counter()
    resync = collections.Counter()
    after = collections.Counter()
    shadow = collections.Counter()
    latched = resolves = records = 0
    latched_without_check = []
    disagreements = []
    failures = collections.Counter()
    with open(path, errors='replace') as handle:
        for line in handle:
            head = line.split(' ', 1)[0]
            if head == 'motion_output_scene_hook_disagreement':
                disagreements.append(fields(line))
                continue
            if head != 'motion_output_frame':
                continue
            f = fields(line)
            if f.get('device') != device:
                continue
            records += 1
            verdict = number(f.get('scene_end_check'), 0)
            check[CHECK_NAMES.get(verdict, str(verdict))] += 1
            source[f.get('scene_end_source')] += 1
            signals[f.get('hook_signals')] += 1
            outside[f.get('hook_outside_scene')] += 1
            state[f.get('hook_state')] += 1
            resync[f.get('rs_resyncs')] += 1
            after[f.get('draws_after_hook')] += 1
            for key in ('rs_queries', 'rs_hits', 'rs_gets', 'rs_resyncs'):
                shadow[key] += number(f.get(key), 0) or 0
            for key in ('apply_failures', 'restore_failures'):
                failures[key] += number(f.get(key), 0) or 0
            if number(f.get('latched'), 0):
                latched += 1
                if verdict == 0:
                    latched_without_check.append(number(f.get('frame')))
            if number(f.get('taa_resolved'), 0):
                resolves += 1
                resolved[f.get('scene_end_source')] += 1
    return {
        'records': records, 'latched_records': latched, 'resolves': resolves,
        'check_distribution': dict(check), 'source_distribution': dict(source),
        'resolved_by_source': dict(resolved),
        'hook_signals': dict(signals), 'hook_outside_scene': dict(outside),
        'hook_state': dict(state),
        'rs_resyncs_distribution': dict(resync),
        'draws_after_hook_distribution': dict(after),
        'state_shadow_totals': dict(shadow),
        'state_shadow_hit_rate': round(shadow['rs_hits'] / shadow['rs_queries'], 6)
        if shadow['rs_queries'] else None,
        'route_failures': dict(failures),
        'latched_frames_without_verdict': latched_without_check,
        'disagreement_records': disagreements,
        'note': 'scene_end_check: 0 None, 1 Agree, 2 HookOnly, 3 StretchOnly, 4 Disagree '
                '(src/proxy/motion_output.h); a latched frame with verdict None resolved '
                'nothing, so there was nothing to cross-check',
    }


# ---- 5. the read path ---------------------------------------------------------------

def engine_reads(kind_counts):
    """Whether the session log carries any engine-read evidence at all."""
    present = {kind: kind_counts[kind] for kind in ENGINE_READ_KINDS if kind_counts.get(kind)}
    return {
        'kinds_present': present,
        'status': 'present' if present else 'absent',
        'note': 'engine_memory::Stats (reads, VirtualQuery queries, rejected spans; '
                'src/proxy/engine_memory.h) is not written to the session log by the '
                'build that produced this log, so what that session read cannot be '
                'established from the log itself - only from the installed binary and '
                'the fixture suites. Later builds log an engine_memory line, and since '
                '2026-09-22 validated direct reads are the only read path',
    }


# ---- 6. the gz read-ahead buffer ---------------------------------------------------

def loading_metric_rows(path, ops=ZLIB_OPS):
    """Per-report-window ``loading_metric`` rows of the named ops, timestamped.

    The rows are **deltas**, not cumulative totals (the ``count`` sequence of a
    single op is not monotonic), so a session total is their sum and a per-phase
    total is the sum of the rows whose report timestamp falls in that phase.
    ``seconds`` is the row's ``qpc`` expressed against the ``telemetry_start``
    anchor, the same origin as ``since_start_us``.
    """
    anchor = frequency = None
    rows = []
    with open(path, errors='replace') as handle:
        for line in handle:
            head = line.split(' ', 1)[0]
            if head == 'telemetry_start':
                f = fields(line)
                anchor, frequency = number(f.get('qpc')), number(f.get('qpc_frequency'))
            elif head == 'loading_trace' and anchor is None:  # pre-telemetry-anchor logs
                f = fields(line)
                anchor = number(f.get('coverage_begin'))
                frequency = number(f.get('frequency'))
            elif head == 'loading_metric':
                f = fields(line)
                if f.get('op') not in ops:
                    continue
                qpc = number(f.get('qpc'))
                seconds = (None if qpc is None or anchor is None or not frequency
                           else (qpc - anchor) / float(frequency))
                rows.append({'op': f['op'], 'qpc': qpc, 'seconds': seconds,
                             'count': number(f.get('count'), 0) or 0,
                             'failures': number(f.get('failures'), 0) or 0,
                             'bytes': number(f.get('bytes'), 0) or 0,
                             'total_us': real(f.get('total_us'), 0.0) or 0.0,
                             'exclusive_us': real(f.get('exclusive_us'), 0.0) or 0.0,
                             'max_us': real(f.get('max_us'), 0.0) or 0.0})
    return {'anchor_qpc': anchor, 'frequency': frequency, 'rows': rows}


def sum_rows(rows):
    out = {}
    for row in rows:
        entry = out.setdefault(row['op'], {'windows': 0, 'count': 0, 'failures': 0,
                                           'bytes': 0, 'total_us': 0.0, 'max_us': 0.0})
        entry['windows'] += 1
        for key in ('count', 'failures', 'bytes'):
            entry[key] += row[key]
        entry['total_us'] = round(entry['total_us'] + row['total_us'], 1)
        entry['max_us'] = max(entry['max_us'], row['max_us'])
    return out


def gz_buffer(path, loads=None):
    """``gz_buffer_file`` rows, the zlib call totals, and their split by load."""
    config = None
    files = []
    with open(path, errors='replace') as handle:
        for line in handle:
            head = line.split(' ', 1)[0]
            if head == 'gz_buffer':
                config = fields(line)
            elif head == 'gz_buffer_file':
                f = fields(line)
                files.append({key: number(f.get(key), 0) for key in GZ_FILE_FIELDS})
    metrics = loading_metric_rows(path)
    totals = sum_rows(metrics['rows'])
    per_load = {}
    for gap in ((loads or {}).get('gaps') or []):
        end = gap.get('ends_since_s')
        if end is None:
            continue
        start = end - gap['seconds']
        inside = [r for r in metrics['rows']
                  if r['seconds'] is not None and start <= r['seconds'] <= end]
        per_load[f"{gap['index']}:{gap['kind']}"] = {
            'window_s': [round(start, 1), round(end, 1)], 'ops': sum_rows(inside)}
    if not files:
        return {'status': 'unavailable', 'reason': 'no gz_buffer_file lines',
                'configuration': config, 'zlib_totals': totals, 'zlib_by_load': per_load}
    main = max(files, key=lambda row: row['calls'])
    served = main['served_bytes']
    return {
        'status': 'evaluated', 'configuration': config, 'files': files,
        'savegame_file': {
            **main,
            'small_call_fraction': round(main['small_calls'] / main['calls'], 6)
            if main['calls'] else None,
            'served_mb': round(served / 1e6, 3),
            'mean_served_bytes_per_call': round(served / main['calls'], 4)
            if main['calls'] else None,
            'calls_per_real_read': round(main['calls'] / main['real_reads'], 1)
            if main['real_reads'] else None,
            'real_bytes_equal_served': main['real_bytes'] == served,
            'errors': main['error'], 'ends': main['end'],
        },
        'zlib_totals': totals, 'zlib_by_load': per_load,
        'note': 'gz_buffer_file counters are cumulative per opened file, so the savegame is '
                'the file with the most calls; loading_metric rows are per-window deltas '
                'and are summed',
    }


# ---- 6b. TAA health and the readback checks ----------------------------------------

def taa_health(health=None, taa=None, readback=None):
    """Headline TAA and readback numbers lifted from the iteration-9/10 reports.

    Nothing is recomputed: ``analyze_iteration09.py`` produces the resolve and
    camera health, ``analyze_iteration09_run2.py`` the hook and blur report and
    ``analyze_motion_readback.py`` the pixel checks.  Only the fields the
    iteration-10 tables carried are copied here so the tracked summary answers
    the question without the multi-megabyte inputs.
    """
    out = {}
    if health:
        core = health.get('health') or {}
        totals = core.get('totals') or {}
        camera = health.get('camera') or {}
        routed, matched = totals.get('routed'), totals.get('matched')
        out['resolves'] = {'records': core.get('frame_records'),
                           'attempted': core.get('taa_attempted'),
                           'resolved': core.get('taa_resolved'),
                           'history': core.get('taa_history'),
                           'skip_reasons': core.get('taa_skip'),
                           'resolved_without_history': core.get('resolved_without_history'),
                           'apply_failures': core.get('apply_failures'),
                           'restore_failures': core.get('restore_failures')}
        out['history_match'] = {
            'routed': routed, 'matched': matched,
            'rate': round(matched / routed, 6) if routed else None,
            'definition': 'matched / routed summed over motion_output_frame records'}
        out['gates'] = {key: totals.get(key) for key in
                        ('draws', 'routed', 'matched', 'gate1', 'gate2', 'gate3', 'gate4',
                         'gate5', 'gate6')}
        out['cuts'] = {'cut_events': core.get('cut_events'),
                       'camera_cut_events': core.get('camera_cut_events')}
        out['camera'] = {'records': camera.get('records'), 'valid': camera.get('valid'),
                         'policy': camera.get('policy'), 'reason': camera.get('reason'),
                         'camera_cuts': camera.get('camera_cuts'),
                         'read_failures': camera.get('read_failures'),
                         'rotation_deg': camera.get('rotation_deg'),
                         'rotation_floor_deg': camera.get('rotation_floor_deg'),
                         'row_norm_deviation_max': camera.get('row_norm_deviation_max')}
    if taa:
        out['scene_hook_report'] = {key: (taa.get('scene_hook') or {}).get(key) for key in
                                    ('records', 'check_distribution', 'source_distribution',
                                     'resolved_by_source', 'latched_frames',
                                     'latched_all_agree', 'latched_draws_after_hook_max',
                                     'latched_outside_scene_total')}
        out['adjacency_timeline'] = {
            key: (taa.get('adjacency_timeline') or {}).get(key) for key in
            ('total_calls', 'total_adjacency_seconds')}
        out['adjacency_phases'] = (taa.get('adjacency_timeline') or {}).get('phases')
        out['mesh_cache_effect'] = (taa.get('mesh_cache') or {}).get('effect')
    if readback:
        out['readback'] = {
            'status': readback.get('status'),
            'failed_checks': readback.get('failed_checks'),
            'hard_errors': readback.get('hard_errors'),
            'captured_frames': readback.get('captured_frames'),
            'frames_with_readback': readback.get('frames_with_readback'),
            'checks': {name: {'status': entry.get('status'),
                              **{key: entry[key] for key in
                                 ('max_error_px', 'unexplained_fraction', 'max_displacement_px',
                                  'suspicious_fraction', 'valid_motion_without_depth',
                                  'clean_frames', 'frames', 'reason', 'note')
                                 if key in entry}}
                       for name, entry in (readback.get('checks') or {}).items()}}
    return out


# ---- 7. the loads ------------------------------------------------------------------

def label_loads(report, scan_records=None):
    """Name each loading gap from the phases the route records on either side.

    ``analyze_iteration09_cost`` already lists the gaps and the labelled phases
    between them; the label of a gap is the transition it performs.
    """
    gaps = report.get('loading_gaps') or []
    phases = report.get('phases') or []

    def phase_before(frame):
        chosen = None
        for phase in phases:
            if phase['frames'][1] <= frame:
                chosen = phase
        return chosen

    def phase_after(frame):
        for phase in phases:
            if phase['frames'][0] >= frame:
                return phase
        return None

    out = []
    for index, gap in enumerate(gaps):
        before = phase_before(gap['frame'])
        after = phase_after(gap['frame'])
        kind = 'unknown'
        before_label = None if before is None else before['label']
        after_label = None if after is None else after['label']
        if before_label in (None, 'unknown'):
            kind = 'startup_to_menu' if after_label == 'menu' else 'startup'
        elif before['label'] == 'menu' and after and after['label'] == 'scene':
            kind = 'save_load'
        elif before['label'] == 'scene' and after and after['label'] == 'scene':
            kind = 'sector_change'
        elif before['label'] == 'scene' and after and after['label'] == 'menu':
            kind = 'return_to_menu'
        elif after is None:
            kind = 'exit'
        out.append({'index': index, 'kind': kind, 'seconds': round(gap['gap_ms'] / 1000.0, 3),
                    'ends_frame': gap['frame'], 'ends_since_s': gap['since_s'],
                    'phase_before': None if before is None else before['label'],
                    'phase_after': None if after is None else after['label']})
    return {'gaps': out, 'total_seconds': round(sum(g['seconds'] for g in out), 3),
            'note': 'a gap length is the frame_normal window maximum that contains it '
                    '(analyze_iteration09_cost.segment): a bound on one Present-to-Present '
                    'interval, not a load timer'}


# ---- assembly ----------------------------------------------------------------------

def build(args):
    runs = dict(_split(item) for item in args.run)
    if args.primary not in runs:
        raise Malformed(f'--primary {args.primary} is not one of --run {sorted(runs)}')
    reports, kinds, metrics = {}, {}, {}
    for label, path in runs.items():
        reports[label] = analyze_run(path, label, device=args.device,
                                     bin_width=args.bin_width)
        kinds[label] = it10.line_kinds(path)
        metrics[label] = it10.metric_table(path)
    primary = args.primary
    comparisons = {}
    for label in runs:
        if label == primary:
            continue
        comparisons[f'{primary}:{label}'] = it09cost.compare(
            reports[primary], reports[label], bin_width=args.bin_width,
            min_bin_windows=args.min_bin_windows)
    if args.previous and args.route_off:
        comparisons[f'{args.previous}:{args.route_off}'] = it09cost.compare(
            reports[args.previous], reports[args.route_off], bin_width=args.bin_width,
            min_bin_windows=args.min_bin_windows)

    report = {
        'tool': 'analyze_iteration11.py',
        'runs': {label: {'path': str(path),
                         'configuration': reports[label]['configuration'],
                         'windows': reports[label]['windows'],
                         'draws_surrogate': reports[label]['draws_surrogate'],
                         'phases': reports[label]['phases'],
                         'phase_check': reports[label]['phase_check']['status'],
                         'scene_fast': (reports[label]['scene'].get('fast') or {}),
                         'menu': reports[label].get('menu', {}),
                         'frame_records_normal': reports[label]['frame_records']['normal'],
                         'line_kinds': len(kinds[label])}
                 for label, path in runs.items()},
        'line_kind_diff': it10.kind_diff(kinds[primary],
                                         [kinds[l] for l in runs if l != primary]),
        'metric_comparison': [it10.metric_comparison(metrics, primary, label)
                              for label in runs if label != primary],
        'route_cost_availability': route_cost_availability(runs[primary], args.device),
        'draws_surrogate_cross_check': {
            label: surrogate_cross_check(path, args.device)
            for label, path in runs.items() if label != primary},
        'draw_bins': {label: reports[label]['scene_draw_bins'] for label in runs},
        'comparisons': comparisons,
        'scene_hook': scene_hook(runs[primary], args.device),
        'engine_reads': engine_reads(kinds[primary]),
        'loads': label_loads(reports[primary]),
        'taa_health': taa_health(_load_json(args.health), _load_json(args.taa),
                                 _load_json(args.readback)),
    }
    report['gz_buffer'] = gz_buffer(runs[primary], report['loads'])
    if args.route_off and args.previous:
        new_cmp = comparisons.get(f'{primary}:{args.route_off}')
        old_cmp = comparisons.get(f'{args.previous}:{args.route_off}')
        report['route_recovery'] = route_recovery(new_cmp, old_cmp, reports[primary],
                                                  reports[args.previous])
        report['route_share'] = {
            primary: route_share(reports[primary], new_cmp),
            args.previous: route_share(reports[args.previous], old_cmp)}
    return report


def _load_json(path):
    return json.loads(Path(path).read_text()) if path else None


def _split(item, separator='='):
    if separator not in item:
        raise Malformed(f'expected LABEL{separator}VALUE, got {item!r}')
    label, _, value = item.partition(separator)
    return label, value


def fmt(value, digits=3):
    if value is None:
        return '-'
    if isinstance(value, float):
        return f'{value:.{digits}f}'
    return str(value)


def render_text(report):
    lines = ['iteration 11: review-25 build, direct engine reads, per-draw stamps off',
             '=' * 74]
    for label, run in report['runs'].items():
        mode = run['configuration']['motion_output']
        lines.append(f"-- {label} {run['path']}")
        lines.append(f"   route={mode.get('requested')} taa={mode.get('taa')} "
                     f"scene_hook={mode.get('scene_hook')} "
                     f"per_draw={run['draws_surrogate'].get('per_draw')} "
                     f"kinds={run['line_kinds']}")
        surrogate = run['draws_surrogate']
        lines.append(f"   draws surrogate {surrogate.get('status')}: "
                     f"{surrogate.get('windows_injected')} windows injected, "
                     f"{surrogate.get('windows_measured')} measured")
        scene = run['scene_fast']
        if scene.get('windows'):
            lines.append(f"   scene fast: {scene['windows']} windows, "
                         f"{fmt((scene.get('mean_ms') or {}).get('median'), 2)} ms at "
                         f"{fmt((scene.get('draws_per_frame') or {}).get('median'), 1)} "
                         f"draws/frame, route metrics "
                         f"{fmt((scene.get('route_exclusive_us_per_frame') or {}).get('median'), 1)} us")
        normal = run['frame_records_normal']
        if normal.get('status') == 'evaluated':
            lines.append(f"   frame records n={normal['records']}: draws "
                         f"{fmt(normal['draws']['median'], 1)} routed "
                         f"{fmt(normal['routed']['median'], 1)}, route exclusive "
                         f"{fmt(normal['route_exclusive_us']['median'], 1)} us/frame, "
                         f"fill {fmt(normal['fill_us']['median'], 1)} taa_run "
                         f"{fmt(normal['taa_run_us']['median'], 1)}")
    availability = report['route_cost_availability']
    lines += ['', f"-- route cost availability (per_draw={availability['telemetry_per_draw']})",
              f"   per-draw metrics absent: {', '.join(availability['per_draw_metrics_absent'])}",
              f"   per-draw frame fields unmeasured: {availability['per_draw_fields_unmeasured']} "
              f"(max {availability['per_draw_frame_fields_max_us']})"]
    for key, value in availability['per_frame_fields_us'].items():
        lines.append(f"   {key:<20s} median {fmt(value.get('median'), 1)} us/frame")
    diff = report['line_kind_diff']
    lines += ['', f"-- line kinds: {diff['kinds_new_run']} in the primary run, "
                  f"{diff['kinds_baseline_union']} in the baselines",
              f"   new: {[k['kind'] for k in diff['new_kinds']] or 'none'}",
              f"   missing: {[k['kind'] for k in diff['missing_kinds']] or 'none'}"]
    for name, comparison in report['comparisons'].items():
        fit = _slope(comparison)
        lines.append('')
        lines.append(f"-- {name} ({comparison.get('role')})")
        if fit.get('us_per_draw') is not None:
            lines.append(f"   scene {fit.get('bins')} bins over {fit.get('draws_span')} "
                         f"draws: {fmt(fit['us_per_draw'])} us/draw "
                         f"(origin {fmt((fit.get('origin_us_per_draw') or {}).get('median'))}), "
                         f"fixed {fmt(fit.get('fixed_ms_per_frame'), 2)} ms, sign p="
                         f"{fmt((fit.get('sign_test') or {}).get('p_two_sided'), 5)}")
        else:
            lines.append('   scene: no paired bins')
    recovery = report.get('route_recovery')
    if recovery:
        lines += ['', '-- route recovery (paired-bin slopes against the route-off run)']
        for name, entry in recovery['comparisons'].items():
            lines.append(f"   {name}: {entry['primary']} vs {entry['baseline']} "
                         f"{fmt(entry['us_per_draw'])} us/draw over {entry['bins']} bins")
        lines.append(f"   attributed us/draw {recovery.get('attributed_us_per_draw')}")
        for name, entry in recovery['diagnostics'].items():
            lines.append(f"   {name} diagnostics: profiler "
                         f"{fmt(entry['profiler_ms_per_frame'], 3)} ms/frame (fixed term), "
                         f"stamps {fmt(entry['stamp_ms_per_frame'], 3)} ms/frame = "
                         f"{fmt(entry['stamp_us_per_draw'])} us/draw at "
                         f"{fmt(entry['draws_per_frame'], 1)} draws")
        if 'recovered_us_per_draw' in recovery:
            lines.append(f"   net {recovery['net_us_per_draw']} us/draw -> recovered "
                         f"{fmt(recovery['recovered_us_per_draw'])} us/draw "
                         f"({fmt(recovery.get('recovered_fraction'), 4)} of the old net)")
    for label, share in (report.get('route_share') or {}).items():
        lines.append(f"   share {label}: frame {fmt(share['frame_ms_median'], 2)} ms at "
                     f"{fmt(share['draws_per_frame_median'], 1)} draws, route "
                     f"{fmt(share.get('route_ms_from_slope'), 2)} ms = "
                     f"{fmt(share.get('route_share_from_slope'), 4)} (metrics "
                     f"{fmt(share.get('route_metrics_share'), 4)})")
    hook = report['scene_hook']
    lines += ['', f"-- scene hook: {hook['records']} records, {hook['latched_records']} latched, "
                  f"{hook['resolves']} resolves",
              f"   check {hook['check_distribution']}; source {hook['source_distribution']}; "
              f"resolved_by_source {hook['resolved_by_source']}",
              f"   rs_resyncs {hook['rs_resyncs_distribution']}; draws_after_hook "
              f"{hook['draws_after_hook_distribution']}; hit rate "
              f"{fmt(hook['state_shadow_hit_rate'], 6)}",
              f"   failures {hook['route_failures']}; disagreement records "
              f"{len(hook['disagreement_records'])}; latched without verdict "
              f"{hook['latched_frames_without_verdict']}"]
    engine = report['engine_reads']
    lines += ['', f"-- engine reads: {engine['status']} {engine['kinds_present'] or ''}"]
    gz = report['gz_buffer']
    if gz.get('status') == 'evaluated':
        main = gz['savegame_file']
        lines += ['', f"-- gz buffer: {len(gz['files'])} files, savegame calls {main['calls']} "
                      f"({fmt(main['small_call_fraction'], 4)} small), served "
                      f"{fmt(main['served_mb'], 2)} MB in {main['real_reads']} real reads "
                      f"({fmt(main['calls_per_real_read'], 1)} calls/read, "
                      f"{fmt(main['mean_served_bytes_per_call'], 3)} bytes/call)"]
        for op, entry in sorted(gz['zlib_totals'].items()):
            lines.append(f"   {op:<10s} count {entry['count']} failures {entry['failures']} "
                         f"bytes {entry['bytes']} total {fmt(entry['total_us'], 1)} us "
                         f"max {fmt(entry['max_us'], 1)} us over {entry['windows']} windows")
        for name, entry in gz['zlib_by_load'].items():
            ops = ', '.join(f"{op} {v['count']} calls {fmt(v['total_us'] / 1e6, 3)} s"
                            for op, v in sorted(entry['ops'].items()))
            lines.append(f"   in load {name} {entry['window_s']}: {ops or 'no zlib calls'}")
    taa = report.get('taa_health') or {}
    if taa:
        lines.append('')
        resolves = taa.get('resolves') or {}
        if resolves:
            lines.append(f"-- taa: attempted {resolves.get('attempted')} resolved "
                         f"{resolves.get('resolved')} history {resolves.get('history')}, "
                         f"skip {resolves.get('skip_reasons')}, failures "
                         f"{resolves.get('apply_failures')}/{resolves.get('restore_failures')}")
        match = taa.get('history_match') or {}
        if match:
            lines.append(f"   history match {match.get('matched')}/{match.get('routed')} = "
                         f"{fmt(match.get('rate'), 6)}")
        camera = taa.get('camera') or {}
        if camera:
            lines.append(f"   camera {camera.get('records')} records, "
                         f"{camera.get('valid')} valid, cuts {camera.get('camera_cuts')}, "
                         f"rotation max {fmt((camera.get('rotation_deg') or {}).get('max'), 4)} "
                         f"deg, floor {camera.get('rotation_floor_deg')}")
        rb = taa.get('readback') or {}
        if rb:
            lines.append(f"   readback {rb.get('status')} failed={rb.get('failed_checks')} "
                         f"frames {rb.get('frames_with_readback')}/{rb.get('captured_frames')}")
            for name, entry in (rb.get('checks') or {}).items():
                lines.append(f"     {name:<24s} {entry.get('status')} "
                             + ' '.join(f"{k}={fmt(v, 4)}" for k, v in entry.items()
                                        if k not in ('status', 'note', 'reason')))
    loads = report['loads']
    lines += ['', f"-- loads: {fmt(loads['total_seconds'], 2)} s over {len(loads['gaps'])} gaps"]
    for gap in loads['gaps']:
        lines.append(f"   {gap['kind']:<16s} {fmt(gap['seconds'], 3)} s ends frame "
                     f"{gap['ends_frame']} at {fmt(gap['ends_since_s'], 1)} s "
                     f"({gap['phase_before']} -> {gap['phase_after']})")
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--run', action='append', default=[], metavar='LABEL=PATH',
                        help='a session log; repeat for each run')
    parser.add_argument('--primary', required=True, help='the label of the new run')
    parser.add_argument('--route-off', help='the label of the route-off baseline run')
    parser.add_argument('--previous', help='the label of the previous route-on run')
    parser.add_argument('--device', default='1')
    parser.add_argument('--bin-width', type=float, default=50.0)
    parser.add_argument('--min-bin-windows', type=int, default=2)
    parser.add_argument('--health', help='analyze_iteration09.py summary JSON of the '
                                         'primary run')
    parser.add_argument('--taa', help='analyze_iteration09_run2.py summary JSON of the '
                                      'primary run')
    parser.add_argument('--readback', help='analyze_motion_readback.py summary JSON of the '
                                           'primary run')
    parser.add_argument('--output', required=True)
    parser.add_argument('--text')
    args = parser.parse_args(argv)
    report = build(args)
    Path(args.output).write_text(json.dumps(report, indent=1, sort_keys=False))
    text = render_text(report)
    if args.text:
        Path(args.text).write_text(text)
    print(text)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
