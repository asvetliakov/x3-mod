#!/usr/bin/env python3
"""Iteration 12: post-resolve sharpen 0.5 and mip LOD bias -0.5 in flight.

One report from the run-10 session log (bottle X3, review-26 build), its
capture directory and the tracked tools' JSON outputs
(docs/verification/iteration-12.md):

* section 1 -- did the two features run: the announcement lines, the
  ``taa_sharpen``/``taa_copy`` flags of every ``motion_output_frame`` record,
  the mip-bias counters (sets/restores/biased draws/failures/left-on), the
  engine's own ``D3DSAMP_MIPMAPLODBIAS`` writes, the teardown summary line and
  the capture's ``sampler state=8`` reads (which cannot see the bias);
* section 3 -- the sampler-state traffic: ordinary against capture frames,
  sets per routed draw, and the interleave of routed and unrouted draws in
  the capture frames (maximal runs of consecutive routed ``motion_route``
  lines), which bounds the coalescing any policy can reach;
* section 2 -- the like-for-like blur table condensed from
  ``analyze_iteration09_run2.py`` reports of this run and of iteration 9 run 2,
  and the ideal-supersampling floor derived from them;
* section 2.4 -- the presented image: RCAS of the resolved FP16 readback in a
  row-vectorised pure-Python port of the fixture runner's double-precision
  reference (``run_motion_output.rcas_reference``; no numpy on this machine),
  or the DLL's own ``present_<device>_<frame>.bgra8`` readback when the capture
  has one (builds after review 26). Checked on it: channels outside the 3x3
  min/max of the unsharpened codes (the shader clamps: the contract is 0), the
  change against the unsharpened codes, the iteration-9 gradient-energy and
  edge-spread metrics with the edge-profile overshoot as the halo measure,
  local-contrast amplification at strong edges, and the frame-to-frame
  flicker classes of ``analyze_iteration08_taa`` on the presented image against
  the resolved one and against a baseline flicker report;
* section 4.2 -- the readback certification, from ``analyze_motion_readback``.

Every number is derived here from the inputs named in the report; nothing is
typed in. ``verification/analysis/test_iteration12.py`` covers the derivations
on synthetic logs/images and pins the published summary.
"""

import argparse
import json
import math
import statistics
import struct
import sys
from collections import Counter, OrderedDict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import analyze_iteration07_taa as it07  # noqa: E402
import analyze_iteration08_taa as it08  # noqa: E402
import analyze_iteration09_run2 as run2  # noqa: E402
from analyze_iteration07_taa import LUMA, fields, number, real  # noqa: E402

WIDTH, HEIGHT = 1280, 768
S_FALSE = '00000001'
# The limiter's largest lobe, the noise-detector floor and the guarded
# denominators of src/temporal/rcas.hlsl.
RCAS_LOBE_LIMIT = -0.1875
RCAS_LUMA_FLOOR = 1.0 / 256.0
RCAS_GUARD = 1.0 / 4096.0
# A strong edge for the halo measure: the unsharpened 3x3 luma range at or
# above this (about 51 codes).
STRONG_EDGE_RANGE = 0.2


class Malformed(Exception):
    pass


# ---- log scan ------------------------------------------------------------------------

def scan_log(path, device='1'):
    """One streaming pass over the session log: the records sections 1 and 3 need."""
    out = {
        'path': str(path), 'device': device, 'configuration': {}, 'frames': [],
        'readback_frames': set(), 'game_writes': [], 'summary_lines': [],
        'sharpen_failed_lines': 0, 'taa_failed_lines': 0,
        'sampler_state8': {'records': 0, 'nonzero_bias': 0},
        'route_flags': [],  # (frame, index, routed) of every motion_route line
        'kinds': Counter(),
    }
    for event, line in it07.stream_records(path):
        out['kinds'][event] += 1
        if event in ('motion_output_mode', 'motion_output_device', 'motion_output_taa',
                     'x3-modern-renderer', 'telemetry_start'):
            out['configuration'].setdefault(event, []).append(fields(line))
        elif event == 'motion_output_frame':
            f = fields(line)
            if f.get('device') == device:
                out['frames'].append(f)
        elif event == 'motion_output_readback':
            f = fields(line)
            if f.get('device') == device and number(f.get('frame')) is not None:
                out['readback_frames'].add(number(f.get('frame')))
        elif event == 'motion_output_mip_bias_game_write':
            out['game_writes'].append(fields(line))
        elif event == 'motion_output_mip_bias_summary':
            out['summary_lines'].append(fields(line))
        elif event == 'motion_output_sharpen_failed':
            out['sharpen_failed_lines'] += 1
        elif event == 'motion_output_taa_failed':
            out['taa_failed_lines'] += 1
        elif event == 'sampler':
            f = fields(line)
            if f.get('state') == '8':
                out['sampler_state8']['records'] += 1
                if number(f.get('value'), 0) != 0 or real(f.get('bias'), 0.0) != 0.0:
                    out['sampler_state8']['nonzero_bias'] += 1
        elif event == 'motion_route':
            f = fields(line)
            if f.get('device') == device:
                out['route_flags'].append((number(f.get('frame')), number(f.get('index')),
                                           f.get('routed') == '1'))
    return out


def configuration_report(scan):
    mode = (scan['configuration'].get('motion_output_mode') or [{}])[0]
    renderer = (scan['configuration'].get('x3-modern-renderer') or [{}])[0]
    telemetry = (scan['configuration'].get('telemetry_start') or [{}])[0]
    taa = scan['configuration'].get('motion_output_taa') or []
    features = OrderedDict()
    features['taa_sharpen'] = real(mode.get('taa_sharpen'))
    features['mip_bias'] = real(mode.get('mip_bias'))
    features['taa_debug'] = number(mode.get('taa_debug'))
    features['capture_frames'] = number(renderer.get('capture_frames'))
    features['per_draw'] = number(telemetry.get('per_draw'))
    announcement = {
        'mode': {k: mode.get(k) for k in ('taa', 'taa_debug', 'jitter', 'jitter_samples', 'scene_hook',
                                          'hdr', 'taa_k', 'mip_bias', 'taa_sharpen', 'rt_mode')},
        'taa': [{k: t.get(k) for k in ('device', 'initialize', 'references', 'sharpen')} for t in taa],
    }
    return features, announcement


# ---- section 1: the sharpen ------------------------------------------------------------

def median_or_none(values):
    return statistics.median(values) if values else None


def sharpen_report(scan):
    frames = scan['frames']
    resolved = [f for f in frames if f.get('taa_resolved') == '1']
    sharpened = [f for f in frames if f.get('taa_sharpen') == '1']
    report = OrderedDict()
    report['records'] = len(frames)
    report['sharpened_frames'] = len(sharpened)
    report['resolves'] = len(resolved)
    report['sharpened_resolved_frames'] = sum(1 for f in resolved if f.get('taa_sharpen') == '1')
    report['sharpened_unresolved_frames'] = sum(1 for f in sharpened if f.get('taa_resolved') != '1')
    report['taa_copy_S_FALSE_records'] = sum(1 for f in frames if f.get('taa_copy') == S_FALSE)
    report['taa_copy_histogram'] = dict(Counter(f.get('taa_copy') for f in frames))
    report['sharpen_failed_lines'] = scan['sharpen_failed_lines']
    report['taa_failed_lines'] = scan['taa_failed_lines']
    report['taa_copy_back_us_median'] = median_or_none([real(f.get('taa_copy_back_us'), 0.0) for f in resolved])
    report['taa_run_us_median'] = median_or_none([real(f.get('taa_run_us'), 0.0) for f in resolved])
    report['taa_draw_us_median'] = median_or_none([real(f.get('taa_draw_us'), 0.0) for f in resolved])
    report['taa_history_frames'] = sum(1 for f in resolved if f.get('taa_history') == '1')
    return report


# ---- section 1/3: the mip bias ----------------------------------------------------------

def routed_runs(route_flags):
    """Maximal runs of consecutive routed `motion_route` lines within a frame.

    The lines are the routed draws and the gate-3/4/5 rejects of the capture
    frames, in draw order; an unrouted line between two routed ones ends a run
    (that is where the bias has to be restored and re-set)."""
    runs = 0
    previous = None
    for frame, index, routed in route_flags:
        if routed and not (previous is not None and previous[0] == frame and previous[2]):
            runs += 1
        previous = (frame, index, routed)
    return runs


def traffic(records):
    sets = sum(number(f.get('mip_bias_sets'), 0) for f in records)
    restores = sum(number(f.get('mip_bias_restores'), 0) for f in records)
    biased = sum(number(f.get('mip_bias_draws'), 0) for f in records)
    draws = sum(number(f.get('draws'), 0) for f in records)
    return OrderedDict([
        ('records', len(records)), ('sets', sets), ('restores', restores), ('biased_draws', biased),
        ('draws', draws),
        ('sets_per_biased_draw', sets / biased if biased else None),
        ('sets_plus_restores_per_biased_draw', (sets + restores) / biased if biased else None),
        ('set_calls_per_frame', (sets + restores) / len(records) if records else None),
    ])


def mip_bias_report(scan):
    frames = scan['frames']
    report = OrderedDict()
    report['records'] = len(frames)
    report['bias_values'] = sorted({f.get('mip_bias') for f in frames if f.get('mip_bias') is not None})
    report['sets'] = sum(number(f.get('mip_bias_sets'), 0) for f in frames)
    report['restores'] = sum(number(f.get('mip_bias_restores'), 0) for f in frames)
    report['biased_draws'] = sum(number(f.get('mip_bias_draws'), 0) for f in frames)
    report['routed_draws'] = sum(number(f.get('routed'), 0) for f in frames)
    report['failures'] = sum(number(f.get('mip_bias_failures'), 0) for f in frames)
    report['biased_now_nonzero_records'] = sum(
        1 for f in frames if f.get('mip_bias_biased_now') not in (None, '0', '0000'))
    report['reads'] = sum(number(f.get('mip_bias_reads'), 0) for f in frames)
    report['reads_frames'] = [number(f.get('frame')) for f in frames if number(f.get('mip_bias_reads'), 0)]
    totals = [number(f.get('mip_bias_game_writes_total'), 0) for f in frames]
    report['game_writes_total'] = max(totals) if totals else 0
    report['game_write_lines'] = len(scan['game_writes'])
    report['game_write_stages'] = sorted({number(w.get('stage')) for w in scan['game_writes']})
    report['game_write_values'] = sorted({real(w.get('bias'), 0.0) for w in scan['game_writes']})
    report['game_write_frames'] = sorted({number(w.get('frame')) for w in scan['game_writes']})
    report['summary_line_present'] = bool(scan['summary_lines'])
    report['summary_lines'] = scan['summary_lines']
    report['stage_mask_histogram'] = OrderedDict(sorted(Counter(f.get('mip_bias_stages') for f in frames).items()))
    report['capture_sampler_state8_records'] = scan['sampler_state8']['records']
    report['capture_sampler_state8_nonzero_bias'] = scan['sampler_state8']['nonzero_bias']
    # Ordinary against capture frames: a capture frame's per-draw diagnostics
    # restore the bias before every draw, so every routed draw re-sets it.
    routed_records = [f for f in frames if number(f.get('routed'), 0) > 0]
    capture = [f for f in routed_records if number(f.get('frame')) in scan['readback_frames']]
    ordinary = [f for f in routed_records if number(f.get('frame')) not in scan['readback_frames']]
    report['ordinary_frames'] = traffic(ordinary)
    report['capture_frames'] = traffic(capture)
    capture_numbers = {number(f.get('frame')) for f in capture}
    flags = [r for r in scan['route_flags'] if r[0] in capture_numbers]
    routed = sum(1 for r in flags if r[2])
    runs = routed_runs(flags)
    report['interleave'] = OrderedDict([
        ('source', f'motion_route lines of the {len(capture)} capture frames'),
        ('draws', len(flags)), ('routed', routed), ('runs_of_consecutive_routed', runs),
        ('mean_routed_per_run', routed / runs if runs else None),
        ('runs_per_frame', runs / len(capture) if capture else None),
    ])
    sets_per_draw = report['capture_frames']['sets_per_biased_draw']
    ordinary_per_draw = report['ordinary_frames']['sets_per_biased_draw']
    if sets_per_draw and ordinary_per_draw and runs:
        # A capture frame sets once per routed draw, so its sets/draw is the mean
        # number of biased stages per draw; the interleave's lower bound for an
        # ordinary frame is one such set group per run.
        report['interleave']['stages_per_set_group'] = sets_per_draw
        report['interleave']['ordinary_draws_per_set_group'] = sets_per_draw / ordinary_per_draw
        report['interleave']['excess_over_interleave_bound'] = (
            (routed / runs) / (sets_per_draw / ordinary_per_draw))
    return report


# ---- section 2: blur table -----------------------------------------------------------------

def span(values):
    values = [v for v in values if v is not None]
    return [min(values), max(values)] if values else None


def condense_burst(entry):
    """One row of the like-for-like table from an analyze_iteration09_run2 burst entry."""
    frames = entry.get('frames') or []
    row = OrderedDict()
    row['frames'] = [frames[0], frames[-1]] if frames else []
    row['status'] = entry.get('status')
    route = entry.get('route') or {}
    if route:
        row['motion'] = route.get('motion')
        row['cut_median_px_peak'] = route.get('cut_median_px_peak')
        rotations = route.get('camera_rotation_deg') or []
        row['camera_rotation_deg_max'] = max(rotations) if rotations else None
    if entry.get('status') != 'evaluated':
        if entry.get('missing'):
            row['missing'] = entry['missing']
        return row
    row['routed_interior_px'] = (entry.get('classes') or {}).get('routed_interior')
    per_frame = entry.get('per_frame') or []
    interior = [f['ratios']['routed_interior']['gradient_energy_ratio'] for f in per_frame]
    everything = [f['ratios']['all']['gradient_energy_ratio'] for f in per_frame]
    row['gradient_energy_ratio_interior'] = {'min': min(interior), 'max': max(interior),
                                             'mean': statistics.fmean(interior)} if interior else None
    row['gradient_energy_ratio_all_mean'] = statistics.fmean(everything) if everything else None
    for label in ('raw', 'resolved'):
        spreads = [(f.get('edge_spread') or {}).get(label) or {} for f in per_frame]
        row[f'{label}_rise_10_90_px'] = span([s.get('rise_10_90_px') for s in spreads])
        row[f'{label}_mtf50_cycles_per_px'] = span([s.get('mtf50_cycles_per_px') for s in spreads])
    spread = entry.get('jitter_phase_spread') or {}
    row['raw_phase_spread'] = {'all': spread.get('relative_spread_all'),
                               'routed_interior': spread.get('relative_spread_routed_interior')}
    captured = entry.get('captured_phase_average') or {}
    ideal = OrderedDict()
    ratios = (captured.get('ratios') or {}).get('routed_interior') or {}
    ideal['four_phase_ratio'] = ratios.get('gradient_energy_ratio')
    ideal['scale_to_resolve_kernel'] = captured.get('scale_to_resolve_kernel')
    if ideal['four_phase_ratio'] is not None and ideal['scale_to_resolve_kernel'] is not None:
        floor = ideal['four_phase_ratio'] * ideal['scale_to_resolve_kernel']
        measured = row['gradient_energy_ratio_interior']['mean'] if interior else None
        ideal['floor'] = floor
        if measured is not None:
            ideal['measured'] = measured
            ideal['measured_over_floor'] = measured / floor if floor else None
            ideal['share_of_loss_ideal'] = (1.0 - floor) / (1.0 - measured) if measured < 1.0 else None
    row['ideal'] = ideal
    return row


def blur_tables(reports):
    """{label: analyze_iteration09_run2 report} -> {label: [condensed rows]}."""
    out = OrderedDict()
    for label, report in reports.items():
        out[label] = [condense_burst(entry) for entry in report.get('blur', [])]
    return out


# ---- section 2.4: the presented image ------------------------------------------------------

def load_rgb_bgra8(path, width, height):
    """Row-major A8R8G8B8 -> three lists of rows of floats in [0, 1]."""
    data = path.read_bytes()
    if len(data) != width * height * 4:
        raise Malformed(f'{path.name}: {len(data)} bytes != {width}x{height}x4')
    scale = 1.0 / 255.0
    channels = ([], [], [])
    stride = width * 4
    for y in range(height):
        row = data[y * stride:(y + 1) * stride]
        channels[0].append([v * scale for v in row[2::4]])
        channels[1].append([v * scale for v in row[1::4]])
        channels[2].append([v * scale for v in row[0::4]])
    return channels


def load_codes_bgra8(path, width, height):
    """Row-major A8R8G8B8 -> three lists of rows of 8-bit codes (R, G, B)."""
    data = path.read_bytes()
    if len(data) != width * height * 4:
        raise Malformed(f'{path.name}: {len(data)} bytes != {width}x{height}x4')
    stride = width * 4
    channels = ([], [], [])
    for y in range(height):
        row = data[y * stride:(y + 1) * stride]
        channels[0].append(list(row[2::4]))
        channels[1].append(list(row[1::4]))
        channels[2].append(list(row[0::4]))
    return channels


def load_rgb_rgba16f(path, width, height):
    """Row-major RGBA binary16 -> three lists of rows of floats (struct 'e', native decode)."""
    data = path.read_bytes()
    if len(data) != width * height * 8:
        raise Malformed(f'{path.name}: {len(data)} bytes != {width}x{height}x8')
    values = struct.unpack(f'<{width * height * 4}e', data)
    channels = ([], [], [])
    stride = width * 4
    for y in range(height):
        row = values[y * stride:(y + 1) * stride]
        channels[0].append(list(row[0::4]))
        channels[1].append(list(row[1::4]))
        channels[2].append(list(row[2::4]))
    return channels


def saturate_rows(rows):
    return [[0.0 if v != v else (0.0 if v < 0.0 else (1.0 if v > 1.0 else v)) for v in row] for row in rows]


def rcas_gain(sharpen):
    """X3M_TAA_SHARPEN -> gain exp2(-stops), stops = 2 (1 - s) (src/temporal/sharpen.h)."""
    return 2.0 ** (-2.0 * (1.0 - sharpen))


def rcas_rows(channels, width, height, gain):
    """RCAS of a display-referred RGB image given as three lists of rows, in
    double, row-vectorised: the arithmetic of src/temporal/rcas.hlsl and of
    run_motion_output.rcas_reference (saturated five-tap cross, luma noise
    detector, guarded peak-range limiter, one lobe scaled by `gain`, clamp to
    the taps' own min/max), clamp-addressed at the borders."""
    r, g, b = (saturate_rows(c) for c in channels)
    out = ([], [], [])
    floor, guard, limit = RCAS_LUMA_FLOOR, RCAS_GUARD, RCAS_LOBE_LIMIT
    for y in range(height):
        yb, yh = max(y - 1, 0), min(y + 1, height - 1)
        rows = []
        for c in (r, g, b):
            e = c[y]
            rows.append({'b': c[yb], 'd': [e[0]] + e[:-1], 'e': e, 'f': e[1:] + [e[-1]], 'h': c[yh]})
        lumas = {}
        for tap in 'bdefh':
            lumas[tap] = [0.5 * rv + gv + 0.5 * bv for rv, gv, bv in zip(rows[0][tap], rows[1][tap], rows[2][tap])]
        nz = [1.0 - 0.5 * min(max(abs(0.25 * (bl + dl + fl + hl) - el)
                                  / max(max(bl, dl, el, fl, hl) - min(bl, dl, el, fl, hl), floor), 0.0), 1.0)
              for bl, dl, el, fl, hl in zip(lumas['b'], lumas['d'], lumas['e'], lumas['f'], lumas['h'])]
        mn4, mx4, lobes = [], [], []
        for t in rows:
            mn = [min(bv, dv, fv, hv) for bv, dv, fv, hv in zip(t['b'], t['d'], t['f'], t['h'])]
            mx = [max(bv, dv, fv, hv) for bv, dv, fv, hv in zip(t['b'], t['d'], t['f'], t['h'])]
            mn4.append(mn); mx4.append(mx)
            lobes.append([max(-(lo / max(4.0 * hi, guard)), (1.0 - hi) / min(4.0 * lo - 4.0, -guard))
                          for lo, hi in zip(mn, mx)])
        lobe = [max(limit, min(max(l0, l1, l2), 0.0)) * gain * n
                for l0, l1, l2, n in zip(lobes[0], lobes[1], lobes[2], nz)]
        for c in range(3):
            t = rows[c]
            out[c].append([min(max(((bv + dv + fv + hv) * lb + ev) / (4.0 * lb + 1.0), min(lo, ev)), max(hi, ev))
                           for bv, dv, ev, fv, hv, lb, lo, hi
                           in zip(t['b'], t['d'], t['e'], t['f'], t['h'], lobe, mn4[c], mx4[c])])
    return out


def quantise_rows(rows):
    """Display values in [0, 1] -> 8-bit codes, round half up (the fixture runner's rounding)."""
    return [[int(255.0 * (0.0 if v != v or v < 0.0 else (1.0 if v > 1.0 else v)) + 0.5) for v in row] for row in rows]


def luma_rows(channels):
    lr, lg, lb = LUMA
    return [[lr * rv + lg * gv + lb * bv for rv, gv, bv in zip(rr, gr, br)]
            for rr, gr, br in zip(*channels)]


def flatten(rows, typecode='f'):
    import array
    out = array.array(typecode)
    for row in rows:
        out.extend(row)
    return out


def neighbourhood_extremes(rows, width, height):
    """Per pixel the min and max over the clamp-addressed 3x3 neighbourhood (rows of numbers)."""
    horizontal_min, horizontal_max = [], []
    for row in rows:
        left = [row[0]] + row[:-1]
        right = row[1:] + [row[-1]]
        horizontal_min.append([min(a, b, c) for a, b, c in zip(left, row, right)])
        horizontal_max.append([max(a, b, c) for a, b, c in zip(left, row, right)])
    mins, maxs = [], []
    for y in range(height):
        yb, yh = max(y - 1, 0), min(y + 1, height - 1)
        mins.append([min(a, b, c) for a, b, c in zip(horizontal_min[yb], horizontal_min[y], horizontal_min[yh])])
        maxs.append([max(a, b, c) for a, b, c in zip(horizontal_max[yb], horizontal_max[y], horizontal_max[yh])])
    return mins, maxs


def escapes_3x3(codes, reference_codes, width, height):
    """Channels of `codes` outside the 3x3 min/max of `reference_codes` (both
    (R, G, B) rows of codes): the count and, per pixel, rows of the number of
    escaping channels."""
    outside = 0
    escapes = [[0] * width for _ in range(height)]
    for c in range(3):
        mins, maxs = neighbourhood_extremes(reference_codes[c], width, height)
        for row, lo, hi, flags in zip(codes[c], mins, maxs, escapes):
            for x, (v, a, b) in enumerate(zip(row, lo, hi)):
                if v < a or v > b:
                    outside += 1
                    flags[x] += 1
    return outside, escapes


def code_change(codes, reference_codes):
    """Pixels changed, the largest and the mean absolute per-channel code difference."""
    changed = 0
    largest = 0
    total = 0
    count = 0
    for rows_a, rows_b, rows_c, ref_a, ref_b, ref_c in zip(*codes, *reference_codes):
        for a, b, c, ra, rb, rc in zip(rows_a, rows_b, rows_c, ref_a, ref_b, ref_c):
            da, db, dc = abs(a - ra), abs(b - rb), abs(c - rc)
            if da or db or dc:
                changed += 1
            m = max(da, db, dc)
            if m > largest:
                largest = m
            total += da + db + dc
            count += 3
    return {'changed_pixels': changed, 'max_code_change': largest,
            'mean_abs_code_change': total / count if count else None}


def code_error(codes, reference_values):
    """Largest and mean |code - 255 * reference| over the three channels (reference rows of floats in [0, 1])."""
    largest = 0.0
    total = 0.0
    count = 0
    for c in range(3):
        for row, ref in zip(codes[c], reference_values[c]):
            for v, x in zip(row, ref):
                e = abs(v - 255.0 * (0.0 if x < 0.0 else (1.0 if x > 1.0 else x)))
                if e > largest:
                    largest = e
                total += e
                count += 1
    return {'max_code_error': largest, 'mean_code_error': total / count if count else None}


def strong_edge_amplification(reference_luma, image_luma, width, height, escapes=None,
                              threshold=STRONG_EDGE_RANGE, margin=2):
    """At pixels whose unsharpened 3x3 luma range is >= `threshold` (a strong
    edge): the mean 3x3 luma range of `image_luma` over that of
    `reference_luma` (local-contrast amplification; RCAS clamps every channel
    to its cross, so values near 1 are expected) and, from `escapes` (the
    per-pixel channel-escape rows of `escapes_3x3`), the channels that left the
    reference's per-channel 3x3 range at those pixels: a halo past the
    neighbourhood, whose contract is 0. Luma itself is not clamped (the
    channel extremes come from different neighbours), so the escape is judged
    per channel, never on the luma."""
    ref_min, ref_max = neighbourhood_extremes(reference_luma, width, height)
    img_min, img_max = neighbourhood_extremes(image_luma, width, height)
    pixels = 0
    reference_range = 0.0
    image_range = 0.0
    outside = 0
    for y in range(margin, height - margin):
        rmin, rmax, imin, imax = ref_min[y], ref_max[y], img_min[y], img_max[y]
        flags = escapes[y] if escapes is not None else None
        for x in range(margin, width - margin):
            rng = rmax[x] - rmin[x]
            if rng >= threshold:
                pixels += 1
                reference_range += rng
                image_range += imax[x] - imin[x]
                if flags is not None:
                    outside += flags[x]
    return {'threshold_luma_range': threshold, 'pixels': pixels,
            'local_range_ratio': image_range / reference_range if reference_range else None,
            'channel_escapes_3x3': outside}


def esf_overshoot(spread, window=2.0):
    """Excursion of an edge_spread ESF (each profile normalised 0..1 by its own
    end values and aligned on its 50% crossing) within `window` px of the
    crossing: how far the mean profile exceeds 1 or undershoots 0, in units
    of the edge contrast. Read as a comparison between images of the same
    edges (a sharpen that rings shows a larger excursion than the image it
    sharpened); the far tails, fed by few profiles per bin, are excluded."""
    knots = (spread or {}).get('esf_knots') or []
    values = [v for x, v in knots if abs(x) <= window]
    if not values:
        return None
    return {'above_one': max(0.0, max(values) - 1.0), 'below_zero': max(0.0, -min(values)),
            'window_px': window, 'edges': spread.get('edges')}


def class_indices(classes):
    buckets = {name: [] for name in it08.CLASSES}
    for index, code in enumerate(classes):
        buckets[it08.CLASSES[code]].append(index)
    return buckets


def flicker_classes(raw_variance, resolved_variance, presented_variance, classes, thin, options):
    """analyze_iteration08_taa's class statistics of the resolved and of the
    presented image against the raw one, plus presented against resolved."""
    buckets = class_indices(classes)
    total_resolved = math.fsum(resolved_variance)
    total_presented = math.fsum(presented_variance)
    report = OrderedDict()
    report['total_raw_variance_energy'] = math.fsum(raw_variance)
    report['total_resolved_variance_energy'] = total_resolved
    report['total_presented_variance_energy'] = total_presented
    report['presented_over_resolved_energy'] = total_presented / total_resolved if total_resolved else None
    report['classes'] = OrderedDict()
    names = list(it08.CLASSES) + ['thin_feature']
    buckets['thin_feature'] = [i for i, v in enumerate(thin) if v]
    for name in names:
        indices = buckets[name]
        entry = OrderedDict()
        entry['pixels'] = len(indices)
        entry['resolved'] = it08.variance_statistics(indices, raw_variance, resolved_variance,
                                                     options['variance_floor'], total_resolved)
        entry['presented'] = it08.variance_statistics(indices, raw_variance, presented_variance,
                                                      options['variance_floor'], total_presented)
        resolved_sum = math.fsum(resolved_variance[i] for i in indices)
        presented_sum = math.fsum(presented_variance[i] for i in indices)
        entry['presented_over_resolved'] = presented_sum / resolved_sum if resolved_sum else None
        entry['aggregate_ratio'] = {'resolved': entry['resolved'].get('aggregate_ratio'),
                                    'presented': entry['presented'].get('aggregate_ratio')}
        if name == 'thin_feature':
            entry['overlay'] = True
        report['classes'][name] = entry
    return report


def baseline_flicker(baseline):
    """Class aggregate ratios of a tracked analyze_iteration08_taa report's first evaluated flicker burst."""
    if not baseline:
        return None
    flicker = baseline.get('flicker') or {}
    for burst in flicker.get('bursts') or []:
        analysis = burst.get('analysis') or {}
        if analysis.get('status') == 'evaluated':
            classes = analysis.get('classes') or {}
            return {'frames': analysis.get('frames'), 'cut_median_px': burst.get('cut_median_px'),
                    'log': (baseline.get('source') or {}).get('log'),
                    'aggregate_ratio': {name: (classes.get(name) or {}).get('aggregate_ratio') for name in classes}}
    return {'status': flicker.get('status', 'unavailable'), 'reason': flicker.get('reason'),
            'log': (baseline.get('source') or {}).get('log')}


def select_model_bursts(bursts, explicit, motions=('stationary', 'slow')):
    selected = []
    for records in bursts:
        first = number(records[0].get('frame'))
        if explicit:
            if first in explicit:
                selected.append(records)
            continue
        if run2.classify_burst(records)['motion'] in motions:
            selected.append(records)
    return selected


def analyze_presented_burst(records, captures, device, sharpen, options, log=None):
    """Section 2.4 for one burst: the RCAS-modelled (or read-back) presented image."""
    width, height = options['width'], options['height']
    numbers = [number(r.get('frame')) for r in records]
    gain = rcas_gain(sharpen)
    entry = OrderedDict()
    entry['frames'] = numbers
    entry['route'] = run2.classify_burst(records)
    entry['sharpen'] = sharpen
    entry['gain'] = gain
    paths = {n: {'colour': captures / f'color_{device}_{n}.bgra8',
                 'resolved': captures / f'taa_{device}_{n}.rgba16f',
                 'depth': captures / f'depth_{device}_{n}.r32f',
                 'motion': captures / f'motion_{device}_{n}.rgba32f',
                 'present': captures / f'present_{device}_{n}.bgra8'} for n in numbers}
    missing = [str(p) for n in numbers for k, p in paths[n].items() if k != 'present' and not p.exists()]
    if missing:
        entry['status'] = 'missing_readbacks'
        entry['missing'] = missing[:8]
        return entry
    entry['status'] = 'evaluated'
    depths = [it08.load_depth_r32f(paths[n]['depth'], width, height) for n in numbers]
    alphas = [it08.load_motion_alpha(paths[n]['motion'], width, height) for n in numbers]
    classes = it08.classify_pixels(depths, alphas, options['depth_tolerance'])
    del depths, alphas
    counts = Counter(classes)
    entry['classes'] = {name: counts.get(index, 0) for index, name in enumerate(it08.CLASSES)}
    raw_lumas, resolved_lumas, presented_lumas = [], [], []
    thin = bytearray(width * height)
    per_frame = []
    for n in numbers:
        if log:
            log(f'  frame {n}: loading')
        raw = load_rgb_bgra8(paths[n]['colour'], width, height)
        resolved = load_rgb_rgba16f(paths[n]['resolved'], width, height)
        if log:
            log(f'  frame {n}: rcas')
        modelled = rcas_rows(resolved, width, height, gain)
        unsharpened_codes = tuple(quantise_rows(c) for c in resolved)
        modelled_codes = tuple(quantise_rows(c) for c in modelled)
        item = OrderedDict()
        item['frame'] = n
        item['source'] = 'rcas_model'
        present_codes = modelled_codes
        presented_values = modelled
        if paths[n]['present'].exists():
            # The DLL's own readback of the main target after the sharpen draw:
            # the presented image itself; the model becomes its reference.
            item['source'] = 'present_readback'
            present_codes = load_codes_bgra8(paths[n]['present'], width, height)
            presented_values = tuple([[v / 255.0 for v in row] for row in c] for c in present_codes)
            item['readback_vs_model'] = code_error(present_codes, modelled)
        item['escapes_3x3'], escape_rows = escapes_3x3(present_codes, unsharpened_codes, width, height)
        item['change_vs_unsharpened'] = code_change(present_codes, unsharpened_codes)
        item['change_vs_unsharpened']['changed_fraction'] = item['change_vs_unsharpened']['changed_pixels'] / (width * height)
        raw_luma = flatten(luma_rows(raw))
        resolved_luma = flatten(luma_rows(resolved))
        presented_luma_rows = luma_rows(presented_values)
        presented_luma = flatten(presented_luma_rows)
        raw_lumas.append(raw_luma); resolved_lumas.append(resolved_luma); presented_lumas.append(presented_luma)
        if log:
            log(f'  frame {n}: metrics')
        item['gradient'] = OrderedDict()
        energies = {}
        for label, image in (('raw', raw_luma), ('resolved', resolved_luma), ('presented', presented_luma)):
            energies[label] = run2.class_gradient_energy(image, classes, width, height, options['margin'], 'gradient')
        for cls in ('routed_interior', 'routed_edge', 'all'):
            raw_g = energies['raw'][cls]['mean']
            res_g = energies['resolved'][cls]['mean']
            pre_g = energies['presented'][cls]['mean']
            item['gradient'][cls] = {'resolved_over_raw': res_g / raw_g if raw_g else None,
                                     'presented_over_raw': pre_g / raw_g if raw_g else None,
                                     'presented_over_resolved': pre_g / res_g if res_g else None}
        if options['edges']:
            spreads = OrderedDict()
            for label, image in (('raw', raw_luma), ('resolved', resolved_luma), ('presented', presented_luma)):
                s = run2.edge_spread(image, classes, width, height, it08.INTERIOR, margin=options['margin'])
                spreads[label] = {'edges': s.get('edges'), 'rise_10_90_px': s.get('rise_10_90_px'),
                                  'mtf50_cycles_per_px': s.get('mtf50_cycles_per_px'),
                                  'overshoot': esf_overshoot(s)}
            item['edge_spread'] = spreads
        item['strong_edges'] = strong_edge_amplification(luma_rows(resolved), presented_luma_rows, width, height,
                                                          escape_rows, options['strong_edge_range'])
        del escape_rows
        mask = it08.thin_feature_mask(raw_luma, width, height, options['tophat_min'], options['contrast_min'])
        for index, value in enumerate(mask):
            if value:
                thin[index] = 1
        per_frame.append(item)
        del raw, resolved, modelled, unsharpened_codes, modelled_codes, present_codes, presented_values
    entry['per_frame'] = per_frame
    entry['escapes_3x3_total'] = sum(f['escapes_3x3'] for f in per_frame)
    entry['sources'] = sorted({f['source'] for f in per_frame})
    if len(numbers) >= 2:
        if log:
            log('  flicker')
        raw_variance = it08.temporal_variance(raw_lumas)
        resolved_variance = it08.temporal_variance(resolved_lumas)
        presented_variance = it08.temporal_variance(presented_lumas)
        entry['flicker'] = flicker_classes(raw_variance, resolved_variance, presented_variance, classes, thin, options)
        entry['flicker']['stationary_gate_0_01_px'] = entry['route']['cut_median_px_peak'] <= 0.01
    return entry


# ---- section 4.2: readback checks ----------------------------------------------------------

def readback_checks(summary):
    checks = OrderedDict()
    for name, check in (summary.get('checks') or {}).items():
        checks[name] = OrderedDict((k, v) for k, v in check.items()
                                   if isinstance(v, (str, int, float, bool)) or v is None)
    return checks


# ---- assembly ---------------------------------------------------------------------------------

def load_json(path):
    return json.loads(Path(path).read_text()) if path else None


def build(args, log=None):
    scan = scan_log(args.log, args.device)
    if not scan['frames']:
        raise Malformed('log contains no motion_output_frame record for the device')
    features, announcement = configuration_report(scan)
    report = OrderedDict()
    report['tool'] = 'tools/analysis/analyze_iteration12.py'
    report['log'] = str(args.log)
    report['log_sha256'] = None if args.no_hash else it07.sha256_of(args.log)
    report['build'] = OrderedDict([('commit', args.build_commit), ('dll_sha256_prefix', args.dll_sha256_prefix),
                                   ('bottle', args.bottle)])
    report['features'] = features
    report['announcement'] = announcement
    report['line_kinds'] = len(scan['kinds'])
    report['failure_kinds'] = sorted(k for k in scan['kinds']
                                     if any(w in k for w in ('fail', 'error', 'warn', 'fault', 'reset', 'unwind', 'veto', 'disagree')))
    report['sharpen'] = sharpen_report(scan)
    if args.previous_log:
        previous = scan_log(args.previous_log, args.device)
        report['sharpen']['taa_copy_back_us_median_previous'] = sharpen_report(previous)['taa_copy_back_us_median']
        report['sharpen']['previous_log'] = str(args.previous_log)
        # Kept under the name the hand-off report used for the run-9 column.
        report['sharpen']['taa_copy_back_us_median_run9'] = report['sharpen']['taa_copy_back_us_median_previous']
    report['mip_bias'] = mip_bias_report(scan)
    taa_reports = OrderedDict()
    if args.taa:
        taa_reports['run10'] = load_json(args.taa)
    if args.taa_baseline:
        taa_reports['iteration09_run2'] = load_json(args.taa_baseline)
    report['blur'] = blur_tables(taa_reports)
    captures = args.captures or args.log.parent
    bursts = run2.burst_frames(scan['frames'], readback_frames=scan['readback_frames'] or None)
    report['bursts'] = [{'frames': [number(r.get('frame')) for r in records], 'route': run2.classify_burst(records)}
                        for records in bursts]
    if args.no_model:
        report['presented'] = {'status': 'skipped'}
    else:
        options = {'width': args.width, 'height': args.height, 'margin': args.margin,
                   'depth_tolerance': args.depth_tolerance, 'variance_floor': args.variance_floor,
                   'tophat_min': args.thin_tophat, 'contrast_min': args.thin_contrast,
                   'strong_edge_range': args.strong_edge_range, 'edges': not args.no_edges}
        sharpen = args.sharpen if args.sharpen is not None else (features['taa_sharpen'] or 0.0)
        selected = select_model_bursts(bursts, args.model_burst)
        results = []
        for records in selected:
            if log:
                log(f'burst {number(records[0].get("frame"))}')
            results.append(analyze_presented_burst(records, Path(captures), args.device, sharpen, options, log))
        report['presented'] = OrderedDict([
            ('status', 'evaluated' if results else 'unavailable'),
            ('method', 'RCAS of the resolved FP16 readback (taa_<device>_<frame>.rgba16f) in double, the '
                       'runner\'s reference arithmetic row-vectorised, quantised to 8-bit codes; a '
                       'present_<device>_<frame>.bgra8 readback beside it replaces the model as the image and '
                       'is compared against it'),
            ('sharpen', sharpen), ('gain', rcas_gain(sharpen)),
            ('selection', 'stationary and slow bursts' if not args.model_burst else f'bursts {sorted(args.model_burst)}'),
            ('captures', str(captures)),
            ('flicker_baseline', baseline_flicker(load_json(args.flicker_baseline))),
            ('bursts', results),
        ])
    if args.readback:
        rb = load_json(args.readback)
        report['readback_checks'] = readback_checks(rb)
        report['readback_status'] = rb.get('status')
        report['readback_failed_checks'] = rb.get('failed_checks')
    report['open'] = open_items(report)
    return report


def open_items(report):
    items = []
    if not report['mip_bias']['summary_line_present']:
        items.append('no motion_output_mip_bias_summary line: the session ended without teardown records')
    if report['mip_bias']['capture_sampler_state8_nonzero_bias'] == 0 and report['mip_bias']['capture_sampler_state8_records']:
        items.append('the capture\'s sampler state=8 reads show the application value (restored before the diagnostics), not the route\'s bias')
    presented = report.get('presented') or {}
    if presented.get('status') == 'evaluated':
        sources = {s for b in presented['bursts'] for s in b.get('sources', [])}
        if sources == {'rcas_model'}:
            items.append('section 2.4 is the RCAS model of the resolved readback, not a GPU measurement: this capture has no present_<device>_<frame>.bgra8 files (added after review 26)')
    if report.get('readback_failed_checks'):
        items.append(f"readback checks failing: {report['readback_failed_checks']} (depth: the nearest-sampled previous-depth comparison, pre-existing)")
    return items


# ---- text ------------------------------------------------------------------------------------

def fmt(value, digits=4):
    if value is None:
        return 'n/a'
    if isinstance(value, float):
        return f'{value:.{digits}f}'
    return str(value)


def fmt_span(values, digits=2):
    if not values:
        return 'n/a'
    return f'{values[0]:.{digits}f}-{values[1]:.{digits}f}'


def render_text(report):
    lines = []
    f = report['features']
    lines.append(f"iteration 12: sharpen {f['taa_sharpen']} + mip bias {f['mip_bias']} in flight, "
                 f"build {report['build']['commit'] or '?'}")
    lines.append('=' * len(lines[-1]))
    lines.append(f"log {report['log']}")
    lines.append(f"  sha256 {report['log_sha256']}  dll {report['build']['dll_sha256_prefix']}  bottle {report['build']['bottle']}")
    lines.append(f"features {dict(f)}  line kinds {report['line_kinds']}  failure kinds {report['failure_kinds']}")
    s = report['sharpen']
    lines.append('')
    lines.append(f"-- sharpen: {s['sharpened_resolved_frames']}/{s['resolves']} resolves sharpened "
                 f"({s['sharpened_unresolved_frames']} unresolved records flagged), "
                 f"taa_copy=S_FALSE on {s['taa_copy_S_FALSE_records']}/{s['records']} records,")
    lines.append(f"   sharpen_failed lines {s['sharpen_failed_lines']}, taa_failed lines {s['taa_failed_lines']}, "
                 f"taa_copy_back {fmt(s['taa_copy_back_us_median'], 1)} us/frame"
                 + (f" (previous run {fmt(s.get('taa_copy_back_us_median_previous'), 1)})" if 'taa_copy_back_us_median_previous' in s else ''))
    m = report['mip_bias']
    lines.append('')
    lines.append(f"-- mip bias {m['bias_values']}: sets {m['sets']} restores {m['restores']} biased draws {m['biased_draws']} "
                 f"of {m['routed_draws']} routed, failures {m['failures']}")
    lines.append(f"   biased_now nonzero records {m['biased_now_nonzero_records']}; reads {m['reads']} on frames {m['reads_frames']}; "
                 f"game writes {m['game_writes_total']} total ({m['game_write_lines']} lines), stages {m['game_write_stages']} values {m['game_write_values']}")
    lines.append(f"   stage mask histogram {dict(m['stage_mask_histogram'])}")
    lines.append(f"   teardown summary line present: {m['summary_line_present']}")
    lines.append(f"   capture sampler state=8 records {m['capture_sampler_state8_records']}, nonzero bias {m['capture_sampler_state8_nonzero_bias']} (documented limit)")
    for label in ('ordinary_frames', 'capture_frames'):
        t = m[label]
        lines.append(f"   {label:16s} n={t['records']:4d} sets={t['sets']:6d} restores={t['restores']:6d} biased_draws={t['biased_draws']:6d} "
                     f"sets/draw={fmt(t['sets_per_biased_draw'], 3)} (sets+rest)/draw={fmt(t['sets_plus_restores_per_biased_draw'], 3)} "
                     f"calls/frame={fmt(t['set_calls_per_frame'], 1)}")
    i = m['interleave']
    lines.append(f"   interleave ({i['source']}): draws {i['draws']} routed {i['routed']} runs {i['runs_of_consecutive_routed']} "
                 f"mean routed/run {fmt(i['mean_routed_per_run'], 3)} runs/frame {fmt(i['runs_per_frame'], 1)}"
                 + (f" excess over bound {fmt(i.get('excess_over_interleave_bound'), 2)}x" if 'excess_over_interleave_bound' in i else ''))
    if report['blur']:
        lines.append('')
        lines.append('-- blur (analyze_iteration09_run2, identical code both runs; routed_interior)')
        lines.append(f"   {'run/burst':22s} {'class':12s} {'int px':>8s} {'ratio mean':>10s} {'raw rise':>12s} {'raw mtf50':>12s} "
                     f"{'res rise':>12s} {'res mtf50':>12s} {'ideal floor':>11s} {'share':>7s}")
        for label, rows in report['blur'].items():
            for row in rows:
                name = f"{label} {row['frames'][0] if row['frames'] else '?'}"
                if row['status'] != 'evaluated':
                    lines.append(f"   {name:22s} {row['status']}")
                    continue
                ideal = row.get('ideal') or {}
                lines.append(f"   {name:22s} {row['motion']:12s} {row['routed_interior_px']:8d} {row['gradient_energy_ratio_interior']['mean']:10.4f} "
                             f"{fmt_span(row['raw_rise_10_90_px']):>12s} {fmt_span(row['raw_mtf50_cycles_per_px']):>12s} "
                             f"{fmt_span(row['resolved_rise_10_90_px']):>12s} {fmt_span(row['resolved_mtf50_cycles_per_px']):>12s} "
                             f"{fmt(ideal.get('floor')):>11s} {fmt(ideal.get('share_of_loss_ideal'), 3):>7s}")
    p = report.get('presented') or {}
    lines.append('')
    lines.append(f"-- presented image (section 2.4): {p.get('status')}")
    if p.get('status') == 'evaluated':
        lines.append(f"   sharpen {p['sharpen']} gain {fmt(p['gain'])}; {p['selection']}; {p['method']}")
        base = p.get('flicker_baseline') or {}
        if base.get('aggregate_ratio'):
            lines.append(f"   flicker baseline {base.get('log')} frames {base.get('frames')}: "
                         + ' '.join(f"{k}={fmt(v)}" for k, v in base['aggregate_ratio'].items()))
        for burst in p['bursts']:
            frames = burst['frames']
            lines.append(f"   burst {frames[0]}-{frames[-1]} {burst['route']['motion']} (peak cut {fmt(burst['route']['cut_median_px_peak'])} px): {burst['status']}")
            if burst['status'] != 'evaluated':
                continue
            lines.append(f"     sources {burst['sources']}; 3x3 escapes total {burst['escapes_3x3_total']}")
            for item in burst['per_frame']:
                ch = item['change_vs_unsharpened']
                g = item['gradient']['routed_interior']
                se = item['strong_edges']
                text = (f"     frame {item['frame']}: escapes {item['escapes_3x3']}, changed {ch['changed_fraction']:.3f} "
                        f"(max {ch['max_code_change']} codes, mean {fmt(ch['mean_abs_code_change'], 3)}), "
                        f"interior gradient presented/raw {fmt(g['presented_over_raw'])} resolved/raw {fmt(g['resolved_over_raw'])} "
                        f"presented/resolved {fmt(g['presented_over_resolved'], 3)}; strong edges {se['pixels']} range ratio {fmt(se['local_range_ratio'], 3)} channel escapes {se['channel_escapes_3x3']}")
                if 'readback_vs_model' in item:
                    text += f"; readback vs model max {fmt(item['readback_vs_model']['max_code_error'], 3)} mean {fmt(item['readback_vs_model']['mean_code_error'], 3)}"
                lines.append(text)
                if 'edge_spread' in item:
                    parts = []
                    for label, sp in item['edge_spread'].items():
                        over = sp.get('overshoot') or {}
                        parts.append(f"{label} rise {fmt(sp['rise_10_90_px'], 2)} mtf50 {fmt(sp['mtf50_cycles_per_px'], 2)} "
                                     f"excursion +{fmt(over.get('above_one'), 3)}/-{fmt(over.get('below_zero'), 3)} ({sp['edges']} edges)")
                    lines.append('       edges: ' + '; '.join(parts))
            fl = burst.get('flicker')
            if fl:
                lines.append(f"     flicker (0.01 px gate {'met' if fl['stationary_gate_0_01_px'] else 'not met'}): "
                             f"presented/resolved energy {fmt(fl['presented_over_resolved_energy'], 3)}")
                for name, cls in fl['classes'].items():
                    r = cls['aggregate_ratio']
                    lines.append(f"       {name:16s} {cls['pixels']:7d} px  resolved/raw {fmt(r['resolved'])}  presented/raw {fmt(r['presented'])}  "
                                 f"presented/resolved {fmt(cls['presented_over_resolved'], 3)}")
    if 'readback_checks' in report:
        lines.append('')
        lines.append(f"-- readback checks: status {report['readback_status']}, failed {report['readback_failed_checks']}")
        for name, check in report['readback_checks'].items():
            rest = ' '.join(f'{k}={v}' for k, v in check.items() if k not in ('status', 'note'))
            lines.append(f"   {name:22s} {str(check.get('status')):12s} {rest}")
    lines.append('')
    lines.append('-- open')
    for item in report['open']:
        lines.append(f'   * {item}')
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--text', type=Path)
    parser.add_argument('--captures', type=Path, help='readback directory (default: beside the log)')
    parser.add_argument('--device', default='1')
    parser.add_argument('--taa', type=Path, help='analyze_iteration09_run2 report of this run')
    parser.add_argument('--taa-baseline', type=Path, help='analyze_iteration09_run2 report of iteration 9 run 2')
    parser.add_argument('--readback', type=Path, help='analyze_motion_readback summary of this run')
    parser.add_argument('--previous-log', type=Path, help='the previous run\'s session log (copy-back timing column)')
    parser.add_argument('--flicker-baseline', type=Path,
                        help='analyze_iteration08_taa report with an evaluated stationary flicker burst')
    parser.add_argument('--build-commit')
    parser.add_argument('--dll-sha256-prefix')
    parser.add_argument('--bottle')
    parser.add_argument('--sharpen', type=float, help='X3M_TAA_SHARPEN to model (default: the log\'s)')
    parser.add_argument('--model-burst', type=int, action='append', help='first frame of a burst to model (repeatable)')
    parser.add_argument('--no-model', action='store_true', help='skip section 2.4')
    parser.add_argument('--no-edges', action='store_true')
    parser.add_argument('--width', type=int, default=WIDTH)
    parser.add_argument('--height', type=int, default=HEIGHT)
    parser.add_argument('--margin', type=int, default=12)
    parser.add_argument('--depth-tolerance', type=float, default=it08.DEPTH_TOLERANCE)
    parser.add_argument('--variance-floor', type=float, default=it08.VARIANCE_FLOOR)
    parser.add_argument('--thin-tophat', type=float, default=0.06)
    parser.add_argument('--thin-contrast', type=float, default=0.10)
    parser.add_argument('--strong-edge-range', type=float, default=STRONG_EDGE_RANGE)
    parser.add_argument('--no-hash', action='store_true')
    parser.add_argument('--quiet', action='store_true')
    args = parser.parse_args(argv)
    log = None if args.quiet else (lambda text: sys.stderr.write(text + '\n'))
    report = build(args, log)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1) + '\n')
    text = render_text(report)
    if args.text:
        args.text.write_text(text)
    sys.stdout.write(text)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
