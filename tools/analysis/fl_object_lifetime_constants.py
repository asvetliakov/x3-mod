#!/usr/bin/env python3
"""X3FL.exe render-registry observer constants for src/engine/fl/object_lifetime_fl.cpp.

Reads X3: Farnham's Legacy's X3FL.exe and writes the generated fragment
src/engine/fl/object_lifetime_fl_inc.h (docs/architecture/farnhams-legacy-lifetime.md,
docs/reverse-engineering/fl/object-lifetimes.md). Decoded with Capstone as whole instructions:

  insert entry   push ebx; mov ebx,edx; push edi; mov edi,ecx      ECX map, EDX key, [esp+4] value; plain RET
  remove entry   push esi; push edi; mov edi,ecx; mov esi,edx      ECX map, EDX key, no stack argument; plain RET
  destroy entry  push ebx; mov ebx,ecx; cmp dword [ebx],0          ECX map; the next instruction is JE (ZF live)
  load call      lea ecx,[ebp-0x70]; call <renderer load>; test eax,eax   ECX stream, no stack argument

Each entry span must decode to whole instructions that end exactly at its length, contain no relative or
absolute operand and no base relocation; no branch of its own function and no E8/E9/0F 8x rel32 in .text may
target the span's interior; every RET of the function is a plain C3. Engine anchors tie the registry to the
three helpers: the camera destructor's removal (mov ecx,[engine]; mov edx,[ebx+0x28]; mov ecx,[ecx+0xc];
call remove), the engine teardown (mov esi,[engine]; mov ecx,[esi+0xc]; test; je; call destroy) and the node
birth (mov ecx,[engine]; mov edx,edi; mov ecx,[ecx+0xc]; call allocate_key) with allocate_key's own insert
call (push ebx; mov edx,esi; call insert; add esp,4). Each anchor's only relocated word is its engine
operand. Every value is an RVA or an offset; nothing is 0x400000 based.

usage: fl_object_lifetime_constants.py [--exe PATH] [--output PATH] [--check] [--json]
  --check  exit 1 when the fragment on disk differs from the generated text
  --json   print the decoded constants and exit (verification/analysis/test_fl_object_lifetime.py)
"""
import argparse
import hashlib
import importlib.util
import json
import struct
import sys
from pathlib import Path

import capstone

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = Path('/mnt/c/X3FL/X3FL.exe')
DEFAULT_OUTPUT = ROOT / 'src/engine/fl/object_lifetime_fl_inc.h'
_spec = importlib.util.spec_from_file_location('fl_identity_constants', ROOT / 'tools/analysis/fl_identity_constants.py')
identity = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(identity)

ENGINE_SLOT_RVA = 0x1aa100  # docs/reverse-engineering/fl/object-lifetimes.md: the engine pointer
REGISTRY_OFFSET = 0xc
NODE_HANDLE = 0x28
# (name, entry RVA, expected entry bytes, function end RVA (exclusive, the INT3 padding), stack bytes read)
ENTRIES = (
    ('insert', 0x10cf30, bytes.fromhex('538bda578bf9'), 0x10cfd3),
    ('remove', 0x10d040, bytes.fromhex('56578bf98bf2'), 0x10d0a9),
    ('destroy', 0x10d120, bytes.fromhex('538bd9833b00'), 0x10d1a5),
)
ALLOCATE_KEY_RVA = 0x10cfe0
LOAD_ANCHOR_RVA = 0x079849   # lea ecx,[ebp-0x70]; call <load>; test eax,eax
LOAD_SITE_OFFSET = 3         # the call rel32 inside the anchor
LOAD_SITE_RVA = LOAD_ANCHOR_RVA + LOAD_SITE_OFFSET
LOAD_TARGET_RVA = 0x093510
LOAD_TARGET_ENTRY = bytes.fromhex('558bec83e4f0')  # push ebp; mov ebp,esp; and esp,-16 (AND kills the flags)
LOAD_TARGET_RETURNS = (0x093571, 0x094150, 0x09416b)  # object-lifetimes.md "Load and foreign-unwind boundary"
# (name, RVA, bytes with the engine disp32 as None, engine operand offset, (rel32 offset, target RVA) or None)
ANCHORS = (
    ('remove_anchor', 0x0a489b, [0x8b, 0x0d, None, None, None, None, 0x8b, 0x53, NODE_HANDLE, 0x8b, 0x49,
                                 REGISTRY_OFFSET, 0xe8, None, None, None, None], 2, (13, 0x10d040)),
    ('destroy_anchor', 0x08aba0, [0x8b, 0x35, None, None, None, None, 0x8b, 0x4e, REGISTRY_OFFSET, 0x85, 0xc9,
                                  0x74, 0x0b, 0xe8, None, None, None, None], 2, (14, 0x10d120)),
    ('birth_anchor', 0x0a2290, [0x8b, 0x0d, None, None, None, None, 0x8b, 0xd7, 0x8b, 0x49, REGISTRY_OFFSET,
                                0xe8, None, None, None, None], 2, (12, ALLOCATE_KEY_RVA)),
    ('allocate_insert_anchor', 0x10d01e, [0x53, 0x8b, 0xd6, 0xe8, None, None, None, None, 0x83, 0xc4, 0x04], None,
     (4, 0x10cf30)),
)

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True


def read(data, fields, rva, n):
    off = identity.rva_to_offset(fields, rva)
    return bytes(data[off:off + n])


def touching(relocated, rva, n):
    return [r for r in relocated if r < rva + n and rva < r + 4]


def instructions(data, fields, rva, end):
    out = list(md.disasm(read(data, fields, rva, end - rva), rva))
    if sum(i.size for i in out) != end - rva:
        raise ValueError(f'0x{rva:x}..0x{end:x} does not decode to whole instructions')
    return out


def branch_target(insn):
    if insn.group(capstone.CS_GRP_JUMP) or insn.group(capstone.CS_GRP_CALL):
        op = insn.operands[0] if insn.operands else None
        if op is not None and op.type == capstone.x86.X86_OP_IMM:
            return op.imm
    return None


def text_rel32_targets(data, fields, lo, hi):
    """Every E8/E9/0F 8x rel32 in .text that lands in [lo, hi) (a byte scan: conservative, may over-count)."""
    text = next(s for s in fields['sections'] if s[0].rstrip(b'\0') == b'.text')
    _, vsize, va, raw_size, raw_ptr, _ = text
    out = []
    end = raw_ptr + min(vsize, raw_size)
    for off in range(raw_ptr, end - 5):
        b = data[off]
        if b in (0xe8, 0xe9):
            field = off + 1
        elif b == 0x0f and 0x80 <= data[off + 1] <= 0x8f:
            field = off + 2
        else:
            continue
        if field + 4 > end:
            continue
        rva = field - raw_ptr + va
        target = (rva + 4 + struct.unpack_from('<i', data, field)[0]) & 0xffffffff
        if lo <= target < hi:
            out.append(field - 1 - (1 if b == 0x0f else 0) - raw_ptr + va)
    return out


def direct_callers(data, fields, target_rva):
    text = next(s for s in fields['sections'] if s[0].rstrip(b'\0') == b'.text')
    _, vsize, va, raw_size, raw_ptr, _ = text
    out = []
    for off in range(raw_ptr, raw_ptr + min(vsize, raw_size) - 4):
        if data[off] == 0xe8:
            rva = off - raw_ptr + va
            if (rva + 5 + struct.unpack_from('<i', data, off + 1)[0]) & 0xffffffff == target_rva:
                out.append(rva)
    return out


def check_entry(data, fields, relocated, name, rva, expected, end):
    entry = read(data, fields, rva, len(expected))
    if entry != expected:
        raise ValueError(f'{name} entry at 0x{rva:x} is {entry.hex()}, documented {expected.hex()}')
    body = instructions(data, fields, rva, end)
    edges, length = [], 0
    for i in body:
        if i.address < rva + len(expected):
            length += i.size
            if branch_target(i) is not None or any(op.type == capstone.x86.X86_OP_MEM and op.mem.base == 0
                                                   for op in i.operands):
                raise ValueError(f'{name}: displaced {i.mnemonic} {i.op_str} has a relative or absolute operand')
        target = branch_target(i)
        if target is not None and rva < target < rva + len(expected):
            raise ValueError(f'{name}: branch at 0x{i.address:x} into the displaced span')
        if i.mnemonic == 'ret' and i.bytes != b'\xc3':
            raise ValueError(f'{name}: {i.bytes.hex()} at 0x{i.address:x} is not a plain RET')
        if i.mnemonic == 'ret':
            edges.append(i.address)
    if length != len(expected):
        raise ValueError(f'{name}: the displaced span ends inside an instruction')
    if touching(relocated, rva, len(expected)):
        raise ValueError(f'{name}: a base relocation touches the entry')
    interior = text_rel32_targets(data, fields, rva + 1, rva + len(expected))
    if interior:
        raise ValueError(f'{name}: rel32 into the displaced span from {[hex(x) for x in interior]}')
    if rva % 8 + 5 > 8:
        raise ValueError(f'{name}: the five patched bytes straddle a qword')
    return body, edges


def check_anchor(data, fields, relocated, name, rva, shape, operand, call):
    span = read(data, fields, rva, len(shape))
    holes = {i for i, b in enumerate(shape) if b is None}
    if any(b is not None and span[i] != b for i, b in enumerate(shape)):
        raise ValueError(f'{name} at 0x{rva:x} is {span.hex()}')
    instructions(data, fields, rva, rva + len(shape))
    masked = bytearray(span)
    if operand is not None:
        slot = struct.unpack_from('<I', span, operand)[0] - fields['image_base']
        if slot != ENGINE_SLOT_RVA:
            raise ValueError(f'{name}: engine operand is slot RVA 0x{slot:x}')
        if touching(relocated, rva, len(shape)) != [rva + operand]:
            raise ValueError(f'{name}: not relocated exactly at its engine operand')
        masked[operand:operand + 4] = b'\0\0\0\0'
    elif touching(relocated, rva, len(shape)):
        raise ValueError(f'{name}: a base relocation touches the anchor')
    at, target = call
    if (rva + at + 4 + struct.unpack_from('<i', span, at)[0]) & 0xffffffff != target:
        raise ValueError(f'{name}: the call does not reach 0x{target:x}')
    covered = set(range(operand, operand + 4)) if operand is not None else set()
    if holes - covered - set(range(at, at + 4)):
        raise ValueError(f'{name}: an undeclared hole')
    return bytes(masked)


def decode(data, fields):
    relocated = identity.relocated_rvas(data, fields)
    out = {'engine_slot_rva': ENGINE_SLOT_RVA, 'registry_offset': REGISTRY_OFFSET, 'node_handle': NODE_HANDLE}
    callers = {}
    for name, rva, expected, end in ENTRIES:
        body, returns = check_entry(data, fields, relocated, name, rva, expected, end)
        out[f'{name}_rva'] = rva
        out[f'{name}_entry'] = expected
        out[f'{name}_returns'] = returns
        callers[name] = direct_callers(data, fields, rva)
        stack_reads = [i for i in body if any(op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_ESP
                                              for op in i.operands)]
        out[f'{name}_stack_reads'] = [(i.address, i.op_str) for i in stack_reads]
        if name == 'destroy':
            following = body[3]
            if following.address != rva + len(expected) or following.mnemonic != 'je':
                raise ValueError('destroy: the instruction after the displaced CMP is not JE')
            out['destroy_continuation'] = bytes(following.bytes)
    # Insert reads its one stack argument at [esp+0x10] after three pushes (entry [esp+4]); remove and destroy read none.
    insert_reads = {op for _, op in out['insert_stack_reads']}
    if insert_reads != {'eax, dword ptr [esp + 0x10]', 'ecx, dword ptr [esp + 0x10]'}:
        raise ValueError(f'insert stack reads {insert_reads}')
    if out['remove_stack_reads'] or out['destroy_stack_reads']:
        raise ValueError('remove/destroy read the stack')
    anchor = read(data, fields, LOAD_ANCHOR_RVA, 10)
    if anchor[:3] != b'\x8d\x4d\x90' or anchor[3] != 0xe8 or anchor[8:] != b'\x85\xc0':
        raise ValueError(f'load anchor at 0x{LOAD_ANCHOR_RVA:x} is {anchor.hex()}')
    target = (LOAD_SITE_RVA + 5 + struct.unpack_from('<i', anchor, 4)[0]) & 0xffffffff
    if target != LOAD_TARGET_RVA:
        raise ValueError(f'the load site calls 0x{target:x}')
    if touching(relocated, LOAD_ANCHOR_RVA, 10) or touching(relocated, LOAD_TARGET_RVA, len(LOAD_TARGET_ENTRY)):
        raise ValueError('a base relocation touches the load site or the load target entry')
    if read(data, fields, LOAD_TARGET_RVA, len(LOAD_TARGET_ENTRY)) != LOAD_TARGET_ENTRY:
        raise ValueError('load target entry differs')
    for ret in LOAD_TARGET_RETURNS:
        if read(data, fields, ret, 1) != b'\xc3':
            raise ValueError(f'load target return at 0x{ret:x} is not a plain RET')
    callers['load_target'] = direct_callers(data, fields, LOAD_TARGET_RVA)
    if callers['load_target'] != [LOAD_SITE_RVA]:
        raise ValueError(f'direct callers of the load target: {[hex(c) for c in callers["load_target"]]}')
    out.update(load_anchor_rva=LOAD_ANCHOR_RVA, load_site_offset=LOAD_SITE_OFFSET, load_site_rva=LOAD_SITE_RVA,
               load_target_rva=LOAD_TARGET_RVA, load_anchor=anchor, load_target_entry=LOAD_TARGET_ENTRY,
               load_site_qword_offset=LOAD_SITE_RVA % 8)
    out['anchors'] = {}
    for name, rva, shape, operand, call in ANCHORS:
        out['anchors'][name] = (rva, check_anchor(data, fields, relocated, name, rva, shape, operand, call), operand)
    out['callers'] = {k: len(v) for k, v in callers.items()}
    return out


def hexes(b):
    return ', '.join(f'0x{x:02x}' for x in b)


def fragment(data, fields):
    d = decode(data, fields)
    lines = [
        '// Generated by tools/analysis/fl_object_lifetime_constants.py from X3FL.exe; do not edit.',
        f'// X3FL.exe SHA-256 {hashlib.sha256(data).hexdigest()} (provenance, never a gate).',
        '// Every value is an RVA (added to fl::module_base()) or an offset; nothing is 0x400000 based. The three entry',
        '// spans are whole instructions with no relative/absolute operand, no base relocation and no branch into their',
        '// interior; every RET of their functions is a plain C3. Anchors: the engine disp32 is the only relocated word.',
        '// clang-format off',
        f'constexpr std::uint32_t engine_slot_rva = 0x{d["engine_slot_rva"]:08x}, registry_offset = 0x{d["registry_offset"]:x}, '
        f'node_handle = 0x{d["node_handle"]:x};',
        '// push ebx; mov ebx,edx; push edi; mov edi,ecx (ECX map, EDX key, [esp+4] value; caller cleans)',
        f'constexpr std::uint32_t insert_rva = 0x{d["insert_rva"]:08x};',
        f'constexpr std::uint8_t insert_entry[6] = {{{hexes(d["insert_entry"])}}};',
        '// push esi; push edi; mov edi,ecx; mov esi,edx (ECX map, EDX key, no stack argument)',
        f'constexpr std::uint32_t remove_rva = 0x{d["remove_rva"]:08x};',
        f'constexpr std::uint8_t remove_entry[6] = {{{hexes(d["remove_entry"])}}};',
        '// push ebx; mov ebx,ecx; cmp dword [ebx],0 (ECX map); followed by JE: the replayed CMP\'s ZF is live',
        f'constexpr std::uint32_t destroy_rva = 0x{d["destroy_rva"]:08x};',
        f'constexpr std::uint8_t destroy_entry[6] = {{{hexes(d["destroy_entry"])}}};',
        '// lea ecx,[ebp-0x70]; call <renderer load>; test eax,eax (the load target\'s only direct caller)',
        f'constexpr std::uint32_t load_anchor_rva = 0x{d["load_anchor_rva"]:08x}, load_site_offset = {d["load_site_offset"]}, '
        f'load_target_rva = 0x{d["load_target_rva"]:08x};',
        f'constexpr std::uint8_t load_anchor[10] = {{{hexes(d["load_anchor"])}}};',
        '// push ebp; mov ebp,esp; and esp,-16',
        f'constexpr std::uint8_t load_target_entry[6] = {{{hexes(d["load_target_entry"])}}};',
        '// Engine anchors, engine disp32 zeroed (engine_operand = 0xff: none); each ends in a call to its helper.',
    ]
    for name, (rva, masked, operand) in d['anchors'].items():
        lines.append(f'constexpr std::uint32_t {name}_rva = 0x{rva:08x}, {name}_operand = '
                     f'{operand if operand is not None else "0xff"};')
        lines.append(f'constexpr std::uint8_t {name}[{len(masked)}] = {{{hexes(masked)}}};')
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
        conv = lambda v: v.hex() if isinstance(v, bytes) else v
        out = {k: (conv(v) if not isinstance(v, dict) else {a: [conv(x) for x in b] if isinstance(b, tuple) else b
                                                            for a, b in v.items()}) for k, v in d.items()}
        print(json.dumps({'sha256': hashlib.sha256(data).hexdigest(), 'image_base': fields['image_base'], **out}))
        return 0
    text = fragment(data, fields)
    if args.check:
        same = args.output.exists() and args.output.read_text() == text
        print('fl_object_lifetime_constants', 'match' if same else 'differs', args.output)
        return 0 if same else 1
    args.output.write_text(text)
    print('wrote', args.output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
