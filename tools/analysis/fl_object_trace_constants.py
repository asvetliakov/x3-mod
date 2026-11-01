#!/usr/bin/env python3
"""X3FL.exe material-submission seam constants for src/engine/fl/object_trace_fl.cpp.

Reads X3: Farnham's Legacy's X3FL.exe and writes the generated fragment
src/engine/fl/object_trace_fl_inc.h (docs/architecture/farnhams-legacy-object-identity.md,
docs/reverse-engineering/fl/material-submit.md). Decoded as whole instructions:

  hook site     call <material target>; add esp,0x10   the only direct caller of the target; the
                                                        caller removes the four stack words
  target entry  push ebx; mov ebx,esp; sub esp,8        the stack-realigning prologue (the SUB kills the
                                                        incoming arithmetic flags)
  engine slot   mov ecx,[slot]; mov edx,edi; mov ecx,[ecx+0xc]
                                                        the node allocator's registry selection; the
                                                        disp32 is the only relocated word

No base relocation may touch the site or the target entry (a rel32 CALL is not relocated), and
exactly one touches the engine-slot operand. The node and camera field offsets the provider reads
are each derived from a native load or store operand (named below) and must equal the documented
FL layout (model +0x144 and LOD +0x150, not AP's +0x140/+0x14c). Every value is an RVA or an
offset; nothing is 0x400000 based.

usage: fl_object_trace_constants.py [--exe PATH] [--output PATH] [--check] [--json]
  --check  exit 1 when the fragment on disk differs from the generated text
  --json   print the decoded constants and exit (verification/analysis/test_fl_object_trace.py)
"""
import argparse
import hashlib
import importlib.util
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = Path('/mnt/c/X3FL/X3FL.exe')
DEFAULT_OUTPUT = ROOT / 'src/engine/fl/object_trace_fl_inc.h'
_spec = importlib.util.spec_from_file_location('fl_identity_constants', ROOT / 'tools/analysis/fl_identity_constants.py')
identity = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(identity)

SITE_RVA = 0x0e7142     # material-submit.md section 1: the convergence CALL 0x4e7142
TARGET_RVA = 0x0e0f30   # the material target 0x4e0f30
WRAPPER_RVA = 0x0e6ef0  # the wrapper 0x4e6ef0 holding the site; its two direct callers (census only)
WRAPPER_CALLERS = (0x097035, 0x097702)
CLEANUP = b'\x83\xc4\x10'                  # add esp,0x10
TARGET_ENTRY = b'\x53\x8b\xdc\x83\xec\x08'  # push ebx; mov ebx,esp; sub esp,8
ENGINE_ANCHOR_RVA = 0x0a2290  # node allocator: mov ecx,[engine]; mov edx,edi; mov ecx,[ecx+0xc]
ENGINE_SLOT_RVA = 0x1aa100    # scene-graph-census.md / object-lifetimes.md: the engine pointer
ENGINE_OPERAND_OFFSET = 2
ENGINE_LENGTH = 11
REGISTRY_OFFSET = 0xc
# (constant name, documented offset, operand RVA, instruction bytes with the offset field as None)
# Each operand is a native load or store of that field (material-submit.md section 2).
FIELDS = (
    ('node_parent', 0x18, 0x0a5809, (0x89, 0x6f, None)),                                  # reparent: mov [edi+0x18],ebp
    ('node_handle', 0x28, 0x0a22a8, (0x89, 0x47, None)),                                  # allocator: mov [edi+0x28],eax
    ('camera_handle', 0x28, 0x0a4606, (0x89, 0x46, None)),                                # camera allocator: mov [esi+0x28],eax
    ('node_scale', 0x70, 0x0de59a, (0xf7, 0x6f, None)),                                   # world matrix: imul [edi+0x70]
    ('node_scale_axes', 0x80, 0x0de973, (0xf7, 0xaf, None, None, None, None)),            # imul [edi+0x80]
    ('node_position', 0xb0, 0x0deb15, (0x66, 0x0f, 0x6e, 0x87, None, None, None, None)),  # movd xmm0,[edi+0xb0]
    ('node_basis', 0xc0, 0x0de9ef, (0x66, 0x0f, 0x6e, 0x87, None, None, None, None)),     # movd xmm0,[edi+0xc0]
    ('node_flags12c', 0x12c, 0x0e0f87, (0x8b, 0x82, None, None, None, None)),             # target: mov eax,[edx+0x12c]
    ('node_flags130', 0x130, 0x0de5af, (0xf7, 0x87, None, None, None, None, 0x00, 0x02, 0x00, 0x00)),
    ('node_model', 0x144, 0x0968e1, (0x8b, 0x8e, None, None, None, None)),                # traversal: mov ecx,[esi+0x144]
    ('node_lod', 0x150, 0x096f96, (0x8b, 0x8e, None, None, None, None)),                  # traversal: mov ecx,[esi+0x150]
)
# Birth initialisers that confirm the two FL-specific offsets (model -1, LOD 0).
BIRTH = (('node_model', 0x0a22cc, b'\xc7\x87\x44\x01\x00\x00\xff\xff\xff\xff'),
         ('node_lod', 0x0a230f, b'\xc7\x87\x50\x01\x00\x00\x00\x00\x00\x00'))


def read(data, fields, rva, n):
    off = identity.rva_to_offset(fields, rva)
    return data[off:off + n]


def touching(relocated, rva, n):
    return [r for r in relocated if r < rva + n and rva < r + 4]


def direct_callers(data, fields, target_rva):
    """Every E8 rel32 in .text that resolves to target_rva (a byte scan: conservative, may over-count)."""
    text = next(s for s in fields['sections'] if s[0].rstrip(b'\0') == b'.text')
    _, vsize, va, raw_size, raw_ptr, _ = text
    out = []
    for off in range(raw_ptr, raw_ptr + min(vsize, raw_size) - 4):
        if data[off] == 0xe8:
            rva = off - raw_ptr + va
            if (rva + 5 + struct.unpack_from('<i', data, off + 1)[0]) & 0xffffffff == target_rva:
                out.append(rva)
    return out


def field_offset(data, fields, name, documented, rva, shape):
    span = read(data, fields, rva, len(shape))
    holes = [i for i, b in enumerate(shape) if b is None]
    if any(b is not None and span[i] != b for i, b in enumerate(shape)):
        raise ValueError(f'{name}: the instruction at 0x{rva:x} is {span.hex()}, not the documented shape')
    raw = bytes(span[i] for i in holes)
    value = raw[0] if len(raw) == 1 else struct.unpack('<i', raw)[0]
    if value != documented:
        raise ValueError(f'{name}: operand at 0x{rva:x} is +0x{value:x}, documented +0x{documented:x}')
    return value


def decode(data, fields):
    relocated = identity.relocated_rvas(data, fields)
    site = read(data, fields, SITE_RVA, 8)
    if site[0] != 0xe8:
        raise ValueError(f'no call rel32 at 0x{SITE_RVA:x}')
    target = (SITE_RVA + 5 + struct.unpack_from('<i', site, 1)[0]) & 0xffffffff
    if target != TARGET_RVA:
        raise ValueError(f'the site calls 0x{target:x}, documented 0x{TARGET_RVA:x}')
    if site[5:8] != CLEANUP:
        raise ValueError(f'no `add esp,0x10` after the site: {site[5:8].hex()}')
    entry = read(data, fields, TARGET_RVA, len(TARGET_ENTRY))
    if entry != TARGET_ENTRY:
        raise ValueError(f'target entry is {entry.hex()}')
    if touching(relocated, SITE_RVA, len(site)) or touching(relocated, TARGET_RVA, len(entry)):
        raise ValueError('a base relocation touches the site or the target entry')
    callers = direct_callers(data, fields, TARGET_RVA)
    if callers != [SITE_RVA]:
        raise ValueError(f'direct callers of the target: {[hex(c) for c in callers]}')
    wrapper_callers = direct_callers(data, fields, WRAPPER_RVA)
    if tuple(wrapper_callers) != WRAPPER_CALLERS:
        raise ValueError(f'direct callers of the wrapper: {[hex(c) for c in wrapper_callers]}')
    engine = read(data, fields, ENGINE_ANCHOR_RVA, ENGINE_LENGTH)
    if engine[0:2] != b'\x8b\x0d' or engine[6:11] != b'\x8b\xd7\x8b\x49' + bytes([REGISTRY_OFFSET]):
        raise ValueError(f'engine anchor at 0x{ENGINE_ANCHOR_RVA:x} is {engine.hex()}')
    slot = struct.unpack_from('<I', engine, ENGINE_OPERAND_OFFSET)[0] - fields['image_base']
    if slot != ENGINE_SLOT_RVA:
        raise ValueError(f'engine operand is slot RVA 0x{slot:x}, documented 0x{ENGINE_SLOT_RVA:x}')
    data_section = next(s for s in fields['sections'] if s[0].rstrip(b'\0') == b'.data')
    if not data_section[2] <= slot < data_section[2] + data_section[1] - 4:
        raise ValueError(f'engine slot 0x{slot:x} outside .data')
    if touching(relocated, ENGINE_ANCHOR_RVA, ENGINE_LENGTH) != [ENGINE_ANCHOR_RVA + ENGINE_OPERAND_OFFSET]:
        raise ValueError('the engine anchor is not relocated exactly at its operand')
    masked = bytearray(engine)
    masked[ENGINE_OPERAND_OFFSET:ENGINE_OPERAND_OFFSET + 4] = b'\0\0\0\0'
    offsets = {name: field_offset(data, fields, name, documented, rva, shape) for name, documented, rva, shape in FIELDS}
    for name, rva, expected in BIRTH:
        if read(data, fields, rva, len(expected)) != expected:
            raise ValueError(f'{name}: birth initialiser at 0x{rva:x} differs')
    return {'site_rva': SITE_RVA, 'target_rva': TARGET_RVA, 'site_bytes': bytes(site), 'target_entry': bytes(entry),
            'engine_anchor_rva': ENGINE_ANCHOR_RVA, 'engine_slot_rva': slot, 'engine_operand_offset': ENGINE_OPERAND_OFFSET,
            'engine_bytes': bytes(masked), 'engine_file_bytes': bytes(engine), 'registry_offset': REGISTRY_OFFSET,
            'offsets': offsets, 'target_callers': callers, 'wrapper_callers': wrapper_callers}


def hexes(b):
    return ', '.join(f'0x{x:02x}' for x in b)


def fragment(data, fields):
    d = decode(data, fields)
    lines = [
        '// Generated by tools/analysis/fl_object_trace_constants.py from X3FL.exe; do not edit.',
        f'// X3FL.exe SHA-256 {hashlib.sha256(data).hexdigest()} (provenance, never a gate).',
        '// Every value is an RVA (added to fl::module_base()) or a field offset; nothing is 0x400000 based.',
        '// The site is the target\'s only direct caller (E8 census); no base relocation touches the site or the target',
        '// entry. The engine anchor\'s disp32 is its only relocated word and reads module_base() + engine_slot_rva.',
        '// clang-format off',
        f'constexpr std::uint32_t site_rva = 0x{d["site_rva"]:08x}, target_rva = 0x{d["target_rva"]:08x};',
        '// call rel32 <target>; add esp,0x10 (the caller removes camera, flags, light1, light2)',
        f'constexpr std::uint8_t site_bytes[{len(d["site_bytes"])}] = {{{hexes(d["site_bytes"])}}};',
        '// push ebx; mov ebx,esp; sub esp,8',
        f'constexpr std::uint8_t target_entry[{len(d["target_entry"])}] = {{{hexes(d["target_entry"])}}};',
        '// mov ecx,[engine]; mov edx,edi; mov ecx,[ecx+0xc] (the node allocator selects the registry), disp32 zeroed',
        f'constexpr std::uint32_t engine_anchor_rva = 0x{d["engine_anchor_rva"]:08x}, engine_slot_rva = 0x{d["engine_slot_rva"]:08x},',
        f'                        engine_operand_offset = {d["engine_operand_offset"]}, registry_offset = 0x{d["registry_offset"]:x};',
        f'constexpr std::uint8_t engine_anchor[{len(d["engine_bytes"])}] = {{{hexes(d["engine_bytes"])}}};',
        '// Field offsets, each read from a native load or store operand (the generator names them).',
    ]
    for name, _, rva, _ in FIELDS:
        lines.append(f'constexpr std::uint32_t {name} = 0x{d["offsets"][name]:x}; // operand at RVA 0x{rva:06x}')
    lines.append('// clang-format on')
    return '\n'.join(lines) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    ap.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--json', action='store_true')
    args = ap.parse_args()
    data = args.exe.read_bytes()
    fields = identity.parse(data)
    if args.json:
        d = decode(data, fields)
        out = {k: (v.hex() if isinstance(v, bytes) else v) for k, v in d.items()}
        print(json.dumps({'sha256': hashlib.sha256(data).hexdigest(), 'image_base': fields['image_base'], **out}))
        return 0
    text = fragment(data, fields)
    if args.check:
        same = args.output.exists() and args.output.read_text() == text
        print('fl_object_trace_constants', 'match' if same else 'differs', args.output)
        return 0 if same else 1
    args.output.write_text(text)
    print('wrote', args.output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
