#!/usr/bin/env python3
"""Read-only fingerprints for reviewed X3AP lifetime callsites; never patches.

Only addresses, five-byte call fingerprints and SHA-256 digests are emitted.
The private disassembly supporting each interpretation is documented separately.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

EXPECTED_SHA256 = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
SITES = (
    ('renderer_scene_deserialize', 0x40508d, 0x47a720),
    ('restore_node_handle', 0x47a6ad, 0x4efbf0),
    ('automatic_handle_insert', 0x4efd09, 0x4efbf0),
    ('ordinary_node_unregister', 0x487d70, 0x4efd30),
    ('camera_unregister', 0x488efd, 0x4efd30),
)


def inspect(path):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if len(data) != 2153984 or digest != EXPECTED_SHA256:
        raise ValueError('Executable size/hash is not the reviewed X3AP version')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[:2] != b'MZ' or data[pe:pe+4] != b'PE\0\0':
        raise ValueError('Invalid PE signatures')
    machine, count = struct.unpack_from('<HH', data, pe+4)
    optional_size = struct.unpack_from('<H', data, pe+20)[0]
    optional = pe+24
    base = struct.unpack_from('<I', data, optional+28)[0]
    if machine != 0x14c or struct.unpack_from('<H', data, optional)[0] != 0x10b or base != 0x400000:
        raise ValueError('Expected x86 PE32 with preferred base 0x400000')
    sections = []
    for index in range(count):
        offset = optional+optional_size+index*40
        virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', data, offset+8)
        sections.append((rva, raw_size, raw))

    def at(va, size):
        rva = va-base
        for start, length, raw in sections:
            if start <= rva and rva+size <= start+length:
                return data[raw+rva-start:raw+rva-start+size]
        raise ValueError(f'VA {va:08x} is not backed by raw section data')

    sites = []
    for name, call, target in SITES:
        instruction = at(call, 5)
        if instruction[0] != 0xe8 or call+5+struct.unpack_from('<i', instruction, 1)[0] != target:
            raise ValueError(f'Call target mismatch at {call:08x}')
        sites.append(dict(name=name, call_va=f'{call:08x}', call_rva=f'{call-base:08x}',
            target_va=f'{target:08x}', call_bytes=instruction.hex(),
            context_va=f'{call-12:08x}', context_bytes=32,
            context_sha256=hashlib.sha256(at(call-12, 32)).hexdigest(),
            target_prefix_bytes=32, target_prefix_sha256=hashlib.sha256(at(target, 32)).hexdigest()))
    return dict(executable=path.name, bytes=len(data), sha256=digest, preferred_base=f'{base:08x}',
                sites=sites, limitation='Static callsite/ABI candidates; no live hook or coverage validation.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = inspect(args.executable)
    report['analyzer_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(f"Verified {len(report['sites'])} read-only callsites -> {args.output}")


if __name__ == '__main__':
    main()
