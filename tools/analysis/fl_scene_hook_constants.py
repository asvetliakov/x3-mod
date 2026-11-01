#!/usr/bin/env python3
"""X3FL.exe scene-end (compositor) seam and compositor-owner constants for src/engine/fl/scene_hook_fl.cpp.

Reads X3: Farnham's Legacy's X3FL.exe and writes src/engine/fl/scene_hook_fl_inc.h
(docs/architecture/farnhams-legacy-scene.md; docs/reverse-engineering/fl/compositor.md section 3,
fl/compositor-owner.md sections 1 and 4). Decoded with capstone as whole instructions:

  boundary window  cmp dword [esi+0x29c],0x11; jle +9; call <compositor>; mov byte [ebp-0xd],1;
                   mov ecx,[esi+0x270]       24 bytes inside the complete pass (RVA 0x8b6d0): the call runs
                                             once per pass, before the first camera above layer 17; the jle and
                                             the done-byte jne at 0x8b91e both land on 0x8b932, after the call
  compositor       push ebp; lea ebp,[esp-0x38]; mov eax,fs:[0]; sub esp,0x38
                                             its body [0xe6770, 0xe6e87) is one contiguous procedure: every
                                             RET a bare c3, no jump leaves it, no operand reaches a caller
                                             stack word ([ebp+d] with d >= 0x3c is the return address and
                                             above, [esp+d] with d >= 0x40), so it takes no stack argument
  glow anchor      mov eax,[config]; test byte [eax+0xf0],0x80; je <early out>
  owner anchor     mov eax,[renderer]; push ebx; mov ebx,[eax+0x18]     renderer -> device record
  device anchor    mov ebx,[ebx]; xor edx,edx                           record[0] -> IDirect3DDevice9
  camera anchor    the ordinary camera activation's same chain (0xded96/0xded9e/0xdeda8; census only)

No base relocation may touch the window or the compositor entry; each anchor is relocated exactly at its
disp32. The compositor's only direct caller is the window's call, no relocated word points at the compositor
or into the window. Every value is an RVA or an offset; nothing is 0x400000 based.

usage: fl_scene_hook_constants.py [--exe PATH] [--output PATH] [--check] [--json]
  --check  exit 1 when the fragment on disk differs from the generated text
  --json   print the decoded constants and exit (verification/analysis/test_fl_scene_hook.py)
"""
import argparse
import hashlib
import importlib.util
import json
import struct
import sys
from pathlib import Path

import capstone
from capstone import x86

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = Path('/mnt/c/X3FL/X3FL.exe')
DEFAULT_OUTPUT = ROOT / 'src/engine/fl/scene_hook_fl_inc.h'
_spec = importlib.util.spec_from_file_location('fl_identity_constants', ROOT / 'tools/analysis/fl_identity_constants.py')
identity = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(identity)

PASS_RVA = 0x08b6d0      # complete pass (compositor.md section 1); its direct caller (census only)
PASS_CALLERS = (0x0782cf,)
WINDOW_RVA = 0x08b920
SITE_RVA = 0x08b929      # call rel32 <compositor>, offset 9 in the window
TARGET_RVA = 0x0e6770    # compositor
TARGET_END = 0x0e6e87    # one past its last RET
CONTINUATION_RVA = 0x08b932  # the jle/jne target after the done-byte write
# The documented 24-byte check window (fl-codex profile), decoded below instruction by instruction.
WINDOW = bytes.fromhex('83be9c020000117e09e842ae0500c645f3018b8e70020000')
WINDOW_SHAPE = (('cmp', 'dword ptr [esi + 0x29c], 0x11'), ('jle', hex(CONTINUATION_RVA)), ('call', hex(TARGET_RVA)),
                ('mov', 'byte ptr [ebp - 0xd], 1'), ('mov', 'ecx, dword ptr [esi + 0x270]'))
# The done byte: reset before the sorted loop, tested at the loop head (the call runs at most once per pass).
DONE_RESET = (0x08b8eb, bytes.fromhex('8845f3'))        # mov [ebp-0xd],al (al = 0)
DONE_TEST = (0x08b917, bytes.fromhex('807df300'))       # cmp byte [ebp-0xd],0
DONE_SKIP = (0x08b91e, bytes.fromhex('7512'))           # jne 0x8b932
TARGET_ENTRY = bytes.fromhex('558d6c24c864a10000000083ec38')  # push ebp; lea ebp,[esp-0x38]; mov eax,fs:[0]; sub esp,0x38
FRAME_BELOW_ENTRY = 0x3c  # ebp = entry esp - 4 - 0x38: [ebp+0x3c] is the return address
GLOW_RVA, GLOW_LENGTH, GLOW_OPERAND = 0x0e6790, 18, 1    # a1 <cfg>; f6 80 f0 00 00 00 80; 0f 84 rel32
CONFIG_SLOT_RVA, GLOW_FLAG_OFFSET, GLOW_FLAG_MASK = 0x1a83b0, 0xf0, 0x80
OWNER_RVA, OWNER_LENGTH, OWNER_OPERAND = 0x0e67a2, 9, 1  # a1 <renderer>; 53; 8b 58 18
RENDERER_SLOT_RVA, RECORD_OFFSET = 0x1aaf94, 0x18
DEVICE_RVA = 0x0e67ec                                     # 8b 1b; 33 d2
DEVICE_ANCHOR = bytes.fromhex('8b1b33d2')
DEVICE_OFFSET = 0
CAMERA_ROOT_RVA, CAMERA_RECORD_RVA, CAMERA_DEVICE_RVA = 0x0ded96, 0x0ded9e, 0x0deda8  # compositor-owner.md section 1


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


def disasm(data, fields, lo, hi):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    return list(md.disasm(read(data, fields, lo, hi - lo), lo))


def slot_anchor(data, fields, relocated, rva, length, operand, slot_rva, name):
    """The anchor's bytes with its disp32 (the only relocated word) zeroed; the disp32 names slot_rva in .data."""
    span = read(data, fields, rva, length)
    if touching(relocated, rva, length) != [rva + operand]:
        raise ValueError(f'{name}: the anchor at 0x{rva:x} is not relocated exactly at its operand')
    slot = struct.unpack_from('<I', span, operand)[0] - fields['image_base']
    if slot != slot_rva:
        raise ValueError(f'{name}: operand names RVA 0x{slot:x}, documented 0x{slot_rva:x}')
    data_section = next(s for s in fields['sections'] if s[0].rstrip(b'\0') == b'.data')
    if not data_section[2] <= slot < data_section[2] + data_section[1] - 4:
        raise ValueError(f'{name}: slot 0x{slot:x} outside .data')
    masked = bytearray(span)
    masked[operand:operand + 4] = b'\0\0\0\0'
    return bytes(masked)


def decode(data, fields):
    relocated = identity.relocated_rvas(data, fields)
    window = read(data, fields, WINDOW_RVA, len(WINDOW))
    if window != WINDOW:
        raise ValueError(f'window at 0x{WINDOW_RVA:x} is {window.hex()}')
    insns = disasm(data, fields, WINDOW_RVA, WINDOW_RVA + len(WINDOW))
    shape = tuple((i.mnemonic, i.op_str) for i in insns)
    if shape != WINDOW_SHAPE or insns[-1].address + insns[-1].size != WINDOW_RVA + len(WINDOW):
        raise ValueError(f'window decodes as {shape}')
    if insns[2].address != SITE_RVA or insns[2].size != 5 or insns[3].address != SITE_RVA + 5:
        raise ValueError('the call is not one five-byte instruction at the site')
    target = (SITE_RVA + 5 + struct.unpack_from('<i', window, SITE_RVA - WINDOW_RVA + 1)[0]) & 0xffffffff
    if target != TARGET_RVA:
        raise ValueError(f'the site calls 0x{target:x}, documented 0x{TARGET_RVA:x}')
    for rva, expected in (DONE_RESET, DONE_TEST, DONE_SKIP):
        if read(data, fields, rva, len(expected)) != expected:
            raise ValueError(f'done-byte instruction at 0x{rva:x} differs')
    entry = read(data, fields, TARGET_RVA, len(TARGET_ENTRY))
    if entry != TARGET_ENTRY:
        raise ValueError(f'compositor entry is {entry.hex()}')
    if touching(relocated, WINDOW_RVA, len(WINDOW)) or touching(relocated, TARGET_RVA, len(TARGET_ENTRY)):
        raise ValueError('a base relocation touches the window or the compositor entry')
    # The compositor body: contiguous, bare RETs only, no jump out, no caller stack word read.
    body = disasm(data, fields, TARGET_RVA, TARGET_END)
    if body[-1].address + body[-1].size != TARGET_END or body[-1].bytes != b'\xc3':
        raise ValueError('the compositor body does not decode to its final RET')
    rets = [i.address for i in body if i.group(x86.X86_GRP_RET)]
    if any(read(data, fields, r, 1) != b'\xc3' for r in rets):
        raise ValueError('a compositor RET pops stack arguments')
    leaving, stack_args = [], []
    for i in body:
        if i.group(x86.X86_GRP_JUMP):
            op = i.operands[0]
            if op.type != x86.X86_OP_IMM or not TARGET_RVA <= op.imm < TARGET_END:
                leaving.append(i.address)
        for op in i.operands:
            if op.type == x86.X86_OP_MEM and ((op.mem.base == x86.X86_REG_EBP and op.mem.disp >= FRAME_BELOW_ENTRY) or
                                              (op.mem.base == x86.X86_REG_ESP and op.mem.disp >= 0x40)):
                stack_args.append(i.address)
    if leaving or stack_args:
        raise ValueError(f'compositor jumps out at {leaving} or reads caller stack at {stack_args}')
    callers = direct_callers(data, fields, TARGET_RVA)
    if callers != [SITE_RVA]:
        raise ValueError(f'direct callers of the compositor: {[hex(c) for c in callers]}')
    pass_callers = direct_callers(data, fields, PASS_RVA)
    if tuple(pass_callers) != PASS_CALLERS:
        raise ValueError(f'direct callers of the complete pass: {[hex(c) for c in pass_callers]}')
    image_base = fields['image_base']
    pointing = [r for r in relocated
                if (struct.unpack_from('<I', data, identity.rva_to_offset(fields, r))[0] - image_base) & 0xffffffff
                in range(WINDOW_RVA, WINDOW_RVA + len(WINDOW)) or
                (struct.unpack_from('<I', data, identity.rva_to_offset(fields, r))[0] - image_base) == TARGET_RVA]
    if pointing:
        raise ValueError(f'relocated words point at the compositor or into the window: {[hex(r) for r in pointing]}')
    # Branches of the complete pass into the window land on instruction boundaries outside the call.
    complete = disasm(data, fields, PASS_RVA, WINDOW_RVA + 0x600)
    boundaries = {i.address for i in insns} | {WINDOW_RVA + len(WINDOW)}
    into = [(i.address, i.operands[0].imm) for i in complete
            if (i.group(x86.X86_GRP_JUMP) or i.group(x86.X86_GRP_CALL)) and i.operands and
            i.operands[0].type == x86.X86_OP_IMM and WINDOW_RVA < i.operands[0].imm < WINDOW_RVA + len(WINDOW)]
    if any(t not in boundaries or SITE_RVA < t < SITE_RVA + 5 for _, t in into):
        raise ValueError(f'a branch lands inside the window: {into}')
    glow = slot_anchor(data, fields, relocated, GLOW_RVA, GLOW_LENGTH, GLOW_OPERAND, CONFIG_SLOT_RVA, 'glow')
    if glow[5:13] != bytes([0xf6, 0x80]) + struct.pack('<I', GLOW_FLAG_OFFSET) + bytes([GLOW_FLAG_MASK, 0x0f]) or glow[13] != 0x84:
        raise ValueError(f'glow test is {glow.hex()}')
    owner = slot_anchor(data, fields, relocated, OWNER_RVA, OWNER_LENGTH, OWNER_OPERAND, RENDERER_SLOT_RVA, 'owner')
    if owner[5:] != bytes([0x53, 0x8b, 0x58, RECORD_OFFSET]):
        raise ValueError(f'owner record load is {owner.hex()}')
    device = read(data, fields, DEVICE_RVA, len(DEVICE_ANCHOR))
    if device != DEVICE_ANCHOR or touching(relocated, DEVICE_RVA, len(DEVICE_ANCHOR)):
        raise ValueError(f'device load is {device.hex()}')
    # The ordinary camera activation selects the same device: root, +0x18, [0] (census; not a runtime gate).
    slot_anchor(data, fields, relocated, CAMERA_ROOT_RVA, 5, 1, RENDERER_SLOT_RVA, 'camera root')
    if read(data, fields, CAMERA_RECORD_RVA, 3) != bytes([0x8b, 0x40, RECORD_OFFSET]) or \
            read(data, fields, CAMERA_DEVICE_RVA, 2) != bytes([0x8b, 0x38]):
        raise ValueError('the camera activation does not select record+0x18, [0]')
    return {'site_rva': SITE_RVA, 'target_rva': TARGET_RVA, 'window_rva': WINDOW_RVA, 'window': bytes(window),
            'window_site_offset': SITE_RVA - WINDOW_RVA, 'continuation_rva': CONTINUATION_RVA,
            'target_entry': bytes(entry), 'target_end': TARGET_END, 'target_instructions': len(body),
            'target_rets': rets, 'glow_anchor_rva': GLOW_RVA, 'glow_anchor': glow, 'glow_operand_offset': GLOW_OPERAND,
            'config_slot_rva': CONFIG_SLOT_RVA, 'glow_flag_offset': GLOW_FLAG_OFFSET, 'glow_flag_mask': GLOW_FLAG_MASK,
            'owner_anchor_rva': OWNER_RVA, 'owner_anchor': owner, 'owner_operand_offset': OWNER_OPERAND,
            'renderer_slot_rva': RENDERER_SLOT_RVA, 'record_offset': RECORD_OFFSET, 'device_anchor_rva': DEVICE_RVA,
            'device_anchor': device, 'device_offset': DEVICE_OFFSET, 'target_callers': callers,
            'pass_callers': pass_callers, 'window_branches': into}


def hexes(b):
    return ', '.join(f'0x{x:02x}' for x in b)


def fragment(data, fields):
    d = decode(data, fields)
    lines = [
        '// Generated by tools/analysis/fl_scene_hook_constants.py from X3FL.exe; do not edit.',
        f'// X3FL.exe SHA-256 {hashlib.sha256(data).hexdigest()} (provenance, never a gate).',
        '// Every value is an RVA (added to fl::module_base()) or a field offset; nothing is 0x400000 based.',
        '// The site is the compositor\'s only direct caller (E8 census); no base relocation touches the window or the',
        f'// compositor entry. The compositor body [0x{d["target_rva"]:x}, 0x{d["target_end"]:x}) has {d["target_instructions"]} '
        'instructions, bare RETs only and reads no caller stack word.',
        '// clang-format off',
        f'constexpr std::uint32_t site_rva = 0x{d["site_rva"]:08x}, target_rva = 0x{d["target_rva"]:08x}, '
        f'window_rva = 0x{d["window_rva"]:08x}, window_site_offset = {d["window_site_offset"]};',
        '// cmp dword [esi+0x29c],0x11; jle +9; call rel32 <compositor>; mov byte [ebp-0xd],1; mov ecx,[esi+0x270]',
        f'constexpr std::uint8_t window[{len(d["window"])}] = {{{hexes(d["window"])}}};',
        '// push ebp; lea ebp,[esp-0x38]; mov eax,fs:[0]; sub esp,0x38',
        f'constexpr std::uint8_t target_entry[{len(d["target_entry"])}] = {{{hexes(d["target_entry"])}}};',
        '// mov eax,[config]; test byte [eax+0xf0],0x80; je <early out> (disp32 zeroed): the compositor\'s glow test',
        f'constexpr std::uint32_t glow_anchor_rva = 0x{d["glow_anchor_rva"]:08x}, glow_operand_offset = {d["glow_operand_offset"]}, '
        f'config_slot_rva = 0x{d["config_slot_rva"]:08x},',
        f'                        glow_flag_offset = 0x{d["glow_flag_offset"]:x}, glow_flag_mask = 0x{d["glow_flag_mask"]:x};',
        f'constexpr std::uint8_t glow_anchor[{len(d["glow_anchor"])}] = {{{hexes(d["glow_anchor"])}}};',
        '// mov eax,[renderer]; push ebx; mov ebx,[eax+0x18] (disp32 zeroed): renderer -> device record',
        f'constexpr std::uint32_t owner_anchor_rva = 0x{d["owner_anchor_rva"]:08x}, owner_operand_offset = {d["owner_operand_offset"]}, '
        f'renderer_slot_rva = 0x{d["renderer_slot_rva"]:08x},',
        f'                        record_offset = 0x{d["record_offset"]:x};',
        f'constexpr std::uint8_t owner_anchor[{len(d["owner_anchor"])}] = {{{hexes(d["owner_anchor"])}}};',
        '// mov ebx,[ebx]; xor edx,edx: record[0] is the IDirect3DDevice9',
        f'constexpr std::uint32_t device_anchor_rva = 0x{d["device_anchor_rva"]:08x}, device_offset = {d["device_offset"]};',
        f'constexpr std::uint8_t device_anchor[{len(d["device_anchor"])}] = {{{hexes(d["device_anchor"])}}};',
        '// clang-format on',
    ]
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
        print('fl_scene_hook_constants', 'match' if same else 'differs', args.output)
        return 0 if same else 1
    args.output.write_text(text)
    print('wrote', args.output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
