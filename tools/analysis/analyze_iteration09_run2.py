#!/usr/bin/env python3
"""Iteration 9 run 2: engine scene-end hook, mesh adjacency cache, loading gaps,
resolve sharpness and the sampler-state baseline, from one `--telemetry
--profile --scene-hook --mesh-cache --taa --taa-debug` session log.

Everything here is streamed: the session log is read once, line by line, and
only sparse records are retained (no per-draw constant/matrix payload).  The
readbacks are loaded one burst at a time.

The sections and the definitions they use.

1. ``scene_hook`` - the engine scene-end patch.  Per logged frame the report
   keeps the `SceneEndCheck` verdict (`scene_end_check`: 0 None, 1 Agree, 2
   HookOnly, 3 StretchOnly, 4 Disagree - src/proxy/motion_output.h), where the
   frame's resolve actually ran (`scene_end_source`), the signal counters and
   `draws_after_hook`.  A frame that never latched a scene has nothing to
   cross-check and is reported separately with its `hook_state` (the selector
   state at an out-of-scene signal).  Any
   `motion_output_scene_hook_disagreement` record is listed verbatim-free (its
   fields only).

2. ``mesh_cache`` - the adjacency cache.  The cumulative `mesh_cache_metric`
   counters, the `mesh_cache_bypass` reason histogram and the
   `mesh_cache_fp_first` diagnostic, decoded against
   `mesh_adjacency_cache.cpp:supported_fp` so a `floating_point` bypass says
   *which* field of the incoming FPU state disqualified the call.

3. ``adjacency_timeline`` - the per-report-window `loading_metric
   op=ID3DXMesh::GenerateAdjacency` deltas, grouped into activity phases by a
   quiet-window gap, so the menu load, the save load and each sector change get
   their own call count, seconds and mean.  Requires no gap detection and works
   for phases the presentation-gap detector misses.

4. ``blur`` - the sharpness measurement, on bursts the log certifies.  A burst
   is classified from `cut_median_px` and `camera_rotation_deg`
   (stationary/slow/turning).  For every frame, on each pixel class of
   `analyze_iteration08_taa.classify_pixels` (sentinel / routed_interior /
   routed_edge), three quantities compare the resolved image with the raw
   pre-resolve colour of the same frame:

   * ``gradient_energy`` - mean squared central-difference luma gradient
     (`analyze_iteration07_taa.gradient_energy`, the iteration 7/8 measure, so
     the ratios are directly comparable).
   * ``laplacian_energy`` - mean squared 5-point Laplacian, a
     second-difference band the box filter attenuates much harder.
   * ``spectral band fraction`` - the fraction of windowed tile energy above a
     given fraction of Nyquist (default 1/4), on the highest-gradient tile.

   The measurement then separates two causes of a lower ratio:

   * ``jitter_phase_spread`` - the same gradient energy of the *four raw frames*
     of the burst.  Four different jitter phases sample the same static scene,
     so the spread of the raw energies is how much of any difference is the raw
     frame itself moving on the sampling grid, not detail loss.
   * ``ideal_supersampling`` - the exact image an ideal resolve would produce
     from this content: the raw frame resampled bilinearly to each of the
     session's jitter offsets and combined with the resolve's own geometric
     history weights (`--history-weight`, `src/renderer/temporal_pass.h`).
     Perfect reprojection, no clamp, no rejection.  Its gradient-energy ratio
     is the *floor* a correct resolve cannot beat; the excess of the measured
     ratio over it is what the resolve adds.
   * ``box_filter_reference`` - the analytic factor for the white-spectrum
     limit: the mean squared central difference after an ideal 1x1 px box
     prefilter, integrated over the Nyquist square (see
     ``box_filter_gradient_factor``).  Content-independent, for scale only.
   * ``edge_spread`` - an MTF-like estimate.  The strongest locally
     one-dimensional edges inside the mask are found, their signed 1-D profiles
     are averaged into one edge-spread function, its derivative is the line
     spread function, and the DFT magnitude of that is the MTF; the report gives
     the 10-90% rise in pixels and the MTF50 in cycles per pixel for the raw
     and the resolved image.

5. ``samplers`` - the sampler-state distribution of routed draws.  The capture
   snapshot records `D3DSAMP_MINFILTER/MAGFILTER/MIPFILTER/MAXANISOTROPY/
   SRGBTEXTURE` per stage (src/proxy/capture.cpp); the report joins each
   captured draw's snapshot with `motion_route routed=1` and gives per-stage
   histograms for routed and unrouted draws separately.  States the snapshot
   does *not* contain (notably `D3DSAMP_MIPMAPLODBIAS`) are listed as missing,
   because absence from the log is not evidence of the default value.

6. ``health`` - route/TAA totals, camera policy and cut distribution, taa_skip
   reasons, device reset count, and the per-window frame-time regimes of this
   run against a second session (`--baseline-log`), via
   `analyze_iteration08_taa.stream_scene_windows`.
"""
import argparse
import array
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
from analyze_iteration07_taa import LUMA, fields, number, real  # noqa: E402

SCENE_END_CHECK = {0: 'None', 1: 'Agree', 2: 'HookOnly', 3: 'StretchOnly', 4: 'Disagree'}
# src/proxy/motion_output.h MotionShadowState order, for hook_state of an
# out-of-scene signal.
SELECTOR_STATE = {0: 'Scene', 1: 'Bloom', 2: 'Post', 3: 'Ui', 4: 'Present', 5: 'Recording',
                  6: 'Queries', 7: 'Initialize', 8: 'Container', 9: 'CameraState', 10: 'Target'}
BYPASS_REASONS = ('input', 'runtime', 'floating_point', 'epsilon', 'configuration', 'disabled',
                  'contention', 'options', 'metadata', 'declaration', 'size', 'output_range',
                  'allocation', 'buffer_read')
# mesh_adjacency_cache.cpp supported_fp(): the verified D3D9 default profile.
FP_EXPECTED = {'control': 0x007f, 'tag': 0xffff, 'status_mask': 0xb800, 'mxcsr': 0x1f80,
               'mxcsr_ignore': 0x3f}
ADJACENCY_OP = 'ID3DXMesh::GenerateAdjacency'
# The states capture.cpp snapshots per draw. MIPMAPLODBIAS (8) and
# MAXMIPLEVEL (9) were added by the mip-bias work: a log that predates it
# reports them under states_not_recorded (computed from what the log holds).
SAMPLER_STATES = {5: 'magfilter', 6: 'minfilter', 7: 'mipfilter', 8: 'mipmaplodbias',
                  9: 'maxmiplevel', 10: 'maxanisotropy', 11: 'srgbtexture'}
SAMPLER_NEVER = {1: 'addressu', 2: 'addressv'}   # never snapshotted; listed as missing
TEXTURE_FILTER = {0: 'NONE', 1: 'POINT', 2: 'LINEAR', 3: 'ANISOTROPIC'}
HISTORY_WEIGHT = it08.HISTORY_WEIGHT  # 0.9, src/renderer/temporal_pass.h
WIDTH, HEIGHT = 1280, 768
# A burst is stationary when the route's own cut detector stays inside this many
# pixels of median displacement in every frame; "slow" up to the second bound.
STATIONARY_PX = 1.0
SLOW_PX = 10.0


class Malformed(Exception):
    pass


# ---- log streaming -----------------------------------------------------------------

FRAME_KEYS = ('frame', 'latched', 'draws', 'routed', 'matched', 'jitter', 'jitter_index',
              'jitter_x', 'jitter_y', 'cut', 'cut_median_px', 'cut_missing', 'cut_samples',
              'taa', 'taa_attempted', 'taa_resolved', 'taa_history', 'taa_skip',
              'camera_valid', 'camera_policy', 'camera_reason', 'camera_cut',
              'camera_rotation_deg', 'scene_end_source', 'scene_end_check', 'hook_signals',
              'hook_outside_scene', 'hook_state', 'draws_after_hook', 'bloom_copy_seen',
              'gate1', 'gate2', 'gate3', 'gate4', 'gate5', 'gate6', 'apply_failures',
              'restore_failures', 'taa_run_us', 'gate_us', 'route_draw_us')


def scan_log(path, device='1'):
    """One streaming pass: the sparse records every section needs."""
    out = {
        'path': str(path), 'device': device,
        'configuration': {}, 'scene_hook_lines': [], 'disagreements': [],
        'frames': [], 'mesh_cache': {'bypass': Counter(), 'metric': None, 'fp_first': None,
                                     'config': [], 'hook': [], 'bypass_lines': 0,
                                     'metric_lines': 0},
        'adjacency_windows': [], 'reset_count': None, 'stretch_rect': 0,
        'sampler_groups': {'routed': Counter(), 'unrouted': Counter()},
        'sampler_stage_groups': {'routed': {}, 'unrouted': {}},
        'sampler_draws': {'routed': 0, 'unrouted': 0},
        'sampler_states_seen': set(),
        'counts': Counter(),
    }
    # A captured draw's records arrive as one block: `draw`, then the state and
    # sampler snapshot, then `motion_route` for a routed draw, then
    # `draw_result`.  The snapshot is therefore folded into the histograms at
    # `draw_result`, and no per-draw record is retained.
    draw = None
    for event, line in it07.stream_records(path):
        out['counts'][event] += 1
        if event in ('motion_output_mode', 'motion_output_device', 'motion_output_taa',
                     'motion_capture_mode', 'x3-modern-renderer', 'telemetry_start',
                     'adapter', 'backend'):
            out['configuration'].setdefault(event, []).append(fields(line))
        elif event == 'scene_hook':
            out['scene_hook_lines'].append(fields(line))
        elif event == 'scene_hook_shutdown':
            out['scene_hook_lines'].append(dict(fields(line), record='shutdown'))
        elif event == 'motion_output_scene_hook_disagreement':
            out['disagreements'].append(fields(line))
        elif event == 'motion_output_frame':
            f = fields(line)
            if f.get('device') == device:
                out['frames'].append({k: f.get(k) for k in FRAME_KEYS})
        elif event == 'mesh_cache':
            out['mesh_cache']['config'].append(fields(line))
        elif event == 'mesh_hook':
            out['mesh_cache']['hook'].append(fields(line))
        elif event == 'mesh_cache_bypass':
            f = fields(line)
            out['mesh_cache']['bypass_lines'] += 1
            out['mesh_cache']['bypass'][f.get('reason')] = number(f.get('count'), 0)
        elif event == 'mesh_cache_metric':
            out['mesh_cache']['metric'] = fields(line)
            out['mesh_cache']['metric_lines'] += 1
        elif event == 'mesh_cache_fp_first':
            out['mesh_cache']['fp_first'] = fields(line)
        elif event == 'loading_metric':
            f = fields(line)
            if f.get('op') == ADJACENCY_OP:
                out['adjacency_windows'].append(
                    {'qpc': number(f.get('qpc')), 'count': number(f.get('count'), 0),
                     'exclusive_us': real(f.get('exclusive_us'), 0.0),
                     'max_us': real(f.get('max_us'), 0.0)})
        elif event == 'telemetry_first_present':
            out['reset_count'] = number(fields(line).get('reset_count'))
        elif event == 'stretch_rect':
            out['stretch_rect'] += 1
        elif event == 'draw':
            f = fields(line)
            draw = {'frame': number(f.get('frame')), 'index': number(f.get('index')),
                    'routed': False, 'stages': {}}
        elif event == 'motion_route' and draw is not None:
            if fields(line).get('routed') == '1':
                draw['routed'] = True
        elif event == 'texture' and draw is not None:
            f = fields(line)
            stage = number(f.get('stage'))
            if stage is not None and f.get('ptr') not in (None, '00000000'):
                draw['stages'].setdefault(stage, {'type': number(f.get('type'))})
        elif event == 'sampler' and draw is not None:
            f = fields(line)
            stage, state = number(f.get('stage')), number(f.get('state'))
            name = SAMPLER_STATES.get(state)
            if name:
                out['sampler_states_seen'].add(name)
            if stage is not None and name and stage in draw['stages']:
                # The LOD bias is a float bit pattern in the DWORD: capture.cpp
                # logs it raw and as `bias=<float>`; the float is the value.
                if state == 8:
                    raw = number(f.get('value'))
                    value = (float(f['bias']) if f.get('bias') is not None
                             else struct.unpack('<f', struct.pack('<I', raw))[0] if raw is not None else None)
                else:
                    value = number(f.get('value'))
                draw['stages'][stage][name] = value
        elif event == 'draw_result' and draw is not None:
            if draw['stages']:
                fold_sampler_draw(out, draw)
            draw = None
    return out


def sampler_signature(state):
    return (TEXTURE_FILTER.get(state.get('minfilter'), state.get('minfilter')),
            TEXTURE_FILTER.get(state.get('magfilter'), state.get('magfilter')),
            TEXTURE_FILTER.get(state.get('mipfilter'), state.get('mipfilter')),
            state.get('maxanisotropy'), state.get('srgbtexture'),
            # None in a log that predates the two states (not the 0 default)
            state.get('mipmaplodbias'), state.get('maxmiplevel'))


def fold_sampler_draw(out, draw):
    group = 'routed' if draw['routed'] else 'unrouted'
    out['sampler_draws'][group] += 1
    for stage, state in draw['stages'].items():
        signature = sampler_signature(state)
        out['sampler_groups'][group][signature] += 1
        out['sampler_stage_groups'][group].setdefault(stage, Counter())[signature] += 1


# ---- 1. scene hook -----------------------------------------------------------------

def scene_hook_report(scan):
    frames = scan['frames']
    checks = Counter()
    sources = Counter()
    unlatched = []
    per_frame = []
    for f in frames:
        check = number(f.get('scene_end_check'), 0)
        checks[SCENE_END_CHECK.get(check, f'unknown({check})')] += 1
        sources[f.get('scene_end_source')] += 1
        row = {
            'frame': number(f.get('frame')), 'latched': number(f.get('latched'), 0),
            'draws': number(f.get('draws'), 0), 'routed': number(f.get('routed'), 0),
            'check': SCENE_END_CHECK.get(check, str(check)),
            'source': f.get('scene_end_source'),
            'hook_signals': number(f.get('hook_signals'), 0),
            'hook_outside_scene': number(f.get('hook_outside_scene'), 0),
            'hook_state': SELECTOR_STATE.get(number(f.get('hook_state'), 0),
                                             f.get('hook_state')),
            'draws_after_hook': number(f.get('draws_after_hook'), 0),
            'bloom_copy_seen': number(f.get('bloom_copy_seen'), 0),
            'taa_attempted': number(f.get('taa_attempted'), 0),
            'taa_resolved': number(f.get('taa_resolved'), 0),
            'taa_skip': number(f.get('taa_skip'), 0),
        }
        per_frame.append(row)
        if not row['latched']:
            unlatched.append(row)
    resolved_at = Counter()
    for row in per_frame:
        if row['taa_resolved']:
            resolved_at[row['source']] += 1
    latched = [r for r in per_frame if r['latched']]
    return {
        'install': scan['scene_hook_lines'],
        'patch_site': {
            'callsite_va': '0x004721b1', 'expected_bytes': 'e8 9a 25 05 00',
            'target_va': '0x004c4750', 'return_va': '0x004721b6',
            'verified_by': 'object_trace::executable_verified() SHA-256 + exact '
                           'callsite bytes + resolved rel32 target (scene_hook.cpp); '
                           'status=active is reached only after all three pass, the '
                           'patch is written, FlushInstructionCache succeeds and the '
                           'original page protection is restored',
        },
        'records': len(frames),
        'check_distribution': dict(checks),
        'source_distribution': dict(sources),
        'resolved_by_source': dict(resolved_at),
        'disagreement_records': scan['disagreements'],
        'latched_frames': len(latched),
        'latched_all_agree': all(r['check'] == 'Agree' for r in latched),
        'latched_draws_after_hook_max': max([r['draws_after_hook'] for r in latched] or [0]),
        'latched_hook_signals': sorted({r['hook_signals'] for r in latched}),
        'latched_outside_scene_total': sum(r['hook_outside_scene'] for r in latched),
        'unlatched_frames': unlatched,
        'stretch_rect_records': scan['stretch_rect'],
        'shutdown_records': [r for r in scan['scene_hook_lines']
                             if r.get('record') == 'shutdown'],
        'per_frame': per_frame,
    }


# ---- 2. mesh adjacency cache -------------------------------------------------------

def decode_fp(diagnostic):
    """Which fields of an incoming FPU state fail mesh_adjacency_cache's
    `supported_fp`, and what the failing bits mean."""
    if not diagnostic:
        return None
    control = int(diagnostic.get('control', '0'), 16)
    status = int(diagnostic.get('status', '0'), 16)
    tag = int(diagnostic.get('tag', '0'), 16)
    mxcsr = int(diagnostic.get('mxcsr', '0'), 16)
    precision = {0: '24-bit single', 1: 'reserved', 2: '53-bit double', 3: '64-bit extended'}
    failures = []
    if control & 0xffff != FP_EXPECTED['control']:
        failures.append({
            'field': 'x87 control word', 'observed': f'0x{control & 0xffff:04x}',
            'expected': f"0x{FP_EXPECTED['control']:04x}",
            'detail': f"precision control {precision[(control >> 8) & 3]} "
                      f"(expected 24-bit single), rounding "
                      f"{(control >> 10) & 3}, exception mask 0x{control & 0x3f:02x}"})
    if tag & 0xffff != FP_EXPECTED['tag']:
        failures.append({'field': 'x87 tag word', 'observed': f'0x{tag & 0xffff:04x}',
                         'expected': '0xffff', 'detail': 'a nonempty x87 register stack'})
    if status & FP_EXPECTED['status_mask']:
        failures.append({'field': 'x87 status word', 'observed': f'0x{status:04x}',
                         'expected': f"masked bits 0x{FP_EXPECTED['status_mask']:04x} clear",
                         'detail': 'sticky stack-fault/overflow/zero-divide/invalid flags'})
    masked = mxcsr & ~FP_EXPECTED['mxcsr_ignore']
    if masked != FP_EXPECTED['mxcsr']:
        bits = []
        if mxcsr & 0x8000:
            bits.append('FZ (flush-to-zero) set')
        if mxcsr & 0x0040:
            bits.append('DAZ (denormals-are-zero) set')
        if (mxcsr >> 13) & 3:
            bits.append(f'rounding mode {(mxcsr >> 13) & 3} (expected round-to-nearest)')
        if (mxcsr & 0x1f80) != 0x1f80:
            bits.append('an SSE exception is unmasked')
        failures.append({'field': 'MXCSR', 'observed': f'0x{mxcsr:04x}',
                         'observed_compared': f'0x{masked:04x}',
                         'expected': f"0x{FP_EXPECTED['mxcsr']:04x}",
                         'detail': '; '.join(bits) or 'unexpected reserved bits'})
    return {'raw': diagnostic, 'supported': not failures, 'failures': failures}


def mesh_cache_report(scan):
    cache = scan['mesh_cache']
    metric = cache['metric'] or {}
    calls = number(metric.get('calls'), 0)
    bypasses = number(metric.get('bypasses'), 0)
    hits = number(metric.get('hits'), 0)
    misses = number(metric.get('misses'), 0)
    ticks = number(metric.get('native_ticks'), 0)
    frequency = frequency_of(scan)
    report = {
        'config': cache['config'], 'hook': cache['hook'],
        'metric_records': cache['metric_lines'], 'bypass_records': cache['bypass_lines'],
        'final': {k: metric.get(k) for k in (
            'dispatch_enabled', 'buffer_contract', 'faulted', 'calls', 'hits', 'misses',
            'bypasses', 'admissions', 'evictions', 'contention', 'native_calls',
            'native_failures', 'retained_entries', 'gate_rejections', 'rejected_fp')},
        'bypass_reasons': dict(cache['bypass']),
        'fractions': {
            'hit': hits / calls if calls else None,
            'miss': misses / calls if calls else None,
            'bypass': bypasses / calls if calls else None,
        },
        'native_seconds': ticks / frequency if frequency else None,
        'gate_seconds': (number(metric.get('gate_ticks'), 0) / frequency) if frequency else None,
        'mean_native_ms': (1000.0 * ticks / frequency / calls) if frequency and calls else None,
        'floating_point': decode_fp(cache['fp_first']),
    }
    reasons = {r: c for r, c in cache['bypass'].items() if c}
    report['single_reason'] = (list(reasons)[0] if len(reasons) == 1 else None)
    report['effect'] = ('none: every call took the original native path'
                        if hits == 0 and misses == 0 and calls == bypasses
                        else 'partial')
    return report


def frequency_of(scan):
    start = (scan['configuration'].get('telemetry_start') or [{}])[0]
    return number(start.get('qpc_frequency')) or 10000000


# ---- 3. adjacency timeline ---------------------------------------------------------

def adjacency_timeline(scan, quiet_gap_s=3.0, phase_min_calls=100):
    """Group the per-window adjacency deltas into activity phases.

    A phase is a maximal run of windows separated by less than `quiet_gap_s` of
    wall clock whose total call count reaches `phase_min_calls`.  This is a
    grouping of report windows, not an engine event: it says when adjacency work
    happened and how much, nothing about why.
    """
    frequency = frequency_of(scan)
    start = (scan['configuration'].get('telemetry_start') or [{}])[0]
    anchor = number(start.get('qpc'))
    rows = []
    for w in scan['adjacency_windows']:
        if w['qpc'] is None or anchor is None:
            continue
        rows.append({'seconds': (w['qpc'] - anchor) / frequency, 'calls': w['count'],
                     'adjacency_seconds': w['exclusive_us'] / 1e6,
                     'max_call_seconds': w['max_us'] / 1e6})
    phases = []
    current = None
    for row in rows:
        if current is not None and row['seconds'] - current['end_seconds'] > quiet_gap_s:
            phases.append(current)
            current = None
        if current is None:
            current = {'start_seconds': row['seconds'], 'end_seconds': row['seconds'],
                       'windows': 0, 'calls': 0, 'adjacency_seconds': 0.0,
                       'max_call_seconds': 0.0}
        current['end_seconds'] = row['seconds']
        current['windows'] += 1
        current['calls'] += row['calls']
        current['adjacency_seconds'] += row['adjacency_seconds']
        current['max_call_seconds'] = max(current['max_call_seconds'], row['max_call_seconds'])
    if current is not None:
        phases.append(current)
    for p in phases:
        p['mean_call_ms'] = 1000.0 * p['adjacency_seconds'] / p['calls'] if p['calls'] else None
    return {
        'windows': len(rows),
        'total_calls': sum(r['calls'] for r in rows),
        'total_adjacency_seconds': sum(r['adjacency_seconds'] for r in rows),
        'quiet_gap_seconds': quiet_gap_s,
        'phase_min_calls': phase_min_calls,
        'phases': [p for p in phases if p['calls'] >= phase_min_calls],
        'minor_phases': [p for p in phases if p['calls'] < phase_min_calls],
    }


# ---- 4. sharpness ------------------------------------------------------------------

def box_filter_gradient_factor(samples=2048):
    """Mean-squared-central-difference attenuation of an ideal 1x1 px box
    prefilter, for a source that is white over the Nyquist square.

    The measurement is the mean square of the central difference
    ``(I[x+1]-I[x-1])/2``, whose frequency response magnitude is
    ``|sin(2*pi*f)|``, so the measured energy of a field with power spectrum
    ``S`` is ``int (sin^2(2 pi fx) + sin^2(2 pi fy)) S``.  A box of width one
    pixel has transfer ``sinc(fx) sinc(fy)``, hence power ``sinc^2 sinc^2``.
    With ``S = 1`` on ``[-1/2, 1/2]^2`` the double integral separates:
    ``ratio = 2 * A * B / (2 * 1/2 * 1)`` with
    ``A = int sin^2(2 pi f) sinc^2(f) df`` and ``B = int sinc^2(f) df``.
    Both are evaluated by the midpoint rule.
    """
    def sinc2(f):
        if f == 0.0:
            return 1.0
        v = math.sin(math.pi * f) / (math.pi * f)
        return v * v

    step = 1.0 / samples
    a = b = 0.0
    for i in range(samples):
        f = -0.5 + (i + 0.5) * step
        s2 = sinc2(f)
        b += s2 * step
        a += math.sin(2.0 * math.pi * f) ** 2 * s2 * step
    return {'gradient_energy_ratio': 2.0 * a * b, 'A': a, 'B': b,
            'model': 'ideal 1x1 px box prefilter, white source over the Nyquist square, '
                     'central-difference gradient operator'}


def class_gradient_energy(image, classes, width, height, margin=12, operator='gradient'):
    """Mean squared central-difference gradient (or 5-point Laplacian) per pixel
    class, on one pass over the interior of the image."""
    totals = [0.0] * 3
    counts = [0] * 3
    for y in range(margin, height - margin):
        row = y * width
        for x in range(margin, width - margin):
            i = row + x
            cls = classes[i]
            if operator == 'gradient':
                gx = (image[i + 1] - image[i - 1]) * 0.5
                gy = (image[i + width] - image[i - width]) * 0.5
                value = gx * gx + gy * gy
            else:
                lap = (image[i + 1] + image[i - 1] + image[i + width] + image[i - width]
                       - 4.0 * image[i])
                value = lap * lap
            totals[cls] += value
            counts[cls] += 1
    out = {}
    for cls, name in enumerate(it08.CLASSES):
        out[name] = {'mean': totals[cls] / counts[cls] if counts[cls] else None,
                     'pixels': counts[cls]}
    total_count = sum(counts)
    out['all'] = {'mean': sum(totals) / total_count if total_count else None,
                  'pixels': total_count}
    return out


def spectral_band_fraction(image, width, origin, size, nyquist_fraction=0.25):
    """Fraction of windowed tile energy above `nyquist_fraction` of Nyquist.

    Bin ``k`` of a size-N DFT is ``k/N`` cycles per pixel and Nyquist is
    ``N/2``, so the band is ``radius > nyquist_fraction * N / 2``.
    """
    ox, oy = origin
    spectrum = it07.fft2(it07.hann_tile(image, width, ox, oy, size), size)
    limit = nyquist_fraction * size / 2.0
    total = high = 0.0
    for index, value in enumerate(spectrum):
        ky, kx = divmod(index, size)
        fy = ky - size if ky > size // 2 else ky
        fx = kx - size if kx > size // 2 else kx
        if fx == 0 and fy == 0:
            continue
        power = value.real * value.real + value.imag * value.imag
        total += power
        if math.hypot(fx, fy) > limit:
            high += power
    return {'total_energy': total, 'high_energy': high,
            'high_fraction': high / total if total else None,
            'band': f'radius > {nyquist_fraction} * Nyquist'}


def axis_taps(displacement, kind='catmull_rom'):
    """Integer taps and weights of one separable axis so that
    ``out(x) = sum w_k * image(x + n_k)`` equals ``image(x - displacement)``.

    ``catmull_rom`` is the resolve's own history filter (`resolve.hlsl`, the
    16-tap 4x4 form with the standard Catmull-Rom weights and the exact-texel
    fast path at zero fraction); ``bilinear`` is the naive alternative, kept for
    the comparison in ``weight_sweep``.
    """
    base = math.floor(-displacement)
    t = (-displacement) - base
    if kind == 'bilinear':
        weights = [(0, 1.0 - t), (1, t)]
    elif kind == 'catmull_rom':
        if t == 0.0:  # resolve.hlsl: `[branch] if (all(f == 0))` reads one texel
            weights = [(0, 1.0)]
        else:
            t2, t3 = t * t, t * t * t
            weights = [(-1, -0.5 * t + t2 - 0.5 * t3),
                       (0, 1.0 - 2.5 * t2 + 1.5 * t3),
                       (1, 0.5 * t + 2.0 * t2 - 1.5 * t3),
                       (2, -0.5 * t2 + 0.5 * t3)]
    else:
        raise ValueError(f'unknown reconstruction filter {kind!r}')
    return [(base + n, w) for n, w in weights if w != 0.0]


def shift_row(row, offset, width):
    """One row shifted by an integer offset with clamped edges."""
    if offset == 0:
        return row
    if offset > 0:
        offset = min(offset, width - 1)
        return row[offset:] + array.array('f', [row[-1]] * offset)
    k = min(-offset, width - 1)
    return array.array('f', [row[0]] * k) + row[:-k]


def shift_image(image, width, height, dx, dy, kind='catmull_rom'):
    """``out(x) = image(x - (dx,dy))`` under the named reconstruction filter,
    clamped at the edges.  Separable and row-sliced, so the inner loops are
    C-level zips rather than per-pixel indexing."""
    tx = axis_taps(dx, kind)
    ty = axis_taps(dy, kind)
    out = array.array('f', bytes(4 * width * height))
    cache = {}

    def filtered_row(y):
        y = min(max(y, 0), height - 1)
        row = cache.get(y)
        if row is None:
            source = image[y * width:(y + 1) * width]
            if len(tx) == 1 and tx[0][1] == 1.0:
                row = shift_row(source, tx[0][0], width)
            else:
                row = array.array('f', bytes(4 * width))
                for offset, weight in tx:
                    shifted = shift_row(source, offset, width)
                    row = array.array('f', [a + weight * b for a, b in zip(row, shifted)])
            cache[y] = row
        return row

    for y in range(height):
        if len(ty) == 1 and ty[0][1] == 1.0:
            merged = filtered_row(y + ty[0][0])
        else:
            merged = array.array('f', bytes(4 * width))
            for offset, weight in ty:
                row = filtered_row(y + offset)
                merged = array.array('f', [a + weight * b for a, b in zip(merged, row)])
        out[y * width:(y + 1) * width] = merged
    return out


def ideal_history_weights(order, weight):
    """Geometric history weights of the resolve's exponential blend, oldest
    last: the current frame gets ``1 - weight`` and each earlier frame
    ``weight`` times its successor.  Truncated after `order` frames and
    renormalized, so the weights sum to one."""
    raw = [(1.0 - weight) * weight ** k for k in range(order)]
    total = sum(raw)
    return [w / total for w in raw]


def effective_kernel(offsets, weights, kind='catmull_rom'):
    """The discrete kernel a resampling accumulation applies: every fractional
    offset becomes the integer taps of the named reconstruction filter, summed
    with its history weight."""
    kernel = {}
    for (dx, dy), weight in zip(offsets, weights):
        for ox, wx in axis_taps(dx, kind):
            for oy, wy in axis_taps(dy, kind):
                w = weight * wx * wy
                if w:
                    kernel[(ox, oy)] = kernel.get((ox, oy), 0.0) + w
    return kernel


def kernel_gradient_factor(kernel, samples=192):
    """Mean-squared-central-difference attenuation of `kernel` for a source that
    is white over the Nyquist square, by the same definition as
    ``box_filter_gradient_factor``: the transfer function of the discrete
    kernel, ``H(f) = sum k[n] exp(-2 pi i f.n)``, integrated with the
    central-difference weight ``sin^2(2 pi fx) + sin^2(2 pi fy)``."""
    step = 1.0 / samples
    numerator = denominator = 0.0
    for iy in range(samples):
        fy = -0.5 + (iy + 0.5) * step
        for ix in range(samples):
            fx = -0.5 + (ix + 0.5) * step
            weight = math.sin(2.0 * math.pi * fx) ** 2 + math.sin(2.0 * math.pi * fy) ** 2
            re = im = 0.0
            for (nx, ny), w in kernel.items():
                phase = -2.0 * math.pi * (fx * nx + fy * ny)
                re += w * math.cos(phase)
                im += w * math.sin(phase)
            numerator += weight * (re * re + im * im)
            denominator += weight
    return numerator / denominator if denominator else None


def offset_gradient_factor(offsets, weights, samples=192):
    """Same white-source factor as ``kernel_gradient_factor``, but for a kernel
    of *continuous* offsets: ``H(f) = sum w_k exp(-2 pi i f . d_k)``.  This is
    the attenuation of the jitter accumulation alone, with a perfect
    reconstruction filter.  Against ``kernel_gradient_factor`` of the same
    offsets it isolates how much of the softness the bilinear history tap adds
    on top of the supersampling itself."""
    step = 1.0 / samples
    numerator = denominator = 0.0
    for iy in range(samples):
        fy = -0.5 + (iy + 0.5) * step
        for ix in range(samples):
            fx = -0.5 + (ix + 0.5) * step
            weight = math.sin(2.0 * math.pi * fx) ** 2 + math.sin(2.0 * math.pi * fy) ** 2
            re = im = 0.0
            for (dx, dy), w in zip(offsets, weights):
                phase = -2.0 * math.pi * (fx * dx + fy * dy)
                re += w * math.cos(phase)
                im += w * math.sin(phase)
            numerator += weight * (re * re + im * im)
            denominator += weight
    return numerator / denominator if denominator else None


def weight_sweep(table, weights=(0.95, 0.9, 0.85, 0.8, 0.7, 0.5), order=None,
                 current_index=0):
    """What the resolve's history weight and reconstruction filter cost in
    sharpness, before any image is involved.

    For each candidate weight the geometric history weights over the session's
    own cycling jitter table give one kernel, and its white-source gradient
    factor is reported three ways:

    * ``no_resampling`` - the offsets alone, which is what a *static* scene
      really gets: the accumulation lives on the unjittered grid, each frame
      contributes its own raster, and the resolve's zero-fraction branch reads
      one texel, so no reconstruction filter is involved at all.
    * ``catmull_rom`` - the resolve's actual history filter at a nonzero
      fractional velocity (`resolve.hlsl`).
    * ``bilinear`` - the naive alternative, for scale.

    The spread across weights says how much lowering the weight would buy; the
    spread across filters says how much the history fetch costs while the
    camera moves.  Both are white-source numbers: they rank levers, they do not
    predict an image.
    """
    if not table:
        return {'status': 'unavailable', 'reason': 'incomplete jitter table'}
    order = order or len(table)
    offsets = relative_offsets(table, current_index)[:order]
    rows = []
    for weight in weights:
        w = ideal_history_weights(order, weight)
        row = {'history_weight': weight,
               'no_resampling': offset_gradient_factor(offsets, w)}
        for kind in ('catmull_rom', 'bilinear'):
            row[kind] = kernel_gradient_factor(effective_kernel(offsets, w, kind))
        if row['no_resampling']:
            row['catmull_rom_penalty'] = row['catmull_rom'] / row['no_resampling']
            row['bilinear_penalty'] = row['bilinear'] / row['no_resampling']
        rows.append(row)
    return {'status': 'evaluated', 'order': order, 'current_index': current_index,
            'rows': rows,
            'note': 'white-source mean-squared-central-difference factors; a higher '
                    'number is sharper. Content-independent, for ranking levers only.'}


def ideal_supersampled(raw, width, height, offsets, weights, kind='catmull_rom'):
    """A *resampled* model of what an ideal resolve would produce from this
    content: one raw frame resampled to each historical jitter offset and
    combined with the resolve's own weights.  Perfect reprojection, no clamp,
    no rejection and no sharpening.

    It is a lower bound on the ideal sharpness, not the ideal itself: the real
    pipeline gets each sub-pixel phase from the rasterizer, while this has to
    reconstruct them from one already-sampled frame, and every reconstruction
    filter costs something.  ``burst_phase_average`` is the resampling-free
    version of the same idea, and is preferred wherever the burst supplies real
    phases.
    """
    acc = array.array('f', bytes(4 * width * height))
    for (dx, dy), w in zip(offsets, weights):
        if dx == 0.0 and dy == 0.0:
            shifted = raw
        else:
            shifted = shift_image(raw, width, height, dx, dy, kind)
        for i in range(0, width * height, width):
            block = shifted[i:i + width]
            acc[i:i + width] = array.array(
                'f', [a + w * b for a, b in zip(acc[i:i + width], block)])
    return acc


def burst_phase_average(images, weights=None):
    """The real multi-phase supersample: the burst's own raw frames averaged.

    On a burst the route certifies static, the raw frames are the same scene
    rasterized at different jitter phases, so their mean *is* a jitter
    supersample - with no interpolation anywhere, unlike
    ``ideal_supersampled``.  With ``n`` frames it is an ``n``-phase equal-weight
    average, which is a different kernel from the resolve's geometric weights
    over the full cycle; ``offset_gradient_factor`` of both kernels gives the
    factor between them.
    """
    count = len(images)
    weights = weights or [1.0 / count] * count
    length = len(images[0])
    acc = array.array('f', bytes(4 * length))
    for image, weight in zip(images, weights):
        for i in range(0, length, 4096):
            block = image[i:i + 4096]
            acc[i:i + 4096] = array.array(
                'f', [a + weight * b for a, b in zip(acc[i:i + 4096], block)])
    return acc


def interpolate_polyline(knots, position):
    """Linear interpolation on a polyline of (x, y) knots, clamped at the ends."""
    if position <= knots[0][0]:
        return knots[0][1]
    if position >= knots[-1][0]:
        return knots[-1][1]
    for (x0, y0), (x1, y1) in zip(knots, knots[1:]):
        if x0 <= position <= x1:
            return y0 if x1 == x0 else y0 + (y1 - y0) * (position - x0) / (x1 - x0)
    return knots[-1][1]


def edge_spread(image, classes, width, height, want=2, radius=3, margin=16,
                minimum_contrast=0.05, anisotropy=3.0, limit=4000, bin_px=0.25):
    """MTF-like estimate from the strongest locally one-dimensional edges.

    The slanted-edge construction, done per edge instead of per image so no
    orientation has to be assumed.  A candidate is an interior pixel of class
    `want` whose horizontal central difference dominates the vertical one by
    `anisotropy` and whose 1-D span across the edge covers at least
    `minimum_contrast` of luma range.  Each 1-D profile is oriented by the sign
    of its gradient, normalized to 0..1 by its own end values, and *aligned by
    its own 50% crossing*: without that alignment, averaging edges that sit at
    different sub-pixel positions widens the mean profile by the spread of those
    positions rather than by the blur.  The aligned samples are binned at
    `bin_px` to a super-resolved edge-spread function; the ESF is resampled on
    that grid, its central difference is the line spread function, and the DFT
    magnitude of the LSF normalized at DC is the MTF.  Reported: the 10-90% rise
    in pixels and MTF50 in cycles per pixel.

    Read as a *comparison* between two images of the same content, never as an
    absolute lens MTF: the edge population, not an engineered target, sets it.
    """
    span = 2 * radius + 1
    candidates = []
    for y in range(margin, height - margin):
        row = y * width
        for x in range(margin + radius, width - margin - radius):
            i = row + x
            if classes[i] != want:
                continue
            gx = (image[i + 1] - image[i - 1]) * 0.5
            gy = (image[i + width] - image[i - width]) * 0.5
            if abs(gx) < 1e-6 or abs(gx) < anisotropy * abs(gy):
                continue
            if any(classes[i + k] != want for k in range(-radius, radius + 1)):
                continue
            left = image[i - radius]
            right = image[i + radius]
            if abs(right - left) < minimum_contrast:
                continue
            candidates.append((abs(right - left), i, 1.0 if right > left else -1.0))
    candidates.sort(reverse=True)
    used = candidates[:limit]
    if len(used) < 32:
        return {'status': 'insufficient_edges', 'candidates': len(candidates)}
    bins = {}
    accepted = 0
    for _, i, sign in used:
        values = [image[i + k] for k in range(-radius, radius + 1)]
        if sign < 0:
            values.reverse()
        low, high = values[0], values[-1]
        scale = high - low
        if scale <= 0:
            continue
        profile = [(v - low) / scale for v in values]
        centre = None
        for k in range(span - 1):
            a, b = profile[k], profile[k + 1]
            if a <= 0.5 <= b and b != a:
                centre = k + (0.5 - a) / (b - a)
                break
        if centre is None:
            continue
        accepted += 1
        for k in range(span):
            distance = k - centre
            slot = math.floor(distance / bin_px)
            entry = bins.setdefault(slot, [0.0, 0.0, 0])
            entry[0] += distance
            entry[1] += profile[k]
            entry[2] += 1
    if accepted < 32 or len(bins) < 4:
        return {'status': 'insufficient_edges', 'candidates': len(candidates),
                'aligned': accepted}
    knots = sorted((entry[0] / entry[2], entry[1] / entry[2]) for entry in bins.values())

    def crossing(level):
        for (x0, y0), (x1, y1) in zip(knots, knots[1:]):
            if (y0 - level) * (y1 - level) <= 0 and y1 != y0:
                return x0 + (level - y0) * (x1 - x0) / (y1 - y0)
        return None

    low_x, high_x = crossing(0.1), crossing(0.9)
    grid = [knots[0][0] + bin_px * k
            for k in range(int((knots[-1][0] - knots[0][0]) / bin_px) + 1)]
    esf = [interpolate_polyline(knots, position) for position in grid]
    lsf = [(esf[k + 1] - esf[k - 1]) * 0.5 for k in range(1, len(esf) - 1)]
    n = len(lsf)
    dc = sum(lsf)
    mtf = []
    if n >= 4 and dc:
        for k in range(n // 2 + 1):
            re = sum(v * math.cos(-2.0 * math.pi * k * j / n) for j, v in enumerate(lsf))
            im = sum(v * math.sin(-2.0 * math.pi * k * j / n) for j, v in enumerate(lsf))
            mtf.append((k / (n * bin_px), math.hypot(re, im) / abs(dc)))
    mtf50 = None
    for (f0, m0), (f1, m1) in zip(mtf, mtf[1:]):
        if m0 >= 0.5 > m1:
            mtf50 = f0 + (m0 - 0.5) * (f1 - f0) / (m0 - m1)
            break
    return {'status': 'evaluated', 'edges': len(used), 'aligned': accepted,
            'candidates': len(candidates), 'bin_px': bin_px,
            'esf_knots': [[round(x, 4), round(y, 6)] for x, y in knots],
            'rise_10_90_px': (high_x - low_x)
            if (low_x is not None and high_x is not None) else None,
            'mtf': mtf, 'mtf50_cycles_per_px': mtf50}


def classify_burst(frames):
    """stationary / slow / turning, from the route's own cut detector."""
    medians = [real(f.get('cut_median_px'), 0.0) for f in frames]
    rotations = [real(f.get('camera_rotation_deg'), 0.0) for f in frames]
    peak = max(medians)
    if peak <= STATIONARY_PX:
        motion = 'stationary'
    elif peak <= SLOW_PX:
        motion = 'slow'
    else:
        motion = 'turning'
    return {'motion': motion, 'cut_median_px': medians,
            'cut_median_px_peak': peak, 'cut_median_px_median': statistics.median(medians),
            'camera_rotation_deg': rotations,
            'cut_flagged': [number(f.get('cut'), 0) for f in frames],
            'cut_missing': [real(f.get('cut_missing'), 0.0) for f in frames],
            'camera_policy': sorted({f.get('camera_policy') for f in frames}),
            'camera_cut': [number(f.get('camera_cut'), 0) for f in frames]}


def burst_frames(frames, gap=1):
    """Consecutive logged frames of the same capture burst."""
    numbers = sorted(number(f.get('frame')) for f in frames
                     if number(f.get('frame')) is not None)
    index = {number(f.get('frame')): f for f in frames}
    groups = []
    for n in numbers:
        if groups and n - groups[-1][-1] <= gap:
            groups[-1].append(n)
        else:
            groups.append([n])
    return [[index[n] for n in g] for g in groups if len(g) >= 2]


def analyze_burst_sharpness(records, captures, device, options):
    numbers = [number(r.get('frame')) for r in records]
    width, height = options['width'], options['height']
    paths = {}
    for n in numbers:
        paths[n] = {
            'colour': captures / f'color_{device}_{n}.bgra8',
            'resolved': captures / f'taa_{device}_{n}.rgba16f',
            'depth': captures / f'depth_{device}_{n}.r32f',
            'motion': captures / f'motion_{device}_{n}.rgba32f',
        }
    missing = [str(p) for n in numbers for p in paths[n].values() if not p.exists()]
    entry = {'frames': numbers, 'route': classify_burst(records)}
    if missing:
        entry['status'] = 'missing_readbacks'
        entry['missing'] = missing[:8]
        return entry
    colour = {n: it07.load_luma_bgra8(paths[n]['colour'], width, height) for n in numbers}
    resolved = {n: it07.load_luma_rgba16f(paths[n]['resolved'], width, height) for n in numbers}
    depths = [it08.load_depth_r32f(paths[n]['depth'], width, height) for n in numbers]
    alphas = [it08.load_motion_alpha(paths[n]['motion'], width, height) for n in numbers]
    classes = it08.classify_pixels(depths, alphas, options['depth_tolerance'])
    del depths, alphas
    counts = Counter(classes)
    entry['classes'] = {name: counts.get(index, 0) for index, name in enumerate(it08.CLASSES)}
    entry['status'] = 'evaluated'

    interior = bytearray(1 if c == it08.INTERIOR else 0 for c in classes)
    shared = it07.erode(interior, width, height, 2)
    entry['interior_eroded_pixels'] = sum(shared)
    # A flat 8-bit quantization floor exists only in the pre-resolve readback.
    quantization = sum(c * c for c in LUMA) * (1.0 / 255.0) ** 2 / 12.0
    entry['colour_quantization_gradient_floor'] = quantization

    per_frame = []
    legacy_masks = {}
    for n in numbers:
        item = {'frame': n}
        for label, image in (('raw', colour[n]), ('resolved', resolved[n])):
            item[label] = {
                'gradient': class_gradient_energy(image, classes, width, height,
                                                  options['margin'], 'gradient'),
                'laplacian': class_gradient_energy(image, classes, width, height,
                                                   options['margin'], 'laplacian'),
            }
        origin = it07.best_tile_origin(colour[n], width, height, options['tile'], shared,
                                       step=options['tile_step'])
        if origin is not None:
            item['tile'] = [origin[0], origin[1], options['tile']]
            for label, image in (('raw', colour[n]), ('resolved', resolved[n])):
                item[label]['band'] = spectral_band_fraction(
                    image, width, origin, options['tile'], options['nyquist_fraction'])
        for cls in ('routed_interior', 'routed_edge', 'sentinel', 'all'):
            raw_g = item['raw']['gradient'][cls]['mean']
            res_g = item['resolved']['gradient'][cls]['mean']
            raw_l = item['raw']['laplacian'][cls]['mean']
            res_l = item['resolved']['laplacian'][cls]['mean']
            item.setdefault('ratios', {})[cls] = {
                'gradient_energy_ratio': res_g / raw_g if raw_g else None,
                'gradient_energy_ratio_noise_corrected':
                    res_g / (raw_g - quantization) if raw_g and raw_g > quantization else None,
                'laplacian_energy_ratio': res_l / raw_l if raw_l else None,
            }
        if 'band' in item['raw']:
            raw_b = item['raw']['band']['high_fraction']
            res_b = item['resolved']['band']['high_fraction']
            item['band_fraction_ratio'] = res_b / raw_b if raw_b else None
        # Directly comparable to iteration 7/8: their blur mask is this frame's
        # motion-validity mask eroded by two, their operator is
        # it07.gradient_energy and their band is above half Nyquist.  Kept
        # beside the per-class numbers so the 0.64-0.75 range of iteration 8 can
        # be compared without re-deriving either measurement.
        legacy = it07.erode(it07.load_validity_mask(paths[n]['motion'], width, height),
                            width, height, 2)
        legacy_origin = it07.best_tile_origin(colour[n], width, height, options['tile'],
                                              legacy, step=options['tile_step'])
        comparable = {'mask_pixels': sum(legacy)}
        for label, image, noise in (('raw', colour[n], quantization),
                                    ('resolved', resolved[n], 0.0)):
            row = it07.gradient_energy(image, width, height, legacy)
            if legacy_origin is not None:
                row.update(it07.spectral_high_fraction(image, width, legacy_origin,
                                                       options['tile'], noise))
            comparable[label] = row
        raw_mean = comparable['raw']['mean_squared_gradient']
        comparable['gradient_energy_ratio'] = (
            comparable['resolved']['mean_squared_gradient'] / raw_mean if raw_mean else None)
        if comparable['raw'].get('high_fraction'):
            comparable['high_frequency_fraction_ratio'] = (
                comparable['resolved']['high_fraction'] / comparable['raw']['high_fraction'])
        item['iteration8_comparable'] = comparable
        item['legacy_mask_pixels'] = comparable['mask_pixels']
        legacy_masks[n] = legacy
        if options['edges']:
            item['edge_spread'] = {
                'raw': edge_spread(colour[n], classes, width, height, it08.INTERIOR,
                                   margin=options['margin']),
                'resolved': edge_spread(resolved[n], classes, width, height, it08.INTERIOR,
                                        margin=options['margin']),
            }
        per_frame.append(item)
    entry['per_frame'] = per_frame

    # Raw frame against raw frame: the same static scene at four jitter phases.
    raw_all = [f['raw']['gradient']['all']['mean'] for f in per_frame]
    raw_int = [f['raw']['gradient']['routed_interior']['mean'] for f in per_frame]
    entry['jitter_phase_spread'] = {
        'jitter_index': [number(r.get('jitter_index')) for r in records],
        'jitter_offsets': [[real(r.get('jitter_x')), real(r.get('jitter_y'))] for r in records],
        'raw_gradient_energy_all': raw_all,
        'raw_gradient_energy_routed_interior': raw_int,
        'relative_spread_all': ((max(raw_all) - min(raw_all)) / statistics.fmean(raw_all)
                                if raw_all and statistics.fmean(raw_all) else None),
        'relative_spread_routed_interior':
            ((max(raw_int) - min(raw_int)) / statistics.fmean(raw_int)
             if raw_int and statistics.fmean(raw_int) else None),
    }

    # The resampling-free reference: the burst's own raw frames, averaged.
    if options['ideal_offsets']:
        phases = [(real(r.get('jitter_x'), 0.0), real(r.get('jitter_y'), 0.0))
                  for r in records]
        equal = [1.0 / len(numbers)] * len(numbers)
        average = burst_phase_average([colour[n] for n in numbers])
        grad = class_gradient_energy(average, classes, width, height,
                                     options['margin'], 'gradient')
        lap = class_gradient_energy(average, classes, width, height,
                                    options['margin'], 'laplacian')
        captured = {'frames': numbers, 'phases': phases, 'weights': equal,
                    'gradient': grad, 'laplacian': lap, 'ratios': {},
                    'analytic_white_gradient_ratio':
                        offset_gradient_factor([(x - phases[0][0], y - phases[0][1])
                                                for x, y in phases], equal),
                    'resampling': 'none (real rasterized phases)'}
        for cls in ('routed_interior', 'routed_edge', 'sentinel', 'all'):
            raw_means = [f['raw']['gradient'][cls]['mean'] for f in per_frame]
            lap_means = [f['raw']['laplacian'][cls]['mean'] for f in per_frame]
            raw_mean = statistics.fmean([m for m in raw_means if m]) if any(raw_means) else None
            lap_mean = statistics.fmean([m for m in lap_means if m]) if any(lap_means) else None
            captured['ratios'][cls] = {
                'gradient_energy_ratio': grad[cls]['mean'] / raw_mean if raw_mean else None,
                'laplacian_energy_ratio': lap[cls]['mean'] / lap_mean if lap_mean else None,
            }
        legacy_mask = legacy_masks.get(numbers[0])
        if legacy_mask:
            legacy_ideal = it07.gradient_energy(average, width, height, legacy_mask)
            legacy_raw = (per_frame[0]['iteration8_comparable']['raw']
                          ['mean_squared_gradient'])
            captured['iteration8_comparable_ratio'] = (
                legacy_ideal['mean_squared_gradient'] / legacy_raw if legacy_raw else None)
        # The resolve's own kernel against this one, both white-source, so the
        # n-phase equal-weight measurement can be scaled to the resolve's
        # geometric weights over the full cycle.
        full = ideal_history_weights(len(options['ideal_offsets']),
                                     options['history_weight'])
        captured['resolve_kernel_analytic_white'] = offset_gradient_factor(
            relative_offsets(options['ideal_offsets'],
                             number(records[0].get('jitter_index'), 0)), full)
        if captured['analytic_white_gradient_ratio']:
            captured['scale_to_resolve_kernel'] = (
                captured['resolve_kernel_analytic_white']
                / captured['analytic_white_gradient_ratio'])
        entry['captured_phase_average'] = captured

    if options['ideal_offsets'] and options['ideal_frames']:
        ideal = []
        weights = ideal_history_weights(len(options['ideal_offsets']), options['history_weight'])
        for n in numbers[:options['ideal_frames']]:
            record = index_of(records, n)
            current = (real(record.get('jitter_x'), 0.0), real(record.get('jitter_y'), 0.0))
            offsets = relative_offsets(options['ideal_offsets'],
                                       number(record.get('jitter_index'), 0))
            image = ideal_supersampled(colour[n], width, height, offsets, weights)
            grad = class_gradient_energy(image, classes, width, height,
                                         options['margin'], 'gradient')
            lap = class_gradient_energy(image, classes, width, height,
                                        options['margin'], 'laplacian')
            kernel = effective_kernel(offsets, weights)
            row = {'frame': n, 'current_jitter': current, 'offsets': offsets,
                   'weights': weights, 'gradient': grad, 'laplacian': lap, 'ratios': {},
                   'kernel_taps': len(kernel),
                   'analytic_white_gradient_ratio': kernel_gradient_factor(kernel)}
            for cls in ('routed_interior', 'routed_edge', 'all'):
                raw_g = per_frame[numbers.index(n)]['raw']['gradient'][cls]['mean']
                raw_l = per_frame[numbers.index(n)]['raw']['laplacian'][cls]['mean']
                row['ratios'][cls] = {
                    'gradient_energy_ratio': grad[cls]['mean'] / raw_g if raw_g else None,
                    'laplacian_energy_ratio': lap[cls]['mean'] / raw_l if raw_l else None,
                }
            if n in legacy_masks:
                legacy_ideal = it07.gradient_energy(image, width, height, legacy_masks[n])
                legacy_raw = (per_frame[numbers.index(n)]['iteration8_comparable']['raw']
                              ['mean_squared_gradient'])
                row['iteration8_comparable_ratio'] = (
                    legacy_ideal['mean_squared_gradient'] / legacy_raw
                    if legacy_raw else None)
            ideal.append(row)
        entry['ideal_supersampling'] = ideal
    return entry


def index_of(records, frame):
    for r in records:
        if number(r.get('frame')) == frame:
            return r
    return {}


def relative_offsets(table, current_index):
    """History offsets relative to the current jitter phase, newest first.

    Frame ``n`` samples the scene at jitter ``o_n``, so the raw current frame is
    ``R(x) = S(x + o_n)`` and the frame ``k`` back is
    ``S(x + o_{n-k}) = R(x + o_{n-k} - o_n)``.  The current frame therefore
    contributes offset ``(0,0)``.  Gradient energy is invariant to a global
    negation of the kernel (for a real kernel ``|H(-f)| = |H(f)|``), so the sign
    convention of the logged jitter does not affect the reported ratio.
    """
    n = len(table)
    current = table[current_index % n]
    out = []
    for k in range(n):
        past = table[(current_index - k) % n]
        out.append((past[0] - current[0], past[1] - current[1]))
    return out


def jitter_table(scan):
    table = {}
    for f in scan['frames']:
        if f.get('jitter') != '1':
            continue
        i = number(f.get('jitter_index'))
        if i is None:
            continue
        table[i] = (real(f.get('jitter_x'), 0.0), real(f.get('jitter_y'), 0.0))
    if not table or sorted(table) != list(range(len(table))):
        return []
    return [table[i] for i in sorted(table)]


# ---- 5. sampler states -------------------------------------------------------------

def sampler_report(scan):
    groups = scan['sampler_groups']
    stage_groups = scan['sampler_stage_groups']
    draws = scan['sampler_draws']

    def render(counter):
        return [{'minfilter': k[0], 'magfilter': k[1], 'mipfilter': k[2],
                 'maxanisotropy': k[3], 'srgbtexture': k[4], 'mipmaplodbias': k[5],
                 'maxmiplevel': k[6], 'stage_samples': v}
                for k, v in counter.most_common()]
    seen = scan.get('sampler_states_seen', set())
    missing = sorted('D3DSAMP_' + name.upper() for name in
                     list(SAMPLER_STATES.values()) + list(SAMPLER_NEVER.values()) if name not in seen)
    if 'mipmaplodbias' in seen:
        note = ('src/proxy/capture.cpp snapshots MIN/MAG/MIPFILTER, MAXANISOTROPY, SRGBTEXTURE, '
                'MIPMAPLODBIAS (raw and as bias=<float>) and MAXMIPLEVEL per bound stage; the '
                'mipmaplodbias column is the LOD bias the device held at the draw (the route '
                'restores its own bias before the capture diagnostics, so a routed draw shows '
                'the application value, 0.0 unless the game wrote one).')
    else:
        note = ('src/proxy/capture.cpp snapshots only MINFILTER/MAGFILTER/MIPFILTER/'
                'MAXANISOTROPY/SRGBTEXTURE in this log (it predates the mip-bias capture). '
                'D3DSAMP_MIPMAPLODBIAS is absent, so this run carries no evidence of the '
                'current LOD bias; absence is not proof of the 0.0 default.')
    return {
        'captured_draws': draws,
        'states_recorded': sorted(seen),
        'states_not_recorded': missing,
        'note': note,
        'routed': render(groups['routed']),
        'unrouted': render(groups['unrouted']),
        'routed_per_stage': {str(s): render(c) for s, c in
                             sorted(stage_groups['routed'].items())},
    }


# ---- 6. health ---------------------------------------------------------------------

def health_report(scan, baseline_path, baseline_label, threshold_us, device):
    frames = scan['frames']
    latched = [f for f in frames if f.get('latched') == '1']
    totals = Counter()
    for f in latched:
        for key in ('draws', 'routed', 'matched', 'gate1', 'gate2', 'gate3', 'gate4',
                    'gate5', 'gate6', 'apply_failures', 'restore_failures'):
            totals[key] += number(f.get(key), 0)
    report = {
        'records': len(frames), 'latched': len(latched),
        'route_totals': dict(totals),
        'taa': {
            'attempted': sum(number(f.get('taa_attempted'), 0) for f in frames),
            'resolved': sum(number(f.get('taa_resolved'), 0) for f in frames),
            'history': sum(number(f.get('taa_history'), 0) for f in frames),
            'skip_reasons': dict(Counter(f.get('taa_skip') for f in frames)),
        },
        'camera': {
            'policy': dict(Counter(f.get('camera_policy') for f in frames)),
            'reason': dict(Counter(f.get('camera_reason') for f in frames)),
            'cuts': sum(number(f.get('camera_cut'), 0) for f in frames),
            'rotation_deg_max': max((real(f.get('camera_rotation_deg'), 0.0)
                                     for f in frames), default=None),
        },
        'cut_detector': {
            'flagged': sum(number(f.get('cut'), 0) for f in latched),
            'median_px_max': max((real(f.get('cut_median_px'), 0.0) for f in latched),
                                 default=None),
            'missing_max': max((real(f.get('cut_missing'), 0.0) for f in latched),
                               default=None),
        },
        'reset_count': scan['reset_count'],
        'record_counts': {k: v for k, v in scan['counts'].most_common(24)},
    }
    primary = it08.stream_scene_windows(Path(scan['path']), device)
    report['timing'] = {'windows': {k: len(v) for k, v in sorted(primary.items())}}
    if baseline_path:
        secondary = it08.stream_scene_windows(Path(baseline_path), device)
        report['timing']['comparison'] = it08.compare_window_timings(
            summarize_windows(primary), summarize_windows(secondary), 'run2', baseline_label)
        report['timing']['mode_split'] = it08.compare_mode_splits(
            primary, secondary, 'run2', baseline_label, threshold_us)
        report['timing']['baseline'] = str(baseline_path)
    return report


def summarize_windows(windows):
    out = {}
    for key, entries in windows.items():
        row = {}
        for regime in ('scene', 'other', 'all'):
            chosen = entries if regime == 'all' else [e for e in entries
                                                      if e['regime'] == regime]
            row[regime] = it07.describe_windows(chosen)
        out[key] = row
    return out


# ---- report ------------------------------------------------------------------------

def build(args):
    scan = scan_log(args.log, args.device)
    captures = args.captures or args.log.parent
    table = jitter_table(scan)
    options = {
        'width': args.width, 'height': args.height, 'margin': args.margin,
        'tile': args.tile, 'tile_step': args.tile_step,
        'nyquist_fraction': args.nyquist_fraction,
        'depth_tolerance': args.depth_tolerance,
        'history_weight': args.history_weight,
        'ideal_offsets': table if not args.no_ideal else [],
        'ideal_frames': args.ideal_frames,
        'edges': not args.no_edges,
    }
    bursts = burst_frames(scan['frames'])
    selected = []
    for records in bursts:
        numbers = [number(r.get('frame')) for r in records]
        if args.burst and numbers[0] not in args.burst:
            continue
        selected.append(records)
    blur = []
    for records in selected:
        blur.append(analyze_burst_sharpness(records, captures, args.device, options))
    report = OrderedDict()
    report['source'] = {'log': str(args.log), 'captures': str(captures),
                        'device': args.device,
                        'sha256': None if args.no_hash else it07.sha256_of(args.log)}
    report['configuration'] = scan['configuration']
    report['scene_hook'] = scene_hook_report(scan)
    report['mesh_cache'] = mesh_cache_report(scan)
    report['adjacency_timeline'] = adjacency_timeline(scan, args.quiet_gap)
    report['jitter_table'] = table
    report['box_filter_reference'] = box_filter_gradient_factor()
    report['weight_sweep'] = weight_sweep(table, current_index=0) if table else {
        'status': 'unavailable', 'reason': 'incomplete jitter table'}
    report['blur'] = blur
    report['samplers'] = sampler_report(scan)
    report['health'] = health_report(scan, args.baseline_log, args.baseline_label,
                                     args.window_mode_split_us, args.device)
    return report


def render_text(report):
    lines = []
    hook = report['scene_hook']
    lines.append('== scene-end hook')
    for row in hook['install']:
        lines.append('  install ' + ' '.join(f'{k}={v}' for k, v in row.items()))
    lines.append(f"  site {hook['patch_site']['callsite_va']} bytes "
                 f"{hook['patch_site']['expected_bytes']} -> "
                 f"{hook['patch_site']['target_va']}")
    lines.append(f"  records {hook['records']}  checks {hook['check_distribution']}"
                 f"  sources {hook['source_distribution']}")
    lines.append(f"  resolves by source {hook['resolved_by_source']}"
                 f"  draws_after_hook max {hook['latched_draws_after_hook_max']}"
                 f"  disagreement records {len(hook['disagreement_records'])}")
    for row in hook['unlatched_frames']:
        lines.append(f"  unlatched frame {row['frame']}: draws={row['draws']} "
                     f"check={row['check']} source={row['source']} "
                     f"signals={row['hook_signals']} outside_scene={row['hook_outside_scene']} "
                     f"state={row['hook_state']} taa_skip={row['taa_skip']}")
    lines.append(f"  shutdown records {len(hook['shutdown_records'])}")

    cache = report['mesh_cache']
    lines.append('== mesh adjacency cache')
    lines.append('  final ' + ' '.join(f'{k}={v}' for k, v in cache['final'].items()))
    lines.append(f"  bypass reasons {cache['bypass_reasons']}  single_reason "
                 f"{cache['single_reason']}  effect {cache['effect']}")
    if cache['native_seconds'] is not None:
        lines.append(f"  native {cache['native_seconds']:.2f} s "
                     f"({cache['mean_native_ms']:.2f} ms/call), gate "
                     f"{cache['gate_seconds']:.3f} s")
    fp = cache['floating_point']
    if fp:
        lines.append('  incoming FPU ' + ' '.join(f'{k}={v}' for k, v in fp['raw'].items()))
        for fail in fp['failures']:
            lines.append(f"    {fail['field']}: observed {fail['observed']} expected "
                         f"{fail['expected']} - {fail['detail']}")

    timeline = report['adjacency_timeline']
    lines.append('== adjacency phases')
    for p in timeline['phases']:
        lines.append(f"  {p['start_seconds']:7.1f} .. {p['end_seconds']:7.1f} s  "
                     f"calls={p['calls']:<6d} adj={p['adjacency_seconds']:7.3f} s  "
                     f"mean={p['mean_call_ms']:.2f} ms  max_call={p['max_call_seconds']:.3f} s")
    lines.append(f"  total {timeline['total_calls']} calls / "
                 f"{timeline['total_adjacency_seconds']:.2f} s")

    box = report['box_filter_reference']
    lines.append(f"== ideal box prefilter: gradient energy ratio "
                 f"{box['gradient_energy_ratio']:.4f} (white source)")
    sweep = report.get('weight_sweep') or {}
    if sweep.get('status') == 'evaluated':
        lines.append('== history-weight sweep (white source, higher is sharper)')
        for row in sweep['rows']:
            lines.append(f"  weight {row['history_weight']:.2f}: no resampling "
                         f"{fmt(row['no_resampling'])}  catmull_rom "
                         f"{fmt(row['catmull_rom'])} (penalty "
                         f"{fmt(row.get('catmull_rom_penalty'))})  bilinear "
                         f"{fmt(row['bilinear'])} (penalty "
                         f"{fmt(row.get('bilinear_penalty'))})")
    for burst in report['blur']:
        head = (f"== burst {burst['frames'][0]}-{burst['frames'][-1]} "
                f"{burst['route']['motion']} (cut median peak "
                f"{burst['route']['cut_median_px_peak']:.3f} px, rotation "
                f"{max(burst['route']['camera_rotation_deg']):.2f} deg) {burst['status']}")
        lines.append(head)
        if burst['status'] != 'evaluated':
            continue
        lines.append(f"  classes {burst['classes']}  interior eroded "
                     f"{burst['interior_eroded_pixels']}")
        for item in burst['per_frame']:
            for cls in ('routed_interior', 'routed_edge', 'sentinel'):
                r = item['ratios'][cls]
                lines.append(
                    f"  frame {item['frame']} {cls:<16s} gradient ratio "
                    f"{fmt(r['gradient_energy_ratio'])} (noise-corrected "
                    f"{fmt(r['gradient_energy_ratio_noise_corrected'])})  laplacian "
                    f"{fmt(r['laplacian_energy_ratio'])}")
            legacy = item.get('iteration8_comparable')
            if legacy:
                lines.append(f"  frame {item['frame']} iteration8-comparable mask "
                             f"({legacy['mask_pixels']} px) gradient ratio "
                             f"{fmt(legacy['gradient_energy_ratio'])}  half-Nyquist "
                             f"fraction ratio "
                             f"{fmt(legacy.get('high_frequency_fraction_ratio'))}")
            if 'band_fraction_ratio' in item:
                lines.append(f"  frame {item['frame']} high-band fraction ratio "
                             f"{fmt(item['band_fraction_ratio'])} (raw "
                             f"{fmt(item['raw']['band']['high_fraction'])} -> resolved "
                             f"{fmt(item['resolved']['band']['high_fraction'])})")
            es = item.get('edge_spread')
            if es:
                for label in ('raw', 'resolved'):
                    row = es[label]
                    if row.get('status') == 'evaluated':
                        lines.append(f"  frame {item['frame']} edge {label}: "
                                     f"10-90 rise {fmt(row['rise_10_90_px'])} px, MTF50 "
                                     f"{fmt(row['mtf50_cycles_per_px'])} c/px, "
                                     f"{row['edges']} edges")
        spread = burst['jitter_phase_spread']
        lines.append(f"  raw-vs-raw jitter phase spread: all {fmt(spread['relative_spread_all'])}"
                     f", routed interior "
                     f"{fmt(spread['relative_spread_routed_interior'])}")
        captured = burst.get('captured_phase_average')
        if captured:
            lines.append(
                f"  captured {len(captured['frames'])}-phase average (no resampling): "
                f"interior "
                f"{fmt(captured['ratios']['routed_interior']['gradient_energy_ratio'])}"
                f" edge {fmt(captured['ratios']['routed_edge']['gradient_energy_ratio'])}"
                f" all {fmt(captured['ratios']['all']['gradient_energy_ratio'])}"
                f" iteration8-mask {fmt(captured.get('iteration8_comparable_ratio'))}"
                f" laplacian interior "
                f"{fmt(captured['ratios']['routed_interior']['laplacian_energy_ratio'])}")
            lines.append(
                f"    analytic white: this kernel "
                f"{fmt(captured['analytic_white_gradient_ratio'])}, resolve kernel "
                f"{fmt(captured['resolve_kernel_analytic_white'])}, scale "
                f"{fmt(captured.get('scale_to_resolve_kernel'))}")
        for row in burst.get('ideal_supersampling', []):
            lines.append(f"  frame {row['frame']} ideal supersampling gradient ratio "
                         f"interior {fmt(row['ratios']['routed_interior']['gradient_energy_ratio'])}"
                         f" all {fmt(row['ratios']['all']['gradient_energy_ratio'])}"
                         f" iteration8-mask {fmt(row.get('iteration8_comparable_ratio'))}"
                         f" laplacian interior "
                         f"{fmt(row['ratios']['routed_interior']['laplacian_energy_ratio'])}"
                         f" analytic-white {fmt(row.get('analytic_white_gradient_ratio'))}"
                         f" ({row.get('kernel_taps')} taps)")

    samplers = report['samplers']
    lines.append('== sampler states on captured draws')
    lines.append(f"  draws {samplers['captured_draws']}  recorded "
                 f"{samplers['states_recorded']}  missing {samplers['states_not_recorded']}")
    for row in samplers['routed'][:8]:
        lines.append(f"  routed min={row['minfilter']} mag={row['magfilter']} "
                     f"mip={row['mipfilter']} aniso={row['maxanisotropy']} "
                     f"srgb={row['srgbtexture']} bias={row['mipmaplodbias']} "
                     f"maxmip={row['maxmiplevel']} n={row['stage_samples']}")
    for row in samplers['unrouted'][:4]:
        lines.append(f"  unrouted min={row['minfilter']} mag={row['magfilter']} "
                     f"mip={row['mipfilter']} aniso={row['maxanisotropy']} "
                     f"srgb={row['srgbtexture']} bias={row['mipmaplodbias']} "
                     f"maxmip={row['maxmiplevel']} n={row['stage_samples']}")

    health = report['health']
    lines.append('== health')
    lines.append(f"  route totals {health['route_totals']}")
    lines.append(f"  taa {health['taa']}")
    lines.append(f"  camera {health['camera']}  cut {health['cut_detector']}")
    lines.append(f"  reset_count {health['reset_count']}")
    comparison = health['timing'].get('comparison')
    if comparison:
        for key, row in comparison.items():
            if not key.endswith('/scene'):
                continue
            lines.append(f"  {key}: mean ratio {fmt(row.get('mean_ratio'))} min ratio "
                         f"{fmt(row.get('min_ratio'))}")
    return '\n'.join(lines) + '\n'


def fmt(value, digits=4):
    return 'n/a' if value is None else f'{value:.{digits}f}'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--text', type=Path)
    parser.add_argument('--captures', type=Path)
    parser.add_argument('--device', default='1')
    parser.add_argument('--baseline-log', type=Path,
                        help='second session for the frame-time regime comparison')
    parser.add_argument('--baseline-label', default='run1')
    parser.add_argument('--window-mode-split-us', type=float, default=45000.0)
    parser.add_argument('--width', type=int, default=WIDTH)
    parser.add_argument('--height', type=int, default=HEIGHT)
    parser.add_argument('--margin', type=int, default=12)
    parser.add_argument('--tile', type=int, default=256)
    parser.add_argument('--tile-step', type=int, default=64)
    parser.add_argument('--nyquist-fraction', type=float, default=0.25)
    parser.add_argument('--depth-tolerance', type=float, default=it08.DEPTH_TOLERANCE)
    parser.add_argument('--history-weight', type=float, default=HISTORY_WEIGHT)
    parser.add_argument('--ideal-frames', type=int, default=1,
                        help='frames per burst that get the ideal-supersampling reference')
    parser.add_argument('--no-ideal', action='store_true')
    parser.add_argument('--no-edges', action='store_true')
    parser.add_argument('--burst', type=int, action='append',
                        help='first frame of a burst to analyze (repeatable)')
    parser.add_argument('--quiet-gap', type=float, default=3.0)
    parser.add_argument('--no-hash', action='store_true')
    args = parser.parse_args(argv)
    report = build(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1, sort_keys=False) + '\n')
    text = render_text(report)
    if args.text:
        args.text.write_text(text)
    sys.stdout.write(text)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
