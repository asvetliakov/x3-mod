#!/usr/bin/env python3
"""Iteration 9 (first gameplay run of the camera-reprojection build) analysis.

This run was captured **without** ``--taa-debug``, so there are no colour or
resolved-image readbacks and none of the image measurements of
``analyze_iteration07_taa`` / ``analyze_iteration08_taa`` can be reproduced.
Run those two tools for the route/TAA health and the telemetry aggregation
(they are imported here, not copied); ``analyze_camera_state.py`` for the
draw-constant cross-check; ``analyze_motion_readback.py`` for the readbacks.

Only the evidence those tools do not produce lives here.

1. ``timing_regimes`` - the session's frame time is bimodal (iteration 8 §6),
   and the iteration-8 analyzer only splits the regimes inside its *two-session*
   comparison, which needs a ``--run-b-log``.  This run has one session, so the
   per-window means are binned and a median is reported per regime, split at
   ``--window-split-us``.  A regime median is not a frame-time measurement of
   any feature: the two regimes are different parts of the scene and the whole
   session had the feature on.

2. ``camera`` - the ``camera_state`` records: validity, the field of view
   derived from ``p00``/``p11``, the policy/reason/cut distributions, and two
   things the camera tool does not compute:

   * the **rotation floor**.  ``camera_rotation_degrees`` (camera_reprojection.h)
     is ``acos((tr(R_a^T R_b) - 1)/2)``, which assumes both rotations are
     orthonormal.  The engine's view rows are float and their norms are short of
     one by up to ~3e-5, so the same matrix against *itself* reports a nonzero
     angle.  That self-angle is the noise floor of every ``rotation_deg`` in the
     log and is computed here per record.
   * the **screen-displacement prediction**.  A pure rotation ``a`` moves a
     far-plane pixel by ``a * p00 * width/2`` horizontally and
     ``a * p11 * height/2`` vertically, which is compared with the route's own
     ``cut_median_px`` and (offline) with the motion readback's median
     displacement.

3. ``samplers`` - the sampler state the game has bound on the draws the route
   sees, from the capture snapshots, split by whether the route routed the draw.
   This is the baseline for deciding a negative texture LOD bias for routed
   draws.  The capture records only the sampler states ``capture.cpp`` asks for;
   the report names the ones it does *not* record, because
   ``D3DSAMP_MIPMAPLODBIAS`` is one of them.

4. ``resolve_lowpass`` - what the resolve's own low-pass costs, as a model
   rather than a measurement: the modulation transfer of one Catmull-Rom
   history resample at a fractional offset, and the steady state of the
   exponential history blend ``A = (1-w) + w*r*A``.  The model is checked
   against the two amplitude ratios the fixture measured
   (docs/verification/temporal-resolve.md, "Resampling blur": Catmull-Rom 0.930,
   bilinear 0.712 for a period-8 pattern at a 0.75-texel offset per frame) and
   then extrapolated to other periods.  It says nothing about this run's
   images, which were not captured.
"""

import argparse
import cmath
import json
import math
import statistics
import sys
from collections import Counter, OrderedDict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze_iteration07_taa as it07  # noqa: E402

# D3DSAMPLERSTATETYPE values the capture may record, and the ones it does not.
SAMPLER_STATES = {1: 'ADDRESSU', 2: 'ADDRESSV', 3: 'ADDRESSW', 4: 'BORDERCOLOR',
                  5: 'MAGFILTER', 6: 'MINFILTER', 7: 'MIPFILTER', 8: 'MIPMAPLODBIAS',
                  9: 'MAXMIPLEVEL', 10: 'MAXANISOTROPY', 11: 'SRGBTEXTURE',
                  12: 'ELEMENTINDEX', 13: 'DMAPOFFSET'}
FILTERS = {0: 'NONE', 1: 'POINT', 2: 'LINEAR', 3: 'ANISOTROPIC', 4: 'PYRAMIDALQUAD', 5: 'GAUSSIANQUAD'}
# The states that decide texture sharpness; a bias question needs all four.
SHARPNESS_STATES = ('MINFILTER', 'MAGFILTER', 'MIPFILTER', 'MAXANISOTROPY', 'MIPMAPLODBIAS')


# ---- log scan -----------------------------------------------------------------------

def scan(path, device='1'):
    """One streaming pass: frame records, camera records, timing windows and the
    per-draw sampler census of the capture snapshots."""
    frames = []
    cameras = []
    windows = {}
    scene = {}
    summary_frame = None
    witnesses = Counter()
    samplers = Counter()          # (classification, state name, value) -> draws
    stage0 = Counter()            # (classification, signature of stage 0) -> draws
    draw_classes = Counter()
    states_seen = Counter()
    draw = None                   # current capture draw block

    def flush(classification):
        """Attribute the open draw block's sampler state to one class."""
        if draw is None:
            return
        draw_classes[classification] += 1
        stages = draw['samplers']
        for stage, values in sorted(stages.items()):
            for state, value in sorted(values.items()):
                name = SAMPLER_STATES.get(state, 'state%d' % state)
                samplers[(classification, stage, name, value)] += 1
        if 0 in stages:
            v = stages[0]
            stage0[(classification,
                    FILTERS.get(v.get(6), str(v.get(6))),
                    FILTERS.get(v.get(5), str(v.get(5))),
                    FILTERS.get(v.get(7), str(v.get(7))),
                    v.get(10), v.get(11))] += 1

    for event, line in it07.stream_records(Path(path)):
        if event == 'motion_output_frame':
            f = it07.fields(line)
            if f.get('device') == device:
                frames.append(f)
                scene[it07.number(f.get('frame'))] = {
                    'latched': it07.number(f.get('latched'), 0),
                    'routed': it07.number(f.get('routed'), 0),
                    'taa_resolved': it07.number(f.get('taa_resolved'), 0)}
        elif event == 'camera_state':
            f = it07.fields(line)
            if f.get('device') == device and 'frame' in f:
                cameras.append(f)
        elif event == 'telemetry_metric':
            f = it07.fields(line)
            if f.get('name') in it07.WINDOW_METRICS:
                windows.setdefault(f.get('device', '?') + ':' + f['name'], []).append(
                    {'frame': summary_frame, 'count': it07.number(f.get('count')),
                     'total_us': it07.real(f.get('total_us')), 'min_us': it07.real(f.get('min_us')),
                     'max_us': it07.real(f.get('max_us'))})
        elif event == 'telemetry_summary':
            f = it07.fields(line)
            if f.get('device') == device:
                summary_frame = it07.number(f.get('frame'))
        elif event == 'draw':
            f = it07.fields(line)
            if f.get('device') == device:
                flush('no_route_record')     # the previous block, if it never reached the gate
                draw = {'frame': it07.number(f.get('frame')), 'index': it07.number(f.get('index')),
                        'vs': f.get('vs'), 'ps': f.get('ps'), 'samplers': {}}
        elif event == 'sampler' and draw is not None:
            f = it07.fields(line)
            stage, state, value = (it07.number(f.get(k)) for k in ('stage', 'state', 'value'))
            # MIPMAPLODBIAS is a float bit pattern in the DWORD; capture.cpp
            # logs the raw value and, since the mip-bias work, `bias=<float>`,
            # which is the value worth tabulating (-0.5, not 3204448256).
            if state == 8 and f.get('bias') is not None:
                value = f['bias']
            if stage is not None and state is not None:
                draw['samplers'].setdefault(stage, {})[state] = value
                states_seen[SAMPLER_STATES.get(state, 'state%d' % state)] += 1
        elif event == 'motion_route':
            f = it07.fields(line)
            if f.get('device') != device:
                continue
            if f.get('routed') == '1':
                flush('routed_matched' if f.get('matched') == '1' else 'routed_unmatched')
            else:
                flush('rejected_gate%s' % f.get('gate'))
            draw = None
        elif event in ('motion_output_device', 'motion_output_taa', 'motion_output_target',
                       'motion_output_variant', 'motion_output_reset', 'motion_output_release',
                       'motion_output_taa_failed', 'device_reset', 'device_destroy',
                       'ownership_factory', 'object_trace', 'object_lifetime',
                       'motion_output_readback', 'motion_output_depth_readback',
                       'motion_output_scene_hook_disagreement'):
            witnesses[event] += 1
    flush('no_route_record')
    return {'frames': frames, 'cameras': cameras, 'windows': windows, 'scene': scene,
            'witnesses': dict(sorted(witnesses.items())), 'samplers': samplers,
            'stage0': stage0, 'draw_classes': draw_classes, 'sampler_states_seen': dict(sorted(states_seen.items()))}


# ---- 1. health ----------------------------------------------------------------------

def health(frames):
    """The `motion_output_frame` aggregation, including the fields iteration 8
    did not have: `scene_end_source`, the state-shadow counters and the
    per-frame route cost."""
    if not frames:
        return {'status': 'unavailable'}
    def hist(key):
        return dict(sorted(Counter(f.get(key) for f in frames).items()))
    def total(key):
        return sum(it07.number(f.get(key), 0) for f in frames)
    resolved = [f for f in frames if f.get('taa_resolved') == '1']
    cuts = [{'frame': it07.number(f.get('frame')), 'cut_median_px': it07.real(f.get('cut_median_px')),
             'cut_missing': it07.real(f.get('cut_missing')), 'cut_samples': it07.number(f.get('cut_samples')),
             'taa_history': it07.number(f.get('taa_history')), 'camera_rotation_deg': it07.real(f.get('camera_rotation_deg'))}
            for f in frames if f.get('cut') == '1']
    costs = {}
    for label, subset in (('capture', [f for f in frames if it07.number(f.get('readbacks'), 0)]),
                          ('normal', [f for f in resolved if not it07.number(f.get('readbacks'), 0)])):
        if not subset:
            continue
        costs[label] = {'frames': len(subset)}
        for key in ('gate_us', 'route_draw_us', 'set_rt_us', 'lazy_flush_us', 'jitter_us', 'fill_us',
                    'taa_run_us', 'readback_us'):
            values = [it07.real(f.get(key), 0.0) for f in subset]
            costs[label][key] = {'mean': statistics.fmean(values), 'max': max(values)}
        # route_gate/route_draw/route_fill/route_lazy_flush exclude each other
        # (docs/verification/telemetry.md); only those four may be added.
        exclusive = [sum(it07.real(f.get(k), 0.0) for k in ('gate_us', 'route_draw_us', 'fill_us', 'lazy_flush_us'))
                     for f in subset]
        costs[label]['route_exclusive_us'] = {'mean': statistics.fmean(exclusive), 'max': max(exclusive)}
    return {
        'status': 'evaluated',
        'frame_records': len(frames),
        'taa_attempted': sum(1 for f in frames if f.get('taa_attempted') == '1'),
        'taa_resolved': len(resolved),
        'taa_history': sum(1 for f in frames if f.get('taa_history') == '1'),
        'taa_skip': hist('taa_skip'), 'taa_result': hist('taa_result'),
        'resolved_without_history': [it07.number(f.get('frame')) for f in resolved if f.get('taa_history') != '1'],
        'selector_state': hist('selector_state'), 'rt_mode': hist('rt_mode'),
        'scene_end_source': hist('scene_end_source'), 'scene_end_check': hist('scene_end_check'),
        'camera_policy': hist('camera_policy'), 'camera_valid': hist('camera_valid'),
        'cut_events': cuts,
        'camera_cut_events': [it07.number(f.get('frame')) for f in frames if f.get('camera_cut') == '1'],
        'routed_frames_without_camera': [it07.number(f.get('frame')) for f in frames
                                         if f.get('camera_valid') != '1' and it07.number(f.get('routed'), 0) > 0],
        'apply_failures': total('apply_failures'), 'restore_failures': total('restore_failures'),
        'state_shadow': {k: total(k) for k in ('rs_queries', 'rs_hits', 'rs_gets', 'rs_resyncs')},
        'totals': {k: total(k) for k in ('draws', 'routed', 'matched', 'gate1', 'gate2', 'gate3',
                                         'gate4', 'gate5', 'gate6', 'jitter_writes', 'set_rt',
                                         'lazy_flushes', 'readbacks')},
        'route_costs': costs}


# ---- 2. timing regimes --------------------------------------------------------------

def timing_regimes(windows, scene, split_us=45000.0, edges=(20000, 30000, 40000, 50000, 60000, 70000, 100000),
                   outlier_us=1000000.0):
    """Per-window means binned, and a median per regime.

    A window reports count/total/min/max only, so the per-window mean
    (total/count) is the finest statistic that exists (it07.describe_windows).
    Windows whose mean exceeds `outlier_us` are load/alt-tab stalls and are
    counted separately instead of entering a median."""
    out = OrderedDict()
    split = it07.split_windows(windows, scene)
    for name, items in sorted(windows.items()):
        entries = [w for w in items if w['count']]
        means = sorted(w['total_us'] / w['count'] for w in entries)
        stalls = [v for v in means if v > outlier_us]
        means = [v for v in means if v <= outlier_us]
        bins = OrderedDict()
        previous = 0
        for edge in edges:
            bins['%d-%dms' % (previous // 1000, edge // 1000)] = sum(1 for v in means if previous <= v < edge)
            previous = edge
        bins['>=%dms' % (previous // 1000)] = sum(1 for v in means if v >= previous)
        fast = [v for v in means if v < split_us]
        slow = [v for v in means if v >= split_us]
        out[name] = {'windows': len(entries), 'stall_windows': len(stalls),
                     'split_us': split_us, 'bins': bins,
                     'fast': {'windows': len(fast), 'median_us': statistics.median(fast) if fast else None},
                     'slow': {'windows': len(slow), 'median_us': statistics.median(slow) if slow else None},
                     'distribution': it07.describe_windows(entries),
                     'by_regime': split.get(name, {}),
                     'note': 'the two regimes are different parts of the scene, not two configurations'}
    return out


# ---- 3. camera ----------------------------------------------------------------------

CAMERA_COLUMNS = ('frame', 'valid', 'policy', 'reason', 'camera_cut', 'rotation_deg',
                  'rotation_floor_deg', 'row_norm_deviation', 'history_view_valid')

REASONS = {0: 'camera_path', 1: 'switch_off', 2: 'current_invalid', 3: 'previous_invalid',
           4: 'rotation_cut', 5: 'transform_failed'}


def rotation_degrees(a, b):
    """`camera_rotation_degrees` of src/renderer/camera_reprojection.h, in Python."""
    trace = sum(x * y for x, y in zip(a, b))
    c = max(-1.0, min(1.0, (trace - 1.0) * 0.5))
    return math.degrees(math.acos(c))


def fov_degrees(p00, p11):
    """The engine's projection is `x_ndc = p00 * x_view / z_view`, so the
    half-angle of the frustum is `atan(1/p00)` horizontally."""
    out = {}
    if p00:
        out['horizontal_deg'] = 2.0 * math.degrees(math.atan(1.0 / p00))
    if p11:
        out['vertical_deg'] = 2.0 * math.degrees(math.atan(1.0 / p11))
    if p00 and p11:
        out['aspect'] = p11 / p00
    return out


def camera_report(cameras, width=1280, height=768, detail=True):
    if not cameras:
        return {'status': 'unavailable'}
    rows = []
    for f in cameras:
        r = [it07.real(f.get('r%d%d' % (i, j)), 0.0) for i in range(3) for j in range(3)]
        rotation = it07.real(f.get('rotation_deg'), 0.0)
        valid = f.get('valid') == '1'
        # The floor: the same rotation against itself must be zero for an
        # orthonormal matrix; it is not, because the engine's rows are short.
        floor = rotation_degrees(r, r) if valid else None
        row = {'frame': it07.number(f.get('frame')), 'valid': valid,
               'policy': it07.number(f.get('policy')),
               'reason': REASONS.get(it07.number(f.get('reason'), 1), 'unknown'),
               'camera_cut': f.get('camera_cut') == '1',
               'read_failure': f.get('read_failure'), 'failure': f.get('failure'),
               'rotation_deg': rotation, 'rotation_floor_deg': floor,
               'p00': it07.real(f.get('p00')), 'p11': it07.real(f.get('p11')),
               'history_view_valid': f.get('history_view_valid') == '1',
               'background_valid': f.get('background_valid') == '1',
               'background_rotation_deg': it07.real(f.get('background_rotation_deg'))}
        norms = [math.sqrt(sum(r[i * 3 + j] ** 2 for j in range(3))) for i in range(3)] if valid else None
        row['row_norm_deviation'] = max(abs(1.0 - n) for n in norms) if norms else None
        if valid and rotation is not None:
            a = math.radians(rotation)
            # A pure rotation `a` moves a far-plane pixel by this much.
            row['predicted_displacement_px'] = [a * row['p00'] * width / 2.0 if row['p00'] else None,
                                                a * row['p11'] * height / 2.0 if row['p11'] else None]
        rows.append(row)
    valid_rows = [r for r in rows if r['valid']]
    floors = [r['rotation_floor_deg'] for r in valid_rows]
    projections = Counter((r['p00'], r['p11']) for r in valid_rows)
    return {
        'status': 'evaluated',
        'records': len(rows), 'valid': len(valid_rows),
        'policy': dict(sorted(Counter(r['policy'] for r in rows).items())),
        'reason': dict(sorted(Counter(r['reason'] for r in rows).items())),
        'read_failures': dict(sorted(Counter((r['read_failure'], r['failure']) for r in rows
                                             if r['read_failure'] not in (None, '0')).items())),
        'camera_cuts': [r['frame'] for r in rows if r['camera_cut']],
        'projections': [{'p00': p00, 'p11': p11, 'records': n, 'fov': fov_degrees(p00, p11)}
                        for (p00, p11), n in projections.most_common()],
        'rotation_deg': {'max': max((r['rotation_deg'] for r in valid_rows), default=None),
                         'median': statistics.median([r['rotation_deg'] for r in valid_rows]) if valid_rows else None},
        'rotation_floor_deg': {'min': min(floors), 'median': statistics.median(floors), 'max': max(floors)} if floors else None,
        'row_norm_deviation_max': max((r['row_norm_deviation'] for r in valid_rows), default=None),
        'invalid_frames': [r['frame'] for r in rows if not r['valid']],
        # One compact row per record: a 42-record session must stay readable.
        'columns': CAMERA_COLUMNS,
        'records_table': [[r[c] for c in CAMERA_COLUMNS] for r in rows],
        'records_detail': rows if detail else None}


# ---- 4. sampler census --------------------------------------------------------------

DEFAULT_STAGE = {'MINFILTER': {'POINT'}, 'MAGFILTER': {'POINT'}, 'MIPFILTER': {'NONE'},
                 'MAXANISOTROPY': {'1'}, 'SRGBTEXTURE': {'0'}, 'MIPMAPLODBIAS': {'0'}, 'MAXMIPLEVEL': {'0'}}


def is_default_stage(names):
    """True when every recorded state of the stage is at its D3D9 default in
    every draw, i.e. the stage samples nothing."""
    for name, expected in DEFAULT_STAGE.items():
        if name in names and set(names[name]) - expected:
            return False
    return bool(names)


def sampler_report(scan_result):
    samplers = scan_result['samplers']
    seen = set(scan_result['sampler_states_seen'])
    missing = [name for state, name in sorted(SAMPLER_STATES.items()) if name not in seen]
    per_state = {}
    per_stage = {}
    for (classification, stage, name, value) in samplers:
        count = samplers[(classification, stage, name, value)]
        label = FILTERS.get(value, str(value)) if name in ('MINFILTER', 'MAGFILTER', 'MIPFILTER') else str(value)
        per_state.setdefault(classification, {}).setdefault(name, Counter())[label] += count
        per_stage.setdefault(classification, {}).setdefault(stage, {}).setdefault(name, Counter())[label] += count
    return {
        'draw_blocks': dict(sorted(scan_result['draw_classes'].items())),
        'states_recorded': scan_result['sampler_states_seen'],
        'states_not_recorded': missing,
        'sharpness_states_missing': [n for n in SHARPNESS_STATES if n in missing],
        'per_class': {c: {n: dict(v.most_common()) for n, v in sorted(d.items())} for c, d in sorted(per_state.items())},
        # Stages left at the D3D9 defaults (POINT/POINT/no mips, anisotropy 1,
        # sRGB off) carry no texture for the draw; they are listed, not tabulated.
        'per_stage': {c: {str(stage): {n: dict(v.most_common()) for n, v in sorted(names.items())}
                          for stage, names in sorted(stages.items()) if not is_default_stage(names)}
                      for c, stages in sorted(per_stage.items())},
        'default_stages': {c: [stage for stage, names in sorted(stages.items()) if is_default_stage(names)]
                           for c, stages in sorted(per_stage.items())},
        'stage0_signatures': [{'class': k[0], 'min': k[1], 'mag': k[2], 'mip': k[3], 'aniso': k[4],
                               'srgb': k[5], 'draws': n}
                              for k, n in scan_result['stage0'].most_common(20)],
        'note': ('capture.cpp records the sampler states it enumerates; MIPMAPLODBIAS and MAXMIPLEVEL '
                 'are among them since the mip-bias work (logged raw and as bias=<float>)'
                 if 'MIPMAPLODBIAS' in seen else
                 'capture.cpp records only the sampler states it enumerates; a texture LOD bias '
                 'decision needs D3DSAMP_MIPMAPLODBIAS, which this log predates')}


# ---- 5. the resolve's own low-pass (model, not a measurement) -----------------------

def catmull_rom_weights(t):
    """The four taps of the resolve's history resample at fractional offset t,
    relative to taps -1, 0, 1, 2 (src/temporal/resolve.hlsl)."""
    t2, t3 = t * t, t * t * t
    return [-0.5 * t3 + t2 - 0.5 * t,
            1.5 * t3 - 2.5 * t2 + 1.0,
            -1.5 * t3 + 2.0 * t2 + 0.5 * t,
            0.5 * t3 - 0.5 * t2]


def bilinear_weights(t):
    return [0.0, 1.0 - t, t, 0.0]


def transfer(weights, period):
    """Magnitude of the resampling filter at one spatial period, in pixels."""
    omega = 2.0 * math.pi / period
    value = sum(w * cmath.exp(-1j * omega * (k - 1)) for k, w in enumerate(weights))
    return abs(value)


def steady_state(ratio, weight):
    """Amplitude retained by `out = (1-w)*current + w*resample(out)` once the
    sequence settles: A = (1-w) / (1 - w*r)."""
    denominator = 1.0 - weight * ratio
    return (1.0 - weight) / denominator if denominator > 0 else None


def resolve_lowpass(weight=0.9, offsets=(0.25, 0.5, 0.75), periods=(2, 3, 4, 6, 8, 16, 32)):
    table = []
    for offset in offsets:
        cr = catmull_rom_weights(offset)
        bl = bilinear_weights(offset)
        for period in periods:
            r_cr, r_bl = transfer(cr, period), transfer(bl, period)
            table.append({'offset': offset, 'period_px': period,
                          'catmull_rom_tap': r_cr, 'catmull_rom_steady': steady_state(r_cr, weight),
                          'bilinear_tap': r_bl, 'bilinear_steady': steady_state(r_bl, weight)})
    # docs/verification/temporal-resolve.md, "Resampling blur": measured on the
    # fixture with exactly this configuration (period 8, 0.75-texel offset, w = 0.9).
    reference = {'period_px': 8, 'offset': 0.75, 'history_weight': weight,
                 'fixture_catmull_rom_amplitude': 0.930, 'fixture_bilinear_amplitude': 0.712,
                 'fixture_stationary_ramp_gradient_energy': 0.9947}
    for key, filt in (('catmull_rom', catmull_rom_weights), ('bilinear', bilinear_weights)):
        modelled = steady_state(transfer(filt(0.75), 8), weight)
        reference['model_%s_amplitude' % key] = modelled
        reference['model_%s_error' % key] = abs(modelled - reference['fixture_%s_amplitude' % key])
    return {'history_weight': weight, 'reference': reference, 'table': table,
            'note': ('a model of the resolve, not a measurement of this run: no colour or resolved '
                     'image was read back without --taa-debug. It bounds the loss that the '
                     'history resample alone causes under sustained fractional motion; the variance '
                     'clip, the dilation and the hard depth rejection are separate and act on top.')}


# ---- report -------------------------------------------------------------------------

def analyze(path, device='1', width=1280, height=768, split_us=45000.0, history_weight=0.9, detail=True):
    result = scan(path, device)
    return OrderedDict([
        ('tool', {'name': 'tools/analysis/analyze_iteration09.py',
                  'imports': 'tools/analysis/analyze_iteration07_taa.py (record stream, window statistics)',
                  'companions': ['tools/analysis/analyze_iteration08_taa.py',
                                 'tools/analysis/analyze_camera_state.py',
                                 'tools/analysis/analyze_motion_readback.py',
                                 'tools/analysis/summarize_telemetry.py']}),
        ('source', {'log': str(path), 'device': device, 'width': width, 'height': height}),
        ('witnesses', result['witnesses']),
        ('health', health(result['frames'])),
        ('timing_regimes', timing_regimes(result['windows'], result['scene'], split_us=split_us)),
        ('camera', camera_report(result['cameras'], width, height, detail=detail)),
        ('samplers', sampler_report(result)),
        ('resolve_lowpass', resolve_lowpass(weight=history_weight)),
    ])


def render_text(report):
    lines = []
    h = report['health']
    lines.append('health: %s frame records, attempted %s, resolved %s, history %s, failures %s/%s'
                 % (h.get('frame_records'), h.get('taa_attempted'), h.get('taa_resolved'),
                    h.get('taa_history'), h.get('apply_failures'), h.get('restore_failures')))
    lines.append('  taa_skip %s; scene_end_source %s; rt_mode %s; selector_state %s'
                 % (h.get('taa_skip'), h.get('scene_end_source'), h.get('rt_mode'), h.get('selector_state')))
    lines.append('  totals %s' % h.get('totals'))
    lines.append('  state shadow %s' % h.get('state_shadow'))
    lines.append('  cut events %s' % [(c['frame'], round(c['cut_median_px'], 3)) for c in h.get('cut_events', [])])
    lines.append('  routed frames without a camera read: %s' % h.get('routed_frames_without_camera'))
    for label, cost in (h.get('route_costs') or {}).items():
        lines.append('  route cost %s frames=%s exclusive mean %.1fus taa_run mean %.1fus'
                     % (label, cost['frames'], cost['route_exclusive_us']['mean'], cost['taa_run_us']['mean']))
    for name, regimes in report['timing_regimes'].items():
        if not name.endswith('frame_normal'):
            continue
        lines.append('timing %s: %s windows (%s stalls), fast %s median %.2fms, slow %s median %.2fms'
                     % (name, regimes['windows'], regimes['stall_windows'], regimes['fast']['windows'],
                        (regimes['fast']['median_us'] or 0) / 1000.0, regimes['slow']['windows'],
                        (regimes['slow']['median_us'] or 0) / 1000.0))
        lines.append('  bins %s' % regimes['bins'])
    c = report['camera']
    lines.append('camera: %s records, %s valid, policy %s, reasons %s, cuts %s'
                 % (c.get('records'), c.get('valid'), c.get('policy'), c.get('reason'), c.get('camera_cuts')))
    for p in c.get('projections', []):
        lines.append('  p00=%s p11=%s on %s records: %s' % (p['p00'], p['p11'], p['records'], p['fov']))
    if c.get('rotation_floor_deg'):
        lines.append('  rotation_deg max %.4f; self-rotation floor %.4f-%.4f deg (row norm deviation %.2e)'
                     % (c['rotation_deg']['max'], c['rotation_floor_deg']['min'],
                        c['rotation_floor_deg']['max'], c['row_norm_deviation_max']))
    s = report['samplers']
    lines.append('samplers: draw blocks %s' % s['draw_blocks'])
    lines.append('  recorded %s' % list(s['states_recorded']))
    lines.append('  NOT recorded %s (sharpness-relevant: %s)' % (s['states_not_recorded'], s['sharpness_states_missing']))
    for sig in s['stage0_signatures'][:8]:
        lines.append('  stage0 %s: min=%s mag=%s mip=%s aniso=%s srgb=%s -> %s draws'
                     % (sig['class'], sig['min'], sig['mag'], sig['mip'], sig['aniso'], sig['srgb'], sig['draws']))
    r = report['resolve_lowpass']['reference']
    lines.append('resolve low-pass model (w=%.2f): period 8 at 0.75 texel, model %.3f vs fixture %.3f (Catmull-Rom), '
                 'model %.3f vs fixture %.3f (bilinear)'
                 % (r['history_weight'], r['model_catmull_rom_amplitude'], r['fixture_catmull_rom_amplitude'],
                    r['model_bilinear_amplitude'], r['fixture_bilinear_amplitude']))
    for row in report['resolve_lowpass']['table']:
        if row['offset'] == 0.5:
            lines.append('  period %2d px: tap %.4f, steady %.4f' % (row['period_px'], row['catmull_rom_tap'],
                                                                     row['catmull_rom_steady']))
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--text', type=Path)
    parser.add_argument('--device', default='1')
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--height', type=int, default=768)
    parser.add_argument('--window-split-us', type=float, default=45000.0,
                        help='the per-window mean that separates the session\'s two frame-time regimes')
    parser.add_argument('--history-weight', type=float, default=0.9)
    parser.add_argument('--no-camera-detail', action='store_true',
                        help='emit only the compact camera table, not one object per record')
    args = parser.parse_args(argv)
    report = analyze(args.log, args.device, args.width, args.height, args.window_split_us,
                     args.history_weight, not args.no_camera_detail)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1, sort_keys=False) + '\n')
    text = render_text(report)
    if args.text:
        args.text.write_text(text)
    sys.stdout.write(text)
    sys.stdout.write('summary: %s\n' % args.output)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
