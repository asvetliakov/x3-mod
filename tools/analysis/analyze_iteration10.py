#!/usr/bin/env python3
"""Iteration 10: the new CrossOver "X3" bottle (arm64 Wine + FEX) against the old one.

This tool derives only the things no existing analyzer produces, and imports
everything else:

  * ``analyze_iteration09``      - route/TAA health and the camera read
  * ``analyze_iteration09_cost`` - frame time, draw-binned Theil-Sen, route cost
  * ``analyze_iteration09_run2`` - the engine scene-end hook and the blur metrics
  * ``analyze_camera_state``     - the draw-constant cross-check
  * ``analyze_motion_readback``  - readback integrity, row pairs, depth

New here:

  * ``line_kinds`` / ``kind_diff``  - the set of log line kinds a run emits, and the
    diff against the old-bottle runs: a new or missing kind is the cheapest signal
    that the emulator changed a code path.
  * ``metric_table``               - per-call medians of the route/TAA telemetry
    metrics, the direct per-call cost comparison between the two emulators.
  * ``precision_table``            - the engine-side values the route depends on
    that an x87 precision change would move: view-matrix row-norm residuals,
    projection p00/p11, the draw-constant cross-check deviations, the motion
    readback row-consistency error and the depth cross-check error.
  * ``speedup_table``              - the paired draw-count bins of a cost
    comparison expressed as a speed-up factor.
  * ``blur_floor_table``           - the stationary-burst ideal-supersampling
    floor of docs/verification/iteration-09-run2.md section 4, computed instead
    of hand-tabulated, so a later run can be compared to it mechanically.
  * ``health_table``               - the per-run health comparison.

Heavy sub-reports (blur images, readbacks, the draw-constant cross-check) are
consumed as the JSON their own tool writes; the cheap ones are computed here
from the log by streaming it. Nothing is read whole into memory except the
sub-report JSON.
"""

import argparse
import collections
import json
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import analyze_iteration09 as it09              # noqa: E402
import analyze_iteration09_cost as it09cost     # noqa: E402

ROUTE_METRICS = ('route_gate', 'route_draw', 'route_set_rt', 'route_fill', 'route_jitter',
                 'taa_run', 'taa_resolve_draw', 'draw_backend', 'frame_normal', 'lock_wait')

# Line kinds whose absence or presence is configuration, not behaviour: a run
# that did not ask for the feature cannot emit them.
OPTIONAL_KINDS = ('mesh_cache_metric', 'mesh_cache_bypass', 'mesh_cache_fp_first',
                  'mesh_cache_hit', 'mesh_cache_miss', 'hdr_pass', 'hdr_metric',
                  'application_admission', 'motion_capture')


class Malformed(Exception):
    pass


def line_kinds(path):
    """Counter of the first whitespace token of every line, streamed."""
    counts = collections.Counter()
    with open(path, errors='replace') as handle:
        for line in handle:
            head = line.split(' ', 1)[0].strip()
            if head:
                counts[head] += 1
    return counts


def kind_diff(new_counts, baseline_counts, optional=OPTIONAL_KINDS):
    """Which line kinds the new run added or dropped against a set of baselines."""
    baseline = set()
    for counts in baseline_counts:
        baseline |= set(counts)
    new = set(new_counts)
    added = sorted(new - baseline)
    dropped = sorted(baseline - new)
    return {
        'new_kinds': [{'kind': k, 'count': new_counts[k]} for k in added],
        'missing_kinds': [{'kind': k, 'configuration': k in optional} for k in dropped],
        'kinds_new_run': len(new),
        'kinds_baseline_union': len(baseline),
    }


def _fields(line):
    out = {}
    for token in line.split()[1:]:
        if '=' in token:
            key, value = token.split('=', 1)
            out[key] = value
    return out


def metric_table(path, names=ROUTE_METRICS):
    """Per-call medians of ``telemetry_metric`` windows, streamed from the log."""
    per_window = collections.defaultdict(list)
    totals = collections.defaultdict(lambda: [0, 0.0])
    failures = collections.Counter()
    with open(path, errors='replace') as handle:
        for line in handle:
            if not line.startswith('telemetry_metric'):
                continue
            fields = _fields(line)
            name = fields.get('name')
            if name not in names:
                continue
            try:
                count = int(fields['count'])
                total = float(fields['total_us'])
            except (KeyError, ValueError) as error:
                raise Malformed(f'telemetry_metric without count/total_us: {line[:120]}') from error
            failures[name] += int(fields.get('failures', 0) or 0)
            if count:
                per_window[name].append(total / count)
            totals[name][0] += count
            totals[name][1] += total
    table = {}
    for name, values in sorted(per_window.items()):
        calls, total_us = totals[name]
        table[name] = {
            'windows': len(values),
            'calls': calls,
            'median_us_per_call': round(statistics.median(values), 4),
            'mean_us_per_call': round(total_us / calls, 4) if calls else None,
            'failures': failures[name],
        }
    return table


def metric_comparison(tables, primary, baseline):
    """``primary`` per-call medians over ``baseline``: <1 means the new bottle is faster."""
    rows = []
    for name in ROUTE_METRICS:
        a = tables.get(primary, {}).get(name)
        b = tables.get(baseline, {}).get(name)
        if not a or not b:
            continue
        rows.append({
            'metric': name,
            'primary_median_us': a['median_us_per_call'],
            'baseline_median_us': b['median_us_per_call'],
            'ratio': round(a['median_us_per_call'] / b['median_us_per_call'], 4)
            if b['median_us_per_call'] else None,
            'primary_calls': a['calls'],
            'baseline_calls': b['calls'],
            'failures': a['failures'] + b['failures'],
        })
    return {'primary': primary, 'baseline': baseline, 'rows': rows,
            'note': 'per-call medians over telemetry_metric windows; a ratio below 1.0 is '
                    'the primary run being cheaper per call. route_set_rt nests inside '
                    'route_draw and taa_resolve_draw inside taa_run.'}


def speedup_table(cost_report, primary, baseline, phase='scene'):
    """The paired draw-count bins of one comparison as a speed-up factor."""
    match = [c for c in cost_report.get('comparisons', [])
             if c.get('primary') == primary and c.get('baseline') == baseline]
    if not match:
        raise Malformed(f'no {primary} against {baseline} comparison in the cost report')
    comparison = match[0]
    pairs = comparison['normalised'][phase]['pairs']
    rows = []
    for pair in pairs:
        factor = (pair['baseline_ms'] / pair['primary_ms']) if pair['primary_ms'] else None
        rows.append({
            'draws': pair['draws'],
            'primary_ms': pair['primary_ms'],
            'baseline_ms': pair['baseline_ms'],
            'factor': round(factor, 3) if factor else None,
            'primary_windows': pair['primary_windows'],
            'baseline_windows': pair['baseline_windows'],
        })
    factors = [r['factor'] for r in rows if r['factor']]
    return {
        'primary': primary, 'baseline': baseline, 'phase': phase, 'bins': len(rows),
        'rows': rows,
        'factor': {'min': min(factors), 'median': round(statistics.median(factors), 3),
                   'max': max(factors)} if factors else None,
        'origin_us_per_draw': comparison['normalised'][phase]['origin_us_per_draw'],
        'sign_test': comparison['normalised'][phase]['sign_test'],
        'attribution': comparison.get('attribution'),
        'note': 'paired medians of the same draws/frame bin; a factor above 1.0 is the '
                'primary run being faster. Menu frames can fall into a high-draw scene '
                'bin, which inflates a single bin rather than the slope.',
    }


def blur_floor(entry):
    """The ideal-supersampling floor of iteration-09-run2 section 4 for one burst.

    ``measured`` is the mean resolved/raw routed-interior gradient-energy ratio of
    the burst's frames. ``four_phase`` is the same ratio for the plain mean of the
    burst's four raw frames - a real four-phase jitter supersample with no
    resampling anywhere. Scaling it by the analytic white-source factor of the
    resolve's own eight-phase kernel over that of the four-phase kernel gives the
    floor an ideal supersample of this content would already sit at.
    """
    average = entry.get('captured_phase_average') or {}
    ratios = (average.get('ratios') or {}).get('routed_interior') or {}
    four_phase = ratios.get('gradient_energy_ratio')
    scale = average.get('scale_to_resolve_kernel')
    measured_values = [
        frame['ratios']['routed_interior']['gradient_energy_ratio']
        for frame in entry.get('per_frame', [])
        if frame.get('ratios', {}).get('routed_interior', {}).get('gradient_energy_ratio') is not None
    ]
    if not measured_values or four_phase is None or scale is None:
        return {'status': 'unavailable', 'frames': entry.get('frames')}
    measured = sum(measured_values) / len(measured_values)
    ideal = four_phase * scale
    loss_share = ((1.0 - ideal) / (1.0 - measured)) if measured < 1.0 else None
    comparable = [(frame.get('iteration8_comparable') or {}).get('gradient_energy_ratio')
                  for frame in entry.get('per_frame', [])]
    comparable = [round(v, 4) for v in comparable if v is not None]
    return {
        'status': 'evaluated',
        'frames': entry.get('frames'),
        'motion': (entry.get('route') or {}).get('motion'),
        'cut_median_px_peak': (entry.get('route') or {}).get('cut_median_px_peak'),
        'rotation_deg_max': max((entry.get('route') or {}).get('camera_rotation_deg') or [0.0]),
        'routed_interior_px': (entry.get('classes') or {}).get('routed_interior'),
        'measured_per_frame': [round(v, 4) for v in measured_values],
        'measured': round(measured, 4),
        'four_phase_average': round(four_phase, 4),
        'analytic_four_phase': average.get('analytic_white_gradient_ratio'),
        'analytic_resolve_kernel': average.get('resolve_kernel_analytic_white'),
        'scale_to_resolve_kernel': scale,
        'ideal_floor': round(ideal, 4),
        'measured_over_ideal': round(measured / ideal, 4) if ideal else None,
        'share_of_loss_that_is_supersampling': round(loss_share, 4) if loss_share else None,
        'iteration8_comparable_ratio': comparable,
        'iteration8_comparable_mean': round(sum(comparable) / len(comparable), 4)
        if comparable else None,
        'sentinel_ratio': round((average.get('ratios', {}).get('sentinel') or {})
                                .get('gradient_energy_ratio', float('nan')), 4),
        'raw_phase_spread_routed_interior':
            (entry.get('jitter_phase_spread') or {}).get('relative_spread_routed_interior'),
    }


def blur_floor_table(taa_report, motion=('stationary',)):
    rows = []
    for entry in taa_report.get('blur', []):
        if entry.get('status') != 'evaluated':
            continue
        if motion and (entry.get('route') or {}).get('motion') not in motion:
            continue
        rows.append(blur_floor(entry))
    evaluated = [r for r in rows if r['status'] == 'evaluated']
    shares = [r['share_of_loss_that_is_supersampling'] for r in evaluated
              if r['share_of_loss_that_is_supersampling'] is not None]
    return {
        'bursts': rows,
        'share_of_loss': {'min': min(shares), 'max': max(shares),
                          'median': round(statistics.median(shares), 4)} if shares else None,
        'box_filter_reference': taa_report.get('box_filter_reference', {})
                                          .get('gradient_energy_ratio'),
        'note': 'mean squared central-difference luma gradient, resolved over raw, on the '
                'routed-interior class of stationary bursts; the floor is the burst\'s own '
                'four real jitter phases rescaled to the resolve\'s eight-phase kernel.',
    }


def health_table(it09_reports):
    """One row per run from analyze_iteration09 reports (already built or built here)."""
    rows = []
    for label, report in it09_reports.items():
        health = report['health']
        camera = report['camera']
        if not health.get('frame_records'):
            continue  # route off: no motion_output_frame records to summarise
        totals = health.get('totals', {})
        routed = totals.get('routed') or 0
        rows.append({
            'label': label,
            'log': report['source']['log'],
            'frame_records': health.get('frame_records'),
            'draws': totals.get('draws'),
            'routed': routed,
            'matched': totals.get('matched'),
            'match_fraction': round(totals['matched'] / routed, 5) if routed else None,
            'gates': {k: totals.get(k) for k in ('gate1', 'gate2', 'gate3', 'gate4',
                                                 'gate5', 'gate6')},
            'taa_attempted': health.get('taa_attempted'),
            'taa_resolved': health.get('taa_resolved'),
            'taa_history': health.get('taa_history'),
            'taa_skip': health.get('taa_skip'),
            'taa_result': health.get('taa_result'),
            'resolved_without_history': len(health.get('resolved_without_history') or []),
            'apply_failures': health.get('apply_failures'),
            'restore_failures': health.get('restore_failures'),
            'state_shadow': health.get('state_shadow'),
            'scene_end_source': health.get('scene_end_source'),
            'scene_end_check': health.get('scene_end_check'),
            'camera_policy': health.get('camera_policy'),
            'camera_valid': health.get('camera_valid'),
            'cut_events': len(health.get('cut_events') or []),
            'camera_cut_events': len(health.get('camera_cut_events') or []),
            'routed_frames_without_camera': len(health.get('routed_frames_without_camera') or []),
            'camera_states': camera.get('records'),
            'camera_states_valid': camera.get('valid'),
            'camera_invalid_frames': camera.get('invalid_frames'),
            'projections': camera.get('projections'),
            'row_norm_deviation_max': camera.get('row_norm_deviation_max'),
            'rotation_deg': camera.get('rotation_deg'),
            'reset_records': report['witnesses'].get('motion_output_reset', 0)
            + report['witnesses'].get('device_reset', 0),
        })
    rows.sort(key=lambda row: row['label'])
    return rows


def precision_table(it09_reports, camera_reports=None, readback_reports=None):
    """The engine-side values a change in x87 precision would move.

    Every quantity here is computed by the engine (or read out of engine memory)
    and then checked against an independent path, so a systematic widening of a
    residual between the two emulators is the signal to look for. A residual that
    is unchanged or smaller is evidence the reduced-precision x87 mode did not
    reach the values the route consumes.
    """
    camera_reports = camera_reports or {}
    readback_reports = readback_reports or {}
    rows = []
    for label, report in sorted(it09_reports.items()):
        camera = report['camera']
        if not camera.get('records'):
            continue  # route off: the camera was never read, nothing to compare
        row = {
            'label': label,
            'view_row_norm_deviation_max': camera.get('row_norm_deviation_max'),
            'projections': [{'p00': p['p00'], 'p11': p['p11'], 'records': p['records']}
                            for p in camera.get('projections') or []],
        }
        cross = camera_reports.get(label)
        if cross:
            checks = {c['name']: c for c in cross.get('checks', [])}
            constants = checks.get('draw_constants_agree', {})
            row['draw_constants'] = {
                'draws': constants.get('draws'),
                'agreeing': constants.get('agreeing'),
                'fraction': round(constants['agreeing'] / constants['draws'], 5)
                if constants.get('draws') else None,
                'max_rotation_deviation': constants.get('max_rotation_deviation'),
                'max_projection_deviation': constants.get('max_projection_deviation'),
            }
            frames = cross.get('capture_frames') or []
            scene = [f for f in frames if f.get('valid')]
            if scene:
                row['capture_frame_deviations'] = {
                    'frames': len(scene),
                    'rotation_max': max(f['max_rotation_deviation'] for f in scene),
                    'translation_max': max(f['max_translation_deviation'] for f in scene),
                    'projection_max': max(f['max_projection_deviation'] for f in scene),
                }
        readback = readback_reports.get(label)
        if readback:
            checks = readback.get('checks', {})
            row['motion_row_consistency'] = {
                'status': checks.get('row_consistency', {}).get('status'),
                'max_error_px': checks.get('row_consistency', {}).get('max_error_px'),
                'unexplained_fraction': checks.get('row_consistency', {}).get('unexplained_fraction'),
                'sampled_pixels': checks.get('row_consistency', {}).get('sampled_pixels'),
            }
            row['depth_cross_check'] = {
                'status': checks.get('depth', {}).get('status'),
                'max_error': checks.get('depth', {}).get('max_error'),
                'worst_within_fraction': checks.get('depth', {}).get('worst_within_fraction'),
                'compared_pixels': checks.get('depth', {}).get('compared_pixels'),
            }
            row['readback_integrity'] = checks.get('readback_integrity', {}).get('status')
        rows.append(row)
    return {
        'rows': rows,
        'note': 'x87 reduced precision computes 80-bit operations at 64-bit, a relative '
                'error near 1e-16: it would show up as a widened residual, not as a '
                'changed value. A residual that did not grow is evidence against any '
                'effect on the values the route reads.',
    }


def build(args):
    it09_reports = {}
    for item in args.it09:
        label, path = _split(item)
        it09_reports[label] = json.loads(Path(path).read_text())
    for item in args.analyze:
        label, path = _split(item)
        it09_reports[label] = it09.analyze(Path(path), device=args.device, detail=False)

    kinds = {}
    for item in args.run:
        label, path = _split(item)
        kinds[label] = line_kinds(path)

    metrics = {}
    for item in args.run:
        label, path = _split(item)
        metrics[label] = metric_table(path)

    camera_reports = {}
    for item in args.camera:
        label, path = _split(item)
        camera_reports[label] = json.loads(Path(path).read_text())
    readback_reports = {}
    for item in args.readback:
        label, path = _split(item)
        readback_reports[label] = json.loads(Path(path).read_text())

    report = collections.OrderedDict()
    report['tool'] = {
        'name': 'tools/analysis/analyze_iteration10.py',
        'imports': ['tools/analysis/analyze_iteration09.py',
                    'tools/analysis/analyze_iteration09_cost.py',
                    'tools/analysis/analyze_iteration09_run2.py',
                    'tools/analysis/analyze_camera_state.py',
                    'tools/analysis/analyze_motion_readback.py'],
    }
    report['runs'] = {label: {'log': path} for label, path in
                      (_split(item) for item in args.run)}

    if kinds and args.new_run and args.new_run in kinds:
        baselines = [counts for label, counts in kinds.items() if label != args.new_run
                     and label not in (args.new_run_peer or [])]
        report['line_kinds'] = {
            'new_run': args.new_run,
            'baselines': [label for label in kinds if label != args.new_run
                          and label not in (args.new_run_peer or [])],
            'diff': kind_diff(kinds[args.new_run], baselines),
            'counts': {label: dict(sorted(counts.items())) for label, counts in kinds.items()}
            if args.kind_counts else None,
        }

    if metrics:
        report['metrics'] = {'tables': metrics}
        if args.metric_pair:
            report['metrics']['comparisons'] = [
                metric_comparison(metrics, *_split(pair, separator=':'))
                for pair in args.metric_pair]

    if it09_reports:
        report['health'] = health_table(it09_reports)
        report['precision'] = precision_table(it09_reports, camera_reports, readback_reports)

    if args.cost:
        cost = json.loads(Path(args.cost).read_text())
        report['cost'] = {'runs': [{k: run.get(k) for k in
                                    ('label', 'path', 'configuration', 'scene', 'menu',
                                     'profiler_share', 'telemetry_stamps', 'frame_records',
                                     'phases', 'phase_check')}
                                   for run in cost.get('runs', [])]}
        report['speedups'] = [speedup_table(cost, *_split(pair, separator=':'))
                              for pair in args.speedup]
    elif args.speedup:
        raise Malformed('--speedup needs --cost')

    if args.taa:
        taa = json.loads(Path(args.taa).read_text())
        report['scene_hook'] = {k: v for k, v in taa.get('scene_hook', {}).items()
                                if k not in ('per_frame',)}
        report['blur'] = blur_floor_table(taa)
        if args.taa_baseline:
            baseline = json.loads(Path(args.taa_baseline).read_text())
            report['blur_baseline'] = blur_floor_table(baseline)
            report['scene_hook_baseline'] = {
                k: v for k, v in baseline.get('scene_hook', {}).items()
                if k not in ('per_frame', 'disagreement_records')}

    report['limits'] = [
        'CPU-side wall clock only; no GPU timing anywhere in this report.',
        'The new-bottle route-on run does not follow the old runs\' flight path before '
        'the station, so only the draw-count-normalised bins and the per-call metric '
        'medians compare; pooled means report occupancy, not speed.',
        'A line kind that is absent because the run did not request the feature is '
        'flagged as configuration, not as a behaviour change.',
    ]
    return report


def _split(item, separator='='):
    if separator not in item:
        raise Malformed(f'expected LABEL{separator}VALUE, got {item!r}')
    label, value = item.split(separator, 1)
    return label, value


def fmt(value, digits=3):
    if value is None:
        return '-'
    if isinstance(value, float):
        return f'{value:.{digits}f}'
    return str(value)


def render_text(report):
    lines = ['iteration 10: the new "X3" bottle (arm64 Wine + FEX) against the old one',
             '=' * 74]
    kinds = report.get('line_kinds')
    if kinds:
        diff = kinds['diff']
        lines.append(f"-- line kinds: {kinds['new_run']} emits {diff['kinds_new_run']} kinds, "
                     f"baselines {kinds['baselines']} {diff['kinds_baseline_union']}")
        if diff['new_kinds']:
            for item in diff['new_kinds']:
                lines.append(f"   new: {item['kind']} x{item['count']}")
        else:
            lines.append('   new: none')
        for item in diff['missing_kinds']:
            tag = 'configuration' if item['configuration'] else 'MISSING'
            lines.append(f"   absent ({tag}): {item['kind']}")
        lines.append('')
    for row in report.get('health', []):
        lines.append(f"-- health {row['label']}  {row['frame_records']} frame records")
        lines.append(f"   draws {row['draws']} routed {row['routed']} matched {row['matched']}"
                     f" ({fmt(row['match_fraction'], 5)})  gates {row['gates']}")
        lines.append(f"   taa attempted {row['taa_attempted']} resolved {row['taa_resolved']}"
                     f" history {row['taa_history']} skip {row['taa_skip']}"
                     f" resolved_without_history {row['resolved_without_history']}")
        lines.append(f"   apply/restore failures {row['apply_failures']}/{row['restore_failures']}"
                     f"  state shadow {row['state_shadow']}  resets {row['reset_records']}")
        lines.append(f"   scene_end source {row['scene_end_source']} check {row['scene_end_check']}")
        lines.append(f"   camera states {row['camera_states_valid']}/{row['camera_states']}"
                     f" policy {row['camera_policy']} cuts {row['camera_cut_events']}"
                     f" routed_without_camera {row['routed_frames_without_camera']}")
        lines.append('')
    precision = report.get('precision')
    if precision:
        lines.append('-- precision (the FEX x87 question)')
        for row in precision['rows']:
            residual = row['view_row_norm_deviation_max']
            lines.append(f"   {row['label']}: view row-norm residual max "
                         f"{residual:.3e}" if residual is not None else
                         f"   {row['label']}: view row-norm residual max -")
            lines.append('      projections ' +
                         ', '.join(f"p00={p['p00']} p11={p['p11']} n={p['records']}"
                                   for p in row['projections']))
            constants = row.get('draw_constants')
            if constants:
                lines.append(f"      draw constants {constants['agreeing']}/{constants['draws']}"
                             f" ({fmt(constants['fraction'], 5)}) max projection deviation "
                             f"{constants['max_projection_deviation']}")
            deviations = row.get('capture_frame_deviations')
            if deviations:
                lines.append(f"      valid capture frames n={deviations['frames']} rotation "
                             f"{deviations['rotation_max']:.3e} translation "
                             f"{deviations['translation_max']:.3e} projection "
                             f"{deviations['projection_max']:.3e}")
            rows = row.get('motion_row_consistency')
            if rows:
                lines.append(f"      motion row consistency {rows['status']} max "
                             f"{rows['max_error_px']:.4f} px unexplained "
                             f"{rows['unexplained_fraction']}")
            depth = row.get('depth_cross_check')
            if depth:
                lines.append(f"      depth cross-check {depth['status']} max error "
                             f"{depth['max_error']} worst within {depth['worst_within_fraction']}")
        lines.append('')
    for comparison in (report.get('metrics') or {}).get('comparisons') or []:
        lines.append(f"-- per-call metric medians, {comparison['primary']} over "
                     f"{comparison['baseline']}")
        for row in comparison['rows']:
            lines.append(f"   {row['metric']:<18} {row['primary_median_us']:>10.4f} us vs "
                         f"{row['baseline_median_us']:>10.4f} us  x{fmt(row['ratio'])}"
                         f"  calls {row['primary_calls']}/{row['baseline_calls']}"
                         f"  failures {row['failures']}")
        lines.append('')
    for run in (report.get('cost') or {}).get('runs', []):
        lines.append(f"-- frame time {run['label']}")
        for phase in ('scene', 'menu'):
            block = run.get(phase) or {}
            for name in ('fast', 'slow'):
                regime = block.get(name)
                if not isinstance(regime, dict) or not regime.get('windows'):
                    continue
                lines.append(f"   {phase}/{name:<5} n={regime['windows']} mean "
                             f"{fmt((regime.get('mean_ms') or {}).get('median'), 2)} ms"
                             f" min {fmt((regime.get('min_ms') or {}).get('median'), 2)} ms"
                             f" draws/f {fmt((regime.get('draws_per_frame') or {}).get('median'), 1)}")
        share = run.get('profiler_share') or {}
        scene_share = share.get('scene') or {}
        if scene_share.get('status') == 'present':
            lines.append(f"   profiler {scene_share['share_of_wall'] * 100:.2f}% of wall = "
                         f"{scene_share['ms_per_frame_at_median']} ms/frame; stamps "
                         f"{(run.get('telemetry_stamps') or {}).get('ms_per_frame')} ms/frame")
    if report.get('cost'):
        lines.append('')
    for table in report.get('speedups', []):
        lines.append(f"-- speed-up {table['primary']} over {table['baseline']} "
                     f"({table['phase']}, {table['bins']} bins)")
        for row in table['rows']:
            lines.append(f"   draws {row['draws']:>7.1f}  {row['primary_ms']:>7.3f} ms vs "
                         f"{row['baseline_ms']:>7.3f} ms  x{fmt(row['factor'], 2)}"
                         f"  windows {row['primary_windows']}/{row['baseline_windows']}")
        if table['factor']:
            lines.append(f"   factor min {table['factor']['min']} median "
                         f"{table['factor']['median']} max {table['factor']['max']}")
        lines.append(f"   slope through the origin: median "
                     f"{table['origin_us_per_draw']['median']} us/draw, sign test "
                     f"{table['sign_test'].get('positive')}+/{table['sign_test'].get('negative')}-"
                     f" p={table['sign_test'].get('p_two_sided')}")
        lines.append('')
    hook = report.get('scene_hook')
    if hook:
        lines.append(f"-- scene hook: records {hook.get('records')} checks "
                     f"{hook.get('check_distribution')} source {hook.get('source_distribution')}"
                     f" resolved by {hook.get('resolved_by_source')}")
        lines.append(f"   latched {hook.get('latched_frames')} all-agree "
                     f"{hook.get('latched_all_agree')} draws after hook max "
                     f"{hook.get('latched_draws_after_hook_max')} outside-scene total "
                     f"{hook.get('latched_outside_scene_total')}")
        baseline = report.get('scene_hook_baseline')
        if baseline:
            lines.append(f"   baseline: checks {baseline.get('check_distribution')} source "
                         f"{baseline.get('source_distribution')}")
        lines.append('')
    blur = report.get('blur')
    if blur:
        lines.append('-- stationary-burst sharpness against the ideal supersampling floor')
        for burst in blur['bursts']:
            if burst['status'] != 'evaluated':
                lines.append(f"   frames {burst['frames']}: unavailable")
                continue
            lines.append(f"   frames {burst['frames'][0]}-{burst['frames'][-1]} "
                         f"interior {burst['routed_interior_px']} px: measured "
                         f"{burst['measured']} ideal {burst['ideal_floor']} "
                         f"ratio {burst['measured_over_ideal']} share of loss "
                         f"{burst['share_of_loss_that_is_supersampling']}")
            lines.append(f"      iteration-8 comparable mean "
                         f"{burst['iteration8_comparable_mean']} {burst['iteration8_comparable_ratio']}"
                         f"  sentinel {burst['sentinel_ratio']}"
                         f"  raw phase spread {fmt(burst['raw_phase_spread_routed_interior'], 4)}")
        if blur['share_of_loss']:
            lines.append(f"   share of loss that is ideal supersampling: "
                         f"{blur['share_of_loss']['min']} to {blur['share_of_loss']['max']}")
        baseline = report.get('blur_baseline')
        if baseline and baseline['share_of_loss']:
            lines.append(f"   baseline run share of loss: {baseline['share_of_loss']['min']}"
                         f" to {baseline['share_of_loss']['max']}")
        lines.append('')
    for limit in report.get('limits', []):
        lines.append(f"! {limit}")
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--run', action='append', default=[], metavar='LABEL=PATH',
                        help='a session log: line kinds and telemetry metric medians')
    parser.add_argument('--new-run', default=None,
                        help='the label whose line kinds are diffed against the others')
    parser.add_argument('--new-run-peer', action='append', default=[],
                        help='a label that is also a new-bottle run, so not a baseline')
    parser.add_argument('--it09', action='append', default=[], metavar='LABEL=JSON',
                        help='an analyze_iteration09.py summary JSON')
    parser.add_argument('--analyze', action='append', default=[], metavar='LABEL=PATH',
                        help='a log to run analyze_iteration09.analyze on directly')
    parser.add_argument('--camera', action='append', default=[], metavar='LABEL=JSON',
                        help='an analyze_camera_state.py report JSON')
    parser.add_argument('--readback', action='append', default=[], metavar='LABEL=JSON',
                        help='an analyze_motion_readback.py summary JSON')
    parser.add_argument('--cost', type=Path, default=None,
                        help='an analyze_iteration09_cost.py report JSON')
    parser.add_argument('--speedup', action='append', default=[], metavar='PRIMARY:BASELINE',
                        help='a comparison in the cost report to tabulate as a factor')
    parser.add_argument('--metric-pair', action='append', default=[],
                        metavar='PRIMARY:BASELINE',
                        help='two run labels to compare per-call metric medians for')
    parser.add_argument('--taa', type=Path, default=None,
                        help='an analyze_iteration09_run2.py summary JSON (scene hook, blur)')
    parser.add_argument('--taa-baseline', type=Path, default=None,
                        help='the same for the old-bottle baseline run')
    parser.add_argument('--kind-counts', action='store_true',
                        help='keep the full per-run line-kind histogram in the JSON')
    parser.add_argument('--device', default='1')
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
