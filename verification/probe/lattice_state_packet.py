#!/usr/bin/env python3
"""Validate bounded lattice state and optional schema-2 geometry bundles."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct

SELECTOR = 'run177_panel_position_v1'
FIELD_LIMIT, WORD_LIMIT, SHADER_WORD_LIMIT = 256, 40000, 16384
MAX_JSON_BYTES = 1024 * 1024
POSITION = [0x00bc6871, 0x002dd083, 0xff415956]
SOURCE = [0x4944d81dfe531b37, 0x5e0a10fe752b6140]
# Public D3D9 enumerants, deliberately explicit to catch missing observations.
RENDER = {7, 8, 9, 14, 15, 19, 20, 22, 23, 24, 25, 26, 27, 28, 29,
          52, 53, 54, 55, 56, 57, 58, 59, 136, 152, 161, 162, 168, 171,
          174, 175, 176, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194,
          195, 206, 207, 208, 209, *range(128, 136), *range(198, 206)}
KINDS = {'source_shader', 'declaration', 'object', 'arguments', 'route', 'shader',
         'constants_f', 'constants_i', 'constants_b', 'render', 'sampler', 'viewport',
         'scissor', 'clip', 'stream', 'stream_frequency', 'vertex_desc', 'indices',
         'index_desc', 'target', 'surface_desc', 'container', 'texture', 'texture_desc',
         'texture_lod', 'caps'}
HEX = re.compile(r'^[0-9a-f]{8}$')
STATUSES = {'complete', 'no_match', 'partial', 'ambiguous', 'unavailable', 'reset',
            'capacity', 'submission_failed'}
GEOMETRY_FORMAT = 'x3_lattice_geometry_v1'
GEOMETRY_SCOPE = 'producer_uploads_bound_at_observation'
GEOMETRY_BYTES = 466224
GEOMETRY_RANGES = (
    {'vertex': (0, 387200), 'index': (387200, 22704), 'vertices': 9680},
    {'vertex': (409904, 50680), 'index': (460584, 5640), 'vertices': 1267},
)
PAIR_STATUSES = {'not_attempted', 'copied', 'unarmed', 'closing', 'active_scope',
                 'selector', 'missing', 'duplicate', 'stale_arm', 'device',
                 'binding', 'revision', 'open_mapping', 'dispatch', 'capacity',
                 'api_error'}
ATTACHMENT_STATUSES = {'valid', 'not_selected', 'allocation_failure', 'copy_refused',
                       'packet_invalid', 'submission_failed', 'sibling_refused',
                       'export_failed'}
INVALIDATED_BY = {'reset', 'ambiguous', 'partial', 'unavailable', 'capacity',
                  'submission_failed', 'no_match'}
HEX64 = re.compile(r'^[0-9a-f]{16}$')
SHA256 = re.compile(r'^[0-9a-f]{64}$')


def need(condition, message):
    if not condition:
        raise ValueError(message)


def _validate_state(packet, require_complete, schema):
    need(packet.get('schema') == schema and packet.get('selector') == SELECTOR, 'unknown schema/selector')
    evidence = packet.get('payload_copy_valid')
    need(packet.get('draw_input_coherence') == 'unqualified' and
         (evidence == 'not_attempted' if schema == 1 else type(evidence) is bool),
         'invalid evidence claim')
    for key, minimum in [('device', 1), ('frame', 0), ('generation', 0), ('query_ticks', 0), ('qpc_frequency', 1)]:
        value = packet.get(key)
        need(type(value) is int and minimum <= value <= 0xffffffffffffffff, f'missing/invalid {key}')
    status = packet.get('status')
    need(status in STATUSES, 'invalid status')
    complete = status == 'complete'
    need(not require_complete or complete, 'incomplete state observation')
    need(type(packet.get('candidates')) is int and 0 <= packet['candidates'] <= 65, 'candidate bound')
    matches = packet.get('matches')
    need(isinstance(matches, list) and len(matches) == 2 and all(type(x) is int and 0 <= x <= 2 for x in matches) and sum(matches) <= 3, 'match bound')
    need(packet['candidates'] >= sum(matches), 'candidate/match inconsistency')
    records = packet.get('records')
    need(isinstance(records, list) and len(records) == 2, 'record bound')
    if complete:
        need(packet.get('matches') == [1, 1], 'ambiguous/partial matches')
        need(packet.get('scope_active_at_arm') is True, 'object trace prerequisite unavailable')
        need(packet['candidates'] <= 64, 'complete candidate bound')
    objects = []
    for slot, record in enumerate(records):
        need(isinstance(record, dict), 'record is not an object')
        need(type(record.get('slot')) is int and record['slot'] == slot, 'record slot')
        draw=record.get('draw')
        need(type(draw) is int and (1 if complete else 0) <= draw <= 0xffffffffffffffff, 'missing/invalid draw')
        fields = record.get('fields')
        need(isinstance(fields, list) and len(fields) <= FIELD_LIMIT, 'field bound')
        lookup, word_count = {}, 0
        for field in fields:
            need(isinstance(field, dict), 'field is not an object')
            kind, index = field.get('kind'), field.get('index')
            need(kind in KINDS and type(index) is int and 0 <= index < 1024, 'field identity')
            key = kind, index
            need(key not in lookup, 'duplicate field')
            hr, words = field.get('hr'), field.get('words')
            need(isinstance(hr, str) and HEX.fullmatch(hr), 'HRESULT encoding')
            need(isinstance(words, list) and all(isinstance(w, str) and HEX.fullmatch(w) for w in words), 'word encoding')
            failed = int(hr, 16) & 0x80000000
            need(not failed or not words, 'failed query contains invented values')
            need(kind != 'shader' or len(words) <= SHADER_WORD_LIMIT, 'shader byte bound')
            word_count += len(words)
            need(word_count <= WORD_LIMIT, 'record byte bound')
            lookup[key] = field
            if complete:
                need(not failed or (kind == 'target' and hr == '88760866'), 'required query unavailable')
        if not complete:
            continue
        need(record.get('submitted') is True and isinstance(record.get('result'), str) and
             HEX.fullmatch(record['result']) and not int(record['result'], 16) & 0x80000000, 'submission unavailable')

        def words(kind, index=0, count=None):
            field = lookup.get((kind, index))
            need(field is not None, f'missing {kind}/{index}')
            result = [int(x, 16) for x in field['words']]
            need(count is None or len(result) == count, f'length {kind}/{index}')
            return result

        caps = words('caps', count=6)
        need(1 <= caps[0] <= 16 and caps[1] <= 32 and 1 <= caps[2] <= 256 and 1 <= caps[5] <= 4, 'unsupported caps')
        for stage in range(2):
            lo, hi = words('source_shader', stage, 2)
            need(lo | hi << 32 == SOURCE[stage], 'source shader mismatch')
            code = words('shader', stage)
            need(len(code) >= 2 and code[-1] == 0xffff, 'truncated shader')
            words('constants_f', stage, caps[2] * 4 if stage == 0 else (224 if caps[4] >> 8 & 255 >= 3 else 32) * 4)
            words('constants_i', stage, 64)
            words('constants_b', stage, 16)
        expected_decl = [v for offset, usage in [(0, 0), (8, 5), (16, 3), (24, 6), (32, 7)]
                         for v in [offset << 16, 16 | usage << 16]] + [255, 17]
        need(words('declaration', 0, 12) == expected_decl, 'original declaration mismatch')
        words('declaration', 1, 12)
        need(words('arguments', count=6) == [4, 3784 if slot == 0 else 940, 0, 9680 if slot == 0 else 1267, 0, 0], 'argument mismatch')
        obj = words('object', count=18)
        need(obj[0] & 1 and obj[1] == 1 and obj[5:7] == [0x54b3, 0], 'object scope mismatch')
        need(words('object', 1, 3) == POSITION, 'position selector mismatch')
        objects.append(obj[3:5] + obj[7:9])
        for i, n in [(2, 9), (3, 4), (4, 16), (5, 16), (6, 16), (7, 16)]:
            words('object', i, n)
        route = words('route', count=20)
        need(route[5] == 1, 'suppressed draw')
        for state in RENDER:
            words('render', state, 1)
        words('viewport', count=6)
        words('scissor', count=4)
        # The producer preserves raw caps but records at most six plane equations.
        # Larger advertised capacity is harmless only when no uncaptured plane
        # is enabled; never promote missing active state to a complete observation.
        captured_planes = min(caps[1], 6)
        need(words('render', 152, 1)[0] & ~((1 << captured_planes) - 1) == 0,
             'enabled clip plane outside captured range')
        for i in range(captured_planes):
            words('clip', i, 4)
        for i in range(caps[0]):
            binding = words('stream', i, 5)
            freq = words('stream_frequency', i, 3)
            need(binding[0] == (1 if i == 0 else 0), 'not stream0 only')
            if i == 0:
                need(freq == [0, 40, 1], 'stream0 layout')
                words('vertex_desc', i, 6)
        need(words('indices', count=5)[0] == 1, 'missing index buffer')
        words('index_desc', count=5)
        for i in [*range(caps[5]), 4]:
            binding = words('target', i)
            if lookup['target', i]['hr'] == '88760866':
                continue
            need(len(binding) == 5, 'target binding')
            if binding[0]:
                words('surface_desc', i, 8)
                words('container', i, 5)
        for stage in [0, 3]:
            for i in range(1, 14):
                words('sampler', stage * 16 + i, 1)
            texture = words('texture', stage, 5)
            if texture[0]:
                meta = words('texture_lod', stage, 3)
                need(meta[0] == 3 and 1 <= meta[2] <= 32, 'unsupported texture descriptor')
                for level in range(meta[2]):
                    words('texture_desc', stage * 32 + level, 8)
    if complete:
        need(objects[0] == objects[1], 'different within-frame objects')
        need(records[0]['draw'] != records[1]['draw'], 'duplicate draw identity')
    return complete


def _uint(value, name):
    need(type(value) is int and 0 <= value <= 0xffffffffffffffff, f'missing/invalid {name}')


def _identity(value, name):
    need(isinstance(value, str) and HEX64.fullmatch(value) and int(value, 16) != 0,
         f'missing/invalid upload {name}')


def _validate_upload(upload, slot, valid, terminal_status):
    need(isinstance(upload, dict), 'missing upload envelope')
    common = {'pair_status', 'attachment_status', 'producer_payload_valid',
              'binding_revision_match_at_observation'}
    need(upload.get('pair_status') in PAIR_STATUSES, 'invalid pair status')
    attachment = upload.get('attachment_status')
    need(attachment in ATTACHMENT_STATUSES, 'invalid attachment status')
    need(type(upload.get('producer_payload_valid')) is bool and
         type(upload.get('binding_revision_match_at_observation')) is bool,
         'invalid upload evidence flags')
    identity = {'arm_serial', 'owner', 'generation', 'invocation', 'vertex', 'index'}
    if valid:
        need(set(upload) == common | identity, 'unexpected/missing valid upload member')
        need(attachment == 'valid' and upload['pair_status'] == 'copied' and
             upload['producer_payload_valid'] is True and
             upload['binding_revision_match_at_observation'] is True,
             'invalid qualified upload')
        for key in ('arm_serial', 'owner', 'generation', 'invocation'):
            _identity(upload.get(key), key)
        for kind in ('vertex', 'index'):
            item = upload.get(kind)
            need(isinstance(item, dict) and set(item) == {'allocation', 'revision', 'offset', 'bytes'},
                 f'invalid {kind} upload identity')
            _identity(item.get('allocation'), f'{kind} allocation')
            _identity(item.get('revision'), f'{kind} revision')
            offset, size = GEOMETRY_RANGES[slot][kind]
            need(type(item.get('offset')) is int and item['offset'] == offset and
                 type(item.get('bytes')) is int and item['bytes'] == size,
                 f'invalid {kind} range')
        need(upload['vertex']['allocation'] != upload['index']['allocation'],
             'vertex/index allocation collision')
    else:
        need(set(upload) == common | {'invalidated_by'}, 'unexpected/missing refused upload member')
        need(attachment != 'valid' and upload['producer_payload_valid'] is False and
             upload['binding_revision_match_at_observation'] is False,
             'invalid refused upload')
        reason = upload.get('invalidated_by')
        need((attachment == 'packet_invalid' and terminal_status in INVALIDATED_BY and
              reason == terminal_status) or
             (attachment != 'packet_invalid' and reason is None), 'invalid attachment invalidation')


def _validate_schema2(packet, complete):
    pid = packet.get('pid')
    need(type(pid) is int and 1 <= pid <= 0xffffffffffffffff, 'missing/invalid pid')
    geometry = packet.get('geometry')
    need(isinstance(geometry, dict) and set(geometry) ==
         {'format', 'scope', 'status', 'file', 'bytes', 'sha256', 'copy_ticks'},
         'invalid geometry envelope')
    need(geometry.get('format') == GEOMETRY_FORMAT and geometry.get('scope') == GEOMETRY_SCOPE,
         'invalid geometry format/scope')
    _uint(geometry.get('copy_ticks'), 'copy_ticks')
    status = geometry.get('status')
    need(status in ('complete', 'unavailable'), 'invalid geometry status')
    valid = status == 'complete'
    need(packet.get('payload_copy_valid') is valid, 'payload validity/status mismatch')
    expected = f"lattice-geometry-{pid}-{packet['device']}-{packet['frame']}-{packet['generation']}.bin"
    if valid:
        need(complete and packet.get('matches') == [1, 1], 'payload without complete state')
        need(geometry.get('file') == expected and type(geometry.get('bytes')) is int and
             geometry['bytes'] == GEOMETRY_BYTES and
             isinstance(geometry.get('sha256'), str) and SHA256.fullmatch(geometry['sha256']),
             'invalid geometry attachment')
    else:
        need(geometry.get('file') is None and type(geometry.get('bytes')) is int and geometry['bytes'] == 0 and
             geometry.get('sha256') is None, 'unavailable geometry references payload')
    uploads = []
    for slot, record in enumerate(packet['records']):
        _validate_upload(record.get('upload'), slot, valid, packet['status'])
        uploads.append(record['upload'])
    if not complete:
        need(all(upload['attachment_status'] == 'packet_invalid' and
                 upload['invalidated_by'] == packet['status'] for upload in uploads),
             'terminal state did not invalidate every attachment')
    if valid:
        for key in ('arm_serial', 'owner', 'generation'):
            need(uploads[0][key] == uploads[1][key], f'upload {key} mismatch')
        need(uploads[0]['invocation'] != uploads[1]['invocation'], 'duplicate upload invocation')
        allocations = [u[k]['allocation'] for u in uploads for k in ('vertex', 'index')]
        need(len(set(allocations)) == len(allocations), 'duplicate upload allocation')


def validate(packet, require_complete=False, require_payload=False):
    need(isinstance(packet, dict), 'packet is not an object')
    schema = packet.get('schema')
    need(type(schema) is int and schema in (1, 2), 'unknown schema/selector')
    complete = _validate_state(packet, require_complete, schema)
    if schema == 1:
        need(not require_payload, 'payload required but schema1 is state-only')
    else:
        _validate_schema2(packet, complete)
        need(not require_payload or packet['payload_copy_valid'] is True,
             'complete geometry payload required')
    return packet


def _read_geometry(path, geometry):
    name = geometry['file']
    need(isinstance(name, str) and name == Path(name).name, 'unsafe geometry basename')
    directory_fd = os.open(Path(path).parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory_fd)
        with os.fdopen(fd, 'rb') as stream:
            before = os.fstat(stream.fileno())
            need(stat.S_ISREG(before.st_mode), 'geometry is not a regular file')
            need(before.st_size == GEOMETRY_BYTES, 'geometry byte size mismatch')
            digest = hashlib.sha256()
            data = bytearray()
            remaining = GEOMETRY_BYTES
            while remaining:
                chunk = stream.read(min(1024 * 1024, remaining))
                need(bool(chunk), 'geometry truncated while reading')
                digest.update(chunk)
                data.extend(chunk)
                remaining -= len(chunk)
            need(not stream.read(1), 'geometry grew while reading')
            after = os.fstat(stream.fileno())
            keys = ('st_dev', 'st_ino', 'st_size', 'st_mtime_ns', 'st_ctime_ns')
            need(all(getattr(before, key) == getattr(after, key) for key in keys),
                 'geometry changed while reading')
    finally:
        os.close(directory_fd)
    need(digest.hexdigest() == geometry['sha256'], 'geometry SHA-256 mismatch')
    for ranges in GEOMETRY_RANGES:
        offset, size = ranges['index']
        need(all(index < ranges['vertices'] for (index,) in struct.iter_unpack('<H', data[offset:offset + size])),
             'geometry index outside vertex range')


def load(path, require_complete=False, require_payload=False):
    path = Path(path)
    raw = path.read_bytes()
    need(len(raw) <= MAX_JSON_BYTES, 'JSON byte bound')
    # Reject duplicate JSON keys rather than silently replacing a validity field.
    def unique(pairs):
        result = {}
        for key, value in pairs:
            need(key not in result, 'duplicate JSON key')
            result[key] = value
        return result
    packet = validate(json.loads(raw, object_pairs_hook=unique), require_complete, require_payload)
    if packet['schema'] == 2:
        expected = (f"lattice-state-{packet['pid']}-{packet['device']}-"
                    f"{packet['frame']}-{packet['generation']}.json")
        need(path.name == expected, 'schema2 state basename/identity mismatch')
    if packet['schema'] == 2 and packet['payload_copy_valid']:
        _read_geometry(path, packet['geometry'])
    return packet


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('packet')
    parser.add_argument('--require-complete', action='store_true')
    parser.add_argument('--require-payload', action='store_true')
    args = parser.parse_args()
    packet = load(args.packet, args.require_complete, args.require_payload)
    print(json.dumps({k: packet[k] for k in ['selector', 'status', 'device', 'frame', 'matches',
                                            'draw_input_coherence', 'payload_copy_valid']}, indent=2))
