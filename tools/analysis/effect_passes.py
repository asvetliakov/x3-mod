#!/usr/bin/env python3
"""Enumerate the technique/pass shader pairings of the installed compiled effects.

The shader sweep records which programs each effect contains, not which vertex
and pixel program a pass binds together. This reads every `shader/**/*.fb`
entry of the numbered CAT/DAT archives in memory (the same decode as
`index_shaders.py`), walks the compiled D3DX effect container and records, per
technique pass, the FNV-1a 64 identity of the vertex and pixel program states.
Only derived facts leave this module: catalogue, virtual path, technique and
pass names, state operations, program hashes and lengths. No effect bytes,
shader words or parameter values are written anywhere.

Container layout (D3DXFX version `0xfeff0901`; the same layout the Wine
d3dx9 implementation parses):

    u32 magic, u32 offset                 -- header; every offset below is
                                             relative to the byte after it
    at 8 + offset:
      u32 parameter_count, technique_count, unknown, object_count
      parameters:  u32 typedef, value, flags, annotations; 2 u32 per annotation
      techniques:  u32 name, annotations, passes; 2 u32 per annotation
        passes:    u32 name, annotations, states; 2 u32 per annotation
          states:  u32 operation, index, typedef, value
      u32 object_data_count, resource_count
      object data: u32 id, size, bytes padded to 4
      resources:   u32 technique, pass, element, state, usage; u32 size, bytes
                   padded to 4 (usage 0 on a shader state: the compiled program)

State operations 146 and 147 are the VertexShader and PixelShader entries of
the D3DX state table. A shader state with no resource record is a NULL shader
(for example the z-only effects bind no pixel shader).
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from index_shaders import fnv1a64, shader_end  # noqa: E402
from inspect_x3 import read_catalogue  # noqa: E402

MAGIC = b'\x01\x09\xff\xfe'
VERTEX_SHADER_STATE = 146
PIXEL_SHADER_STATE = 147
STATE_STAGES = {VERTEX_SHADER_STATE: 'vs', PIXEL_SHADER_STATE: 'ps'}


class MalformedEffect(ValueError):
    """The container does not follow the documented layout."""


def _u32(data, at, count=1):
    if at + 4 * count > len(data):
        raise MalformedEffect('record exceeds effect at %d' % at)
    return struct.unpack_from('<%dI' % count, data, at)


def _name(data, base, offset):
    (size,) = _u32(data, base + offset)
    start = base + offset + 4
    if start + size > len(data):
        raise MalformedEffect('name exceeds effect')
    return data[start:start + size].split(b'\0', 1)[0].decode('latin-1')


def parse_effect(data):
    """Return the passes of one compiled effect with their shader states.

    Each pass is a dict with technique/pass names and `states`: a list of
    (operation, stage or None, fnv1a64 or None, dword_count or None, status)
    for the shader states only. `status` is `program` for a complete token
    stream, `null` when the state has no resource, `not_a_program` when the
    resource bytes are not one complete SM1-3 stream.
    """
    if data[:4] != MAGIC:
        raise MalformedEffect('unexpected compiled effect magic')
    _, offset = _u32(data, 0, 2)
    base = 8
    at = base + offset
    parameter_count, technique_count, _, _ = _u32(data, at, 4)
    at += 16
    for _ in range(parameter_count):
        _, _, _, annotations = _u32(data, at, 4)
        at += 16 + 8 * annotations
    techniques = []
    for _ in range(technique_count):
        name, annotations, pass_count = _u32(data, at, 3)
        at += 12 + 8 * annotations
        passes = []
        for _ in range(pass_count):
            pass_name, pass_annotations, state_count = _u32(data, at, 3)
            at += 12 + 8 * pass_annotations
            states = []
            for _ in range(state_count):
                operation, index, _, _ = _u32(data, at, 4)
                at += 16
                states.append((operation, index))
            passes.append((_name(data, base, pass_name), states))
        techniques.append((_name(data, base, name), passes))
    object_count, resource_count = _u32(data, at, 2)
    at += 8
    for _ in range(object_count):
        _, size = _u32(data, at, 2)
        at += 8 + ((size + 3) & ~3)
    resources = {}
    for _ in range(resource_count):
        technique, index, _, state, usage = _u32(data, at, 5)
        (size,) = _u32(data, at + 20)
        at += 24
        if at + size > len(data):
            raise MalformedEffect('resource exceeds effect')
        if technique != 0xffffffff:
            resources[(technique, index, state)] = (usage, at, size)
        at += (size + 3) & ~3
    if at != len(data):
        raise MalformedEffect('trailing bytes after the resource section')
    result = []
    for technique_index, (technique, passes) in enumerate(techniques):
        for pass_index, (pass_name, states) in enumerate(passes):
            shader_states = []
            for state_index, (operation, _) in enumerate(states):
                stage = STATE_STAGES.get(operation)
                if stage is None:
                    continue
                resource = resources.get((technique_index, pass_index, state_index))
                if resource is None:
                    shader_states.append((operation, stage, None, None, 'null'))
                    continue
                usage, start, size = resource
                if usage != 0:
                    shader_states.append((operation, stage, None, None, 'usage_%d' % usage))
                    continue
                words = struct.unpack('<%dI' % (size // 4), data[start:start + size // 4 * 4])
                end = shader_end(words, 0) if words else None
                if end is None:
                    shader_states.append((operation, stage, None, None, 'not_a_program'))
                    continue
                code = data[start:start + 4 * end]
                shader_states.append((operation, stage, fnv1a64(code), end, 'program'))
            result.append({'technique_index': technique_index, 'technique': technique,
                           'pass_index': pass_index, 'pass': pass_name, 'states': shader_states})
    return result


def archive_passes(game):
    """Every pass of every installed numbered-archive effect, derived facts only."""
    passes, effects = [], 0
    for cat in sorted([*game.glob('[0-9][0-9].cat'), *game.glob('addon/[0-9][0-9].cat')]):
        entries = [e for e in read_catalogue(cat)
                   if e['path'].startswith('shader/') and e['path'].endswith('.fb')]
        if not entries:
            continue
        catalogue = str(cat.relative_to(game))
        with cat.with_suffix('.dat').open('rb') as stream:
            for entry in entries:
                stream.seek(entry['offset'])
                data = bytes(value ^ 0x33 for value in stream.read(entry['size']))
                effects += 1
                digest = hashlib.sha256(data).hexdigest()
                for item in parse_effect(data):
                    record = {'catalogue': catalogue, 'path': entry['path'], 'effect_sha256': digest,
                              'technique_index': item['technique_index'], 'technique': item['technique'],
                              'pass_index': item['pass_index'], 'pass': item['pass'],
                              'vs': None, 'ps': None, 'vs_dwords': None, 'ps_dwords': None,
                              'vs_status': 'absent', 'ps_status': 'absent'}
                    for _, stage, fingerprint, dwords, status in item['states']:
                        if record[stage + '_status'] != 'absent':
                            raise MalformedEffect('two %s states in one pass: %s' % (stage, entry['path']))
                        record[stage] = fingerprint
                        record[stage + '_dwords'] = dwords
                        record[stage + '_status'] = status
                    passes.append(record)
    return {'schema': 1,
            'scope': 'Technique/pass shader pairings of the installed compiled effects; '
                     'derived names, hashes and lengths only.',
            'effect_count': effects, 'pass_count': len(passes),
            'status_counts': dict(sorted(Counter('%s:%s' % (stage, p[stage + '_status'])
                                                 for p in passes for stage in ('vs', 'ps')).items())),
            'passes': passes}


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('game', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    arguments = parser.parse_args()
    result = archive_passes(arguments.game)
    arguments.output.write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps({key: value for key, value in result.items() if key != 'passes'}, indent=2))


if __name__ == '__main__':
    main()
