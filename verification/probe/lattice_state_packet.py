#!/usr/bin/env python3
"""Validate a bounded state observation; never qualify payloads or draw coherence."""
import argparse
import json
from pathlib import Path
import re

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


def need(condition, message):
    if not condition:
        raise ValueError(message)


def validate(packet, require_complete=False):
    need(packet.get('schema') == 1 and packet.get('selector') == SELECTOR, 'unknown schema/selector')
    need(packet.get('draw_input_coherence') == 'unqualified' and
         packet.get('payload_copy_valid') == 'not_attempted', 'invalid evidence claim')
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
        need(record.get('slot') == slot, 'record slot')
        draw=record.get('draw')
        need(type(draw) is int and (1 if complete else 0) <= draw <= 0xffffffffffffffff, 'missing/invalid draw')
        fields = record.get('fields')
        need(isinstance(fields, list) and len(fields) <= FIELD_LIMIT, 'field bound')
        lookup, word_count = {}, 0
        for field in fields:
            kind, index = field.get('kind'), field.get('index')
            need(kind in KINDS and isinstance(index, int) and 0 <= index < 1024, 'field identity')
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
        need(1 <= caps[0] <= 16 and caps[1] <= 6 and 1 <= caps[2] <= 256 and 1 <= caps[5] <= 4, 'unsupported caps')
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
        for i in range(caps[1]):
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
    return packet


def load(path, require_complete=False):
    raw = Path(path).read_bytes()
    need(len(raw) <= MAX_JSON_BYTES, 'JSON byte bound')
    # Reject duplicate JSON keys rather than silently replacing a validity field.
    def unique(pairs):
        result = {}
        for key, value in pairs:
            need(key not in result, 'duplicate JSON key')
            result[key] = value
        return result
    return validate(json.loads(raw, object_pairs_hook=unique), require_complete)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('packet')
    parser.add_argument('--require-complete', action='store_true')
    args = parser.parse_args()
    packet = load(args.packet, args.require_complete)
    print(json.dumps({k: packet[k] for k in ['selector', 'status', 'device', 'frame', 'matches',
                                            'draw_input_coherence', 'payload_copy_valid']}, indent=2))
