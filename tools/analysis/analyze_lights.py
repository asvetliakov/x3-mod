#!/usr/bin/env python3
"""Decode named point-light inputs from complete schema-2 draw snapshots.

Reports shader inputs, not a sector light registry. Sparse float zeros require a
successful bounded query; integer count must be captured explicitly. Unused array
entries are ignored, since constants can retain data from an earlier draw.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct
from summarize_capture import summarize


def successful(value):
    try:
        return not (int(value, 16) & 0x80000000)
    except (ValueError, TypeError):
        return False


def decode(draw, metadata, stage):
    """Return None for unrelated shaders; reject missing/ambiguous evidence."""
    parameters = metadata.get(stage + '_' + draw[stage], [])
    arrays = [p for p in parameters if p['name'] == 'g_LightPoint']
    counts = [p for p in parameters if p['name'] == 'g_nNumLightPoint']
    if not arrays and not counts:
        return None
    if len(arrays) != 1 or len(counts) != 1:
        raise ValueError('missing or ambiguous light metadata')
    if not successful(draw.get('draw_result', {}).get('result')):
        raise ValueError('draw did not succeed')
    array, count = arrays[0], counts[0]
    layout = [(p.get('name'), p.get('parameter_class'), p.get('parameter_type'),
               p.get('rows'), p.get('columns'), p.get('elements'), p.get('struct_members'))
              for p in array.get('members', [])]
    expected = [(name, 1, 3, 1, width, 1, 0) for name, width in [('pos',3),('color',3),('atten',4)]]
    capacity = array.get('elements', 0)
    if (array.get('register_set') != 2 or array.get('parameter_class') != 5
            or not 0 < capacity <= 64 or array.get('count') != 3*capacity or layout != expected):
        raise ValueError('unsupported point-light array layout')
    if any(count.get(k) != v for k,v in dict(register_set=1, count=1, parameter_class=0,
                                            parameter_type=2, rows=1, columns=1, elements=1).items()):
        raise ValueError('unsupported point-light count layout')
    status = draw.get('constant_status', {}).get(stage, {})
    values = draw.get('constants', {}).get(stage, {})
    i = status.get('i', {})
    reg = count['register']
    if (not 0 <= reg < 16 or not successful(i.get('result')) or i.get('encoding') != 'full'
            or not reg < int(i.get('count', 0)) <= 16):
        raise ValueError('integer count query unavailable')
    iv = values.get('i', {}).get(str(reg))
    if iv is None or len(iv) != 4:
        raise ValueError('integer count missing')
    active = iv[0]
    if not 0 <= active <= capacity:
        raise ValueError('integer count exceeds array capacity')
    lights = []
    if active:
        f = status.get('f', {})
        first = array['register']
        if (first < 0 or not successful(f.get('result')) or f.get('encoding') != 'sparse_zero'
                or not first+3*active <= int(f.get('count', 0)) <= (256 if stage == 'vs' else 224)):
            raise ValueError('float array query unavailable')
        for index in range(active):
            light = {}
            for offset,(name,width) in enumerate([('pos',3),('color',3),('atten',4)]):
                bits = values.get('f', {}).get(str(first+3*index+offset), '00000000,'*3+'00000000').split(',')
                if len(bits) != 4:
                    raise ValueError('malformed float register')
                words = [int(b,16) for b in bits]
                if any(not 0 <= word <= 0xffffffff for word in words):
                    raise ValueError('float register exceeds uint32')
                row = [struct.unpack('<f', struct.pack('<I', word))[0] for word in words]
                if not all(math.isfinite(x) for x in row):
                    raise ValueError('nonfinite light register')
                light[name] = row[:width]
            lights.append(light)
    return dict(active_count=active, capacity=capacity, lights=lights)


def analyze(trace, metadata):
    capture = summarize(trace, {}, include_floats=True)
    result = dict(meaning='Per-draw shader inputs; not persistent engine light identities.', frames={})
    for key,frame in capture['frames'].items():
        valid = frame['complete'] and frame.get('draw_count_matches', False) and successful(frame.get('present_result'))
        report = dict(complete=valid, total_draws=len(frame['draws']), counts={}, observations=[], rejected=[])
        result['frames'][key] = report
        if not valid:
            continue
        counts = Counter()
        for draw in frame['draws']:
            for stage in ('vs','ps'):
                try:
                    decoded = decode(draw, metadata, stage)
                    if decoded is not None:
                        counts[str(decoded['active_count'])] += 1
                        report['observations'].append(dict(draw=draw['index'], stage=stage, shader=draw[stage], **decoded))
                except ValueError as exc:
                    report['rejected'].append(dict(draw=draw['index'], stage=stage, reason=str(exc)))
        report['counts'] = dict(counts)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    raw, meta = args.trace.read_bytes(), args.metadata.read_bytes()
    result = analyze(raw.decode(), json.loads(meta))
    result['source'] = dict(trace=args.trace.name, sha256=hashlib.sha256(raw).hexdigest(),
                          metadata=args.metadata.name, metadata_sha256=hashlib.sha256(meta).hexdigest())
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    print(json.dumps({key:dict(counts=f['counts'], rejected=len(f['rejected'])) for key,f in result['frames'].items()},indent=2))


if __name__ == '__main__':
    main()
