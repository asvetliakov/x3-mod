#!/usr/bin/env python3
"""Iteration 9 run 4: the first gameplay session of the FP16 HDR scene path
(stage 1 topology, identity write-back) from one `--direct --ownership
--object-trace --object-lifetime --motion-output --taa --taa-debug --telemetry
--scene-hook --hdr` session log plus its four capture bursts.

Nothing here reads a large file whole: the session log is streamed once and only
sparse records are retained, and the readbacks are loaded one burst at a time
(one `array` per image, no per-pixel Python objects kept between frames).

What the sections mean, and what the readbacks are on this build (commit
1d36c29, `src/proxy/motion_output.cpp`):

* ``hdr_<device>_<frame>.rgba16f`` is the FP16 scene target, read back inside
  ``MotionOutput::hdr_writeback`` *before* the frame's first write-back, so it
  is the scene exactly as the game's shaders left it in A16B16G16R16F.
* ``color_<device>_<frame>.bgra8`` is the application's 8-bit main target read
  back inside ``MotionOutput::resolve``, which on this build runs *after*
  ``end_redirect`` at the engine scene-end hook.  With `--hdr` it is therefore
  the **post-write-back** main target - the identity tonemap of the image
  above - and not an independently rendered 8-bit scene.  (Without `--hdr` the
  same file is the game's own 8-bit scene; the two runs are not the same
  quantity, which is why section 2 compares `color` against `hdr` and not
  against run 2's `color`.)
* ``taa_<device>_<frame>.rgba16f`` is the resolved image, still produced from
  the 8-bit `color` input in this build (stage 3 moves TAA onto the FP16
  image), and ``depth``/``motion`` are the route's RT2/RT1 as in run 2.

1. ``hdr_path`` - health of the redirect.  The `hdr_device` gate, the
   `hdr_target` creations and the `hdr_tonemap` configuration verbatim-free
   (their fields only), then the distribution of every `hdr_frame` field the
   stage-1 record defines (`redirected`, `end`, `writebacks`, `flushes`,
   `writeback_source`, `suspended`, `resumed`, `dirty_at_present`, `unwind`,
   `blocked`, `refused_msaa`), the frames whose `end` is not the expected
   `hook`, and for every such frame the `motion_output_frame` context of the
   same frame (selector state, `scene_end_source`, hook signals, `draws`,
   `taa_skip`) so an anomalous end can be explained rather than counted.
   `hdr_unwind` and `hdr_recheck` records are listed if any exist.

2. ``identity`` - the write-back's own identity check, per captured frame.
   The FP16 readback is converted to 8-bit exactly as the fixed-function
   conversion at the end of the write-back draw does - clamp to [0,1] then
   round - and compared against the `color` readback of the same frame, per
   channel and per pixel class.  Both plausible round-to-nearest tie rules are
   evaluated (`half_up` and `half_even`) because D3D9 does not specify which
   the backend uses; a value whose two rules disagree is a tie candidate and
   is counted separately.  For a binary16 source the tie rule provably cannot
   matter - `v * 255` is a half integer only for `v = 0.5`, which both rules
   send to 128 - so the two columns agree by construction and the result is
   independent of the backend's rounding mode (pinned in the paired test).
   This is a *self*-consistency test of one write-back
   (the FP16 image and the 8-bit image are the same frame's content), so
   unlike the synthetic twin comparison in
   `docs/verification/hdr-scene-path.md` it has no double-rounding term and
   the expectation is exactness.

3. ``headroom`` - what the game's current shaders actually put above 1.0 in
   the FP16 target, i.e. what stage 4 (removing the radiance clamp) would have
   to carry.  Per frame: the fraction of pixels whose largest RGB channel
   exceeds 1.0, the maximum channel value, a log2-octave histogram and a fine
   histogram of that maximum, the same split by pixel class, and the largest
   connected components of the over-1 set (4-connectivity, union-find over
   only those pixels) with their bounding boxes, so "where" is answered by
   geometry and not by an impression.  Alpha and negative channels are
   reported too: the write-back carries alpha through, and a negative channel
   would be a sentinel leak into RT0.

4. ``taa`` - the resolve with the redirect on, and the same counters from a
   baseline log (run 2) for comparison: attempted/resolved/history, the
   `taa_skip` histogram decoded against `TaaSkip` (src/proxy/motion_output.h),
   the `scene_end_source`/`scene_end_check` verdicts and the camera
   sentinel-policy distribution decoded against `SentinelReason`
   (src/renderer/camera_reprojection.h).

5. ``cost`` - the CPU-inclusive `hdr_frame` timings (`redirect_us`,
   `writeback_us`, `writeback_draw_us`, `writeback_stretch_us`, `bind_us`)
   summarized separately for ordinary and capture frames, because a capture
   frame's write-back draw is submitted behind that frame's readbacks; the
   cumulative `telemetry_metric` aggregates for the same metrics; and, when
   `--cost-json` names a report from `analyze_iteration09_cost.py`, the
   frame-time regimes of this run against its baselines.  Every number is
   CPU-side wall clock; none of it is GPU time.

6. ``window`` - the focus/cursor record.  `telemetry_window`,
   `telemetry_window_context` and `telemetry_cursor_poll` are change-only
   records; the section groups them into departure/return episodes (foreground
   or GUI focus leaving the device window) with the cursor samples inside each,
   which is the evidence `docs/reverse-engineering/cursor-observations.md`
   asks for, and states whether the redirect was active during any of them.
"""
import argparse
import array
import json
import math
import statistics
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze_iteration07_taa as it07  # noqa: E402
import analyze_iteration08_taa as it08  # noqa: E402

fields, number, real = it07.fields, it07.number, it07.real

WIDTH, HEIGHT = 1280, 768
CLASS_NAMES = {it08.SENTINEL: 'sentinel', it08.INTERIOR: 'routed_interior',
               it08.EDGE: 'routed_edge'}
TAA_SKIP = {0: 'none', 1: 'disabled', 2: 'not_reached', 3: 'no_jitter', 4: 'not_filled',
            5: 'recording', 6: 'queries', 7: 'initialize', 8: 'container',
            9: 'camera_state', 10: 'target'}
SENTINEL_REASON = {0: 'camera_path', 1: 'switch_off', 2: 'current_invalid',
                   3: 'previous_invalid', 4: 'rotation_cut', 5: 'transform_failed'}
BOUNDARY_STATE = {0: 'await_initial_clear', 1: 'background', 2: 'scene', 3: 'await_copy',
                  4: 'await_bloom_target', 5: 'await_bloom_draw', 6: 'await_depth_rebind',
                  7: 'await_final_clear', 8: 'selected', 9: 'rejected'}
SCENE_END_CHECK = {0: 'none', 1: 'agree', 2: 'hook_only', 3: 'stretch_only', 4: 'disagree'}
HDR_METRICS = ('hdr_redirect', 'hdr_writeback', 'hdr_writeback_draw',
               'hdr_writeback_stretch', 'hdr_bind', 'hdr_recheck', 'hdr_meter',
               'hdr_meter_readback')
HDR_FRAME_ENUMS = ('end', 'writeback_source', 'caps', 'tonemap', 'recheck', 'unwind_reason')
HDR_FRAME_FLAGS = ('redirected', 'unwind', 'blocked', 'suspended', 'resumed',
                   'dirty_at_present', 'refused_msaa', 'writebacks', 'flushes',
                   'tonemapped', 'fallback')
HDR_FRAME_TIMES = ('redirect_us', 'writeback_us', 'writeback_draw_us',
                   'writeback_stretch_us', 'bind_us', 'recheck_us')
FRAME_KEYS = ('frame', 'latched', 'draws', 'routed', 'matched', 'selector_state',
              'taa_attempted', 'taa_resolved', 'taa_history', 'taa_skip',
              'camera_policy', 'camera_reason', 'camera_cut', 'camera_rotation_deg',
              'scene_end_source', 'scene_end_check', 'hook_signals',
              'hook_outside_scene', 'hook_state', 'draws_after_hook', 'bloom_copy_seen',
              'restore_failures', 'apply_failures', 'cut')


class Malformed(Exception):
    pass


# ---- the write-back's 8-bit conversion ---------------------------------------------

def unorm8(value, tie='half_up'):
    """One channel of the fixed-function A16B16G16R16F -> A8R8G8B8 conversion the
    write-back draw ends with: NaN to 0, clamp to [0,1], scale by 255 and round
    to nearest.  D3D9 does not specify the tie rule, so both are available."""
    if value != value:
        return 0
    if value <= 0.0:
        return 0
    if value >= 1.0:
        return 255
    scaled = value * 255.0
    low = math.floor(scaled)
    remainder = scaled - low
    if remainder > 0.5:
        return int(low) + 1
    if remainder < 0.5:
        return int(low)
    if tie == 'half_even':
        return int(low) + (1 if int(low) & 1 else 0)
    return int(low) + 1


def writeback_table(tie='half_up'):
    """The conversion for every binary16 bit pattern (a 65536-entry bytes table,
    so a whole readback maps with one `bytes(map(...))`)."""
    return bytes(unorm8(it07.HALF_TABLE[bits], tie) for bits in range(65536))


# ---- log streaming -----------------------------------------------------------------

def scan_log(path, device='1'):
    """One streaming pass over a session log; only sparse records are retained."""
    out = {'path': str(path), 'device': device, 'configuration': {}, 'hdr_frames': [],
           'hdr_unwind': [], 'hdr_recheck': [], 'hdr_target': [], 'frames': [],
           'readbacks': [], 'telemetry': {}, 'window': [], 'window_context': [],
           'cursor': [], 'reset_count': None, 'counts': Counter()}
    for event, line in it07.stream_records(path):
        out['counts'][event] += 1
        if event in ('motion_output_mode', 'motion_output_device', 'motion_output_taa',
                     'motion_output_target', 'motion_capture_mode', 'hdr_device',
                     'hdr_tonemap', 'scene_hook', 'telemetry_start', 'telemetry_presentation',
                     'x3-modern-renderer', 'adapter', 'backend', 'create_device'):
            out['configuration'].setdefault(event, []).append(fields(line))
        elif event == 'hdr_target':
            out['hdr_target'].append(fields(line))
        elif event == 'hdr_frame':
            f = fields(line)
            if f.get('device') == device:
                out['hdr_frames'].append(f)
        elif event.startswith('hdr_unwind'):
            out['hdr_unwind'].append(fields(line))
        elif event == 'hdr_recheck':
            out['hdr_recheck'].append(fields(line))
        elif event == 'motion_output_frame':
            f = fields(line)
            if f.get('device') == device:
                out['frames'].append({k: f.get(k) for k in FRAME_KEYS})
        elif event in ('hdr_readback', 'motion_output_color_readback',
                       'motion_output_taa_readback', 'motion_output_readback',
                       'motion_output_depth_readback'):
            f = fields(line)
            out['readbacks'].append({'record': event, 'frame': number(f.get('frame')),
                                     'file': f.get('file'), 'result': f.get('result')})
        elif event == 'telemetry_metric':
            it07.telemetry_accumulate(out['telemetry'], fields(line))
        elif event == 'telemetry_window':
            out['window'].append(fields(line))
        elif event == 'telemetry_window_context':
            out['window_context'].append(fields(line))
        elif event == 'telemetry_cursor_poll':
            out['cursor'].append(fields(line))
        elif event == 'telemetry_first_present':
            out['reset_count'] = number(fields(line).get('reset_count'))
    it07.telemetry_finish(out['telemetry'])
    return out


def describe(values, digits=3):
    """Count and order statistics of a sample; no distribution is assumed."""
    values = sorted(v for v in values if v is not None)
    if not values:
        return {'count': 0}
    return {'count': len(values), 'min': round(values[0], digits),
            'median': round(statistics.median(values), digits),
            'mean': round(statistics.fmean(values), digits),
            'p90': round(values[int(0.9 * (len(values) - 1))], digits),
            'max': round(values[-1], digits), 'total': round(math.fsum(values), digits)}


# ---- 1. HDR path health ------------------------------------------------------------

def hdr_report(scan, capture_frames):
    frames = scan['hdr_frames']
    distributions = {}
    for key in HDR_FRAME_ENUMS + HDR_FRAME_FLAGS:
        counter = Counter(f.get(key) for f in frames)
        distributions[key] = {str(k): v for k, v in sorted(counter.items(),
                                                           key=lambda kv: (-kv[1], str(kv[0])))}
    by_frame = {number(f.get('frame')): f for f in scan['frames']}
    anomalies = []
    for f in frames:
        if f.get('end') == 'hook' and f.get('redirected') == '1':
            continue
        frame = number(f.get('frame'))
        context = by_frame.get(frame, {})
        state = number(context.get('selector_state'))
        anomalies.append({
            'frame': frame, 'redirected': f.get('redirected'), 'end': f.get('end'),
            'writebacks': number(f.get('writebacks')), 'flushes': number(f.get('flushes')),
            'writeback_source': f.get('writeback_source'),
            'dirty_at_present': number(f.get('dirty_at_present')),
            'suspended': number(f.get('suspended')), 'resumed': number(f.get('resumed')),
            'target_create': f.get('target_create'), 'latch_bind': f.get('latch_bind'),
            'context': {'latched': context.get('latched'), 'draws': context.get('draws'),
                        'routed': context.get('routed'),
                        'selector_state': state,
                        'selector_state_name': BOUNDARY_STATE.get(state),
                        'scene_end_source': context.get('scene_end_source'),
                        'scene_end_check': SCENE_END_CHECK.get(
                            number(context.get('scene_end_check'))),
                        'hook_signals': context.get('hook_signals'),
                        'hook_outside_scene': context.get('hook_outside_scene'),
                        'hook_state': BOUNDARY_STATE.get(number(context.get('hook_state'))),
                        'bloom_copy_seen': context.get('bloom_copy_seen'),
                        'taa_skip': TAA_SKIP.get(number(context.get('taa_skip')))}})
    latched = [f for f in frames if f.get('redirected') == '1']
    unwinds = [f for f in frames if f.get('unwind') != '0']
    return {
        'device_gate': (scan['configuration'].get('hdr_device') or [{}])[0],
        'tonemap': (scan['configuration'].get('hdr_tonemap') or [{}])[0],
        'targets': scan['hdr_target'],
        'lines': len(frames), 'latched': len(latched),
        'distributions': distributions,
        'unwind_records': scan['hdr_unwind'], 'recheck_records': scan['hdr_recheck'],
        'unwind_frames': [number(f.get('frame')) for f in unwinds],
        'capture_frames_all_latched': all(
            f.get('redirected') == '1' for f in frames
            if number(f.get('frame')) in capture_frames),
        'anomalies': anomalies,
        'verdict': {
            'every_latched_frame_wrote_back_with_the_shader': all(
                f.get('writeback_source') == 'shader' and number(f.get('writebacks')) >= 1
                for f in latched),
            'no_unwind': not unwinds and not scan['hdr_unwind'],
            'no_recheck': not scan['hdr_recheck'],
            'no_content_pending_at_present': all(
                number(f.get('dirty_at_present')) == 0 for f in frames),
            'no_suspend': all(number(f.get('suspended')) == 0 for f in frames),
            'no_msaa_refusal': all(number(f.get('refused_msaa')) == 0 for f in frames),
            'reset_count': scan['reset_count']}}


# ---- 2 and 3: the captured frames --------------------------------------------------

def burst_groups(frames, gap=1):
    """Consecutive captured frames grouped into bursts: a jump larger than `gap`
    starts a new burst."""
    out = []
    for frame in sorted(frames):
        if out and frame - out[-1][-1] <= gap:
            out[-1].append(frame)
        else:
            out.append([frame])
    return out


def load_halves(path, width=WIDTH, height=HEIGHT):
    if path.stat().st_size != width * height * 8:
        raise Malformed(f'{path.name}: size != {width}x{height}x8')
    raw = array.array('H')
    with path.open('rb') as stream:
        raw.fromfile(stream, width * height * 4)
    if sys.byteorder != 'little':
        raw.byteswap()
    return raw


def load_bgra8(path, width=WIDTH, height=HEIGHT):
    data = path.read_bytes()
    if len(data) != width * height * 4:
        raise Malformed(f'{path.name}: {len(data)} bytes != {width}x{height}x4')
    return data


def compare_frame(halves, colour, classes, tables):
    """One pass over a captured frame: the identity comparison of the FP16 image
    against the 8-bit write-back, per channel and per pixel class."""
    up, even = tables
    pixels = len(colour) // 4
    channels = ('b', 'g', 'r', 'a')
    # `colour` is B, G, R, A; the FP16 readback is R, G, B, A.
    order = (2, 1, 0, 3)
    totals = {'pixels': pixels, 'exact_half_up': 0, 'exact_half_even': 0,
              'max_difference': 0, 'tie_candidates': 0,
              'channel_differences': {c: 0 for c in channels},
              'channel_max_difference': {c: 0 for c in channels},
              'difference_histogram': Counter()}
    per_class = {}
    for name in CLASS_NAMES.values():
        per_class[name] = {'pixels': 0, 'exact_half_up': 0, 'max_difference': 0,
                           'differing': 0}
    for index in range(pixels):
        base = index * 4
        worst = 0
        exact_even = True
        tie = False
        for channel, source in enumerate(order):
            bits = halves[base + source]
            expected = up[bits]
            actual = colour[base + channel]
            difference = expected - actual
            if difference < 0:
                difference = -difference
            if difference:
                name = channels[channel]
                totals['channel_differences'][name] += 1
                if difference > totals['channel_max_difference'][name]:
                    totals['channel_max_difference'][name] = difference
            if difference > worst:
                worst = difference
            if even[bits] != actual:
                exact_even = False
            if even[bits] != expected:
                tie = True
        totals['difference_histogram'][worst] += 1
        if not worst:
            totals['exact_half_up'] += 1
        if exact_even:
            totals['exact_half_even'] += 1
        if tie:
            totals['tie_candidates'] += 1
        if worst > totals['max_difference']:
            totals['max_difference'] = worst
        bucket = per_class[CLASS_NAMES[classes[index]]]
        bucket['pixels'] += 1
        if worst:
            bucket['differing'] += 1
            if worst > bucket['max_difference']:
                bucket['max_difference'] = worst
        else:
            bucket['exact_half_up'] += 1
    totals['difference_histogram'] = {str(k): v for k, v
                                      in sorted(totals['difference_histogram'].items())}
    totals['exact_fraction_half_up'] = totals['exact_half_up'] / pixels
    totals['exact_fraction_half_even'] = totals['exact_half_even'] / pixels
    totals['classes'] = per_class
    return totals


def components_of(indices, width):
    """4-connected components of a sparse pixel set, by union-find over only the
    set's own members (the image itself is never labelled)."""
    parent = {index: index for index in indices}

    def find(node):
        root = node
        while parent[root] != root:
            root = parent[root]
        while parent[node] != root:
            parent[node], node = root, parent[node]
        return root

    for index in indices:
        for neighbour in (index - 1 if index % width else None, index - width):
            if neighbour is not None and neighbour in parent:
                a, b = find(index), find(neighbour)
                if a != b:
                    parent[a] = b
    groups = {}
    for index in indices:
        groups.setdefault(find(index), []).append(index)
    return list(groups.values())


def headroom_frame(halves, classes, width=WIDTH, components=6):
    """What the frame's FP16 target holds above 1.0, and where."""
    table = it07.HALF_TABLE
    pixels = len(halves) // 4
    over = []
    maximum = 0.0
    octaves = Counter()
    fine = Counter()
    negative = 0
    alpha = Counter()
    non_finite = 0
    per_class = {name: {'pixels': 0, 'over_one': 0, 'max': 0.0}
                 for name in CLASS_NAMES.values()}
    for index in range(pixels):
        base = index * 4
        r, g, b = table[halves[base]], table[halves[base + 1]], table[halves[base + 2]]
        a = table[halves[base + 3]]
        peak = r if r > g else g
        if b > peak:
            peak = b
        if peak != peak or peak in (math.inf, -math.inf):
            non_finite += 1
            continue
        if r < 0.0 or g < 0.0 or b < 0.0:
            negative += 1
        alpha[1 if a == 1.0 else (0 if a == 0.0 else 2)] += 1
        bucket = per_class[CLASS_NAMES[classes[index]]]
        bucket['pixels'] += 1
        if peak > bucket['max']:
            bucket['max'] = peak
        if peak > maximum:
            maximum = peak
        # A fine histogram of the values that matter for the clamp: below 1 only
        # the saturated shoulder is interesting, above 1 every quarter stop.
        if peak > 1.0:
            over.append(index)
            bucket['over_one'] += 1
            octaves[int(math.floor(math.log2(peak)))] += 1
            fine[round(math.floor(peak * 4) / 4, 2)] += 1
        elif peak == 1.0:
            fine['1.0_exact'] += 1
    groups = sorted(components_of(over, width), key=len, reverse=True)
    top = []
    for group in groups[:components]:
        xs = [index % width for index in group]
        ys = [index // width for index in group]
        peaks = []
        for index in group:
            base = index * 4
            peaks.append(max(table[halves[base]], table[halves[base + 1]],
                             table[halves[base + 2]]))
        top.append({'pixels': len(group), 'x': [min(xs), max(xs)], 'y': [min(ys), max(ys)],
                    'max': round(max(peaks), 4),
                    'classes': {CLASS_NAMES[value]: n for value, n in
                                Counter(classes[index] for index in group).items()}})
    for bucket in per_class.values():
        bucket['max'] = round(bucket['max'], 4)
        bucket['over_one_fraction'] = (bucket['over_one'] / bucket['pixels']
                                       if bucket['pixels'] else 0.0)
    return {'pixels': pixels, 'over_one': len(over), 'over_one_fraction': len(over) / pixels,
            'max': round(maximum, 4), 'non_finite': non_finite, 'negative_channel': negative,
            'alpha': {'one': alpha[1], 'zero': alpha[0], 'other': alpha[2]},
            'octave_histogram': {str(k): v for k, v in sorted(octaves.items())},
            'peak_histogram': {str(k): v for k, v in sorted(fine.items(), key=str)},
            'classes': per_class, 'components': len(groups), 'largest_components': top}


def capture_report(directory, scan, device='1', width=WIDTH, height=HEIGHT,
                   components=6):
    """Sections 2 and 3, one burst at a time."""
    captured = sorted({r['frame'] for r in scan['readbacks']
                       if r['record'] == 'hdr_readback'})
    tables = (writeback_table('half_up'), writeback_table('half_even'))
    identity, headroom, bursts = {}, {}, []
    for burst in burst_groups(captured):
        depths, alphas = {}, {}
        for frame in burst:
            depths[frame] = it08.load_depth_r32f(
                directory / f'depth_{device}_{frame}.r32f', width, height)
            alphas[frame] = it08.load_motion_alpha(
                directory / f'motion_{device}_{frame}.rgba32f', width, height)
        classes = it08.classify_pixels([depths[f] for f in burst],
                                       [alphas[f] for f in burst])
        depths.clear()
        alphas.clear()
        counts = Counter(classes)
        bursts.append({'frames': burst,
                       'classes': {CLASS_NAMES[k]: counts[k] for k in CLASS_NAMES}})
        for frame in burst:
            halves = load_halves(directory / f'hdr_{device}_{frame}.rgba16f', width, height)
            colour = load_bgra8(directory / f'color_{device}_{frame}.bgra8', width, height)
            identity[str(frame)] = compare_frame(halves, colour, classes, tables)
            del colour
            headroom[str(frame)] = headroom_frame(halves, classes, width, components)
            del halves
    exact = [identity[k]['exact_fraction_half_up'] for k in identity]
    return ({'frames': identity, 'bursts': bursts,
             'readback_results': sorted({r['result'] for r in scan['readbacks']}),
             'verdict': {'frames': len(identity),
                         'all_frames_exact': all(v == 1.0 for v in exact),
                         'min_exact_fraction': min(exact) if exact else None,
                         'max_difference': max((identity[k]['max_difference']
                                                for k in identity), default=None),
                         'tie_candidates': sum(identity[k]['tie_candidates']
                                               for k in identity)}},
            {'frames': headroom,
             'verdict': {
                 'max_over_all_frames': max((headroom[k]['max'] for k in headroom),
                                            default=None),
                 'max_over_one_fraction': max((headroom[k]['over_one_fraction']
                                               for k in headroom), default=None),
                 'any_negative_channel': any(headroom[k]['negative_channel']
                                             for k in headroom),
                 'any_non_finite': any(headroom[k]['non_finite'] for k in headroom)}})


# ---- 4. TAA ------------------------------------------------------------------------

def taa_report(scan):
    frames = scan['frames']
    attempted = [f for f in frames if f.get('taa_attempted') == '1']
    resolved = [f for f in frames if f.get('taa_resolved') == '1']
    return {
        'logged_frames': len(frames), 'attempted': len(attempted), 'resolved': len(resolved),
        'used_history': sum(1 for f in frames if f.get('taa_history') == '1'),
        'skip': {TAA_SKIP.get(number(f), str(f)): n for f, n in
                 Counter(number(f.get('taa_skip')) for f in frames).items()},
        'scene_end_source': dict(Counter(f.get('scene_end_source') for f in frames)),
        'scene_end_check': {SCENE_END_CHECK.get(number(v), str(v)): n for v, n in
                            Counter(number(f.get('scene_end_check')) for f in frames).items()},
        'camera_policy': dict(Counter(f.get('camera_policy') for f in frames)),
        'camera_reason': {SENTINEL_REASON.get(number(v), str(v)): n for v, n in
                          Counter(number(f.get('camera_reason')) for f in frames).items()},
        'camera_cut': dict(Counter(f.get('camera_cut') for f in frames)),
        'camera_rotation_deg': describe([real(f.get('camera_rotation_deg'))
                                         for f in resolved]),
        'restore_failures': sum(number(f.get('restore_failures'), 0) for f in frames),
        'apply_failures': sum(number(f.get('apply_failures'), 0) for f in frames),
        'draws': describe([real(f.get('draws')) for f in frames], 1),
        'routed': describe([real(f.get('routed')) for f in frames], 1)}


# ---- 5. cost -----------------------------------------------------------------------

def cost_report(scan, capture_frames, cost_json=None):
    latched = [f for f in scan['hdr_frames'] if f.get('redirected') == '1']
    groups = {'ordinary': [f for f in latched
                           if number(f.get('frame')) not in capture_frames],
              'capture': [f for f in latched if number(f.get('frame')) in capture_frames]}
    timings = {}
    for label, rows in groups.items():
        entry = {'frames': len(rows)}
        for key in HDR_FRAME_TIMES:
            entry[key] = describe([real(f.get(key)) for f in rows], 1)
        entry['total_us'] = describe(
            [sum(real(f.get(k), 0.0) for k in
                 ('redirect_us', 'writeback_us', 'bind_us', 'recheck_us')) for f in rows], 1)
        timings[label] = entry
    metrics = {}
    for key, value in sorted(scan['telemetry'].items()):
        if value['name'] in HDR_METRICS and value['device'] == scan['device']:
            metrics[value['name']] = {
                'count': value['count'], 'failures': value['failures'],
                'total_us': round(value['total_us'], 1),
                'mean_us': round(value['mean_us'], 3) if value['mean_us'] else None,
                'min_us': round(value['min_us'], 1), 'max_us': round(value['max_us'], 1),
                'windows': value['windows'], 'buckets': value.get('buckets'),
                'bucket_labels': value.get('bucket_labels')}
    out = {'hdr_frame_timings': timings, 'telemetry_metrics': metrics}
    if cost_json:
        report = json.loads(Path(cost_json).read_text())
        runs = {}
        for run in report.get('runs', []):
            regimes = dict(run.get('scene', {}))
            regimes['menu'] = run.get('menu', {})
            runs[run['label']] = {
                'flags': {k: v for k, v in
                          (run.get('configuration', {}).get('motion_output') or {}).items()
                          if k in ('route', 'taa', 'taa_debug', 'hdr', 'scene_hook')},
                'profiler': run.get('configuration', {}).get('profiler'),
                'regimes': {name: {'windows': regimes.get(name, {}).get('windows'),
                                   'mean_ms': (regimes.get(name, {}).get('mean_ms') or {})
                                   .get('median'),
                                   'min_ms': (regimes.get(name, {}).get('min_ms') or {})
                                   .get('median'),
                                   'draws_per_frame': (regimes.get(name, {})
                                                       .get('draws_per_frame') or {})
                                   .get('median')}
                            for name in ('fast', 'slow', 'menu') if name in regimes}}
        out['frame_time'] = {'source': str(cost_json), 'runs': runs,
                             'comparisons': [
                                 {'primary': c.get('primary'), 'baseline': c.get('baseline'),
                                  'role': c.get('role'), 'controlled': c.get('controlled'),
                                  'regimes': {k: {
                                      'delta_mean_ms': v.get('delta_mean_ms'),
                                      'delta_mean_percent': v.get('delta_mean_percent'),
                                      'delta_min_ms': v.get('delta_min_ms')}
                                      for k, v in (c.get('regimes') or {}).items()}}
                                 for c in report.get('comparisons', [])]}
    return out


# ---- 6. focus and cursor -----------------------------------------------------------

def window_report(scan):
    """Change-only window/cursor records grouped into focus episodes."""
    device_window = None
    presentation = scan['configuration'].get('telemetry_presentation') or []
    if presentation:
        device_window = presentation[0].get('device_window')
    context = {number(c.get('frame')): c for c in scan['window_context']}
    episodes, current = [], None
    for record in scan['window']:
        frame = number(record.get('frame'))
        away = (record.get('foreground') != device_window
                or record.get('thread_focus') in (None, '00000000'))
        entry = {'frame': frame, 'foreground': record.get('foreground'),
                 'thread_focus': record.get('thread_focus'),
                 'iconic': record.get('iconic'), 'visible': record.get('visible'),
                 'window_rect': record.get('window_rect'),
                 'clip': (context.get(frame) or {}).get('clip'),
                 'gui_active': (context.get(frame) or {}).get('gui_active'),
                 'gui_capture': (context.get(frame) or {}).get('gui_capture')}
        if away:
            current = {'departure': entry, 'return': None, 'cursor': []}
            episodes.append(current)
        elif current is not None and current['return'] is None:
            current['return'] = entry
    for record in scan['cursor']:
        frame = number(record.get('frame'))
        sample = {'frame': frame, 'flags': record.get('flags'),
                  'cursor': record.get('cursor'),
                  'position': (record.get('x'), record.get('y'))}
        for episode in episodes:
            end = (episode['return'] or {}).get('frame')
            if frame >= episode['departure']['frame'] and (end is None or frame <= end):
                episode['cursor'].append(sample)
    redirected = {number(f.get('frame')) for f in scan['hdr_frames']
                  if f.get('redirected') == '1'}
    for episode in episodes:
        start = episode['departure']['frame']
        end = (episode['return'] or {}).get('frame', start)
        nearby = [f for f in sorted(redirected) if start - 60 <= f <= end + 60]
        episode['redirect_latched_within_60_frames'] = nearby
    return {'device_window': device_window, 'window_records': len(scan['window']),
            'cursor_records': len(scan['cursor']),
            'cursor_visible_records': [
                {'frame': number(r.get('frame')), 'flags': r.get('flags'),
                 'cursor': r.get('cursor'), 'position': (r.get('x'), r.get('y'))}
                for r in scan['cursor'] if r.get('flags') not in (None, '0')],
            'episodes': episodes, 'reset_count': scan['reset_count'],
            'iconic_seen': any(r.get('iconic') == '1' for r in scan['window'])}


# ---- report ------------------------------------------------------------------------

def build(args):
    scan = scan_log(Path(args.log), args.device)
    captured = sorted({r['frame'] for r in scan['readbacks']
                       if r['record'] == 'hdr_readback'})
    capture_frames = set(captured)
    report = {'log': scan['path'], 'device': args.device,
              'configuration': scan['configuration'], 'record_counts':
              {k: v for k, v in sorted(scan['counts'].items(), key=lambda kv: -kv[1])
               if v < 5000},
              'capture_frames': captured,
              'hdr_path': hdr_report(scan, capture_frames),
              'taa': taa_report(scan),
              'cost': cost_report(scan, capture_frames, args.cost_json),
              'window': window_report(scan)}
    if args.baseline:
        baseline = scan_log(Path(args.baseline), args.device)
        report['taa_baseline'] = {'log': baseline['path'], 'label': args.baseline_label,
                                  'hdr_frame_lines': len(baseline['hdr_frames']),
                                  **taa_report(baseline)}
    if args.captures and captured:
        identity, headroom = capture_report(Path(args.captures), scan, args.device,
                                            args.width, args.height, args.components)
        report['identity'] = identity
        report['headroom'] = headroom
    report['limits'] = [
        'One session of one build (1d36c29) on CrossOver Preview; no Windows evidence.',
        'With --hdr the `color` readback is the post-write-back main target, so the '
        'identity section is a self-consistency check of one write-back and not a '
        'comparison against an independently rendered 8-bit frame.',
        'The FP16 readback is taken before the frame\'s first write-back and the 8-bit '
        'readback after it, both inside the same frame; a frame with more than one '
        'write-back would compare the first FP16 state against the last 8-bit state.',
        'hdr_frame is emitted in capture frames and every X3M_MOTION_FRAME_LOG frames, '
        'so the distributions describe the logged frames, not every frame of the run.',
        'All timings are CPU-inclusive QPC spans of the calling thread; never GPU time.',
        'The window/cursor records are change-only and at most 4 Hz: an unobserved '
        'transition between two records is not excluded.']
    return report


def render_text(report):
    out = []
    add = out.append
    add('iteration 9 run 4: FP16 HDR scene path (stage 1) in gameplay')
    add('=' * 72)
    add(f"log {report['log']}  device {report['device']}")
    hdr = report['hdr_path']
    gate = hdr['device_gate']
    add('')
    add('-- 1. HDR path')
    add(f"   gate enabled={gate.get('enabled')} reason={gate.get('reason')} "
        f"main_format={gate.get('main_format')} targets={gate.get('self_test_targets')} "
        f"stretch_conversion={gate.get('stretch_conversion')}")
    for target in hdr['targets']:
        add(f"   target frame={target.get('frame')} {target.get('width')}x"
            f"{target.get('height')} bytes={target.get('bytes')} "
            f"create={target.get('create')} meter={target.get('meter')}")
    add(f"   hdr_frame lines {hdr['lines']}, latched {hdr['latched']}")
    for key in ('end', 'writeback_source', 'writebacks', 'flushes', 'redirected',
                'suspended', 'resumed', 'dirty_at_present', 'unwind', 'blocked',
                'refused_msaa'):
        add(f"   {key:18s} {hdr['distributions'][key]}")
    add(f"   unwind records {len(hdr['unwind_records'])}, "
        f"recheck records {len(hdr['recheck_records'])}, "
        f"reset count {hdr['verdict']['reset_count']}")
    for anomaly in hdr['anomalies']:
        add(f"   frame {anomaly['frame']}: redirected={anomaly['redirected']} "
            f"end={anomaly['end']} writebacks={anomaly['writebacks']} "
            f"flushes={anomaly['flushes']} dirty_at_present={anomaly['dirty_at_present']} "
            f"source={anomaly['writeback_source']}")
        context = anomaly['context']
        add(f"      draws={context['draws']} routed={context['routed']} "
            f"selector={context['selector_state_name']} "
            f"scene_end={context['scene_end_source']} "
            f"hook_signals={context['hook_signals']} "
            f"bloom_copy_seen={context['bloom_copy_seen']} "
            f"taa_skip={context['taa_skip']}")
    if 'identity' in report:
        identity = report['identity']
        add('')
        add('-- 2. identity of the write-back (FP16 -> 8-bit, same frame)')
        add('   %-8s %10s %10s %6s %6s' % ('frame', 'exact', 'fraction', 'maxd', 'ties'))
        for frame, entry in identity['frames'].items():
            add('   %-8s %10d %10.6f %6d %6d'
                % (frame, entry['exact_half_up'], entry['exact_fraction_half_up'],
                   entry['max_difference'], entry['tie_candidates']))
        verdict = identity['verdict']
        add(f"   all frames exact: {verdict['all_frames_exact']}  "
            f"min exact fraction {verdict['min_exact_fraction']}  "
            f"max difference {verdict['max_difference']}")
    if 'headroom' in report:
        headroom = report['headroom']
        add('')
        add('-- 3. FP16 headroom above 1.0')
        add('   %-8s %10s %10s %8s  %s' % ('frame', 'over1', 'fraction', 'max', 'octaves'))
        for frame, entry in headroom['frames'].items():
            add('   %-8s %10d %10.6f %8.4f  %s'
                % (frame, entry['over_one'], entry['over_one_fraction'], entry['max'],
                   entry['octave_histogram']))
        for frame, entry in headroom['frames'].items():
            classes = ' '.join('%s %d/%d' % (name, bucket['over_one'], bucket['pixels'])
                               for name, bucket in entry['classes'].items())
            add(f"   {frame} classes: {classes}")
            for component in entry['largest_components'][:3]:
                add(f"      component {component['pixels']} px "
                    f"x={component['x']} y={component['y']} max={component['max']} "
                    f"{component['classes']}")
    taa = report['taa']
    add('')
    add('-- 4. TAA with the redirect on')
    add(f"   logged {taa['logged_frames']} attempted {taa['attempted']} "
        f"resolved {taa['resolved']} history {taa['used_history']}")
    add(f"   skip {taa['skip']}  scene_end {taa['scene_end_source']} "
        f"check {taa['scene_end_check']}")
    add(f"   camera policy {taa['camera_policy']} reason {taa['camera_reason']} "
        f"cut {taa['camera_cut']}")
    if 'taa_baseline' in report:
        base = report['taa_baseline']
        add(f"   baseline {base['label']}: logged {base['logged_frames']} "
            f"attempted {base['attempted']} resolved {base['resolved']} "
            f"history {base['used_history']} skip {base['skip']} "
            f"camera policy {base['camera_policy']}")
    cost = report['cost']
    add('')
    add('-- 5. cost (CPU-inclusive)')
    for label, entry in cost['hdr_frame_timings'].items():
        add(f"   {label} frames={entry['frames']}")
        for key in HDR_FRAME_TIMES + ('total_us',):
            stats = entry[key]
            if stats.get('count'):
                add('      %-22s median %8.1f mean %8.1f max %8.1f'
                    % (key, stats['median'], stats['mean'], stats['max']))
    for name, entry in cost['telemetry_metrics'].items():
        add(f"   metric {name}: count={entry['count']} failures={entry['failures']} "
            f"total_us={entry['total_us']} max_us={entry['max_us']}")
    if 'frame_time' in cost:
        for label, run in cost['frame_time']['runs'].items():
            add(f"   {label} {run['flags']} profiler={run['profiler']} "
                + ' '.join('%s %s ms (%s windows, %s draws/f)'
                           % (name, regime['mean_ms'], regime['windows'],
                              regime['draws_per_frame'])
                           for name, regime in run['regimes'].items()))
        for comparison in cost['frame_time']['comparisons']:
            add(f"   {comparison['primary']} vs {comparison['baseline']} "
                f"({comparison['role']}): "
                + ' '.join('%s %+.2f ms (%+.1f%%)'
                           % (k, v['delta_mean_ms'], v['delta_mean_percent'])
                           for k, v in comparison['regimes'].items()
                           if v['delta_mean_ms'] is not None))
    window = report['window']
    add('')
    add('-- 6. focus and cursor')
    add(f"   device window {window['device_window']}  records {window['window_records']} "
        f"cursor {window['cursor_records']} iconic seen {window['iconic_seen']}")
    for episode in window['episodes']:
        departure, back = episode['departure'], episode['return']
        add(f"   away frame {departure['frame']} foreground={departure['foreground']} "
            f"focus={departure['thread_focus']} clip={departure['clip']} -> "
            f"back frame {back['frame'] if back else None}")
        for sample in episode['cursor']:
            add(f"      cursor frame {sample['frame']} flags={sample['flags']} "
                f"handle={sample['cursor']} at {sample['position']}")
        add(f"      redirect latched near the episode: "
            f"{episode['redirect_latched_within_60_frames']}")
    add('')
    for limit in report['limits']:
        add(f"   limit: {limit}")
    return '\n'.join(out)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--log', required=True, help='the run-4 session log')
    parser.add_argument('--captures', help='directory holding the readback files')
    parser.add_argument('--baseline', help='a session log for the TAA comparison (run 2)')
    parser.add_argument('--baseline-label', default='run2')
    parser.add_argument('--cost-json', help='a report from analyze_iteration09_cost.py')
    parser.add_argument('--device', default='1')
    parser.add_argument('--width', type=int, default=WIDTH)
    parser.add_argument('--height', type=int, default=HEIGHT)
    parser.add_argument('--components', type=int, default=6,
                        help='over-1 components listed per frame (default 6)')
    parser.add_argument('--json', type=Path)
    parser.add_argument('--text', type=Path)
    args = parser.parse_args(argv)
    report = build(args)
    if args.json:
        args.json.write_text(json.dumps(report, indent=1, sort_keys=False) + '\n')
    text = render_text(report)
    if args.text:
        args.text.write_text(text + '\n')
    print(text)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
