#!/usr/bin/env python3
"""Summarize one gameplay session of the live same-draw motion route (iteration 6).

The capture log of a gameplay run is hundreds of megabytes, so this tool streams
it exactly once and keeps only bounded per-draw metadata for the captured frames
(the only frames that carry per-draw records). Nothing here needs renderer code,
game geometry or shader bytes; every number comes from the log's own records,
from the shader dumps' version word and from the untracked archive inventory.

What it reports (docs/verification/motion-output.md, docs/architecture/live-motion-route.md):

* ``motion_output_frame`` -- one line per captured frame and every 60th frame:
  distributions of draws/routed/matched, the gate histogram in total and per
  capture burst, apply/restore failures, fill results, ``selector_state``,
  ``committed`` and ``latched`` flags.
* ``motion_route`` -- one line per scene draw of a captured frame: gate
  outcomes, matched rate, key cardinalities, the VS/PS pairs that reached gate 3
  without a reviewed variant (the *unclassified* pairs that a future profile
  table must cover), the bound ``i0`` of gate-4 rejections together with the
  exact gate-4 predicate that failed (recovered from the capture's ``state``
  records), gate-5 scope failures with the observer's reason codes, and gate-6
  history misses with a reconstructed reason.
* Why a captured frame routed nothing: the route's own scene selector is
  replayed from the capture's event stream, so a frame rejected in the
  Background phase (an unreviewed background pair) is distinguished from one
  rejected inside the Scene phase (for example a z-only draw with a null pixel
  shader).
* Identity timeline: load/registry epoch and camera/node serial changes across
  the session and how history matching behaved around them.
* Shader models actually used: every program the session created is resolved to
  its shader model from the first DWORD of its ``{vs,ps}_<hash>.bin`` dump (and
  cross-checked against the archive inventory), and draws are counted per
  ``(VS model, PS model)`` pair split by render phase.
* Telemetry: frame-interval and Present-duration aggregates, split by the
  capture flag and, separately, by whether the route was routing draws in that
  part of the session. The limits of these numbers are carried in the output.
* Diagnostics: nonzero HRESULTs, failure counters, observer status and the
  records that a clean shutdown would have produced.

Usage::

    python3 tools/analysis/analyze_iteration06_motion.py <session.log> \
        --captures <dir with the .bin shader dumps> \
        --output verification/results/iteration-06-motion-summary.json

Determinism: every aggregate is derived from the log alone and every collection
written to the JSON is sorted by an explicit key, so two runs over the same
inputs produce byte-identical output.
"""
from __future__ import annotations

import argparse
import bisect
import collections
import hashlib
import json
import math
import os
import re
import struct
import sys

FIELDS = re.compile(r'(\S+?)=(\S*)')

# Render states the capture dumps per draw; the gate-4 predicate names follow
# src/proxy/motion_output.cpp `before_draw`.
RS_ZENABLE = '7'
RS_ZWRITEENABLE = '14'
RS_ALPHATESTENABLE = '15'
RS_ALPHABLENDENABLE = '27'
RS_COLORWRITEENABLE = '168'
RS_SRGBWRITEENABLE = '194'

# Gate numbering of MotionGate in src/proxy/motion_output.cpp.
GATE_NAMES = {
    0: 'matched',
    1: 'gate1_feature',
    2: 'gate2_scene',
    3: 'gate3_pair',
    4: 'gate4_draw_state',
    5: 'gate5_scope',
    6: 'gate6_history',
}

# BoundaryState in src/renderer/scene_boundary.h.
SELECTOR_STATES = {
    0: 'AwaitInitialClear', 1: 'Background', 2: 'Scene', 3: 'AwaitCopy',
    4: 'AwaitBloomTarget', 5: 'AwaitBloomDraw', 6: 'AwaitDepthRebind',
    7: 'AwaitFinalClear', 8: 'Selected', 9: 'Rejected',
}

# The three background pairs the default SceneSignatures carries
# (src/renderer/scene_boundary.h). Used only to explain a rejected frame; the
# route itself is not consulted here.
BACKGROUND_PAIRS = frozenset({
    ('7b6393fe2d3e1d85', '6109cf64c03529dd'),
    ('37c34a7478544c14', '5f82ecacd39529cd'),
    ('be199829a9bb78db', 'cd6d6eb4b3d99443'),
})

# The 25 logged parts of the DLL's RigidDrawKey, in the order motion_route
# writes them (docs/verification/motion-readback.md, "Conventions assumed").
KEY_FIELDS = ('node', 'camera', 'node_handle', 'camera_handle', 'node_serial', 'camera_serial',
              'load_epoch', 'registry_epoch', 'model', 'lod', 'vb', 'ib', 'declaration', 'offset',
              'stride', 'vs', 'position_offset', 'position_type', 'topology', 'first', 'primitives',
              'base_vertex', 'min_vertex', 'vertex_count', 'indexed')

# Gates whose draws enter the current history table (0 matched, 6 lookup miss).
RECORDING_GATES = (0, 6)
# Gates reached only after gate 4, i.e. the draws whose RigidDrawKey fields the
# route actually filled in. A gate-1..4 motion_route line prints a zeroed key.
KEYED_GATES = (0, 5, 6)

PHASES = ('background', 'scene', 'bloom', 'overlay_gui')

TELEMETRY_LIMITS = [
    'CPU-side wall-clock spans, not GPU execution time (docs/verification/telemetry.md).',
    'Frame intervals include application work, pacing, resource loading and diagnostics.',
    'Intervals adjacent to a captured frame are classified as capture intervals and '
    'cannot enter the normal distribution, so frame_capture is the F8 hitch, not a cost model.',
    'Metric categories can overlap or nest; never add totals of different names together.',
    'Summary windows flush on observed calls, so a gap does not prove idle time.',
    'No run with the route disabled exists for this scene, so no metric here isolates the '
    'route cost; the routed/unrouted split below compares parts of one session that also '
    'differ in scene content.',
]


def fields(line):
    return dict(FIELDS.findall(line))


def to_int(text, default=None):
    try:
        return int(text, 10)
    except (TypeError, ValueError):
        return default


def hex_int(text, default=None):
    try:
        return int(text, 16)
    except (TypeError, ValueError):
        return default


def distribution(values):
    """Deterministic min/median/mean/p95/max of a list of numbers."""
    if not values:
        return dict(count=0)
    ordered = sorted(values)
    n = len(ordered)

    def quantile(q):
        return ordered[min(n - 1, max(0, int(math.ceil(q * n)) - 1))]

    return dict(count=n, min=ordered[0], median=quantile(0.5), mean=sum(ordered) / n,
                p95=quantile(0.95), max=ordered[-1], total=sum(ordered))


def counter_to_sorted(counter):
    """Counter -> list of [key, count] sorted by count descending then key."""
    items = list(counter.items())
    items.sort(key=lambda kv: (-kv[1], str(kv[0])))
    return [[list(k) if isinstance(k, tuple) else k, v] for k, v in items]


class DrawRecord:
    """Bounded per-draw metadata of one captured frame."""

    __slots__ = ('index', 'vs', 'ps', 'phase', 'gate', 'routed', 'matched', 'key',
                 'states', 'integer0', 'lifetime', 'scoped', 'valid', 'rows_hash',
                 'load_epoch', 'registry_epoch', 'node_serial', 'camera_serial', 'primitives')

    def __init__(self, index, vs, ps, phase, primitives):
        self.index, self.vs, self.ps, self.phase = index, vs, ps, phase
        self.primitives = primitives
        self.gate = self.routed = self.matched = None
        self.key = None
        self.states = {}
        self.integer0 = None
        self.lifetime = None
        self.scoped = self.valid = None
        self.rows_hash = None
        self.load_epoch = self.registry_epoch = None
        self.node_serial = self.camera_serial = None


class FrameRecord:
    def __init__(self, device, frame):
        self.device, self.frame = device, frame
        self.draws = []
        self.counters = None          # motion_output_frame fields
        self.reported_draws = None
        self.present = None
        self.phase_end = {}
        self.selector_trace = None    # why the route's selector rejected the frame


# Gate-4 predicates in the order ``before_draw`` evaluates its short-circuiting
# conjunction (src/proxy/motion_output.cpp). The DLL stops at the first false
# one; the capture records every state, so the analysis can report both.
GATE4_PREDICATES = (
    ('z_enable', RS_ZENABLE, (1,)),
    ('z_write', RS_ZWRITEENABLE, (1,)),
    ('alpha_blend', RS_ALPHABLENDENABLE, (0,)),
    ('alpha_test', RS_ALPHATESTENABLE, (0,)),
    ('srgb_write', RS_SRGBWRITEENABLE, (0,)),
    ('color_write_mask', RS_COLORWRITEENABLE, (15,)),
)


def gate4_failures(draw):
    """Every gate-4 predicate a draw violates, and the one the DLL saw first.

    Returns ``(all_false_predicates, first_false_predicate)``. Predicates whose
    state the capture did not record are not claimed to be false.
    """
    reasons = []
    for name, state, allowed in GATE4_PREDICATES:
        value = draw.states.get(state)
        if value is not None and value not in allowed:
            reasons.append(name)
    if draw.integer0 is not None and not 0 <= draw.integer0[0] <= 8:
        reasons.append('light_loop_i0')
    if not draw.primitives:
        reasons.append('no_primitives')
    if not reasons:
        return ['not_recoverable_from_capture'], 'not_recoverable_from_capture'
    return reasons, reasons[0]


def passes_gate4_states(draw):
    """True when the capture's own states satisfy every gate-4 state predicate.

    Used to separate the unclassified gate-3 pairs that are ordinary opaque
    material (worth a profile row) from transparent or depth-read-only effects
    that gate 4 would reject anyway.
    """
    for _, state, allowed in GATE4_PREDICATES:
        if draw.states.get(state) not in allowed:
            return False
    return True


def selector_explanation(frame):
    """Replay the route's scene selector over the frame's own draw order.

    Only the two transitions that this session actually exercises are modelled:
    the Background phase accepts the reviewed background pairs and the
    depth-only Clear, and the Scene phase accepts draws with both stages bound.
    The result names the first draw that forced ``advance`` to fail, which is
    the reason every later draw of the frame is rejected at gate 2.
    """
    state = 'background'
    for draw in frame.draws:
        if draw.phase == 'background':
            if (draw.vs, draw.ps) not in BACKGROUND_PAIRS:
                return dict(rejected=True, state='Background', draw_index=draw.index,
                            vs=draw.vs, ps=draw.ps,
                            reason='background draw whose VS/PS pair is not one of the three '
                                   'reviewed background signatures')
            continue
        if draw.phase != 'scene':
            break
        state = 'scene'
        if not draw.ps or draw.ps == '0' * 16:
            return dict(rejected=True, state='Scene', draw_index=draw.index,
                        vs=draw.vs, ps=draw.ps,
                        reason='scene draw with a null pixel shader (z-only pass); '
                               'SceneBoundarySelector::scene_draw requires both stages')
        if not draw.vs or draw.vs == '0' * 16:
            return dict(rejected=True, state='Scene', draw_index=draw.index,
                        vs=draw.vs, ps=draw.ps,
                        reason='scene draw with a null vertex shader')
    return dict(rejected=False, state=state.capitalize())


def history_miss_reasons(frame, previous_frame):
    """Reconstruct why each gate-6 draw of ``frame`` found no previous entry.

    The DLL's table is keyed by the 25 logged key fields and is filled by every
    frame, captured or not. The log therefore proves a reason only when frame
    N-1 was captured too; otherwise the miss is reported as unobservable.
    """
    counts = collections.Counter()
    detail = []
    if previous_frame is None:
        misses = sum(1 for d in frame.draws if d.gate == 6)
        if misses:
            counts['previous_frame_not_captured'] = misses
        return counts, detail
    previous_keys = collections.Counter(d.key for d in previous_frame.draws
                                        if d.gate in RECORDING_GATES and d.key)
    previous_epochs = {(d.load_epoch, d.registry_epoch) for d in previous_frame.draws
                       if d.gate in KEYED_GATES}
    consumed = collections.Counter()
    for draw in frame.draws:
        if draw.key is None or draw.gate not in KEYED_GATES:
            continue
        if draw.gate in RECORDING_GATES:
            consumed[draw.key] += 1
        if draw.gate != 6:
            continue
        available = previous_keys.get(draw.key, 0)
        if previous_epochs and (draw.load_epoch, draw.registry_epoch) not in previous_epochs:
            reason = 'load_or_registry_epoch_changed'
        elif available == 0:
            reason = 'key_absent_in_previous_frame'
        elif available > 1:
            reason = 'duplicate_key_poisoned_previous_entry'
        elif consumed[draw.key] > 1:
            reason = 'previous_entry_consumed_by_earlier_duplicate'
        else:
            reason = 'unexplained_by_log'
        counts[reason] += 1
        if len(detail) < 64:
            detail.append(dict(frame=frame.frame, index=draw.index, reason=reason,
                               vs=draw.vs, ps=draw.ps))
    return counts, detail


def shader_models(captures_dir, hashes):
    """Resolve each program hash to its shader model from its dumped bytecode.

    The proxy writes ``{vs,ps}_<fnv1a64>.bin`` for every program it inspects.
    The first DWORD is the D3D9 version token: ``0xfffe`` marks a vertex shader,
    ``0xffff`` a pixel shader, and the low two bytes are major/minor. Only the
    derived model string leaves this function; no shader bytes are kept.
    """
    models = {}
    if not captures_dir or not os.path.isdir(captures_dir):
        return models
    for name in sorted(os.listdir(captures_dir)):
        if not name.endswith('.bin') or '_' not in name:
            continue
        stage, _, rest = name.partition('_')
        if stage not in ('vs', 'ps'):
            continue
        digest = rest[:-4]
        if hashes and digest not in hashes:
            continue
        try:
            with open(os.path.join(captures_dir, name), 'rb') as handle:
                head = handle.read(4)
            if len(head) != 4:
                continue
            word = struct.unpack('<I', head)[0]
        except OSError:
            continue
        kind = {0xfffe: 'vs', 0xffff: 'ps'}.get(word >> 16)
        if kind is None:
            continue
        models[digest] = dict(model=f'{kind}_{(word >> 8) & 255}_{word & 255}',
                              stage=kind, source=name)
    return models


def archive_facts(inventory_path, aliases_path, hashes):
    """Model and catalogue paths for the programs, from the untracked sweep.

    Both inputs are optional: a session whose programs are all archive programs
    gains effect paths, and a program the sweep never saw is simply reported
    without one.
    """
    models, paths = {}, collections.defaultdict(list)
    if inventory_path and os.path.exists(inventory_path):
        with open(inventory_path, encoding='utf-8') as handle:
            inventory = json.load(handle)
        for program in inventory.get('programs', []):
            digest = program.get('fnv1a64')
            if digest in hashes:
                models[digest] = f"{program.get('stage')}_{program.get('model')}"
    if aliases_path and os.path.exists(aliases_path):
        with open(aliases_path, encoding='utf-8') as handle:
            aliases = json.load(handle)
        for effect in aliases.get('effects', []):
            for program_id in effect.get('program_occurrences', {}):
                digest = program_id.partition('_')[2]
                if digest in hashes:
                    paths[digest].append(effect.get('path'))
    return models, {k: sorted(set(v)) for k, v in paths.items()}


def analyze(log_path, captures_dir, inventory_path, aliases_path):
    frames = []                       # captured frames, in log order
    frame_lines = []                  # every motion_output_frame record
    variants = []
    session = dict(header=[], device_gate=None, target=None, observers={}, ownership=[],
                   adapter=None, presentation=[], admission=[], copy_depth=collections.Counter())
    telemetry_metrics = {}
    telemetry_windows = []            # (frame, {name: (count,total_us)}) for device 1
    diagnostics = dict(nonzero_results=collections.Counter(), failure_lines=[],
                       resets=0, releases=0, device_destroy=0, rejected_records=[])
    draw_pairs = collections.Counter()          # (phase, vs, ps) -> draws
    used_hashes = set()
    shader_lines = {}

    current = None                    # FrameRecord being filled
    draw = None                       # DrawRecord being filled
    phase = 'background'
    window = None

    with open(log_path, 'r', errors='replace') as handle:
        for line in handle:
            event = line.partition(' ')[0]

            # ---------- per-draw records of a captured frame (the bulk) ----------
            if event == 'draw':
                f = fields(line)
                if current is None:
                    continue
                draw = DrawRecord(to_int(f.get('index')), f.get('vs'), f.get('ps'), phase,
                                  to_int(f.get('primitives'), 0))
                current.draws.append(draw)
                draw_pairs[(phase, f.get('vs'), f.get('ps'))] += 1
                used_hashes.add(f.get('vs'))
                used_hashes.add(f.get('ps'))
                continue
            if event == 'state':
                if draw is not None:
                    f = fields(line)
                    draw.states[f.get('id')] = to_int(f.get('value'))
                continue
            if event == 'constant':
                if draw is not None and line.startswith('constant kind=vs type=i reg=0 '):
                    f = fields(line)
                    draw.integer0 = [to_int(v, 0) for v in f.get('values', '').split(',')]
                continue
            if event == 'motion_route':
                f = fields(line)
                if draw is not None and to_int(f.get('index')) == draw.index:
                    draw.gate = to_int(f.get('gate'))
                    draw.routed = to_int(f.get('routed'))
                    draw.matched = to_int(f.get('matched'))
                    draw.key = tuple(f.get(k) for k in KEY_FIELDS)
                    draw.rows_hash = f.get('rows_hash')
                    draw.load_epoch = f.get('load_epoch')
                    draw.registry_epoch = f.get('registry_epoch')
                    draw.node_serial = f.get('node_serial')
                    draw.camera_serial = f.get('camera_serial')
                    if not hex_int(f.get('result'), 0) == 0:
                        diagnostics['nonzero_results'][('motion_route', f.get('result'))] += 1
                continue
            if event == 'motion_lifetime':
                if draw is not None:
                    f = fields(line)
                    draw.lifetime = (f.get('before_known'), f.get('after_known'),
                                     f.get('before_reason'), f.get('after_reason'))
                continue
            if event == 'object_context':
                if draw is not None:
                    f = fields(line)
                    draw.scoped, draw.valid = f.get('scoped'), f.get('valid')
                continue
            if event in ('sampler', 'object_matrix', 'stream', 'transform', 'texture',
                         'constants', 'vertex_element', 'texture_desc', 'object_basis',
                         'surface', 'buffer_content', 'geometry', 'indices', 'viewport',
                         'draw_args', 'motion_input', 'motion_geometry', 'index_buffer',
                         'object_position', 'vertex_buffer', 'resource'):
                continue

            # ---------- frame and phase structure ----------
            if event == 'frame_begin':
                f = fields(line)
                current = FrameRecord(to_int(f.get('device')), to_int(f.get('frame')))
                frames.append(current)
                phase, draw = 'background', None
                continue
            if event == 'frame_end':
                f = fields(line)
                if current is not None and to_int(f.get('frame')) == current.frame:
                    current.reported_draws = to_int(f.get('draws'))
                    current.present = f.get('present')
                    if not hex_int(f.get('present'), 0) == 0:
                        diagnostics['nonzero_results'][('frame_end.present', f.get('present'))] += 1
                current, draw = None, None
                phase = 'background'
                continue
            if event == 'clear':
                # Phase advance: the scene's depth-only Clear ends the background.
                if current is not None and phase == 'background':
                    f = fields(line)
                    if to_int(f.get('flags')) == 2:
                        phase = 'scene'
                        current.phase_end['background'] = draw.index if draw else 0
                continue
            if event == 'set_depth':
                if current is not None:
                    f = fields(line)
                    unbound = hex_int(f.get('ptr'), 0) == 0
                    if unbound and phase == 'scene':
                        phase = 'bloom'
                        current.phase_end['scene'] = draw.index if draw else 0
                    elif not unbound and phase == 'bloom':
                        phase = 'overlay_gui'
                        current.phase_end['bloom'] = draw.index if draw else 0
                continue

            # ---------- route diagnostics ----------
            if event == 'motion_output_frame':
                f = fields(line)
                record = {k: to_int(v, v) for k, v in f.items()}
                frame_lines.append(record)
                if current is not None and record.get('frame') == current.frame:
                    current.counters = record
                for name in ('fill_result', 'fill_restore', 'present'):
                    if f.get(name) and hex_int(f[name], 0) not in (0, 1):
                        diagnostics['nonzero_results'][('motion_output_frame.' + name, f[name])] += 1
                continue
            if event == 'motion_output_variant':
                variants.append(fields(line))
                continue
            if event == 'motion_output_device':
                session['device_gate'] = fields(line)
                continue
            if event == 'motion_output_target':
                session['target'] = fields(line)
                continue
            if event == 'motion_output_reset':
                diagnostics['resets'] += 1
                diagnostics['failure_lines'].append(line.strip()[:300])
                continue
            if event == 'motion_output_release':
                diagnostics['releases'] += 1
                continue
            if event in ('motion_output_fill_failed', 'motion_output_apply_failed',
                         'motion_output_restore_failed'):
                diagnostics['failure_lines'].append(line.strip()[:300])
                continue
            if event in ('motion_output_mode', 'motion_capture_mode', 'ownership_mode',
                         'x3-modern-renderer', 'backend', 'create_device', 'create_device_result',
                         'capture_caps', 'device_hooked', 'mesh_cache', 'mesh_trace',
                         'loading_trace', 'object_trace', 'object_lifetime'):
                session['header'].append(line.strip()[:400])
                if event in ('object_trace', 'object_lifetime'):
                    session['observers'][event] = fields(line)
                continue
            if event == 'ownership_factory':
                session['ownership'].append(fields(line))
                continue
            if event == 'ownership_copy_depth':
                f = fields(line)
                session['copy_depth'][(f.get('phase'), f.get('status'), f.get('available'))] += 1
                continue
            if event == 'adapter':
                session['adapter'] = fields(line)
                continue
            if event in ('telemetry_presentation', 'telemetry_window', 'telemetry_present_window',
                         'telemetry_first_present', 'telemetry_start'):
                if event in ('telemetry_presentation', 'telemetry_start', 'telemetry_first_present'):
                    session['presentation'].append(line.strip()[:300])
                continue
            if event in ('application_admission_mode', 'application_admission_final'):
                session['admission'].append(fields(line))
                continue
            if event == 'shader':
                f = fields(line)
                shader_lines[f.get('id')] = dict(stage=f.get('kind'), bytes=to_int(f.get('bytes')),
                                                 dumped=f.get('dumped'))
                continue
            if event == 'device_destroy':
                diagnostics['device_destroy'] += 1
                continue

            # ---------- telemetry ----------
            if event == 'telemetry_summary':
                f = fields(line)
                window = dict(device=f.get('device'), frame=to_int(f.get('frame')), metrics={})
                telemetry_windows.append(window)
                continue
            if event == 'telemetry_metric':
                f = fields(line)
                key = f"{f.get('device')}:{f.get('name')}"
                aggregate = telemetry_metrics.setdefault(key, dict(
                    device=f.get('device'), name=f.get('name'), count=0, failures=0,
                    total_us=0.0, min_us=None, max_us=0.0, windows=0, buckets=[0] * 6))
                count = to_int(f.get('count'), 0)
                total = float(f.get('total_us', 0) or 0)
                minimum = float(f.get('min_us', 0) or 0)
                maximum = float(f.get('max_us', 0) or 0)
                aggregate['count'] += count
                aggregate['failures'] += to_int(f.get('failures'), 0)
                aggregate['total_us'] += total
                aggregate['max_us'] = max(aggregate['max_us'], maximum)
                aggregate['min_us'] = minimum if aggregate['min_us'] is None else min(aggregate['min_us'], minimum)
                aggregate['windows'] += 1
                buckets = [to_int(b, 0) for b in f.get('buckets', '').split(',') if b != '']
                for i, value in enumerate(buckets[:6]):
                    aggregate['buckets'][i] += value
                if window is not None and window['device'] == f.get('device'):
                    window['metrics'][f.get('name')] = (count, total)
                continue

    # ---------------- aggregation ----------------
    captured = [f for f in frames if f.counters is not None]
    bursts = []
    for frame in frames:
        if bursts and frame.frame == bursts[-1][-1].frame + 1 and frame.device == bursts[-1][-1].device:
            bursts[-1].append(frame)
        else:
            bursts.append([frame])

    # Fills FrameRecord.selector_trace, which the burst rows report.
    selection = summarize_selection(frames)

    summary = dict(
        tool='tools/analysis/analyze_iteration06_motion.py',
        log=str(log_path),
        log_sha256=file_sha256(log_path),
        log_bytes=os.path.getsize(log_path),
        session=summarize_session(session, variants, shader_lines),
        frame_counters=summarize_frame_lines(frame_lines),
        scene_selection=selection,
        bursts=summarize_bursts(bursts),
        routes=summarize_routes(frames),
        identity_timeline=summarize_identity(bursts),
        shader_models=summarize_shader_models(draw_pairs, used_hashes, shader_lines,
                                              captures_dir, inventory_path, aliases_path),
        telemetry=summarize_telemetry(telemetry_metrics, telemetry_windows, frame_lines),
        diagnostics=summarize_diagnostics(diagnostics, session, captured),
    )
    return summary


def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(1 << 22), b''):
            digest.update(block)
    return digest.hexdigest()


def summarize_session(session, variants, shader_lines):
    kinds = collections.Counter(v.get('kind') for v in variants)
    originals = {(v.get('kind'), v.get('original')) for v in variants}
    bad = [v for v in variants if hex_int(v.get('create'), 0) != 0 or v.get('transform') != '0']
    return dict(
        header=sorted(session['header']),
        device_gate=session['device_gate'],
        target=session['target'],
        observers=session['observers'],
        ownership_factory=[dict(m) for m in session['ownership']],
        adapter=session['adapter'],
        presentation=sorted(session['presentation']),
        admission=[dict(a) for a in session['admission']],
        copy_depth_phases=counter_to_sorted(session['copy_depth']),
        variants=dict(lines=len(variants), by_stage=counter_to_sorted(kinds),
                      distinct_programs=len(originals),
                      failed_or_untransformed=[dict(v) for v in bad]),
        shaders_created=dict(count=len(shader_lines),
                             by_stage=counter_to_sorted(
                                 collections.Counter(s['stage'] for s in shader_lines.values()))),
    )


def summarize_frame_lines(frame_lines):
    if not frame_lines:
        return dict(count=0)
    gate_keys = ('gate1', 'gate2', 'gate3', 'gate4', 'gate5', 'gate6')
    totals = {k: sum(r.get(k, 0) for r in frame_lines) for k in gate_keys}
    totals['matched'] = sum(r.get('matched', 0) for r in frame_lines)
    totals['routed'] = sum(r.get('routed', 0) for r in frame_lines)
    totals['draws'] = sum(r.get('draws', 0) for r in frame_lines)
    captured = [r for r in frame_lines if r.get('history_previous') is not None]
    return dict(
        count=len(frame_lines),
        telemetry_sampled=sum(1 for r in frame_lines if r.get('frame', 0) % 60 == 0),
        totals=totals,
        # gate1+gate2 also count non-scene draws that never write a motion_route line.
        draws=distribution([r.get('draws', 0) for r in frame_lines]),
        routed=distribution([r.get('routed', 0) for r in frame_lines]),
        matched=distribution([r.get('matched', 0) for r in frame_lines]),
        frames_with_routed=sum(1 for r in frame_lines if r.get('routed', 0)),
        frames_with_matched=sum(1 for r in frame_lines if r.get('matched', 0)),
        apply_failures=sum(r.get('apply_failures', 0) for r in frame_lines),
        restore_failures=sum(r.get('restore_failures', 0) for r in frame_lines),
        fill_results=counter_to_sorted(collections.Counter(str(r.get('fill_result')) for r in frame_lines)),
        fill_restores=counter_to_sorted(collections.Counter(str(r.get('fill_restore')) for r in frame_lines)),
        latched=counter_to_sorted(collections.Counter(r.get('latched') for r in frame_lines)),
        filled=counter_to_sorted(collections.Counter(r.get('filled') for r in frame_lines)),
        committed=counter_to_sorted(collections.Counter(r.get('committed') for r in frame_lines)),
        selector_state=counter_to_sorted(collections.Counter(
            SELECTOR_STATES.get(r.get('selector_state'), str(r.get('selector_state')))
            for r in frame_lines)),
        history_previous=distribution([r.get('history_previous', 0) for r in captured]),
    )


def summarize_bursts(bursts):
    out = []
    for index, burst in enumerate(bursts):
        rows = []
        for frame in burst:
            c = frame.counters or {}
            phases = collections.Counter(d.phase for d in frame.draws)
            rows.append(dict(
                frame=frame.frame, draws=c.get('draws'), routed=c.get('routed'),
                matched=c.get('matched'),
                gates={k: c.get(k) for k in ('gate1', 'gate2', 'gate3', 'gate4', 'gate5', 'gate6')},
                apply_failures=c.get('apply_failures'), restore_failures=c.get('restore_failures'),
                filled=c.get('filled'), latched=c.get('latched'), committed=c.get('committed'),
                selector_state=SELECTOR_STATES.get(c.get('selector_state')),
                captured_draws=len(frame.draws),
                phase_draws={p: phases.get(p, 0) for p in PHASES},
            ))
        epochs = sorted({(d.load_epoch, d.registry_epoch) for f in burst for d in f.draws
                         if d.gate in KEYED_GATES})
        cameras = sorted({d.camera_serial for f in burst for d in f.draws if d.gate in KEYED_GATES})
        routed = sum(r['routed'] or 0 for r in rows)
        matched = sum(r['matched'] or 0 for r in rows)
        out.append(dict(
            burst=index, frames=[f.frame for f in burst],
            routed=routed, matched=matched,
            matched_rate=(matched / routed) if routed else None,
            load_registry_epochs=[list(e) for e in epochs],
            camera_serials=cameras[:8], camera_serial_count=len(cameras),
            selector=burst[0].selector_trace,
            per_frame=rows,
        ))
    return out


def summarize_routes(frames):
    gates = collections.Counter()
    pairs_by_gate = collections.defaultdict(collections.Counter)
    gate3_frames = collections.defaultdict(set)
    gate4_reasons = collections.Counter()
    gate4_first = collections.Counter()
    gate4_i0 = collections.Counter()
    gate3_opaque = collections.Counter()
    matched_i0 = collections.Counter()
    gate5_reasons = collections.Counter()
    gate6_counts = collections.Counter()
    gate6_detail = []
    keys = collections.Counter()
    in_frame_duplicates = 0
    distinct = {k: set() for k in ('node', 'camera', 'model', 'vb', 'ib', 'declaration')}
    total = 0
    previous = None
    for frame in frames:
        frame_keys = collections.Counter()
        for draw in frame.draws:
            if draw.gate is None:
                continue
            total += 1
            gates[draw.gate] += 1
            pairs_by_gate[draw.gate][(draw.vs, draw.ps)] += 1
            if draw.gate == 3:
                gate3_frames[(draw.vs, draw.ps)].add(frame.frame)
                if passes_gate4_states(draw):
                    gate3_opaque[(draw.vs, draw.ps)] += 1
            if draw.gate == 4:
                reasons, first = gate4_failures(draw)
                for reason in reasons:
                    gate4_reasons[reason] += 1
                gate4_first[first] += 1
                if draw.integer0 is not None:
                    gate4_i0[draw.integer0[0]] += 1
            if draw.gate == 5:
                gate5_reasons[draw.lifetime or ('unknown',)] += 1
            if draw.gate == 0 and draw.integer0 is not None:
                matched_i0[draw.integer0[0]] += 1
            if draw.key and draw.gate in KEYED_GATES:
                keys[draw.key] += 1
                frame_keys[draw.key] += 1
                for i, name in enumerate(KEY_FIELDS):
                    if name in distinct:
                        distinct[name].add(draw.key[i])
        in_frame_duplicates += sum(c - 1 for c in frame_keys.values() if c > 1)
        adjacent = previous if (previous is not None and previous.device == frame.device and
                                previous.frame == frame.frame - 1) else None
        counts, detail = history_miss_reasons(frame, adjacent)
        gate6_counts.update(counts)
        gate6_detail.extend(detail[:8])
        previous = frame
    routed = sum(v for k, v in gates.items() if k in (0, 5, 6))
    matched = gates.get(0, 0)
    unclassified = []
    for pair, count in pairs_by_gate[3].items():
        seen = sorted(gate3_frames[pair])
        unclassified.append(dict(vs=pair[0], ps=pair[1], draws=count,
                                 draws_passing_gate4_states=gate3_opaque.get(pair, 0),
                                 frames=len(seen), first_frame=seen[0], last_frame=seen[-1]))
    unclassified.sort(key=lambda row: (-row['draws'], row['vs'], row['ps']))
    return dict(
        motion_route_lines=total,
        gate_outcomes=[[GATE_NAMES.get(g, str(g)), c] for g, c in sorted(gates.items())],
        routed=routed, matched=matched,
        matched_rate_of_routed=(matched / routed) if routed else None,
        matched_rate_of_scene_draws=(matched / total) if total else None,
        distinct_keys=len(keys),
        duplicate_keys_within_a_frame=in_frame_duplicates,
        keys_seen_in_more_than_one_frame=sum(1 for c in keys.values() if c > 1),
        distinct_key_values={k: len(v) for k, v in sorted(distinct.items())},
        gate3_unclassified_pairs=unclassified,
        gate4=dict(draws=gates.get(4, 0),
                   first_failing_predicate=counter_to_sorted(gate4_first),
                   predicate_counts=counter_to_sorted(gate4_reasons),
                   i0_x_values=counter_to_sorted(gate4_i0),
                   i0_x_values_of_matched_draws=counter_to_sorted(matched_i0),
                   note='the DLL short-circuits at the first false predicate; the counts name '
                        'every predicate the capture shows false for that draw'),
        gate5=dict(draws=gates.get(5, 0),
                   lifetime_states=[[list(k), v] for k, v in
                                    sorted(gate5_reasons.items(), key=lambda kv: (-kv[1], str(kv[0])))],
                   note='tuple is (before_known, after_known, before_reason, after_reason) '
                        'of the matching motion_lifetime record'),
        gate6=dict(draws=gates.get(6, 0), reasons=counter_to_sorted(gate6_counts),
                   examples=gate6_detail[:32]),
    )


def summarize_selection(frames):
    rejected, accepted = [], 0
    for frame in frames:
        trace = selector_explanation(frame)
        frame.selector_trace = trace
        routed = (frame.counters or {}).get('routed', 0)
        if routed:
            accepted += 1
        else:
            rejected.append(dict(frame=frame.frame, routed=routed, **trace))
    causes = collections.Counter((r['state'], r.get('vs'), r.get('ps')) for r in rejected)
    return dict(
        captured_frames=len(frames),
        frames_with_routed_draws=accepted,
        frames_without_routed_draws=len(rejected),
        rejected_frames=sorted(rejected, key=lambda r: r['frame']),
        causes=[[list(k), v] for k, v in sorted(causes.items(), key=lambda kv: (-kv[1], str(kv[0])))],
        note='the route\'s SceneBoundarySelector fails closed: one unrecognized draw in the '
             'Background or Scene phase rejects the rest of the frame, so every later draw is '
             'counted at gate 2.',
    )


def summarize_identity(bursts):
    timeline = []
    for index, burst in enumerate(bursts):
        epochs = sorted({(d.load_epoch, d.registry_epoch) for f in burst for d in f.draws
                         if d.gate in KEYED_GATES})
        cameras = sorted({d.camera_serial for f in burst for d in f.draws if d.gate in KEYED_GATES},
                         key=lambda v: to_int(v, 0))
        nodes = {d.node_serial for f in burst for d in f.draws if d.gate in KEYED_GATES}
        routed = sum((f.counters or {}).get('routed', 0) for f in burst)
        matched = sum((f.counters or {}).get('matched', 0) for f in burst)
        timeline.append(dict(
            burst=index, first_frame=burst[0].frame, last_frame=burst[-1].frame,
            load_epochs=sorted({e[0] for e in epochs if e[0] is not None}),
            registry_epochs=sorted({e[1] for e in epochs if e[1] is not None}),
            camera_serial_min=cameras[0] if cameras else None,
            camera_serial_max=cameras[-1] if cameras else None,
            distinct_camera_serials=len(cameras),
            distinct_node_serials=len(nodes),
            routed=routed, matched=matched,
            matched_rate=(matched / routed) if routed else None,
        ))
    # A burst that routed nothing observes no identity at all, so a change is
    # always measured against the last burst that did observe one.
    changes = []
    last = None
    for entry in timeline:
        if not entry['load_epochs'] and entry['camera_serial_max'] is None:
            continue
        if last is not None:
            change = {}
            if last['load_epochs'] != entry['load_epochs']:
                change['load_epoch'] = [last['load_epochs'], entry['load_epochs']]
            if last['registry_epochs'] != entry['registry_epochs']:
                change['registry_epoch'] = [last['registry_epochs'], entry['registry_epochs']]
            if to_int(entry['camera_serial_min'], 0) != to_int(last['camera_serial_max'], 0):
                change['camera_serial'] = [last['camera_serial_max'], entry['camera_serial_min']]
            if change:
                change.update(from_burst=last['burst'], to_burst=entry['burst'],
                              bursts_without_identity_between=entry['burst'] - last['burst'] - 1,
                              last_frame_before=last['last_frame'],
                              first_frame_after=entry['first_frame'],
                              matched_rate_before=last['matched_rate'],
                              matched_rate_after=entry['matched_rate'])
                changes.append(change)
        last = entry
    return dict(per_burst=timeline, changes_between_bursts=changes,
                note='per-draw identity is logged only in captured frames, so a change is '
                     'localized to the gap between two bursts, never to a single frame.')


def summarize_shader_models(draw_pairs, used_hashes, shader_lines,
                            captures_dir, inventory_path, aliases_path):
    used_hashes = {h for h in used_hashes if h}
    dumped = shader_models(captures_dir, used_hashes | set(shader_lines))
    archive_models, effect_paths = archive_facts(inventory_path, aliases_path,
                                                 used_hashes | set(shader_lines))

    def model_of(digest, stage):
        if digest in (None, '', '0' * 16):
            return f'{stage}_null'
        entry = dumped.get(digest)
        if entry:
            return entry['model']
        return archive_models.get(digest, f'{stage}_unknown')

    per_phase = collections.defaultdict(collections.Counter)
    per_phase_programs = collections.defaultdict(collections.Counter)
    for (phase, vs, ps), count in draw_pairs.items():
        per_phase[phase][(model_of(vs, 'vs'), model_of(ps, 'ps'))] += count
        per_phase_programs[phase][(vs, ps)] += count

    # Which programs below shader model 3 actually draw, and where.
    legacy = []
    for phase, counter in per_phase_programs.items():
        for (vs, ps), count in counter.items():
            vs_model, ps_model = model_of(vs, 'vs'), model_of(ps, 'ps')
            if vs_model.endswith('_3_0') and ps_model.endswith('_3_0'):
                continue
            legacy.append(dict(phase=phase, vs=vs, ps=ps, vs_model=vs_model, ps_model=ps_model,
                               draws=count,
                               vs_effects=effect_paths.get(vs, [])[:6],
                               ps_effects=effect_paths.get(ps, [])[:6]))
    legacy.sort(key=lambda row: (row['phase'], -row['draws'], row['vs'], row['ps']))

    programs = []
    for digest in sorted(used_hashes | set(shader_lines)):
        if digest in (None, '', '0' * 16):
            continue
        stage = (shader_lines.get(digest) or {}).get('stage') or (dumped.get(digest) or {}).get('stage') or '?'
        programs.append(dict(hash=digest, stage=stage, model=model_of(digest, stage),
                             dumped=digest in dumped,
                             in_archive_inventory=digest in archive_models,
                             effects=effect_paths.get(digest, [])[:6]))
    return dict(
        programs_created=len(programs),
        model_counts=counter_to_sorted(collections.Counter(p['model'] for p in programs)),
        draws_by_phase={p: sum(per_phase[p].values()) for p in PHASES},
        pairs_by_phase={phase: [[list(pair), count] for pair, count in
                                sorted(per_phase[phase].items(), key=lambda kv: (-kv[1], str(kv[0])))]
                        for phase in PHASES},
        draw_counts_by_phase_and_program={
            phase: [[list(pair), count] for pair, count in
                    sorted(per_phase_programs[phase].items(), key=lambda kv: (-kv[1], str(kv[0])))][:40]
            for phase in PHASES},
        below_sm3_pairs=legacy,
        programs=programs,
        note='model comes from the first DWORD of the program\'s own {vs,ps}_<hash>.bin dump; '
             'the archive inventory only supplies effect paths and a cross-check. Phases are '
             'derived from the capture event stream: background ends at the scene\'s depth-only '
             'Clear, the scene ends where depth is unbound for the bloom chain, and the bloom '
             'chain ends where depth is rebound for the overlays.',
    )


def summarize_telemetry(metrics, windows, frame_lines):
    for aggregate in metrics.values():
        aggregate['mean_us'] = aggregate['total_us'] / aggregate['count'] if aggregate['count'] else None

    # Attribute each 1-second telemetry window to the nearest preceding
    # motion_output_frame record, which says whether the route was routing then.
    marks = sorted((r['frame'], r.get('routed', 0)) for r in frame_lines if isinstance(r.get('frame'), int))
    frames_sorted = [m[0] for m in marks]

    def routed_at(frame):
        if not marks or frame is None:
            return None
        position = bisect.bisect_right(frames_sorted, frame) - 1
        return marks[max(0, position)][1]

    split = {'routed': collections.Counter(), 'unrouted': collections.Counter()}
    totals = {'routed': collections.Counter(), 'unrouted': collections.Counter()}
    for window in windows:
        if window['device'] != '1':
            continue
        routed = routed_at(window['frame'])
        if routed is None:
            continue
        bucket = 'routed' if routed else 'unrouted'
        for name, (count, total) in window['metrics'].items():
            split[bucket][name] += count
            totals[bucket][name] += total
    routed_split = {}
    for bucket in ('routed', 'unrouted'):
        routed_split[bucket] = {
            name: dict(count=split[bucket][name], total_us=totals[bucket][name],
                       mean_us=totals[bucket][name] / split[bucket][name] if split[bucket][name] else None)
            for name in sorted(split[bucket])
        }
    return dict(
        metrics={k: metrics[k] for k in sorted(metrics)},
        frame_interval=dict(
            normal=metrics.get('1:frame_normal'),
            capture=metrics.get('1:frame_capture'),
        ),
        present_duration=dict(
            normal=metrics.get('1:present_normal'),
            capture=metrics.get('1:present_capture'),
        ),
        by_route_activity=routed_split,
        limits=TELEMETRY_LIMITS,
    )


def summarize_diagnostics(diagnostics, session, captured):
    observers = session['observers']
    missing = []
    if not diagnostics['device_destroy']:
        missing.append('device_destroy')
    if not diagnostics['releases']:
        missing.append('motion_output_release')
    return dict(
        nonzero_results=[[list(k), v] for k, v in
                         sorted(diagnostics['nonzero_results'].items(), key=lambda kv: (-kv[1], str(kv[0])))],
        motion_output_failure_lines=diagnostics['failure_lines'][:32],
        motion_output_resets=diagnostics['resets'],
        motion_output_releases=diagnostics['releases'],
        device_destroy_records=diagnostics['device_destroy'],
        missing_shutdown_records=missing,
        object_trace=observers.get('object_trace'),
        object_lifetime=observers.get('object_lifetime'),
        object_lifetime_baseline_complete=(observers.get('object_lifetime') or {}).get('baseline_complete'),
        captured_frames=len(captured),
    )


def render_text(summary):
    out = []
    add = out.append
    add(f"iteration-06 motion route summary")
    add(f"log {summary['log']} ({summary['log_bytes']} bytes, sha256 {summary['log_sha256']})")
    fc = summary['frame_counters']
    add(f"motion_output_frame lines: {fc['count']}; routed frames {fc['frames_with_routed']}, "
        f"matched frames {fc['frames_with_matched']}")
    add(f"  draws {fc['draws']['total']}, routed {fc['totals']['routed']}, matched {fc['totals']['matched']}")
    add(f"  gates {fc['totals']}")
    add(f"  apply_failures {fc['apply_failures']} restore_failures {fc['restore_failures']}")
    add(f"  selector_state {fc['selector_state']}  committed {fc['committed']}  latched {fc['latched']}")
    routes = summary['routes']
    add(f"motion_route lines: {routes['motion_route_lines']}; routed {routes['routed']}, "
        f"matched {routes['matched']} ({routes['matched_rate_of_routed']})")
    add(f"  gate outcomes {routes['gate_outcomes']}")
    add(f"  unclassified gate-3 pairs: {len(routes['gate3_unclassified_pairs'])}")
    for row in routes['gate3_unclassified_pairs'][:12]:
        add(f"    {row['vs']} / {row['ps']}  draws={row['draws']} "
            f"(opaque {row['draws_passing_gate4_states']}) frames={row['frames']} "
            f"[{row['first_frame']}..{row['last_frame']}]")
    add(f"  gate4 first failing predicate {routes['gate4']['first_failing_predicate']}")
    add(f"  gate4 all false predicates {routes['gate4']['predicate_counts']}")
    add(f"  gate4 i0.x {routes['gate4']['i0_x_values']}")
    add(f"  gate5 {routes['gate5']['draws']} {routes['gate5']['lifetime_states'][:4]}")
    add(f"  gate6 {routes['gate6']['draws']} {routes['gate6']['reasons']}")
    sel = summary['scene_selection']
    add(f"captured frames {sel['captured_frames']}: routed {sel['frames_with_routed_draws']}, "
        f"nothing routed {sel['frames_without_routed_draws']}")
    for cause in sel['causes']:
        add(f"    cause {cause}")
    sm = summary['shader_models']
    add(f"shader models: {sm['model_counts']}")
    add(f"  draws by phase {sm['draws_by_phase']}")
    for row in sm['below_sm3_pairs'][:20]:
        add(f"    {row['phase']}: {row['vs_model']}/{row['ps_model']} draws={row['draws']} "
            f"{row['vs']}/{row['ps']}")
    tel = summary['telemetry']
    for name, value in sorted(tel['frame_interval'].items()):
        if value:
            add(f"frame interval {name}: n={value['count']} mean={value['mean_us']:.1f}us "
                f"min={value['min_us']} max={value['max_us']}")
    for name, value in sorted(tel['present_duration'].items()):
        if value:
            add(f"present {name}: n={value['count']} mean={value['mean_us']:.1f}us max={value['max_us']}")
    diag = summary['diagnostics']
    add(f"diagnostics: nonzero results {diag['nonzero_results']}, resets {diag['motion_output_resets']}, "
        f"missing shutdown records {diag['missing_shutdown_records']}")
    return '\n'.join(out) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', help='capture session log (streamed once)')
    parser.add_argument('--captures', help='directory holding the {vs,ps}_<hash>.bin dumps '
                                           '(default: the log directory)')
    parser.add_argument('--inventory', default='verification/results/shader-sweep-inventory.json',
                        help='archive shader inventory, for a model cross-check (optional)')
    parser.add_argument('--aliases', default='verification/results/shader-sweep-aliases.json',
                        help='archive effect alias table, for catalogue paths (optional)')
    parser.add_argument('--output', required=True, help='summary JSON to write')
    parser.add_argument('--text', help='optional text report path')
    args = parser.parse_args(argv)

    captures = args.captures or os.path.dirname(os.path.abspath(args.log))
    summary = analyze(args.log, captures, args.inventory, args.aliases)
    os.makedirs(os.path.dirname(os.path.abspath(args.output)) or '.', exist_ok=True)
    with open(args.output, 'w', encoding='utf-8') as handle:
        json.dump(summary, handle, indent=2, sort_keys=False, default=str)
        handle.write('\n')
    text = render_text(summary)
    if args.text:
        with open(args.text, 'w', encoding='utf-8') as handle:
            handle.write(text)
    sys.stdout.write(text)
    print(f'summary: {args.output}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
