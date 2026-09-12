#!/usr/bin/env python3
"""Iteration 13: the presented sharpened image measured, and the sharpen A/B.

Run D (bottle X3, review-29 build `a34c389`, `--taa-sharpen 1.0
--taa-mip-bias -0.5`) is the first capture that reads the **presented** image
back after the RCAS draw (`present_<device>_<frame>.bgra8`,
`motion_output_present_readback`), so section 1 of
`docs/verification/iteration-13.md` is a GPU measurement rather than a model.

This tool is thin: it consumes the JSON reports of the tools that did the
reading -- `analyze_iteration12.py` (one report per sharpen value; the one on
run D's own capture reads the present files, the `--ignore-present --sharpen s`
ones model a value the run did not use), `analyze_iteration09_run2.py` (the
blur table), `analyze_iteration09.py` (health), `analyze_motion_readback.py`
(certification), `analyze_loading_profile.py` (loads) and
`analyze_iteration11.py` (frame time at matched draw counts) -- and derives:

* ``model_agreement`` -- present readback against the RCAS reference of the
  resolved FP16 image, per burst: the contract is <= 0.5 code (half a
  quantiser step). This is what validates the model iteration 12 had to use
  for run 4 (run 10, sharpen 0.5), and the models of the other sharpen values
  here;
* ``sharpen_ab`` -- one condensed row per burst per sharpen value (escapes,
  code change, gradient energy, 10-90 % rise and MTF50 for raw/resolved/
  presented with the share of the resolve's loss recovered, ESF excursion,
  strong-edge halo, flicker), plus ``same_scene`` deltas between the values
  measured/modelled on *the same frames*, which is the clean A/B;
* ``oversharpening`` -- the indicators whose contract is known: 3x3
  neighbourhood escapes (0), strong-edge local-contrast ratio, ESF excursion
  against the resolve *and* against the raw frames, and the flicker energy
  the sharpen adds, against the square-law prediction of its own gradient
  amplification;
* ``mip_bias_isolation`` -- the raw frames' own sharpness in both runs, so the
  A/B can be attributed to the sharpen rather than to a changed LOD bias;
* ``health`` -- resolves, history, hook agreement, cuts, sampler traffic, the
  readback certification with the suspicious-displacement pixels attributed to
  bursts, the loads and the frame time at matched draw counts.

Every number is derived from the inputs named in the report.
`verification/analysis/test_iteration13.py` covers the derivations on synthetic
reports and pins the published summary.
"""

import argparse
import json
import statistics
import sys
from collections import OrderedDict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import analyze_iteration12 as it12  # noqa: E402

# The fixture's contract for the present readback against the RCAS reference of
# the resolved image: half an 8-bit quantiser step (taa-sharpen.md).
MODEL_CODE_TOLERANCE = 0.5
# The readback is 8-bit and the reference is double, so a pixel whose reference
# value lands on the .5 quantiser boundary can round the other way on the GPU
# and read exactly half a code out. Judging `<= 0.5` on doubles would call that
# a failure, so the comparison carries a rounding epsilon and the report prints
# the excess over half a code, which is what says how close the tie was.
MODEL_CODE_EPSILON = 1.0e-3
# A rise/MTF50 recovery share is only reported where the resolve actually cost
# something on that frame's edges (otherwise the denominator is noise).
RISE_LOSS_FLOOR_PX = 0.05
MTF_LOSS_FLOOR = 0.02


class Malformed(Exception):
    pass


# ---- small helpers -------------------------------------------------------------------


def load_json(path):
    return json.loads(Path(path).read_text()) if path else None


def stat_span(values):
    """{'min','max','mean'} of the values that are not None, or None."""
    values = [v for v in values if v is not None]
    if not values:
        return None
    return OrderedDict([('min', min(values)), ('max', max(values)),
                        ('mean', statistics.fmean(values))])


def ratio(numerator, denominator):
    return numerator / denominator if denominator else None


def recovery_share(raw, resolved, presented, floor):
    """Share of the resolve's own loss that the sharpen gives back.

    For the 10-90 % rise the resolve *raises* the value (blur) and the sharpen
    lowers it, for MTF50 the signs are the other way round; both are written
    as (presented - resolved) / (raw - resolved), so 0 is 'the sharpen changed
    nothing' and 1 is 'the presented image is as sharp as the raw frame'."""
    if raw is None or resolved is None or presented is None:
        return None
    loss = raw - resolved
    if abs(loss) < floor:
        return None
    return (presented - resolved) / loss


# ---- section 1: the present readback against the model --------------------------------


def frame_agreement(item):
    """One per-frame agreement row of an analyze_iteration12 burst."""
    error = item.get('readback_vs_model')
    row = OrderedDict([('frame', item.get('frame')), ('source', item.get('source'))])
    if error:
        row['max_code_error'] = error.get('max_code_error')
        row['mean_code_error'] = error.get('mean_code_error')
        row['excess_over_tolerance'] = (None if error.get('max_code_error') is None
                                        else max(0.0, error['max_code_error'] - MODEL_CODE_TOLERANCE))
        row['within_tolerance'] = error.get('max_code_error') is not None and \
            error['max_code_error'] <= MODEL_CODE_TOLERANCE + MODEL_CODE_EPSILON
    return row


def model_agreement(report):
    """Section 1: present readback vs the RCAS reference, per burst and overall."""
    presented = report.get('presented') or {}
    out = OrderedDict([('tolerance_code', MODEL_CODE_TOLERANCE),
                       ('tolerance_epsilon_code', MODEL_CODE_EPSILON),
                       ('sharpen', presented.get('sharpen')), ('gain', presented.get('gain'))])
    bursts = []
    measured, modelled, worst, means, outside = 0, 0, None, [], 0
    for burst in presented.get('bursts') or []:
        frames = [frame_agreement(item) for item in burst.get('per_frame') or []]
        entry = OrderedDict([('frames', [f['frame'] for f in frames]),
                             ('motion', (burst.get('route') or {}).get('motion')),
                             ('sources', burst.get('sources')),
                             ('per_frame', frames)])
        errors = [f['max_code_error'] for f in frames if 'max_code_error' in f]
        entry['max_code_error'] = max(errors) if errors else None
        excesses = [f['excess_over_tolerance'] for f in frames if f.get('excess_over_tolerance') is not None]
        entry['max_excess_over_half_code'] = max(excesses) if excesses else None
        entry['mean_code_error'] = stat_span([f.get('mean_code_error') for f in frames])
        entry['frames_outside_tolerance'] = sum(1 for f in frames
                                                if 'within_tolerance' in f and not f['within_tolerance'])
        bursts.append(entry)
        for f in frames:
            if f['source'] == 'present_readback':
                measured += 1
            else:
                modelled += 1
            if 'max_code_error' in f:
                means.append(f['mean_code_error'])
                worst = f['max_code_error'] if worst is None else max(worst, f['max_code_error'])
                if not f['within_tolerance']:
                    outside += 1
    out['bursts'] = bursts
    out['frames_measured'] = measured
    out['frames_modelled_only'] = modelled
    out['worst_max_code_error'] = worst
    out['worst_excess_over_half_code'] = (None if worst is None
                                          else max(0.0, worst - MODEL_CODE_TOLERANCE))
    out['mean_code_error'] = stat_span(means)
    out['frames_outside_tolerance'] = outside
    out['status'] = ('unavailable' if not measured else
                     ('pass' if outside == 0 and worst is not None else 'fail'))
    return out


# ---- section 2: the A/B rows ----------------------------------------------------------


def edge_row(spread, label):
    s = (spread or {}).get(label) or {}
    over = s.get('overshoot') or {}
    return {'rise': s.get('rise_10_90_px'), 'mtf50': s.get('mtf50_cycles_per_px'),
            'excursion_above_one': over.get('above_one'),
            'excursion_below_zero': over.get('below_zero'), 'edges': s.get('edges')}


def burst_row(burst):
    """One condensed A/B row from an analyze_iteration12 presented burst."""
    row = OrderedDict()
    frames = burst.get('frames') or []
    row['frames'] = [frames[0], frames[-1]] if frames else []
    row['status'] = burst.get('status')
    route = burst.get('route') or {}
    row['motion'] = route.get('motion')
    row['cut_median_px_peak'] = route.get('cut_median_px_peak')
    rotations = route.get('camera_rotation_deg') or []
    row['camera_rotation_deg_max'] = max(rotations) if rotations else None
    if burst.get('status') != 'evaluated':
        row['missing'] = (burst.get('missing') or [])[:4]
        return row
    row['sources'] = burst.get('sources')
    row['routed_interior_px'] = (burst.get('classes') or {}).get('routed_interior')
    per_frame = burst.get('per_frame') or []
    row['frames_n'] = len(per_frame)
    row['escapes_3x3_total'] = burst.get('escapes_3x3_total')
    row['changed_fraction'] = stat_span([f['change_vs_unsharpened']['changed_fraction'] for f in per_frame])
    row['max_code_change'] = max(f['change_vs_unsharpened']['max_code_change'] for f in per_frame)
    row['mean_abs_code_change'] = stat_span([f['change_vs_unsharpened']['mean_abs_code_change']
                                             for f in per_frame])
    for key in ('presented_over_resolved', 'presented_over_raw', 'resolved_over_raw'):
        row[f'gradient_{key}'] = stat_span([f['gradient']['routed_interior'][key] for f in per_frame])
    edges = {label: [edge_row(f.get('edge_spread'), label) for f in per_frame]
             for label in ('raw', 'resolved', 'presented')}
    for label, rows in edges.items():
        row[f'{label}_rise_10_90_px'] = stat_span([e['rise'] for e in rows])
        row[f'{label}_mtf50_cycles_per_px'] = stat_span([e['mtf50'] for e in rows])
        row[f'{label}_esf_excursion'] = stat_span([e['excursion_above_one'] for e in rows])
    row['rise_recovery_share'] = stat_span([
        recovery_share(a['rise'], b['rise'], c['rise'], RISE_LOSS_FLOOR_PX)
        for a, b, c in zip(edges['raw'], edges['resolved'], edges['presented'])])
    row['mtf50_recovery_share'] = stat_span([
        recovery_share(a['mtf50'], b['mtf50'], c['mtf50'], MTF_LOSS_FLOOR)
        for a, b, c in zip(edges['raw'], edges['resolved'], edges['presented'])])
    row['strong_edge_px'] = stat_span([f['strong_edges']['pixels'] for f in per_frame])
    row['strong_edge_local_range_ratio'] = stat_span([f['strong_edges']['local_range_ratio']
                                                      for f in per_frame])
    row['strong_edge_channel_escapes'] = sum(f['strong_edges']['channel_escapes_3x3'] for f in per_frame)
    flicker = burst.get('flicker') or {}
    if flicker:
        row['flicker'] = OrderedDict([
            ('stationary_gate_0_01_px', flicker.get('stationary_gate_0_01_px')),
            ('presented_over_resolved_energy', flicker.get('presented_over_resolved_energy')),
            ('classes', OrderedDict(
                (name, OrderedDict([('pixels', cls['pixels']),
                                    ('resolved_over_raw', cls['aggregate_ratio']['resolved']),
                                    ('presented_over_raw', cls['aggregate_ratio']['presented']),
                                    ('presented_over_resolved', cls['presented_over_resolved'])]))
                for name, cls in (flicker.get('classes') or {}).items())),
        ])
    return row


def ab_rows(reports):
    """{label: analyze_iteration12 report} -> {label: {first frame: condensed row}}."""
    out = OrderedDict()
    for label, report in reports.items():
        presented = (report or {}).get('presented') or {}
        rows = OrderedDict()
        for burst in presented.get('bursts') or []:
            row = burst_row(burst)
            row['sharpen'] = presented.get('sharpen')
            row['gain'] = presented.get('gain')
            row['image_source'] = 'present_readback' if row.get('sources') == ['present_readback'] \
                else 'rcas_model'
            rows[str(row['frames'][0]) if row['frames'] else '?'] = row
        out[label] = rows
    return out


SAME_SCENE_KEYS = (
    ('gradient_presented_over_resolved', 'mean'),
    ('presented_rise_10_90_px', 'mean'),
    ('presented_mtf50_cycles_per_px', 'mean'),
    ('rise_recovery_share', 'mean'),
    ('mtf50_recovery_share', 'mean'),
    ('presented_esf_excursion', 'mean'),
    ('strong_edge_local_range_ratio', 'mean'),
    ('changed_fraction', 'mean'),
)


def same_scene(rows_low, rows_high):
    """The A/B on identical frames: per burst, both values and the delta.

    `rows_low`/`rows_high` are two entries of `ab_rows` over the same capture
    (for example modelled 0.5 and measured 1.0 on run D), so every difference
    is the sharpen value and nothing else."""
    out = OrderedDict()
    for key, row_high in rows_high.items():
        row_low = rows_low.get(key)
        if row_low is None or row_high.get('status') != 'evaluated' or row_low.get('status') != 'evaluated':
            continue
        entry = OrderedDict([('frames', row_high['frames']), ('motion', row_high['motion']),
                             ('cut_median_px_peak', row_high['cut_median_px_peak']),
                             ('sharpen_low', row_low['sharpen']), ('sharpen_high', row_high['sharpen']),
                             ('image_source_low', row_low['image_source']),
                             ('image_source_high', row_high['image_source'])])
        for metric, field in SAME_SCENE_KEYS:
            low = (row_low.get(metric) or {}).get(field)
            high = (row_high.get(metric) or {}).get(field)
            entry[metric] = OrderedDict([('low', low), ('high', high),
                                         ('delta', None if low is None or high is None else high - low)])
        for metric in ('escapes_3x3_total', 'strong_edge_channel_escapes', 'max_code_change'):
            entry[metric] = OrderedDict([('low', row_low.get(metric)), ('high', row_high.get(metric))])
        low_fl = row_low.get('flicker') or {}
        high_fl = row_high.get('flicker') or {}
        entry['flicker_presented_over_resolved_energy'] = OrderedDict([
            ('low', low_fl.get('presented_over_resolved_energy')),
            ('high', high_fl.get('presented_over_resolved_energy')),
            ('gate_met', high_fl.get('stationary_gate_0_01_px'))])
        entry['flicker_classes'] = OrderedDict(
            (name, OrderedDict([('low', ((low_fl.get('classes') or {}).get(name) or {}).get('presented_over_resolved')),
                                ('high', cls.get('presented_over_resolved')),
                                ('presented_over_raw_high', cls.get('presented_over_raw')),
                                ('resolved_over_raw', cls.get('resolved_over_raw'))]))
            for name, cls in (high_fl.get('classes') or {}).items())
        out[key] = entry
    return out


# ---- section 2c: the over-sharpening indicators ---------------------------------------


def oversharpening(rows_by_label):
    """The indicators with a known contract, per sharpen value and burst."""
    out = OrderedDict()
    for label, rows in rows_by_label.items():
        entries = OrderedDict()
        for key, row in rows.items():
            if row.get('status') != 'evaluated':
                continue
            flicker = row.get('flicker') or {}
            gradient = (row.get('gradient_presented_over_resolved') or {}).get('mean')
            energy = flicker.get('presented_over_resolved_energy')
            entry = OrderedDict([
                ('sharpen', row.get('sharpen')), ('motion', row.get('motion')),
                ('escapes_3x3', row.get('escapes_3x3_total')),
                ('strong_edge_channel_escapes', row.get('strong_edge_channel_escapes')),
                ('strong_edge_local_range_ratio', (row.get('strong_edge_local_range_ratio') or {}).get('mean')),
                ('esf_excursion_raw', (row.get('raw_esf_excursion') or {}).get('mean')),
                ('esf_excursion_resolved', (row.get('resolved_esf_excursion') or {}).get('mean')),
                ('esf_excursion_presented', (row.get('presented_esf_excursion') or {}).get('mean')),
            ])
            resolved_exc = entry['esf_excursion_resolved']
            presented_exc = entry['esf_excursion_presented']
            entry['esf_excursion_over_resolved'] = (None if resolved_exc is None or presented_exc is None
                                                    else presented_exc - resolved_exc)
            entry['esf_excursion_under_raw'] = (None if entry['esf_excursion_raw'] is None or presented_exc is None
                                                else entry['esf_excursion_raw'] - presented_exc)
            entry['flicker_gate_met'] = flicker.get('stationary_gate_0_01_px')
            entry['flicker_presented_over_resolved_energy'] = energy
            # Amplifying gradients by g multiplies a variance by about g^2; more
            # than that means the sharpen is amplifying something other than the
            # edges it is meant to.
            entry['square_law_prediction'] = None if gradient is None else gradient ** 2
            entry['energy_over_square_law'] = (None if energy is None or not gradient
                                               else energy / gradient ** 2)
            entries[key] = entry
        out[label] = entries
    return out


# ---- section 3: the mip bias is unchanged ---------------------------------------------


def mip_bias_isolation(blur_tables, reports):
    """Question 3: the raw frames' sharpness and the bias counters, per run."""
    out = OrderedDict()
    for label, rows in blur_tables.items():
        report = reports.get(label) or {}
        bias = report.get('mip_bias') or {}
        features = report.get('features') or {}
        entry = OrderedDict([
            ('mip_bias_announced', features.get('mip_bias')),
            ('taa_sharpen_announced', features.get('taa_sharpen')),
            ('bias_values', bias.get('bias_values')),
            ('biased_draws', bias.get('biased_draws')), ('routed_draws', bias.get('routed_draws')),
            ('failures', bias.get('failures')),
            ('biased_now_nonzero_records', bias.get('biased_now_nonzero_records')),
            ('all_routed_draws_biased', bias.get('biased_draws') == bias.get('routed_draws')
             if bias else None),
        ])
        bursts = OrderedDict()
        raw_rise, raw_mtf = [], []
        for row in rows:
            if row.get('status') != 'evaluated':
                continue
            bursts[str(row['frames'][0])] = OrderedDict([
                ('motion', row.get('motion')),
                ('routed_interior_px', row.get('routed_interior_px')),
                ('raw_rise_10_90_px', row.get('raw_rise_10_90_px')),
                ('raw_mtf50_cycles_per_px', row.get('raw_mtf50_cycles_per_px')),
                ('resolved_rise_10_90_px', row.get('resolved_rise_10_90_px')),
                ('gradient_energy_ratio_interior_mean',
                 (row.get('gradient_energy_ratio_interior') or {}).get('mean')),
            ])
            if row.get('motion') in ('stationary', 'slow'):
                raw_rise.extend(v for v in (row.get('raw_rise_10_90_px') or []) if v is not None)
                raw_mtf.extend(v for v in (row.get('raw_mtf50_cycles_per_px') or []) if v is not None)
        entry['bursts'] = bursts
        entry['stationary_or_slow_raw_rise_px'] = stat_span(raw_rise)
        entry['stationary_or_slow_raw_mtf50'] = stat_span(raw_mtf)
        out[label] = entry
    return out


# ---- section 4: health ----------------------------------------------------------------


def health_summary(health, report):
    """The counters that can report a fault, from analyze_iteration09 and analyze_iteration12."""
    h = (health or {}).get('health') or {}
    camera = (health or {}).get('camera') or {}
    timing = ((health or {}).get('timing_regimes') or {}).get('1:frame_normal') or {}
    sharpen = (report or {}).get('sharpen') or {}
    out = OrderedDict([
        ('frame_records', h.get('frame_records')), ('taa_attempted', h.get('taa_attempted')),
        ('taa_resolved', h.get('taa_resolved')), ('taa_history', h.get('taa_history')),
        ('attempted_equals_resolved', h.get('taa_attempted') == h.get('taa_resolved')),
        ('resolved_without_history', h.get('resolved_without_history')),
        ('scene_end_check', h.get('scene_end_check')), ('scene_end_source', h.get('scene_end_source')),
        ('apply_failures', h.get('apply_failures')), ('restore_failures', h.get('restore_failures')),
        ('state_shadow', h.get('state_shadow')),
        ('totals', h.get('totals')),
        ('cut_events', h.get('cut_events')), ('camera_cut_events', h.get('camera_cut_events')),
        ('camera_records', camera.get('records')), ('camera_valid', camera.get('valid')),
        ('camera_cuts', camera.get('camera_cuts')),
        ('camera_rotation_deg_max', (camera.get('rotation_deg') or {}).get('max')),
        ('camera_rotation_floor_deg_max', (camera.get('rotation_floor_deg') or {}).get('max')),
        ('frame_normal_windows', timing.get('windows')),
        ('frame_normal_fast', timing.get('fast')), ('frame_normal_slow', timing.get('slow')),
        ('sharpened_resolved_frames', sharpen.get('sharpened_resolved_frames')),
        ('sharpen_failed_lines', sharpen.get('sharpen_failed_lines')),
        ('taa_failed_lines', sharpen.get('taa_failed_lines')),
        ('taa_copy_S_FALSE_records', sharpen.get('taa_copy_S_FALSE_records')),
        ('taa_copy_back_us_median', sharpen.get('taa_copy_back_us_median')),
        ('taa_run_us_median', sharpen.get('taa_run_us_median')),
        ('line_kinds', (report or {}).get('line_kinds')),
        ('failure_kinds', (report or {}).get('failure_kinds')),
    ])
    matched = (h.get('totals') or {}).get('matched')
    routed = (h.get('totals') or {}).get('routed')
    out['history_match_fraction'] = ratio(matched, routed)
    shadow = h.get('state_shadow') or {}
    out['rs_hit_fraction'] = ratio(shadow.get('rs_hits'), shadow.get('rs_queries'))
    return out


def displacement_attribution(readback, bursts):
    """The `displacement` check's suspicious pixels, grouped into the capture bursts.

    `bursts` is the `bursts` list of an analyze_iteration12 report (frames and
    route classification); the readback summary carries one row per captured
    frame. A bound of 64 px is a heuristic, so where the flagged pixels sit
    decides whether the failure is a defect or a genuine fast turn."""
    frames = {}
    for row in (readback or {}).get('frames') or []:
        pixels = row.get('pixels') or {}
        displacement = pixels.get('displacement_px') or {}
        frames[row.get('frame')] = {
            'suspicious': pixels.get('suspicious_over_bound'),
            'max_px': displacement.get('max'), 'p99_px': displacement.get('p99')}
    out = OrderedDict()
    for burst in bursts or []:
        numbers = burst.get('frames') or []
        rows = [frames.get(n) for n in numbers if frames.get(n)]
        suspicious = [r['suspicious'] for r in rows if r['suspicious'] is not None]
        out[str(numbers[0]) if numbers else '?'] = OrderedDict([
            ('frames', numbers), ('motion', (burst.get('route') or {}).get('motion')),
            ('cut_median_px_peak', (burst.get('route') or {}).get('cut_median_px_peak')),
            ('suspicious_pixels', sum(suspicious) if suspicious else 0),
            ('frames_with_suspicious', sum(1 for s in suspicious if s)),
            ('max_displacement_px', max((r['max_px'] for r in rows if r['max_px'] is not None), default=None)),
        ])
    check = ((readback or {}).get('checks') or {}).get('displacement') or {}
    total = sum(entry['suspicious_pixels'] for entry in out.values())
    flagged = [key for key, entry in out.items() if entry['suspicious_pixels']]
    return OrderedDict([('status', check.get('status')), ('bound_px', check.get('bound_px')),
                        ('suspicious_pixels', check.get('suspicious_pixels')),
                        ('suspicious_fraction', check.get('suspicious_fraction')),
                        ('max_displacement_px', check.get('max_displacement_px')),
                        ('attributed_pixels', total),
                        ('bursts_with_suspicious_pixels', flagged),
                        ('bursts', out)])


def load_table(profiles):
    """{label: analyze_loading_profile report} -> the load gaps, per run."""
    out = OrderedDict()
    for label, profile in profiles.items():
        gaps = []
        for index, gap in enumerate((profile or {}).get('gaps') or []):
            gaps.append(OrderedDict([('index', index), ('seconds', gap.get('gap_seconds')),
                                     ('ends_frame', gap.get('end_frame')),
                                     ('ends_since_s', gap.get('end_seconds')),
                                     ('hooked_exclusive_seconds',
                                      (gap.get('hooked') or {}).get('exclusive_seconds'))]))
        out[label] = OrderedDict([('gaps', gaps),
                                  ('total_seconds', sum(g['seconds'] for g in gaps if g['seconds'])),
                                  ('log', ((profile or {}).get('source') or {}).get('name'))])
    return out


def frame_time(cost, pair):
    """The iteration-11 comparison of two runs, at matched draw counts."""
    comparison = ((cost or {}).get('comparisons') or {}).get(pair) or {}
    fast = ((comparison.get('regimes') or {}).get('fast') or {})
    scene = ((comparison.get('normalised') or {}).get('scene') or {})
    return OrderedDict([
        ('pair', pair), ('role', comparison.get('role')),
        ('primary_fast_mean_ms', (fast.get('primary') or {}).get('mean_ms')),
        ('baseline_fast_mean_ms', (fast.get('baseline') or {}).get('mean_ms')),
        ('primary_draws_per_frame', (fast.get('primary') or {}).get('draws_per_frame')),
        ('baseline_draws_per_frame', (fast.get('baseline') or {}).get('draws_per_frame')),
        ('delta_mean_ms', fast.get('delta_mean_ms')), ('delta_mean_percent', fast.get('delta_mean_percent')),
        ('matched_bins', scene.get('bins')), ('draws_span', scene.get('draws_span')),
        ('matched_delta_ms_median', (scene.get('delta_ms') or {}).get('median')),
        ('matched_delta_ms_span', [(scene.get('delta_ms') or {}).get('min'),
                                   (scene.get('delta_ms') or {}).get('max')]),
        ('matched_us_per_draw_median', (scene.get('origin_us_per_draw') or {}).get('median')),
        ('sign_test_p', (scene.get('sign_test') or {}).get('p_two_sided')),
    ])


# ---- assembly --------------------------------------------------------------------------


def build(args):
    measured = load_json(args.measured)
    if not measured:
        raise Malformed('--measured report is required')
    reports = OrderedDict()
    reports[args.measured_label] = measured
    for spec in args.model or []:
        label, _, path = spec.partition('=')
        if not path:
            raise Malformed(f'--model expects LABEL=PATH, got {spec!r}')
        reports[label] = load_json(path)
    if args.modelled_previous:
        reports[args.previous_label] = load_json(args.modelled_previous)

    report = OrderedDict()
    report['tool'] = 'tools/analysis/analyze_iteration13.py'
    report['inputs'] = OrderedDict([
        ('measured', str(args.measured)),
        ('models', list(args.model or [])),
        ('modelled_previous', str(args.modelled_previous) if args.modelled_previous else None),
        ('health', str(args.health) if args.health else None),
        ('taa', str(args.taa) if args.taa else None),
        ('taa_previous', str(args.taa_previous) if args.taa_previous else None),
        ('readback', str(args.readback) if args.readback else None),
        ('loading', str(args.loading) if args.loading else None),
        ('loading_previous', str(args.loading_previous) if args.loading_previous else None),
        ('cost', str(args.cost) if args.cost else None),
        ('flicker_baseline', str(args.flicker_baseline) if args.flicker_baseline else None),
    ])
    report['build'] = OrderedDict([('commit', args.build_commit), ('bottle', args.bottle),
                                   ('log', measured.get('log')), ('log_sha256', measured.get('log_sha256'))])
    report['features'] = measured.get('features')
    report['model_agreement'] = model_agreement(measured)
    rows = ab_rows(reports)
    report['sharpen_ab'] = rows
    report['same_scene'] = same_scene(rows.get(args.same_scene_low or '', OrderedDict()),
                                      rows.get(args.measured_label, OrderedDict()))
    report['oversharpening'] = oversharpening(rows)
    blur = OrderedDict()
    if args.taa:
        blur[args.measured_label] = it12.blur_tables({'x': load_json(args.taa)})['x']
    if args.taa_previous:
        blur[args.previous_label] = it12.blur_tables({'x': load_json(args.taa_previous)})['x']
    elif args.blur_previous_key:
        # The previous run's condensed blur rows are already published inside its
        # own iteration report; the run-2 tool need not read its images again.
        rows = ((reports.get(args.previous_label) or {}).get('blur') or {}).get(args.blur_previous_key)
        if rows:
            blur[args.previous_label] = rows
    report['mip_bias_isolation'] = mip_bias_isolation(blur, reports)
    report['flicker_baseline'] = it12.baseline_flicker(load_json(args.flicker_baseline))
    health = OrderedDict()
    health['counters'] = health_summary(load_json(args.health), measured)
    readback = load_json(args.readback)
    if readback:
        health['readback_status'] = readback.get('status')
        health['readback_failed_checks'] = readback.get('failed_checks')
        health['readback_checks'] = it12.readback_checks(readback)
        health['displacement'] = displacement_attribution(readback, measured.get('bursts'))
    profiles = OrderedDict()
    if args.loading:
        profiles[args.measured_label] = load_json(args.loading)
    if args.loading_previous:
        profiles[args.previous_label] = load_json(args.loading_previous)
    if profiles:
        health['loads'] = load_table(profiles)
    cost = load_json(args.cost)
    if cost:
        health['frame_time'] = frame_time(cost, args.cost_pair)
        health['line_kind_diff'] = cost.get('line_kind_diff')
    report['health'] = health
    report['open'] = open_items(report)
    return report


def open_items(report):
    items = []
    agreement = report['model_agreement']
    if agreement['status'] == 'unavailable':
        items.append('no present_<device>_<frame>.bgra8 readback in the capture: section 1 would be a model')
    elif agreement['frames_outside_tolerance']:
        items.append(f"{agreement['frames_outside_tolerance']} frames disagree with the RCAS reference "
                     f"by more than {MODEL_CODE_TOLERANCE} code")
    for label, entries in report['oversharpening'].items():
        escapes = sum(e['escapes_3x3'] or 0 for e in entries.values())
        if escapes:
            items.append(f'{label}: {escapes} channels left the 3x3 neighbourhood (the contract is 0)')
    modelled = [label for label, rows in report['sharpen_ab'].items()
                if any(row.get('image_source') == 'rcas_model' for row in rows.values())]
    if modelled:
        items.append('modelled, not measured, sharpen values: ' + ', '.join(modelled)
                     + ' (the RCAS reference validated at the measured value by section 1)')
    health = report.get('health') or {}
    if health.get('readback_failed_checks'):
        items.append(f"readback checks failing: {health['readback_failed_checks']}")
    return items


# ---- text ------------------------------------------------------------------------------


def fmt(value, digits=4):
    if value is None:
        return 'n/a'
    if isinstance(value, float):
        return f'{value:.{digits}f}'
    return str(value)


def fmt_stat(stat, digits=3):
    if not stat:
        return 'n/a'
    return f"{stat['min']:.{digits}f}-{stat['max']:.{digits}f}({stat['mean']:.{digits}f})"


def render_text(report):
    lines = []
    features = report.get('features') or {}
    lines.append(f"iteration 13: presented image measured at sharpen {features.get('taa_sharpen')}, "
                 f"mip bias {features.get('mip_bias')}, build {report['build']['commit'] or '?'}")
    lines.append('=' * len(lines[-1]))
    lines.append(f"log {report['build']['log']}  bottle {report['build']['bottle']}")
    a = report['model_agreement']
    lines.append('')
    lines.append(f"-- 1. present readback vs RCAS(taa) at gain {fmt(a['gain'], 3)}: {a['status']} "
                 f"({a['frames_measured']} measured frames, {a['frames_modelled_only']} modelled)")
    lines.append(f"   worst max error {fmt(a['worst_max_code_error'], 7)} code (tolerance "
                 f"{a['tolerance_code']} + {a['tolerance_epsilon_code']} rounding epsilon; excess over "
                 f"half a code {fmt(a['worst_excess_over_half_code'], 8)}), mean error "
                 f"{fmt_stat(a['mean_code_error'], 5)}, frames outside tolerance "
                 f"{a['frames_outside_tolerance']}")
    for burst in a['bursts']:
        lines.append(f"   burst {burst['frames'][0]}-{burst['frames'][-1]} {burst['motion']}: "
                     f"max {fmt(burst['max_code_error'], 7)} mean {fmt_stat(burst['mean_code_error'], 5)} "
                     f"sources {burst['sources']}")
    lines.append('')
    lines.append('-- 2. A/B rows (routed_interior; span min-max(mean) over the burst frames)')
    for label, rows in report['sharpen_ab'].items():
        for key, row in rows.items():
            if row.get('status') != 'evaluated':
                lines.append(f"   {label} {key}: {row.get('status')}")
                continue
            lines.append(f"   {label} {key} {row['motion']} s={row['sharpen']} ({row['image_source']}) "
                         f"cut {fmt(row['cut_median_px_peak'], 4)} px, interior {row['routed_interior_px']} px")
            lines.append(f"     changed {fmt_stat(row['changed_fraction'], 4)} max {row['max_code_change']} codes "
                         f"mean {fmt_stat(row['mean_abs_code_change'], 4)}; gradient p/res "
                         f"{fmt_stat(row['gradient_presented_over_resolved'], 4)} p/raw "
                         f"{fmt_stat(row['gradient_presented_over_raw'], 4)} r/raw "
                         f"{fmt_stat(row['gradient_resolved_over_raw'], 4)}")
            lines.append(f"     rise raw {fmt_stat(row['raw_rise_10_90_px'], 2)} res "
                         f"{fmt_stat(row['resolved_rise_10_90_px'], 2)} pre "
                         f"{fmt_stat(row['presented_rise_10_90_px'], 2)} (recovered "
                         f"{fmt_stat(row['rise_recovery_share'], 3)}); mtf50 raw "
                         f"{fmt_stat(row['raw_mtf50_cycles_per_px'], 2)} res "
                         f"{fmt_stat(row['resolved_mtf50_cycles_per_px'], 2)} pre "
                         f"{fmt_stat(row['presented_mtf50_cycles_per_px'], 2)} (recovered "
                         f"{fmt_stat(row['mtf50_recovery_share'], 3)})")
            lines.append(f"     escapes {row['escapes_3x3_total']}/{row['strong_edge_channel_escapes']} "
                         f"strong-edge range {fmt_stat(row['strong_edge_local_range_ratio'], 4)}; "
                         f"excursion raw {fmt_stat(row['raw_esf_excursion'], 3)} res "
                         f"{fmt_stat(row['resolved_esf_excursion'], 3)} pre "
                         f"{fmt_stat(row['presented_esf_excursion'], 3)}")
            flicker = row.get('flicker') or {}
            if flicker:
                lines.append(f"     flicker gate {flicker['stationary_gate_0_01_px']} energy p/res "
                             f"{fmt(flicker['presented_over_resolved_energy'], 4)}; "
                             + ' '.join(f"{name}={fmt(cls['presented_over_resolved'], 3)}"
                                        for name, cls in flicker['classes'].items()))
    if report['same_scene']:
        lines.append('')
        lines.append('-- 2b. same-scene A/B (identical frames, only the sharpen differs)')
        for key, entry in report['same_scene'].items():
            lines.append(f"   burst {key} {entry['motion']} (cut {fmt(entry['cut_median_px_peak'], 4)} px): "
                         f"{entry['sharpen_low']} ({entry['image_source_low']}) -> "
                         f"{entry['sharpen_high']} ({entry['image_source_high']})")
            for metric, _ in SAME_SCENE_KEYS:
                item = entry[metric]
                lines.append(f"     {metric:38s} {fmt(item['low'], 4):>9s} -> {fmt(item['high'], 4):>9s} "
                             f"({fmt(item['delta'], 4)})")
            energy = entry['flicker_presented_over_resolved_energy']
            lines.append(f"     {'flicker energy p/res':38s} {fmt(energy['low'], 4):>9s} -> "
                         f"{fmt(energy['high'], 4):>9s} (gate {energy['gate_met']})")
            for name, cls in entry['flicker_classes'].items():
                lines.append(f"       {name:18s} p/res {fmt(cls['low'], 3)} -> {fmt(cls['high'], 3)}  "
                             f"res/raw {fmt(cls['resolved_over_raw'], 4)} pre/raw {fmt(cls['presented_over_raw_high'], 4)}")
    lines.append('')
    lines.append('-- 2c. over-sharpening indicators')
    for label, entries in report['oversharpening'].items():
        for key, entry in entries.items():
            lines.append(f"   {label} {key} s={entry['sharpen']}: escapes {entry['escapes_3x3']}/"
                         f"{entry['strong_edge_channel_escapes']}, halo {fmt(entry['strong_edge_local_range_ratio'], 4)}, "
                         f"excursion pre {fmt(entry['esf_excursion_presented'], 3)} "
                         f"(res {fmt(entry['esf_excursion_resolved'], 3)}, raw {fmt(entry['esf_excursion_raw'], 3)}, "
                         f"vs res {fmt(entry['esf_excursion_over_resolved'], 3)}), flicker energy "
                         f"{fmt(entry['flicker_presented_over_resolved_energy'], 4)} vs square law "
                         f"{fmt(entry['square_law_prediction'], 4)} = {fmt(entry['energy_over_square_law'], 3)}")
    baseline = report.get('flicker_baseline') or {}
    if baseline.get('aggregate_ratio'):
        lines.append(f"   flicker baseline {baseline.get('log')} frames {baseline.get('frames')}: "
                     + ' '.join(f"{k}={fmt(v)}" for k, v in baseline['aggregate_ratio'].items()))
    lines.append('')
    lines.append('-- 3. mip bias unchanged (raw-frame sharpness and the bias counters)')
    for label, entry in report['mip_bias_isolation'].items():
        lines.append(f"   {label}: announced bias {entry['mip_bias_announced']} sharpen "
                     f"{entry['taa_sharpen_announced']}, values {entry['bias_values']}, biased "
                     f"{entry['biased_draws']}/{entry['routed_draws']} routed, failures {entry['failures']}, "
                     f"left on {entry['biased_now_nonzero_records']}")
        lines.append(f"     stationary/slow raw rise {fmt_stat(entry['stationary_or_slow_raw_rise_px'], 3)} px, "
                     f"raw mtf50 {fmt_stat(entry['stationary_or_slow_raw_mtf50'], 3)} c/px")
        for key, burst in entry['bursts'].items():
            lines.append(f"     burst {key:6s} {burst['motion']:10s} interior {burst['routed_interior_px']:7d} "
                         f"raw rise {it12.fmt_span(burst['raw_rise_10_90_px'])} raw mtf50 "
                         f"{it12.fmt_span(burst['raw_mtf50_cycles_per_px'])} taa/raw "
                         f"{fmt(burst['gradient_energy_ratio_interior_mean'], 3)}")
    health = report.get('health') or {}
    counters = health.get('counters') or {}
    lines.append('')
    lines.append(f"-- 4. health: {counters.get('frame_records')} records, attempted "
                 f"{counters.get('taa_attempted')} = resolved {counters.get('taa_resolved')} "
                 f"({counters.get('attempted_equals_resolved')}), history {counters.get('taa_history')}, "
                 f"failures {counters.get('apply_failures')}/{counters.get('restore_failures')}")
    lines.append(f"   history match {fmt(counters.get('history_match_fraction'), 5)}, shadow hits "
                 f"{fmt(counters.get('rs_hit_fraction'), 6)}, hook {counters.get('scene_end_check')}, "
                 f"cuts {counters.get('cut_events')}, camera cuts {counters.get('camera_cuts')}, rotation max "
                 f"{fmt(counters.get('camera_rotation_deg_max'), 4)} (floor {fmt(counters.get('camera_rotation_floor_deg_max'), 4)})")
    lines.append(f"   sharpen {counters.get('sharpened_resolved_frames')} resolves, failed lines "
                 f"{counters.get('sharpen_failed_lines')}/{counters.get('taa_failed_lines')}, copy-back "
                 f"{fmt(counters.get('taa_copy_back_us_median'), 1)} us, taa_run "
                 f"{fmt(counters.get('taa_run_us_median'), 1)} us; line kinds {counters.get('line_kinds')}, "
                 f"failure kinds {counters.get('failure_kinds')}")
    if 'readback_checks' in health:
        lines.append(f"   readback {health['readback_status']}, failed {health['readback_failed_checks']}")
        for name, check in health['readback_checks'].items():
            rest = ' '.join(f'{k}={v}' for k, v in check.items() if k not in ('status', 'note'))
            lines.append(f"     {name:22s} {str(check.get('status')):12s} {rest}")
        d = health['displacement']
        lines.append(f"   displacement {d['status']}: {d['suspicious_pixels']} suspicious px over "
                     f"{d['bound_px']} px bound, max {fmt(d['max_displacement_px'], 1)} px, "
                     f"in bursts {d['bursts_with_suspicious_pixels']}")
        for key, burst in d['bursts'].items():
            lines.append(f"     burst {key:6s} {burst['motion']:10s} cut {fmt(burst['cut_median_px_peak'], 3):>8s} px "
                         f"suspicious {burst['suspicious_pixels']:8d} in {burst['frames_with_suspicious']} frames, "
                         f"max {fmt(burst['max_displacement_px'], 1)} px")
    for label, entry in (health.get('loads') or {}).items():
        lines.append(f"   loads {label}: total {fmt(entry['total_seconds'], 2)} s over {len(entry['gaps'])} gaps "
                     + ' '.join(f"[{g['index']}]={fmt(g['seconds'], 3)}s@f{g['ends_frame']}" for g in entry['gaps']))
    frame = health.get('frame_time')
    if frame:
        lines.append(f"   frame time {frame['pair']}: {fmt(frame['primary_fast_mean_ms'], 3)} ms at "
                     f"{frame['primary_draws_per_frame']} draws vs {fmt(frame['baseline_fast_mean_ms'], 3)} ms at "
                     f"{frame['baseline_draws_per_frame']}; matched {frame['matched_bins']} bins over "
                     f"{frame['draws_span']} draws, median delta {fmt(frame['matched_delta_ms_median'], 3)} ms "
                     f"({fmt(frame['matched_us_per_draw_median'], 3)} us/draw), sign p {frame['sign_test_p']}")
    if health.get('line_kind_diff'):
        lines.append(f"   line kinds {json.dumps(health['line_kind_diff'])}")
    lines.append('')
    lines.append('-- open')
    for item in report['open']:
        lines.append(f'   * {item}')
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--measured', type=Path, required=True,
                        help='analyze_iteration12 report of the run whose capture has present_* files')
    parser.add_argument('--measured-label', default='runD_1.0')
    parser.add_argument('--model', action='append', metavar='LABEL=PATH',
                        help='analyze_iteration12 report modelling another sharpen value on the same '
                             'capture (--ignore-present --sharpen s); repeatable')
    parser.add_argument('--modelled-previous', type=Path,
                        help='tracked analyze_iteration12 report of the previous run (iteration-12.json)')
    parser.add_argument('--previous-label', default='run10_0.5')
    parser.add_argument('--same-scene-low', help='label of the model to put beside the measured value')
    parser.add_argument('--health', type=Path, help='analyze_iteration09 report of the measured run')
    parser.add_argument('--taa', type=Path, help='analyze_iteration09_run2 report of the measured run')
    parser.add_argument('--taa-previous', type=Path, help='analyze_iteration09_run2 report of the previous run')
    parser.add_argument('--blur-previous-key', default='run10',
                        help="key of the previous run's condensed blur rows inside its own iteration "
                             'report, used when --taa-previous is not given')
    parser.add_argument('--readback', type=Path, help='analyze_motion_readback summary of the measured run')
    parser.add_argument('--loading', type=Path, help='analyze_loading_profile JSON of the measured run')
    parser.add_argument('--loading-previous', type=Path, help='analyze_loading_profile JSON of the previous run')
    parser.add_argument('--cost', type=Path, help='analyze_iteration11 report covering both runs')
    parser.add_argument('--cost-pair', default='runD:run10')
    parser.add_argument('--flicker-baseline', type=Path, help='analyze_iteration08_taa report with a gated burst')
    parser.add_argument('--build-commit')
    parser.add_argument('--bottle')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--text', type=Path)
    args = parser.parse_args(argv)
    report = build(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1) + '\n')
    text = render_text(report)
    if args.text:
        args.text.write_text(text)
    sys.stdout.write(text)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
