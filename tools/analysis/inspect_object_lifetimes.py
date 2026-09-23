#!/usr/bin/env python3
"""Read-only fingerprints for reviewed X3AP lifetime callsites; never patches.

Only addresses, short instruction fingerprints and SHA-256 digests are emitted.
The private disassembly supporting each interpretation is documented separately.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'verification' / 'probe'))
import exe_identity  # noqa: E402  structure + anchors gate; the hash is INFO (docs/reverse-engineering/executable-identity.md)

EXPECTED_SHA256 = exe_identity.SHIPPED_SHA256  # provenance only
SITES = (
    ('renderer_scene_deserialize', 0x40508d, 0x47a720),
    ('restore_node_handle', 0x47a6ad, 0x4efbf0),
    ('automatic_handle_insert', 0x4efd09, 0x4efbf0),
    ('ordinary_node_unregister', 0x487d70, 0x4efd30),
    ('camera_unregister', 0x488efd, 0x4efd30),
    ('render_registry_destroy', 0x4712e1, 0x4efe10),
    ('recording_camera_insert', 0x473672, 0x4efbf0),
    ('playback_camera_record_insert', 0x4770d1, 0x4efbf0),
)
CENTRAL_BOUNDARIES = (
    # Exact whole-instruction ranges contain no PC-relative instruction. A
    # synthetic detour/unwind test remains required before any runtime use.
    ('insert_or_replace', 0x4efbf0, 0x4efcc0, 0x4efbf0, '558b6c2408'),
    ('remove_nonempty_map', 0x4efd30, 0x4efda0, 0x4efd39, '8b4f0483e901'),
    ('destroy_map', 0x4efe10, 0x4efeb0, 0x4efe10, '538b5c2408'),
    ('rehash_map', 0x4efeb0, 0x4effa0, 0x4efeb0, '83ec085355'),
)


def inspect(path):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if not exe_identity.identity_ok(data):
        raise ValueError('Executable structure/anchors are not the reviewed X3AP image')
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
    central = []
    for name, entry, end, hook, expected in CENTRAL_BOUNDARIES:
        size = len(expected)//2
        if at(hook, size).hex() != expected:
            raise ValueError(f'Instruction boundary mismatch at {hook:08x}')
        central.append(dict(name=name, function_entry_va=f'{entry:08x}', hook_va=f'{hook:08x}',
            hook_rva=f'{hook-base:08x}', displaced_bytes=expected, resume_va=f'{hook+size:08x}',
            reviewed_region_bytes=end-entry, reviewed_region_sha256=hashlib.sha256(at(entry,end-entry)).hexdigest()))
    return dict(executable=path.name, bytes=len(data), sha256=digest, exe_info=exe_identity.info(data), preferred_base=f'{base:08x}',
                sites=sites, central_boundaries=central,
                limitation='Static callsite/ABI candidates; no live hook or coverage validation.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = inspect(args.executable)
    report['analyzer_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(f"Verified {len(report['sites'])} calls and {len(report['central_boundaries'])} central boundaries -> {args.output}")


if __name__ == '__main__':
    main()
