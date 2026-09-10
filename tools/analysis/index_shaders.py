#!/usr/bin/env python3
"""Index embedded D3D9 shader hashes from a user's local compiled effects.

No assets or shader bytecode are written. Candidate token streams are bounded by
version and instruction-boundary END tokens, preserving comments (including CTAB)
for byte-for-byte comparison with IDirect3D*Shader9::GetFunction. Container padding
is excluded. Hash matches still need runtime verification, particularly when an
effect compiler modifies tokens. Multiple effect paths may share one shader.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from inspect_x3 import read_catalogue

FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
# SM1 instruction operand counts; unknown instructions fail closed.
SM1_ARITY = {0: 0, 1: 2, 2: 3, 3: 3, 4: 4, 5: 3, 6: 2, 7: 2,
             8: 3, 9: 3, 10: 3, 11: 3, 12: 3, 13: 3, 14: 2, 15: 2,
             16: 2, 17: 3, 18: 4, 19: 2, 20: 3, 21: 3, 22: 3, 23: 3,
             24: 3, 31: 2, 64: 1, 65: 1, 66: 1, 67: 2, 68: 2, 69: 2,
             70: 2, 71: 2, 72: 2, 73: 2, 74: 2, 76: 3, 77: 2,
             78: 2, 79: 2, 80: 4, 81: 5, 82: 2, 83: 2, 84: 2,
             85: 2, 86: 2, 87: 1, 88: 4, 89: 3, 90: 4, 0xfffd: 0}


def fnv1a64(data):
    value = FNV_OFFSET
    for byte in data:
        value = ((value ^ byte) * FNV_PRIME) & 0xffffffffffffffff
    return f'{value:016x}'


def shader_end(words, start):
    """Return exclusive DWORD end for SM1-3; None for a malformed candidate."""
    version = words[start]
    major, minor = (version >> 8) & 255, version & 255
    if version >> 16 not in (0xfffe, 0xffff) or not (1 <= major <= 3):
        return None
    if (major == 1 and minor > 4) or (major > 1 and minor != 0):
        return None
    pos = start + 1
    while pos < len(words):
        token = words[pos]
        opcode = token & 65535
        if token == 0x0000ffff:
            return pos + 1
        if token & 0x80000000:
            return None
        if opcode == 0xfffe:
            count = (token >> 16) & 32767
        elif major == 1:
            count = SM1_ARITY.get(opcode)
            if count is None:
                return None
            if minor == 4 and opcode in (64, 66):
                count = 2
        else:
            if opcode > 96:
                return None
            count = (token >> 24) & 15
        pos += count + 1
    return None


def embedded_shaders(data):
    words = struct.unpack('<' + 'I' * (len(data) // 4), data[:len(data) // 4 * 4])
    pos = 0
    while pos < len(words):
        end = shader_end(words, pos)
        if end is None:
            pos += 1
            continue
        code = data[pos * 4:end * 4]
        version = words[pos]
        yield dict(offset=pos * 4, bytes=len(code),
                   stage='ps' if version >> 16 == 0xffff else 'vs',
                   model=f'{(version >> 8) & 255}_{version & 255}',
                   fnv1a64=fnv1a64(code), sha256=hashlib.sha256(code).hexdigest())
        pos = end


def index(root):
    results = []
    for cat in sorted([*root.glob('[0-9][0-9].cat'), *root.glob('addon/[0-9][0-9].cat')]):
        entries = [e for e in read_catalogue(cat) if e['path'].startswith('shader/') and e['path'].endswith('.fb')]
        if not entries:
            continue
        with cat.with_suffix('.dat').open('rb') as stream:
            for entry in entries:
                stream.seek(entry['offset'])
                data = bytes(value ^ 0x33 for value in stream.read(entry['size']))
                if data[:4] != b'\x01\x09\xff\xfe':
                    raise ValueError(f"Unexpected compiled effect magic: {entry['path']}")
                results.append(dict(catalogue=str(cat.relative_to(root)), path=entry['path'],
                                    effect_sha256=hashlib.sha256(data).hexdigest(),
                                    shaders=list(embedded_shaders(data))))
    return dict(hash_algorithm='FNV-1a 64-bit over complete token stream including version, comments and END',
                runtime_match_verified=False, effects=results)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = index(args.game)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f"Indexed {len(result['effects'])} effects; "
          f"{sum(len(e['shaders']) for e in result['effects'])} embedded streams")


if __name__ == '__main__':
    main()
