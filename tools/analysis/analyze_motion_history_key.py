#!/usr/bin/env python3
"""Empirical validation of the proposed cross-frame motion-history draw key.

Reads one fixed schema-2 capture snapshot once, keeps only bounded per-draw
metadata, and reports how well candidate keys pair Scene-phase draws between
adjacent captured frames.

Scope and limits. A unique key match is a *candidate* correspondence between two
recorded draw submissions. It is not proof of engine object identity, of stable
vertex payloads, of a correct motion vector, or of temporal-history eligibility.
Buffer contents are not captured; an allocation identity may be rewritten. Rows
are the constants the application actually submitted, never a recomposed W*V*P.
Missing, unscoped or failed records never become zero-valued stable facts.

Scene phase (same boundary evidence the existing iteration-05 analyzers use):
the frame must retain a confirmed scene-depth boundary (``scene_depth_copy
valid=1`` plus ``scene_depth_boundary confirmed=1``); the phase then starts after
the first full depth-only Clear of the latched main color/depth pair that follows
at least one background draw, and ends at the first subsequent non-``draw_begin``
device event (the StretchRect/bloom/ColorFill/SetDepthStencil group).
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import re
import struct

FIELDS = re.compile(r'(\w+)=([^\s]+)')
DEFAULT_MATRIX_REGISTER = 24          # Reviewed SM3 material profiles use c24-27.
MAX_FRAMES = 1024
MAX_DRAWS = 100000
MAX_EVENTS = 200000
EXAMPLE_LIMIT = 20

# Bit 256 of motion_input `blockers` is SubmittedRows (see analyze_iteration05_depth_motion).
BLOCKER_SUBMITTED_ROWS = 256


def fields(line):
    return dict(FIELDS.findall(line))


def number(value, base=10):
    """Parse a bounded non-negative integer, or None. Never guesses a default."""
    if not isinstance(value, str):
        return None
    pattern = r'(?:0x)?[0-9a-fA-F]+' if base == 16 else r'[0-9]+'
    if not re.fullmatch(pattern, value):
        return None
    result = int(value, base)
    return result if result < (1 << 64) else None


def signed(value):
    if not isinstance(value, str) or not re.fullmatch(r'-?[0-9]+', value):
        return None
    result = int(value)
    return result if -(1 << 31) <= result < (1 << 31) else None


def positive(value, base=10):
    result = number(value, base)
    return result is not None and result > 0


def good_hresult(value):
    result = number(value, 16)
    return result is not None and result < 0x80000000


def real_equal(value, expected):
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return False
    return math.isfinite(parsed) and parsed == expected


def fnv1a64(payload):
    digest = 0xcbf29ce484222325
    for byte in payload:
        digest ^= byte
        digest = (digest * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return digest


def rows_from_words(words):
    """Decode 16 raw register words into 4 float rows; reject non-finite data."""
    values = [struct.unpack('<f', struct.pack('<I', word))[0] for word in words]
    if not all(math.isfinite(value) for value in values):
        return None
    return [values[0:4], values[4:8], values[8:12], values[12:16]]


def rounded(value):
    return None if value is None else float(f'{value:.6g}')


def quantiles(values):
    """Deterministic order statistics; no interpolation, no invented samples."""
    if not values:
        return None
    ordered = sorted(values)
    count = len(ordered)
    pick = lambda q: ordered[min(count - 1, max(0, math.ceil(q * count) - 1))]
    return dict(count=count, median=rounded(pick(0.5)), p95=rounded(pick(0.95)),
                max=rounded(ordered[-1]), min=rounded(ordered[0]))


def load_shader_profiles(path):
    """Optional tracked registry: per-VS matrix register and shader versions.

    The capture log itself does not record a shader model, so the model column is
    derived metadata about the same FNV-1a-64 program identities, not a log fact.
    """
    if path is None or not path.exists():
        return {}, {}, None
    raw = path.read_bytes()
    document = json.loads(raw.decode())
    vertices, pixels = {}, {}
    for entry in document.get('vertices', []):
        version = entry.get('shader_version')
        model = None
        if isinstance(version, int) and (version >> 16) in (0xFFFE, 0xFFFF):
            model = f'{(version >> 8) & 0xFF}_{version & 0xFF}'
        vertices[entry.get('fnv1a64')] = dict(
            matrix_register=entry.get('matrix_register'), shader_model=model,
            named_world_view_projection=bool(entry.get('named_world_view_projection')),
            position_path=entry.get('position_path'))
    for entry in document.get('pixels', []):
        pixels[entry.get('fnv1a64')] = dict(
            shader_model=(entry.get('coverage') or {}).get('shader_model'))
    return vertices, pixels, hashlib.sha256(raw).hexdigest()


class Draw:
    """Bounded per-draw metadata retained from one captured draw block."""
    __slots__ = ('index', 'header', 'context', 'motion', 'life', 'result', 'stream0',
                 'indices', 'args', 'constants_status', 'words', 'duplicate')

    def __init__(self, index, header):
        self.index = index
        self.header = header
        self.context = self.motion = self.life = self.result = None
        self.stream0 = self.indices = self.args = self.constants_status = None
        self.words = {}
        self.duplicate = set()

    def attach(self, name, record):
        if getattr(self, name) is not None:
            self.duplicate.add(name)
        setattr(self, name, record)


def parse(lines, vertex_profiles, max_frames=MAX_FRAMES, max_draws=MAX_DRAWS,
          max_events=MAX_EVENTS):
    """Single streaming pass. Coordinate-bearing records must match their draw.

    Positional records (`stream`, `indices`, `draw_args`, `constant*`) carry no
    coordinates in this format and are attributed to the enclosing draw block
    only; a frame boundary or a coordinate-bearing record for another draw ends
    that block rather than extending it.
    """
    frames = {}
    frame_order = []
    current_frame = None
    current_draw = None
    wanted = ()
    event = None
    draws_seen = events_seen = 0

    for line in lines:
        head = line[0] if line else ''
        if head == 'c':
            if wanted and line.startswith(wanted):
                record = fields(line)
                register = number(record.get('reg'))
                bits = record.get('bits', '')
                if register is not None and re.fullmatch(r'[0-9a-fA-F]{8}(?:,[0-9a-fA-F]{8}){3}', bits):
                    current_draw.words[register] = [int(word, 16) for word in bits.split(',')]
            elif line.startswith('constants kind=vs type=f ') and current_draw is not None:
                current_draw.attach('constants_status', fields(line))
            elif line.startswith('capture_event '):
                record = fields(line)
                events_seen += 1
                if events_seen > max_events:
                    raise ValueError('capture-event bound exceeded')
                event = None
                if record.get('op') != 'draw_begin' and current_frame is not None:
                    if record.get('device') == current_frame['device'] and record.get('frame') == current_frame['frame']:
                        event = dict(fields=record, details={})
                        current_frame['events'].append(event)
                current_draw, wanted = None, ()
            elif line.startswith('clear ') and event is not None:
                event['details'].setdefault('clear', []).append(fields(line))
            elif line.startswith('clear_viewport ') and event is not None:
                event['details'].setdefault('viewport', []).append(fields(line))
            continue
        if head == 'd':
            if line.startswith('draw '):
                record = fields(line)
                key = (record.get('device'), record.get('frame'))
                frame = frames.get(key)
                current_draw, wanted, event = None, (), None
                if frame is None or frame is not current_frame:
                    continue
                index = number(record.get('index'))
                if index is None:
                    raise ValueError('invalid draw index')
                draws_seen += 1
                if draws_seen > max_draws:
                    raise ValueError('draw bound exceeded')
                if index in frame['draws']:
                    frame['duplicate_draw'] = True
                    continue
                current_draw = Draw(index, record)
                frame['draws'][index] = current_draw
                profile = vertex_profiles.get(record.get('vs')) or {}
                base = profile.get('matrix_register')
                if base is None:
                    base = DEFAULT_MATRIX_REGISTER
                wanted = tuple(f'constant kind=vs type=f reg={base + offset} bits='
                               for offset in range(4))
            elif line.startswith('draw_args ') and current_draw is not None:
                current_draw.attach('args', fields(line))
            elif line.startswith('draw_result '):
                record = fields(line)
                target = _target(frames, record)
                if target is not None:
                    target.attach('result', record)
                current_draw, wanted = (current_draw, wanted) if target is current_draw else (None, ())
            continue
        if head == 'f':
            if line.startswith('frame_begin '):
                record = fields(line)
                key = (record.get('device'), record.get('frame'))
                if key in frames:
                    raise ValueError(f'duplicate frame begin {key}')
                if len(frames) >= max_frames:
                    raise ValueError('frame bound exceeded')
                frames[key] = dict(device=key[0], frame=key[1], draws={}, events=[],
                                   end=None, depth_copy=None, depth_boundary=None,
                                   duplicate_draw=False, duplicate_end=False)
                frame_order.append(key)
                current_frame = frames[key]
                current_draw, wanted, event = None, (), None
            elif line.startswith('frame_end '):
                record = fields(line)
                frame = frames.get((record.get('device'), record.get('frame')))
                if frame is not None:
                    if frame['end'] is not None:
                        frame['duplicate_end'] = True
                    frame['end'] = record
                current_frame = current_draw = event = None
                wanted = ()
            continue
        if head == 'o' and line.startswith('object_context '):
            record = fields(line)
            target = _target(frames, record)
            if target is not None:
                target.attach('context', record)
            continue
        if head == 'm':
            if line.startswith('motion_input '):
                record = fields(line)
                target = _target(frames, record)
                if target is not None:
                    target.attach('motion', record)
            elif line.startswith('motion_lifetime '):
                record = fields(line)
                target = _target(frames, record)
                if target is not None:
                    target.attach('life', record)
            continue
        if head == 's':
            if line.startswith('stream slot=0 ') and current_draw is not None:
                current_draw.attach('stream0', fields(line))
            elif line.startswith('surface role=clear_') and event is not None:
                record = fields(line)
                event['details'].setdefault(record.get('role'), []).append(record)
            elif line.startswith('scene_depth_copy ') and current_frame is not None:
                record = fields(line)
                if record.get('device') == current_frame['device'] and record.get('frame') == current_frame['frame']:
                    current_frame['depth_copy'] = record if current_frame['depth_copy'] is None else False
            elif line.startswith('scene_depth_boundary ') and current_frame is not None:
                record = fields(line)
                if record.get('device') == current_frame['device'] and record.get('frame') == current_frame['frame']:
                    current_frame['depth_boundary'] = record if current_frame['depth_boundary'] is None else False
            continue
        if head == 'i' and line.startswith('indices ') and current_draw is not None:
            current_draw.attach('indices', fields(line))
            continue
    return frames, frame_order


def _target(frames, record):
    frame = frames.get((record.get('device'), record.get('frame')))
    if frame is None:
        return None
    index = number(record.get('index'))
    return frame['draws'].get(index)


def one(records):
    return records[0] if len(records) == 1 else None


def scene_boundary(frame):
    """Latch main color/depth from the first Clear, then bracket the Scene phase."""
    events = frame['events']
    main_rt = main_depth = None
    start = None
    for event in events:
        record = event['fields']
        if record.get('op') != 'clear' or not good_hresult(record.get('result')):
            continue
        clear = one(event['details'].get('clear', []))
        rt0 = one(event['details'].get('clear_rt0', []))
        depth = one(event['details'].get('clear_depth', []))
        if not (clear and rt0 and depth):
            continue
        flags = number(clear.get('flags'))
        if flags is None:
            continue
        if main_rt is None:
            # The latch requires the initial full color+depth Clear of the frame.
            if flags == 3 and clear.get('rect_count') == '0':
                main_rt, main_depth = rt0.get('identity'), depth.get('identity')
            continue
        after = number(record.get('after_draw'))
        if (flags == 2 and clear.get('rect_count') == '0' and real_equal(clear.get('z'), 1)
                and rt0.get('identity') == main_rt and depth.get('identity') == main_depth
                and after is not None and after >= 1):
            start = event
            break
    if start is None:
        return None
    start_seq = number(start['fields'].get('seq'))
    after_start = number(start['fields'].get('after_draw'))
    end_event = None
    for event in events:
        seq = number(event['fields'].get('seq'))
        if seq is not None and start_seq is not None and seq > start_seq:
            end_event = event
            break
    if end_event is None:
        return None
    after_end = number(end_event['fields'].get('after_draw'))
    if after_end is None or after_end < after_start:
        return None
    return dict(main_color=main_rt, main_depth=main_depth,
                start_clear_event=start_seq, start_after_draw=after_start,
                end_event=number(end_event['fields'].get('seq')),
                end_op=end_event['fields'].get('op'), end_after_draw=after_end,
                first_scene_draw=after_start + 1, last_scene_draw=after_end)


def frame_complete(frame):
    end = frame['end']
    if not end or frame['duplicate_end'] or frame['duplicate_draw']:
        return False
    if end.get('capture') != '1' or not good_hresult(end.get('present')):
        return False
    indices = sorted(frame['draws'])
    return (number(end.get('draws')) == len(indices)
            and indices == list(range(1, len(indices) + 1)))


def selected_depth_boundary(frame):
    copy, boundary = frame['depth_copy'], frame['depth_boundary']
    if not copy or not boundary or copy is False or boundary is False:
        return False
    return (copy.get('valid') == '1' and good_hresult(copy.get('result'))
            and boundary.get('confirmed') == '1'
            and copy.get('copy_epoch') == boundary.get('copy_epoch')
            and number(boundary.get('source_epoch')) == (number(copy.get('copy_epoch')) or -2) + 1)


def known_scope(draw):
    """The lifetime/scope gate reused from the iteration-05 lifetime audit."""
    context, life, result = draw.context, draw.life, draw.result
    if draw.duplicate & {'context', 'life', 'result', 'motion'}:
        return False
    if not (context and life and result and good_hresult(result.get('result'))):
        return False
    if context.get('scoped') != '1' or context.get('valid') != '127':
        return False
    if not all(positive(context.get(name), 16) for name in ('node', 'camera', 'registry')):
        return False
    if not all(positive(context.get(name)) for name in ('node_handle', 'camera_handle')):
        return False
    if life.get('registry') != context.get('registry'):
        return False
    if life.get('before_known') != '1' or life.get('after_known') != '1':
        return False
    if life.get('before_reason') != '0' or life.get('after_reason') != '0':
        return False
    pairs = [('observer_epoch', 'observer_epoch_after'), ('load_epoch', 'load_epoch_after'),
             ('registry_epoch', 'registry_epoch_after'), ('mutation_before', 'mutation_after'),
             ('node_serial', 'node_serial_after'), ('camera_serial', 'camera_serial_after')]
    if not all(life.get(a) is not None and life.get(a) == life.get(b) for a, b in pairs):
        return False
    return all(positive(life.get(name)) for name in
               ('observer_epoch', 'load_epoch', 'registry_epoch', 'node_serial', 'camera_serial'))


def geometry_facts(draw):
    """Resource/range facts needed by K1; incomplete geometry yields None."""
    header, motion, args, stream = draw.header, draw.motion, draw.args, draw.stream0
    if not (motion and args and stream):
        return None
    if not good_hresult(stream.get('result')) or stream.get('frequency') != '1':
        return None
    indexed = header.get('kind') == 'indexed'
    if indexed and not (draw.indices and good_hresult(draw.indices.get('result'))
                        and positive(draw.indices.get('identity'))):
        return None
    values = dict(topology=header.get('topology'), primitives=header.get('primitives'),
                  vb=motion.get('vb'), ib=motion.get('ib') if indexed else '0',
                  declaration=motion.get('declaration'),
                  stream_offset=stream.get('offset'), stride=stream.get('stride'),
                  start_index=args.get('start_index'), base_vertex=args.get('base_vertex'),
                  min_vertex=args.get('min_vertex'), num_vertices=args.get('num_vertices'),
                  indexed='1' if indexed else '0')
    if any(value is None for value in values.values()):
        return None
    if not (positive(values['vb']) and positive(values['topology'])):
        return None
    if signed(values['base_vertex']) is None:
        return None
    if not all(number(values[name]) is not None for name in
               ('primitives', 'stream_offset', 'stride', 'start_index', 'min_vertex', 'num_vertices')):
        return None
    if stream.get('identity') != values['vb']:
        return None            # Stream binding and motion reader must agree.
    if indexed and draw.indices.get('identity') != values['ib']:
        return None
    return values


def submitted_rows(draw, vertex_profiles):
    """Return (rows, status). Rows are accepted only when the producer's own
    FNV-1a-64 `rows_hash` over the same four registers reproduces exactly."""
    motion = draw.motion or {}
    logged = motion.get('rows_hash')
    if not logged or not re.fullmatch(r'[0-9a-fA-F]{16}', logged):
        return None, 'no_rows_hash'
    blockers = number(motion.get('blockers'), 16)
    if blockers is not None and blockers & BLOCKER_SUBMITTED_ROWS:
        return None, 'submitted_rows_blocked'
    profile = vertex_profiles.get(draw.header.get('vs')) or {}
    base = profile.get('matrix_register')
    assumed = base is None
    if assumed:
        base = DEFAULT_MATRIX_REGISTER
    status = draw.constants_status or {}
    sparse = status.get('encoding') == 'sparse_zero' and good_hresult(status.get('result'))
    words = []
    for offset in range(4):
        row = draw.words.get(base + offset)
        if row is None:
            if not sparse:
                return None, 'row_unavailable'
            row = [0, 0, 0, 0]   # Omitted only under a successful sparse_zero query.
        words.extend(row)
    payload = b''.join(struct.pack('<I', word) for word in words)
    if f'{fnv1a64(payload):016x}' != logged.lower():
        return None, 'rows_hash_mismatch_assumed_register' if assumed else 'rows_hash_mismatch'
    rows = rows_from_words(words)
    if rows is None:
        return None, 'non_finite_rows'
    return rows, 'verified_assumed_register' if assumed else 'verified'


def row_deltas(before, after):
    """Row-space deltas. `linear` is the 3x3 block, `translation` the 4th column."""
    linear = max(abs(after[i][j] - before[i][j]) for i in range(3) for j in range(3))
    linear_scale = max(max(abs(before[i][j]) for i in range(3) for j in range(3)), 1e-30)
    translation = max(abs(after[i][3] - before[i][3]) for i in range(4))
    translation_scale = max(max(abs(before[i][3]) for i in range(4)), 1e-30)
    return dict(linear_abs=linear, linear_rel=linear / linear_scale,
                translation_abs=translation, translation_rel=translation / translation_scale)


# RigidDrawKey::pass of the live route (src/renderer/motion_history.h,
# MotionPass): every draw this analyzer keys lies inside the Scene-phase
# bracket, which is the main scene pass by definition, so the field is the
# constant 1 here. The capture format carries no pass field; a future capture
# of a depth-only or shadow pass must bracket those draws separately before
# they can be keyed apart (docs/reverse-engineering/motion-history-key.md).
PASS_MAIN_SCENE = 1


def build_key_fields(draw, geometry):
    life, context = draw.life, draw.context
    return dict(load_epoch=life['load_epoch'], registry_epoch=life['registry_epoch'],
                node_serial=life['node_serial'], camera_serial=life['camera_serial'],
                model=context.get('model'), lod=context.get('lod'), **geometry,
                render_pass=PASS_MAIN_SCENE)


K1_FIELDS = ('load_epoch', 'registry_epoch', 'node_serial', 'camera_serial', 'model', 'lod',
             'vb', 'ib', 'stream_offset', 'stride', 'declaration', 'topology',
             'start_index', 'primitives', 'base_vertex', 'min_vertex', 'num_vertices', 'indexed',
             'render_pass')
# K2 drops the three allocation/declaration identities only.
K2_FIELDS = tuple(name for name in K1_FIELDS if name not in ('vb', 'ib', 'declaration'))
# K2b additionally drops the stream binding geometry, leaving lifetime + draw args.
K2B_FIELDS = tuple(name for name in K2_FIELDS if name not in ('stream_offset', 'stride'))


def collect_scene(frame, boundary, vertex_profiles):
    """Retain the Scene-phase draws of one frame with their candidate keys."""
    entries = []
    counts = Counter({name: 0 for name in (
        'scene_draws', 'unscoped', 'known_lifetime', 'without_known_lifetime',
        'known_without_complete_geometry', 'keyable')})
    for index in sorted(frame['draws']):
        if not (boundary['first_scene_draw'] <= index <= boundary['last_scene_draw']):
            continue
        draw = frame['draws'][index]
        counts['scene_draws'] += 1
        context = draw.context or {}
        counts['unscoped'] += context.get('scoped') == '0'
        if not known_scope(draw):
            counts['without_known_lifetime'] += 1
            continue
        counts['known_lifetime'] += 1
        geometry = geometry_facts(draw)
        if geometry is None:
            counts['known_without_complete_geometry'] += 1
            continue
        counts['keyable'] += 1
        rows, status = submitted_rows(draw, vertex_profiles)
        counts['rows_' + status] += 1
        values = build_key_fields(draw, geometry)
        entries.append(dict(index=index, values=values, rows=rows, rows_status=status,
                            rows_hash=(draw.motion or {}).get('rows_hash'),
                            vs=draw.header.get('vs'), ps=draw.header.get('ps'),
                            node_serial=values['node_serial'],
                            camera_serial=values['camera_serial'], vb=values['vb'],
                            lod=values['lod'], model=values['model']))
    per_node = Counter(entry['node_serial'] for entry in entries)
    per_vb = Counter(entry['vb'] for entry in entries)
    counts['distinct_node_serials'] = len(per_node)
    counts['max_draws_per_node_serial'] = max(per_node.values(), default=0)
    counts['draws_sharing_a_node_serial'] = sum(value for value in per_node.values() if value > 1)
    counts['distinct_vertex_buffers'] = len(per_vb)
    counts['draws_sharing_a_vertex_buffer'] = sum(value for value in per_vb.values() if value > 1)
    ordinal = Counter()
    for entry in entries:
        node = entry['node_serial']
        entry['ordinal'] = ordinal[node]
        ordinal[node] += 1
    for entry in entries:
        entry['keys'] = {
            'K1': tuple(entry['values'][name] for name in K1_FIELDS),
            'K2': tuple(entry['values'][name] for name in K2_FIELDS),
            'K2b': tuple(entry['values'][name] for name in K2B_FIELDS),
            'K3': (entry['node_serial'], entry['ordinal']),
        }
    return entries, counts


def index_keys(entries, name):
    table = defaultdict(list)
    for entry in entries:
        table[entry['keys'][name]].append(entry)
    return table


def duplicate_report(entries, table):
    """Describe within-frame duplicates: repeated node/material submissions."""
    duplicated = {key: items for key, items in table.items() if len(items) > 1}
    identical = sum(len({item['rows_hash'] for item in items}) == 1
                    for items in duplicated.values())
    detail = []
    for key, items in sorted(duplicated.items(), key=lambda pair: -len(pair[1]))[:EXAMPLE_LIMIT]:
        rows = {item['rows_hash'] for item in items}
        detail.append(dict(draws=[item['index'] for item in items[:8]], occurrences=len(items),
                           node_serial=items[0]['node_serial'], model=items[0]['model'],
                           lod=items[0]['lod'], vs=items[0]['vs'], ps=items[0]['ps'],
                           distinct_submitted_rows=len(rows),
                           identical_rows=len(rows) == 1,
                           adjacent_draw_indices=all(b['index'] - a['index'] == 1
                                                     for a, b in zip(items, items[1:]))))
    return dict(duplicated_keys=len(duplicated),
                draws_in_duplicated_keys=sum(len(items) for items in duplicated.values()),
                duplicated_keys_with_identical_submitted_rows=identical,
                duplicated_keys_with_differing_submitted_rows=len(duplicated) - identical,
                examples=detail)


def match_pairs(previous, current, name):
    """Design rule: a previous key duplicated in frame N-1 is poisoned; a key
    duplicated in frame N is consumed once by its lowest draw index."""
    table = index_keys(previous, name)
    matched, consumed = {}, set()
    ambiguous_previous = duplicate_current = 0
    for entry in current:
        key = entry['keys'][name]
        items = table.get(key)
        if not items:
            continue
        if len(items) > 1:
            ambiguous_previous += 1
            continue
        if key in consumed:
            duplicate_current += 1
            continue
        consumed.add(key)
        matched[entry['index']] = items[0]
    return matched, dict(matched=len(matched), ambiguous_previous=ambiguous_previous,
                         duplicate_current=duplicate_current)


def analyze(lines, vertex_profiles=None, pixel_profiles=None, session_complete=False):
    vertex_profiles = vertex_profiles or {}
    pixel_profiles = pixel_profiles or {}
    frames, order = parse(lines, vertex_profiles)

    per_frame = []
    scenes = {}
    for key in order:
        frame = frames[key]
        complete = frame_complete(frame)
        selected = selected_depth_boundary(frame)
        boundary = scene_boundary(frame) if complete else None
        item = dict(device=key[0], frame=int(key[1]), total_draws=len(frame['draws']),
                    complete_successful_capture=complete,
                    selected_depth_boundary=selected,
                    gameplay_scene=bool(complete and selected and boundary),
                    boundary=boundary)
        if item['gameplay_scene']:
            entries, counts = collect_scene(frame, boundary, vertex_profiles)
            scenes[key] = entries
            item['counts'] = dict(counts)
            for name in ('K1', 'K2', 'K2b', 'K3'):
                table = index_keys(entries, name)
                item.setdefault('keys', {})[name] = dict(distinct=len(table),
                                                         **duplicate_report(entries, table))
        per_frame.append(item)

    # ---- bursts -------------------------------------------------------------
    bursts = []
    for item in per_frame:
        if not item['gameplay_scene']:
            bursts.append([])
            continue
        if bursts and bursts[-1] and bursts[-1][-1]['device'] == item['device'] \
                and bursts[-1][-1]['frame'] + 1 == item['frame']:
            bursts[-1].append(item)
        else:
            bursts.append([item])
    bursts = [burst for burst in bursts if burst]

    # ---- adjacent-pair key comparison --------------------------------------
    transitions = []
    delta_pool = defaultdict(list)
    suspicious = []
    lod_switch_records = []
    argon_pairs = []
    for burst in bursts:
        for before, after in zip(burst, burst[1:]):
            previous = scenes[(before['device'], str(before['frame']))]
            current = scenes[(after['device'], str(after['frame']))]
            record = dict(device=after['device'], previous_frame=before['frame'],
                          frame=after['frame'], previous_keyable=len(previous),
                          current_keyable=len(current),
                          current_scene_draws=after['counts']['scene_draws'])
            previous_per_node = Counter(entry['node_serial'] for entry in previous)
            current_per_node = Counter(entry['node_serial'] for entry in current)
            shared_nodes = set(previous_per_node) & set(current_per_node)
            record['node_serials'] = dict(
                previous=len(previous_per_node), current=len(current_per_node),
                shared=len(shared_nodes),
                current_only=len(set(current_per_node) - shared_nodes),
                previous_only=len(set(previous_per_node) - shared_nodes),
                # Latent K3 hazard: an ordinal shifts whenever a node's scene-draw
                # count changes, silently re-pairing the surviving submissions.
                shared_with_changed_scene_draw_count=sum(
                    previous_per_node[node] != current_per_node[node] for node in shared_nodes))
            k1_matched = None
            for name in ('K1', 'K2', 'K2b', 'K3'):
                matched, stats = match_pairs(previous, current, name)
                if name == 'K1':
                    k1_matched = matched
                stats['match_rate_of_keyable'] = rounded(stats['matched'] / len(current)) if current else None
                stats['match_rate_of_scene_draws'] = rounded(stats['matched'] / record['current_scene_draws']) if record['current_scene_draws'] else None
                if name == 'K1':
                    stats['matches_with_shifted_draw_index'] = sum(
                        1 for index, item in matched.items() if item['index'] != index)
                    stats['unmatched_keyable_current_draws'] = len(current) - stats['matched']
                if name != 'K1':
                    disagree = sum(1 for index, item in matched.items()
                                   if index not in k1_matched or k1_matched[index]['index'] != item['index'])
                    extra = sum(1 for index in matched if index not in k1_matched)
                    stats['pairs_differing_from_K1'] = disagree
                    stats['matches_K1_did_not_make'] = extra
                record.setdefault('keys', {})[name] = stats

            # ---- row sanity on K1 matches ----------------------------------
            deltas, static, unavailable = [], 0, 0
            for index, item in sorted(k1_matched.items()):
                entry = next(e for e in current if e['index'] == index)
                if entry['rows'] is None or item['rows'] is None:
                    unavailable += 1
                    continue
                if entry['rows_hash'] == item['rows_hash']:
                    static += 1
                deltas.append((index, item, entry, row_deltas(item['rows'], entry['rows'])))
            linear_rel = [delta['linear_rel'] for _, _, _, delta in deltas]
            median_linear_rel = quantiles(linear_rel)['median'] if linear_rel else None
            record['rows'] = dict(
                compared=len(deltas), unavailable_rows=unavailable,
                unchanged_rows=static, changed_rows=len(deltas) - static,
                unchanged_fraction=rounded(static / len(deltas)) if deltas else None,
                linear_abs=quantiles([d['linear_abs'] for *_, d in deltas]),
                linear_rel=quantiles(linear_rel),
                translation_abs=quantiles([d['translation_abs'] for *_, d in deltas]),
                translation_rel=quantiles([d['translation_rel'] for *_, d in deltas]))
            for name in ('linear_abs', 'linear_rel', 'translation_abs', 'translation_rel'):
                delta_pool[name].extend(d[name] for *_, d in deltas)
            # Flag a match whose 3x3 block moves far more than its neighbours'.
            # The key pins camera_serial, so this is not a camera change.
            for index, item, entry, delta in deltas:
                if median_linear_rel is not None and delta['linear_rel'] > 0.25 \
                        and median_linear_rel < 0.02:
                    suspicious.append(dict(device=after['device'], previous_frame=before['frame'],
                                           frame=after['frame'], previous_draw=item['index'],
                                           draw=index, node_serial=entry['node_serial'],
                                           camera_serial=entry['camera_serial'],
                                           model=entry['model'], lod=entry['lod'],
                                           vs=entry['vs'], ps=entry['ps'],
                                           linear_rel=rounded(delta['linear_rel']),
                                           translation_rel=rounded(delta['translation_rel']),
                                           pair_median_linear_rel=median_linear_rel))

            # ---- LOD / vertex-buffer switches on a stable node serial -------
            previous_vb = defaultdict(set)
            current_vb = defaultdict(set)
            for entry in previous:
                previous_vb[entry['node_serial']].add(entry['vb'])
            for entry in current:
                current_vb[entry['node_serial']].add(entry['vb'])
            switches = []
            k2_matched, _ = match_pairs(previous, current, 'K2')
            k2b_matched, _ = match_pairs(previous, current, 'K2b')
            k3_matched, _ = match_pairs(previous, current, 'K3')
            for node in sorted(set(previous_vb) & set(current_vb)):
                if previous_vb[node] == current_vb[node]:
                    continue
                affected = [entry for entry in current if entry['node_serial'] == node]
                lods = {entry['lod'] for entry in affected} | {
                    entry['lod'] for entry in previous if entry['node_serial'] == node}
                rows_continuous = None
                for entry in affected:
                    item = k2_matched.get(entry['index']) or k3_matched.get(entry['index'])
                    if item and item['rows'] is not None and entry['rows'] is not None:
                        delta = row_deltas(item['rows'], entry['rows'])
                        rows_continuous = rounded(delta['linear_rel']) if rows_continuous is None \
                            else max(rows_continuous, rounded(delta['linear_rel']))
                switches.append(dict(
                    node_serial=node, previous_vb=sorted(previous_vb[node]),
                    current_vb=sorted(current_vb[node]), distinct_lods=sorted(lods),
                    current_draws=len(affected),
                    matched_by_K1=sum(entry['index'] in k1_matched for entry in affected),
                    matched_by_K2=sum(entry['index'] in k2_matched for entry in affected),
                    matched_by_K2b=sum(entry['index'] in k2b_matched for entry in affected),
                    matched_by_K3=sum(entry['index'] in k3_matched for entry in affected),
                    max_linear_rel_of_weak_match=rows_continuous))
            record['vertex_buffer_switches_on_stable_node_serial'] = dict(
                nodes=len(switches), examples=switches[:EXAMPLE_LIMIT])
            lod_switch_records.extend(switches)

            # ---- reviewed Argon pair ---------------------------------------
            argon_current = [entry for entry in current
                             if entry['vs'] == ARGON_VS and entry['ps'] == ARGON_PS]
            argon_matched = sum(entry['index'] in k1_matched for entry in argon_current)
            argon_k3 = sum(entry['index'] in k3_matched for entry in argon_current)
            record['argon_pair'] = dict(
                current_draws=len(argon_current),
                fraction_of_scene_draws=rounded(len(argon_current) / record['current_scene_draws'])
                if record['current_scene_draws'] else None,
                matched_K1=argon_matched, matched_K3=argon_k3,
                match_rate_K1=rounded(argon_matched / len(argon_current)) if argon_current else None)
            argon_pairs.append(record['argon_pair'])
            transitions.append(record)

    # ---- shader-pair census over Scene phases -------------------------------
    pair_counts = Counter()
    argon_per_frame = []
    for item in per_frame:
        if not item['gameplay_scene']:
            continue
        entries = scenes[(item['device'], str(item['frame']))]
        for entry in entries:
            pair_counts[(entry['vs'], entry['ps'])] += 1
        argon_draws = sum(entry['vs'] == ARGON_VS and entry['ps'] == ARGON_PS for entry in entries)
        argon_per_frame.append(dict(device=item['device'], frame=item['frame'],
                                    scene_draws=item['counts']['scene_draws'],
                                    keyable_scene_draws=len(entries),
                                    argon_draws=argon_draws,
                                    fraction_of_scene_draws=rounded(argon_draws / item['counts']['scene_draws'])
                                    if item['counts']['scene_draws'] else None))
    top_pairs = []
    for (vs, ps), count in pair_counts.most_common(15):
        vertex = vertex_profiles.get(vs) or {}
        pixel = pixel_profiles.get(ps) or {}
        top_pairs.append(dict(vs=vs, ps=ps, scene_draws=count,
                              vs_shader_model=vertex.get('shader_model'),
                              ps_shader_model=pixel.get('shader_model'),
                              vs_matrix_register=vertex.get('matrix_register'),
                              vs_position_path=vertex.get('position_path')))

    # ---- camera serial and epoch movement inside bursts ---------------------
    burst_reports = []
    for burst in bursts:
        frames_report = []
        for item in burst:
            entries = scenes[(item['device'], str(item['frame']))]
            cameras = Counter(entry['camera_serial'] for entry in entries)
            epochs = Counter((entry['values']['load_epoch'], entry['values']['registry_epoch'])
                             for entry in entries)
            frames_report.append(dict(
                frame=item['frame'], keyable_scene_draws=len(entries),
                camera_serials=sorted(cameras), dominant_camera_serial=cameras.most_common(1)[0][0] if cameras else None,
                camera_serial_counts={serial: count for serial, count in sorted(cameras.items())},
                load_registry_epochs=sorted(f'{a}/{b}' for a, b in epochs)))
        changes = []
        for before, after in zip(frames_report, frames_report[1:]):
            entry = dict(previous_frame=before['frame'], frame=after['frame'],
                         dominant_camera_serial_changed=before['dominant_camera_serial'] != after['dominant_camera_serial'],
                         camera_serial_set_changed=before['camera_serials'] != after['camera_serials'],
                         epochs_changed=before['load_registry_epochs'] != after['load_registry_epochs'],
                         previous_camera_serials=before['camera_serials'],
                         camera_serials=after['camera_serials'],
                         previous_epochs=before['load_registry_epochs'],
                         epochs=after['load_registry_epochs'])
            changes.append(entry)
        burst_reports.append(dict(device=burst[0]['device'],
                                  frames=[item['frame'] for item in burst],
                                  per_frame=frames_report, adjacent_changes=changes))

    totals = Counter()
    for item in per_frame:
        if item['gameplay_scene']:
            totals.update(item['counts'])
            totals['gameplay_scene_frames'] += 1
    key_totals = {}
    for name in ('K1', 'K2', 'K2b', 'K3'):
        matched = sum(record['keys'][name]['matched'] for record in transitions)
        keyable = sum(record['previous_keyable'] and record['current_keyable'] for record in transitions)
        denominator = sum(record['current_keyable'] for record in transitions)
        scene_denominator = sum(record['current_scene_draws'] for record in transitions)
        key_totals[name] = dict(
            matched=matched, keyable_draws=denominator, scene_draws=scene_denominator,
            match_rate_of_keyable=rounded(matched / denominator) if denominator else None,
            match_rate_of_scene_draws=rounded(matched / scene_denominator) if scene_denominator else None,
            ambiguous_previous=sum(record['keys'][name]['ambiguous_previous'] for record in transitions),
            duplicate_current=sum(record['keys'][name]['duplicate_current'] for record in transitions),
            pairs_differing_from_K1=sum(record['keys'][name].get('pairs_differing_from_K1', 0) for record in transitions),
            matches_K1_did_not_make=sum(record['keys'][name].get('matches_K1_did_not_make', 0) for record in transitions),
            duplicated_keys_per_frame=sum(item['keys'][name]['duplicated_keys'] for item in per_frame if item['gameplay_scene']),
            draws_in_duplicated_keys=sum(item['keys'][name]['draws_in_duplicated_keys'] for item in per_frame if item['gameplay_scene']),
            duplicated_keys_with_differing_submitted_rows=sum(
                item['keys'][name]['duplicated_keys_with_differing_submitted_rows']
                for item in per_frame if item['gameplay_scene']),
            distinct_keys=sum(item['keys'][name]['distinct'] for item in per_frame if item['gameplay_scene']),
            matches_with_shifted_draw_index=sum(
                record['keys'][name].get('matches_with_shifted_draw_index', 0) for record in transitions),
            unmatched_keyable_current_draws=sum(record['current_keyable'] for record in transitions) - matched)
        _ = keyable

    return dict(
        scope=('completed-session fixed snapshot' if session_complete else
               'partial fixed snapshot; gameplay may continue'),
        purpose='offline candidate-key validation for the live same-draw motion route',
        scene_phase_definition=(
            'draws after the first full depth-only Clear of the latched main color/depth pair '
            'that follows at least one background draw, through the draw preceding the next '
            'non-draw_begin device event (StretchRect/ColorFill/SetDepthStencil bloom group); '
            'only frames with a confirmed scene-depth boundary are treated as gameplay'),
        key_definitions=dict(K1=list(K1_FIELDS), K2=list(K2_FIELDS), K2b=list(K2B_FIELDS),
                             K3=['node_serial', 'ordinal_within_frame_scene_draws_of_that_node']),
        reviewed_pair=dict(vs=ARGON_VS, ps=ARGON_PS),
        totals_gameplay_scene_frames=dict(totals),
        totals_note=('Every per-frame count is summed over the gameplay Scene frames. '
                     'distinct_node_serials/distinct_vertex_buffers therefore total per-frame '
                     'distinct counts and are not a distinct count over the whole capture.'),
        key_totals=key_totals,
        node_serial_totals=dict(
            shared_nodes_with_changed_scene_draw_count=sum(
                record['node_serials']['shared_with_changed_scene_draw_count'] for record in transitions),
            shared_nodes=sum(record['node_serials']['shared'] for record in transitions),
            current_only_nodes=sum(record['node_serials']['current_only'] for record in transitions),
            previous_only_nodes=sum(record['node_serials']['previous_only'] for record in transitions)),
        row_delta_totals={name: quantiles(values) for name, values in sorted(delta_pool.items())},
        suspicious_row_changes=dict(count=len(suspicious), threshold=dict(linear_rel_above=0.25, pair_median_linear_rel_below=0.02),
                                    examples=suspicious[:EXAMPLE_LIMIT]),
        vertex_buffer_switches_total=dict(
            observations=len(lod_switch_records),
            matched_by_K1=sum(item['matched_by_K1'] for item in lod_switch_records),
            matched_by_K2=sum(item['matched_by_K2'] for item in lod_switch_records),
            matched_by_K2b=sum(item['matched_by_K2b'] for item in lod_switch_records),
            matched_by_K3=sum(item['matched_by_K3'] for item in lod_switch_records),
            current_draws=sum(item['current_draws'] for item in lod_switch_records)),
        argon_pair_per_frame=argon_per_frame,
        top_scene_shader_pairs=top_pairs,
        bursts=burst_reports, frames=per_frame, transitions=transitions,
        limitations=[
            'A unique key match is a candidate correspondence between two recorded draws, '
            'not proof of engine object identity, geometry stability or a correct motion vector.',
            'Vertex/index contents are not captured; an allocation identity can be rewritten in place.',
            'Submitted rows are accepted only when the producer rows_hash reproduces exactly; '
            'other draws are reported as unavailable, never as zero rows.',
            'Shader model is derived from the tracked shader profile registry; the capture log '
            'records no shader version.',
            'Row deltas are algebraic differences of submitted constants, not measured pixels.',
        ])


ARGON_VS = '53a0a641107ed76c'
ARGON_PS = '8759c7838bbc86c2'


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('--expected-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--session-complete', action='store_true')
    parser.add_argument('--shader-profiles', type=Path,
                        default=Path(__file__).resolve().parents[2] /
                        'verification/results/shader-profile-registry.json')
    args = parser.parse_args()

    vertex_profiles, pixel_profiles, profiles_digest = load_shader_profiles(args.shader_profiles)
    before = args.snapshot.stat()
    digest = hashlib.sha256()
    with args.snapshot.open('rb') as source:
        def lines():
            for raw in source:
                digest.update(raw)
                yield raw.decode('utf-8', errors='strict')
        report = analyze(lines(), vertex_profiles, pixel_profiles, args.session_complete)
    after = args.snapshot.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns) \
            or digest.hexdigest() != args.expected_sha256:
        raise ValueError('snapshot changed or expected hash mismatch')

    root = Path(__file__).resolve().parents[2]
    sources = ['tools/analysis/analyze_motion_history_key.py', 'src/renderer/motion_history.h',
               'src/proxy/draw_input.cpp', 'src/proxy/capture.cpp']
    report['provenance'] = dict(
        snapshot=str(args.snapshot), size=after.st_size, sha256=digest.hexdigest(),
        shader_profile_registry=str(args.shader_profiles) if profiles_digest else None,
        shader_profile_registry_sha256=profiles_digest,
        local_interpretation_source_sha256={
            name: hashlib.sha256((root / name).read_bytes()).hexdigest()
            for name in sources if (root / name).exists()})
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    print(json.dumps(dict(totals=report['totals_gameplay_scene_frames'],
                          key_totals=report['key_totals']), indent=2))


if __name__ == '__main__':
    main()
