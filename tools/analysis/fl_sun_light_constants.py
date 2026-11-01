#!/usr/bin/env python3
"""X3FL.exe light-candidate layout for src/engine/fl/sun_light_poll_fl.cpp (FL edition of sun_light_poll).

Reads X3: Farnham's Legacy's X3FL.exe and writes src/engine/fl/sun_light_poll_fl_inc.h
(docs/reverse-engineering/fl/sun-light.md, section "2026-11-01: poll contract"). The AP contract is the
header comment of src/proxy/sun_light_poll.h; this file establishes every address and field of it for FL
from whole decoded instructions (capstone), side by side with the AP functions they match:

  context global   *(base + 0x1aa100), AP 0x00608518: read by the collector, the selector and the Dir-slot
                   rule; published at 0x77697 / 0x8a5f1 (AP 0x403367 / 0x470df8)
  collector        RVA 0x95450 (AP 0x47c640): array at R+0x5e8c, append cap 0xfe, zero terminator, RET 8
  selector         RVA 0x964c0 (AP 0x47d5e0, score at 0x47d641): trunc(0.587 G + 0.299 R + 0.114 B)
                   (+0x300 when directional); the doubles are read from .rdata
  Dir-slot rule    [0xe6f6d, 0xe6fc8) (AP 0x4c5030-0x4c508f): flags & 0x800000 or range > 0x256250
  slot count       R+0x6288 = min(renderer caps +0x4f4, 8) at 0xda1b7 (AP 0x4b9bb3)
  lazy flag        RVA 0xde1c0 (AP 0x4bdbf0): record +0x170, D3DLIGHTTYPE spot 2 (flags 0x10) / point 1
                   (0x400000) / directional 3, then `or [+0x12c],0x800000` for type 3
  node fields      flags +0x12c, position +0xb0/b4/b8 (both editions); colour +0x154/156/158, range +0x15c,
                   light record +0x170 (AP +0x150/152/154, +0x158, +0x16c); record position +0x34

Every runtime anchor is one or more whole instructions; at most one HIGHLOW relocation may touch it and
then exactly at the instruction's disp32 (the global or an .rdata constant); its bytes are emitted with
that word zeroed. Census checks (direct callers, the per-view order in the complete pass, the cockpit
light scene, the writers of the array) are verified here and documented, not emitted as runtime bytes.
Every emitted value is an RVA or an offset; nothing is 0x400000 based.

usage: fl_sun_light_constants.py [--exe PATH] [--output PATH] [--check] [--json]
  --check  exit 1 when the fragment on disk differs from the generated text
  --json   print the decoded constants and exit (verification/analysis/test_fl_sun_light_constants.py)
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
DEFAULT_OUTPUT = ROOT / 'src/engine/fl/sun_light_poll_fl_inc.h'
_spec = importlib.util.spec_from_file_location('fl_identity_constants', ROOT / 'tools/analysis/fl_identity_constants.py')
identity = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(identity)

# The contract (each value is spelled into the decoded shapes below, so a different image is refused).
CONTEXT_SLOT_RVA = 0x1aa100
ARRAY_OFFSET, ARRAY_WORDS, ARRAY_CAPACITY = 0x5e8c, 255, 0xfe
SLOT_COUNT_OFFSET, SLOT_COUNT_MAX = 0x6288, 8
SELECTION_RECORDS, SELECTION_STRIDE = 0x628c, 12
CAPS_RECORD, CAPS_HOLDER, CAPS_FIELD = 0x18, 4, 0x4f4  # renderer -> +0x18 -> +4 -> +0x4f4
FLAGS, POSITION, COLOUR, RANGE, RECORD, RECORD_POSITION = 0x12c, 0xb0, 0x154, 0x15c, 0x170, 0x34
CAMERA_POSITION = 0x30
LIGHT_BIT, SPOT_BIT, POINT_BIT, DIRECTIONAL_BIT, EXCLUDED_BITS = 0x4, 0x10, 0x400000, 0x800000, 0x400010
# D3DLIGHTTYPE written to record[0] by the filler: 0x10 -> 2 (spot), 0x400000 -> 1 (point), otherwise 3 (directional).
DIRECTIONAL_BONUS, RANGE_THRESHOLD = 0x300, 0x256250
LUMA = (('green', 0x191b38, 0.587), ('red', 0x191b10, 0.299), ('blue', 0x191ae0, 0.114))
COLOUR_SCALE_RVA, COLOUR_SCALE = 0x191a80, 1.0 / 256.0
COLLECTOR_RVA, SELECTOR_RVA, DIR_RULE_RVA, DIR_RULE_END, LAZY_FLAG_RVA = 0x95450, 0x964c0, 0xe6f6d, 0xe6fc8, 0xde1c0
DIR_WRAPPER_RVA = 0xe6ef0       # the material wrapper that contains the Dir-slot rule
COMPLETE_PASS_RVA = 0x8b6d0
COCKPIT_INIT_RVA, COCKPIT_LIGHTS_RVA = 0x21b60, 0x22fb0
LIGHT_BIRTH_RVA, SCENE_DETACH_RVA, SCENE_ATTACH_RVA = 0xa3da0, 0xa5740, 0xa56b0
CALLERS = {  # target RVA: its direct E8 callers (census)
    COLLECTOR_RVA: (0x23007, 0x95783, 0x97857),   # cockpit lights, ordinary activation, cube face
    SELECTOR_RVA: (0x23012, 0x96fa7),             # cockpit lights, per submitted node
    DIR_WRAPPER_RVA: (0x97035, 0x97702),
    0x95670: (0x8ba43, 0x8bbef, 0x8bc87),          # camera activation: main loop, auxiliary loop, engine+0x64 view
    COMPLETE_PASS_RVA: (0x782cf,),
    COCKPIT_INIT_RVA: (0x305be,),
    COCKPIT_LIGHTS_RVA: (0x2292a,),
}
MAX_ANCHOR = 32


def anchor_table():
    """(name, rva, shape) with VA operands as functions of the preferred image base."""
    def ctx(reg):
        return lambda b: ('mov', f'{reg}, dword ptr [{hex(b + CONTEXT_SLOT_RVA)}]')

    def fixed(m, o):
        return lambda b: (m, o)

    def jump(m, rva):
        return lambda b: (m, hex(b + rva))

    def luma(reg, rva):
        return lambda b: ('movsd', f'{reg}, qword ptr [{hex(b + rva)}]')

    F = fixed
    pops = [F('pop', 'edi'), F('pop', 'esi'), F('pop', 'ebp'), F('pop', 'ebx'), F('add', 'esp, 0xc'), F('ret', '8')]
    return (
        # The render-context global (AP 0x00608518).
        ('collector_context', 0x95457, [ctx('ecx'), F('push', 'ebx'), F('push', 'ebp')]),
        ('selector_context', 0x964c9, [ctx('edx'), F('push', 'esi'), F('push', 'edi')]),
        ('dir_rule_context', DIR_RULE_RVA, [ctx('edi'), F('xor', 'esi, esi'), F('xor', 'ecx, ecx')]),
        ('context_publish', 0x77697, [lambda b: ('mov', f'dword ptr [{hex(b + CONTEXT_SLOT_RVA)}], eax')]),
        ('context_restore', 0x8a5f1, [lambda b: ('mov', f'dword ptr [{hex(b + CONTEXT_SLOT_RVA)}], edi')]),
        ('slot_count_source', 0xda1a5, [ctx('ecx'), F('mov', f'eax, dword ptr [eax + {hex(CAPS_RECORD)}]'),
                                         F('mov', f'eax, dword ptr [eax + {CAPS_HOLDER}]'),
                                         F('mov', f'eax, dword ptr [eax + {hex(CAPS_FIELD)}]')]),
        ('slot_count_clamp', 0xda1b7, [F('mov', f'dword ptr [ecx + {hex(SLOT_COUNT_OFFSET)}], eax'),
                                        F('cmp', f'eax, {SLOT_COUNT_MAX}'), jump('jle', 0xda1cc),
                                        F('mov', f'dword ptr [ecx + {hex(SLOT_COUNT_OFFSET)}], {SLOT_COUNT_MAX}')]),
        # The collector (AP 0x47c640): root list, then one level of children.
        ('collector_entry', COLLECTOR_RVA, [F('sub', 'esp, 0xc'), F('mov', 'eax, dword ptr [esp + 0x10]')]),
        ('collector_array', 0x95472, [F('lea', f'ebx, [ecx + {hex(ARRAY_OFFSET)}]')]),
        ('collector_light_bit', 0x95480, [F('mov', f'ecx, dword ptr [esi + {hex(FLAGS)}]'), F('test', f'cl, {LIGHT_BIT}')]),
        ('collector_capacity', 0x9548f, [F('cmp', f'edi, {hex(ARRAY_CAPACITY)}')]),
        ('collector_position_x', 0x9549f, [F('mov', f'eax, dword ptr [esi + {hex(POSITION)}]'),
                                            F('sub', f'eax, dword ptr [edx + {hex(CAMERA_POSITION)}]')]),
        ('collector_position_y', 0x954ab, [F('mov', f'eax, dword ptr [esi + {hex(POSITION + 4)}]')]),
        ('collector_position_z', 0x954c9, [F('mov', f'eax, dword ptr [esi + {hex(POSITION + 8)}]'),
                                            F('sub', f'eax, dword ptr [edx + {hex(CAMERA_POSITION + 8)}]')]),
        ('collector_exclusion', 0x954ef, [F('test', f'ecx, {hex(EXCLUDED_BITS)}'), jump('je', 0x95523),
                                           F('mov', f'ecx, dword ptr [esi + {hex(RANGE)}]'), F('cmp', 'ecx, eax')]),
        ('collector_child_array', 0x9555c, [F('lea', f'ebx, [edi + {hex(ARRAY_OFFSET // 4)}]'), F('lea', 'ebx, [ecx + ebx*4]')]),
        ('collector_child_light_bit', 0x95570, [F('mov', f'ecx, dword ptr [esi + {hex(FLAGS)}]'), F('test', f'cl, {LIGHT_BIT}')]),
        ('collector_child_capacity', 0x9557f, [F('cmp', f'edi, {hex(ARRAY_CAPACITY)}')]),
        ('collector_child_exclusion', 0x955df, [F('test', f'ecx, {hex(EXCLUDED_BITS)}'), jump('je', 0x95613),
                                                 F('mov', f'ecx, dword ptr [esi + {hex(RANGE)}]'), F('cmp', 'ecx, eax')]),
        ('collector_terminator', 0x9563d, [F('mov', f'dword ptr [eax + edi*4 + {hex(ARRAY_OFFSET)}], 0')] + pops),
        ('collector_terminator_empty', 0x95652, [F('mov', f'dword ptr [ecx + edi*4 + {hex(ARRAY_OFFSET)}], 0')] + pops),
        # The selector (AP 0x47d5e0; score at 0x47d641).
        ('selector_array', 0x9650f, [F('lea', f'edi, [ecx + {hex(ARRAY_OFFSET)}]')]),
        ('luma_green', 0x96523, [luma('xmm2', LUMA[0][1])]),
        ('luma_red', 0x9652b, [luma('xmm3', LUMA[1][1])]),
        ('luma_blue', 0x96533, [luma('xmm4', LUMA[2][1])]),
        ('selector_green', 0x96540, [F('movsx', f'eax, word ptr [edi + {hex(COLOUR + 2)}]'), F('movd', 'xmm1, eax')]),
        ('selector_red', 0x9654b, [F('movsx', f'eax, word ptr [edi + {hex(COLOUR)}]'), F('cvtdq2pd', 'xmm1, xmm1')]),
        ('selector_blue', 0x9655a, [F('movsx', f'eax, word ptr [edi + {hex(COLOUR + 4)}]'), F('cvtdq2pd', 'xmm0, xmm0'),
                                     F('mulsd', 'xmm1, xmm2'), F('mulsd', 'xmm0, xmm3'), F('addsd', 'xmm1, xmm0')]),
        ('selector_score', 0x96575, [F('mov', f'eax, dword ptr [edi + {hex(FLAGS)}]'), F('cvtdq2pd', 'xmm0, xmm0'),
                                      F('mulsd', 'xmm0, xmm4'), F('addsd', 'xmm1, xmm0'), F('cvttsd2si', 'esi, xmm1')]),
        ('selector_directional_bonus', 0x9658f, [F('test', f'eax, {hex(DIRECTIONAL_BIT)}'), jump('je', 0x965a1),
                                                  F('add', f'esi, {hex(DIRECTIONAL_BONUS)}')]),
        ('selector_exclusion', 0x965a1, [F('test', f'eax, {hex(EXCLUDED_BITS)}')]),
        ('selector_range', 0x9660f, [F('mov', f'ecx, dword ptr [edi + {hex(RANGE)}]')]),
        # The Dir-slot rule (AP 0x4c5030-0x4c508f).
        ('dir_rule_slot_count', 0xe6f7a, [F('cmp', f'dword ptr [edi + {hex(SLOT_COUNT_OFFSET)}], ecx')]),
        ('dir_rule_records', 0xe6f82, [F('lea', f'edx, [edi + {hex(SELECTION_RECORDS)}]')]),
        ('dir_rule_array', 0xe6f8e, [F('mov', f'eax, dword ptr [edi + eax*4 + {hex(ARRAY_OFFSET)}]')]),
        ('dir_rule_directional', 0xe6f99, [F('test', f'dword ptr [eax + {hex(FLAGS)}], {hex(DIRECTIONAL_BIT)}'),
                                            jump('jne', 0xe6fb1)]),
        ('dir_rule_range', 0xe6fa5, [F('cmp', f'dword ptr [eax + {hex(RANGE)}], {hex(RANGE_THRESHOLD)}'), jump('jle', 0xe6fb7)]),
        ('dir_rule_stride', 0xe6fb8, [F('add', f'edx, {hex(SELECTION_STRIDE)}'),
                                       F('cmp', f'ecx, dword ptr [edi + {hex(SLOT_COUNT_OFFSET)}]')]),
        # The record filler and lazy directional flag (AP 0x4bdbf0).
        ('lazy_record', LAZY_FLAG_RVA, [F('mov', f'edx, dword ptr [ecx + {hex(RECORD)}]'), F('test', 'edx, edx')]),
        ('lazy_spot', 0xde1ce, [F('mov', f'eax, dword ptr [ecx + {hex(FLAGS)}]'), F('test', f'al, {hex(SPOT_BIT)}')]),
        ('lazy_point', 0xde1e0, [F('test', f'eax, {hex(POINT_BIT)}')]),
        ('lazy_directional', 0xde1ef, [F('mov', 'dword ptr [edx], 3'), F('test', f'eax, {hex(DIRECTIONAL_BIT)}'),
                                        jump('jne', 0xde206), F('or', f'dword ptr [ecx + {hex(FLAGS)}], {hex(DIRECTIONAL_BIT)}')]),
        ('record_colour', 0xde206, [F('movsx', f'eax, word ptr [ecx + {hex(COLOUR + 2)}]'),
                                     lambda b: ('movsd', f'xmm2, qword ptr [{hex(b + COLOUR_SCALE_RVA)}]')]),
        ('record_position_record', 0xde416, [F('mov', f'edx, dword ptr [ecx + {hex(RECORD)}]')]),
        ('record_position_x', 0xde429, [F('movd', f'xmm0, dword ptr [ecx + {hex(POSITION)}]')]),
        ('record_position_store', 0xde43c, [F('movss', f'dword ptr [edx + {hex(RECORD_POSITION)}], xmm0')]),
        # Setters: light birth, colour, range, type.
        ('birth_light_bit', 0xa3e5b, [F('or', f'dword ptr [esi + {hex(FLAGS)}], {LIGHT_BIT}')]),
        ('colour_setter', 0xa3eec, [F('mov', f'word ptr [ecx + {hex(COLOUR)}], ax'), F('mov', 'ax, word ptr [esp + 8]'),
                                     F('mov', f'word ptr [ecx + {hex(COLOUR + 2)}], ax'), F('mov', 'ax, word ptr [esp + 0xc]'),
                                     F('mov', f'word ptr [ecx + {hex(COLOUR + 4)}], ax')]),
        ('range_setter', 0xa3f2b, [F('mov', f'dword ptr [ecx + {hex(RANGE)}], eax')]),
        ('type_directional', 0xa3f81, [F('or', f'dword ptr [ecx + {hex(FLAGS)}], {hex(DIRECTIONAL_BIT)}')]),
    )


def census_table():
    """Whole-instruction shapes checked by the generator only (documented, never runtime bytes)."""
    def call(rva):
        return lambda b: ('call', hex(b + rva))

    def F(m, o):
        return lambda b: (m, o)

    return (
        # Main loop: cockpit updater registry walk, then the complete pass (one thread, in this order).
        ('frame_order', 0x782ca, [call(0x1e550), call(COMPLETE_PASS_RVA)]),
        # Complete pass, per sorted camera: propagate, activate (collects), prepare, then the layer draws.
        ('view_order', 0x8ba36, [F('mov', 'ecx, dword ptr [esi + 0x1c]'), F('push', 'esi'), call(0x94b00),
                                 F('mov', 'ecx, dword ptr [esi + 0x1c]'), F('push', 'esi'), call(0x95670),
                                 F('mov', 'ecx, dword ptr [esi + 0x1c]'), F('push', 'esi'), call(0x97720)]),
        ('view_draws', 0x8ba7b, [F('push', '0'), F('push', 'edi'), F('push', 'esi'), call(0x978e0),
                                 F('mov', 'edx, esi'), call(0x975c0), F('mov', 'edx, esi'), call(0x97680)]),
        ('activation_collect', 0x95783, [call(COLLECTOR_RVA)]),
        # Cockpit HUD light scene (AP 0x420260): 16 light births, forced directional at creation.
        ('cockpit_camera_marker', 0x21b9d, [F('or', 'dword ptr [eax + 0x270], 0x24')]),
        ('cockpit_light_count', 0x21bf2, [F('mov', 'edi, 0x10'), F('add', 'esi, 0x14'), F('lea', 'ebx, [edi + 0x70]')]),
        ('cockpit_light_birth', 0x21c00, [call(LIGHT_BIRTH_RVA), F('mov', 'dword ptr [esi], eax')]),
        ('cockpit_light_colour', 0x21c29, [F('mov', f'dword ptr [ecx + {hex(COLOUR)}], 0x800080'),
                                           F('mov', f'word ptr [ecx + {hex(COLOUR + 4)}], bx')]),
        ('cockpit_light_directional', 0x21c49, [F('or', f'dword ptr [eax + {hex(FLAGS)}], {hex(DIRECTIONAL_BIT)}')]),
        # Per-frame cockpit light refresh (AP 0x4216e0): detach all 16, collect + select for the ship, copy the
        # selected non-spot lights (colour, range, flags & 0xc00000, view-relative position) and attach to the HUD scene.
        ('cockpit_detach', 0x22fd8, [F('push', 'dword ptr [ebp + 4]'), call(SCENE_DETACH_RVA)]),
        ('cockpit_collect', 0x23007, [call(COLLECTOR_RVA), F('mov', 'eax, dword ptr [ebp + 0x10]'),
                                      F('push', 'dword ptr [eax + 0x70]'), call(SELECTOR_RVA)]),
        ('cockpit_skip_spot', 0x23052, [F('test', f'byte ptr [ebx + {hex(FLAGS)}], {hex(SPOT_BIT)}')]),
        ('cockpit_copy_red', 0x230ae, [F('movzx', f'eax, word ptr [ebx + {hex(COLOUR)}]'),
                                       F('mov', f'word ptr [edi + {hex(COLOUR)}], ax')]),
        ('cockpit_copy_flags', 0x230d8, [F('mov', f'eax, dword ptr [ebx + {hex(FLAGS)}]'), F('and', 'eax, 0xc00000'),
                                         F('or', f'eax, dword ptr [edi + {hex(FLAGS)}]')]),
        ('cockpit_copy_range', 0x230f4, [F('mov', f'eax, dword ptr [ebx + {hex(RANGE)}]'),
                                         F('mov', f'dword ptr [edi + {hex(RANGE)}], eax')]),
        ('cockpit_attach', 0x23128, [F('push', 'dword ptr [eax + 4]'), call(SCENE_ATTACH_RVA)]),
    )


# Every instruction in .text whose operand names the array (a linear sweep; census of its writers and readers).
ARRAY_REFERENCES = (0x2304b, 0x95472, 0x9555c, 0x9563d, 0x95652, 0x9650f, 0xde54d, 0xe4875, 0xe6f8e)


def read(data, fields, rva, n):
    off = identity.rva_to_offset(fields, rva)
    return data[off:off + n]


def section_of(fields, rva):
    for name, vsize, va, raw_size, _, _ in fields['sections']:
        if va <= rva < va + max(vsize, raw_size):
            return name.rstrip(b'\0').decode()
    return None


def decode_shape(data, fields, name, rva, shape):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    base = fields['image_base']
    insns = []
    for i in md.disasm(read(data, fields, rva, 16 * len(shape)), base + rva):
        insns.append(i)
        if len(insns) == len(shape):
            break
    got = tuple((i.mnemonic, i.op_str) for i in insns)
    want = tuple(s(base) for s in shape)
    if got != want:
        raise ValueError(f'{name} at 0x{rva:x} decodes as {got}, expected {want}')
    return insns


def absolute_operands(insns, base):
    """(offset in span, VA) of every absolute memory disp32 (no base, no index register)."""
    out, offset = [], 0
    for i in insns:
        for op in i.operands:
            if op.type == x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                out.append((offset + i.disp_offset, op.mem.disp & 0xffffffff))
        offset += i.size
    return out


def decode_anchor(data, fields, relocated, name, rva, shape):
    base = fields['image_base']
    insns = decode_shape(data, fields, name, rva, shape)
    length = sum(i.size for i in insns)
    if length > MAX_ANCHOR:
        raise ValueError(f'{name}: {length} bytes exceed {MAX_ANCHOR}')
    span = read(data, fields, rva, length)
    touching = sorted(r - rva for r in relocated if r < rva + length and rva < r + 4)
    absolute = absolute_operands(insns, base)
    if len(absolute) > 1:
        raise ValueError(f'{name}: more than one absolute operand')
    if touching != [off for off, _ in absolute]:
        raise ValueError(f'{name}: relocations at {touching}, absolute operands at {absolute}')
    reloc_offset, target_rva = 0xff, 0
    masked = bytearray(span)
    if absolute:
        reloc_offset, va = absolute[0]
        target_rva = va - base
        if struct.unpack_from('<I', span, reloc_offset)[0] != va:
            raise ValueError(f'{name}: disp32 is not at the relocated word')
        masked[reloc_offset:reloc_offset + 4] = b'\0\0\0\0'
    return {'name': name, 'rva': rva, 'length': length, 'reloc_offset': reloc_offset, 'target_rva': target_rva,
            'bytes': bytes(masked), 'file_bytes': bytes(span), 'instructions': len(insns)}


def direct_callers(data, fields, target_rva):
    """Every E8 rel32 in .text that resolves to target_rva (a byte scan: conservative, may over-count)."""
    text = next(s for s in fields['sections'] if s[0].rstrip(b'\0') == b'.text')
    _, vsize, va, raw_size, raw_ptr, _ = text
    region = data[raw_ptr:raw_ptr + min(vsize, raw_size)]
    out, pos = [], region.find(b'\xe8')
    while 0 <= pos < len(region) - 4:
        rva = pos + va
        if (rva + 5 + struct.unpack_from('<i', region, pos + 1)[0]) & 0xffffffff == target_rva:
            out.append(rva)
        pos = region.find(b'\xe8', pos + 1)
    return tuple(out)


def array_references(data, fields):
    """Instructions with an immediate or displacement naming the array (0x5e8c, or 0x17a3 = 0x5e8c / 4), linear sweep."""
    text = next(s for s in fields['sections'] if s[0].rstrip(b'\0') == b'.text')
    _, vsize, va, raw_size, raw_ptr, _ = text
    code = data[raw_ptr:raw_ptr + min(vsize, raw_size)]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    found, off = [], 0
    needles = (ARRAY_OFFSET, ARRAY_OFFSET // 4)
    while off < len(code):
        advanced = False
        for i in md.disasm(code[off:], va + off):
            for op in i.operands:
                if (op.type == x86.X86_OP_MEM and op.mem.disp in needles) or (op.type == x86.X86_OP_IMM and op.imm in needles):
                    found.append(i.address)
                    break
            off = i.address + i.size - va
            advanced = True
        if not advanced or off < len(code):
            off += 1
    return tuple(sorted(set(found)))


def decode(data, fields):
    relocated = set(identity.relocated_rvas(data, fields))
    anchors = [decode_anchor(data, fields, relocated, *row) for row in anchor_table()]
    for a in anchors:
        if a['target_rva']:
            section = section_of(fields, a['target_rva'])
            want = '.data' if a['target_rva'] == CONTEXT_SLOT_RVA else '.rdata'
            if section != want:
                raise ValueError(f'{a["name"]}: target 0x{a["target_rva"]:x} in {section}, expected {want}')
    context = [a['name'] for a in anchors if a['target_rva'] == CONTEXT_SLOT_RVA]
    if len(context) != 6:
        raise ValueError(f'context anchors {context}')
    by_name = {a['name']: a for a in anchors}
    for colour, rva, value in LUMA:
        if by_name[f'luma_{colour}']['target_rva'] != rva or struct.unpack('<d', read(data, fields, rva, 8))[0] != value:
            raise ValueError(f'luma {colour} constant at 0x{rva:x} is not {value}')
    if struct.unpack('<d', read(data, fields, COLOUR_SCALE_RVA, 8))[0] != COLOUR_SCALE:
        raise ValueError('record colour scale is not 1/256')
    for row in census_table():
        decode_shape(data, fields, *row)
        span_end = row[1] + sum(i.size for i in decode_shape(data, fields, *row))
        if any(r < span_end and row[1] < r + 4 for r in relocated):
            raise ValueError(f'census {row[0]}: a relocation touches the span')
    callers = {}
    for target, expected in CALLERS.items():
        got = direct_callers(data, fields, target)
        if got != expected:
            raise ValueError(f'direct callers of 0x{target:x}: {[hex(c) for c in got]}, expected {[hex(c) for c in expected]}')
        callers[f'0x{target:x}'] = list(got)
    refs = array_references(data, fields)
    if refs != ARRAY_REFERENCES:
        raise ValueError(f'array references {[hex(r) for r in refs]}')
    return {'context_slot_rva': CONTEXT_SLOT_RVA, 'anchors': anchors, 'callers': callers,
            'array_references': list(refs), 'census': [row[0] for row in census_table()]}


def hexes(b):
    return ', '.join(f'0x{x:02x}' for x in b)


def fragment(data, fields):
    d = decode(data, fields)
    lines = [
        '// Generated by tools/analysis/fl_sun_light_constants.py from X3FL.exe; do not edit.',
        f'// X3FL.exe SHA-256 {hashlib.sha256(data).hexdigest()} (provenance, never a gate).',
        '// The FL light-candidate contract of sun_light_poll (src/proxy/sun_light_poll.h for AP;',
        '// docs/reverse-engineering/fl/sun-light.md, "2026-11-01: poll contract"). Every address is an RVA',
        '// (added to fl::module_base()); every other value is a field offset, bit or constant read from the image.',
        '// Self-contained: include at namespace scope (the reader wraps it in its own namespace).',
        '#include <cstdint>',
        '// clang-format off',
        f'constexpr std::uint32_t context_slot_rva = 0x{CONTEXT_SLOT_RVA:08x}; // R = *(module_base() + rva); AP 0x00608518',
        f'constexpr std::uint32_t array_offset = 0x{ARRAY_OFFSET:x}, array_words = {ARRAY_WORDS}, array_capacity = {ARRAY_CAPACITY};'
        ' // null-terminated node pointers',
        f'constexpr std::uint32_t slot_count_offset = 0x{SLOT_COUNT_OFFSET:x}, slot_count_max = {SLOT_COUNT_MAX};'
        ' // min(renderer caps +0x4f4, 8); the dword after the array',
        f'constexpr std::uint32_t selection_records = 0x{SELECTION_RECORDS:x}, selection_stride = {SELECTION_STRIDE};'
        ' // {array index, score, distance}',
        f'constexpr std::uint32_t node_flags = 0x{FLAGS:x}, node_position = 0x{POSITION:x}, node_colour = 0x{COLOUR:x},'
        f' node_range = 0x{RANGE:x}, node_record = 0x{RECORD:x};',
        f'constexpr std::uint32_t record_position = 0x{RECORD_POSITION:x}; // float[3] at [node+node_record]+0x34: position x scene scale',
        f'constexpr std::uint32_t light_bit = 0x{LIGHT_BIT:x}, spot_bit = 0x{SPOT_BIT:x}, point_bit = 0x{POINT_BIT:x},'
        f' directional_bit = 0x{DIRECTIONAL_BIT:x}, excluded_bits = 0x{EXCLUDED_BITS:x};',
        f'constexpr std::uint32_t directional_bonus = 0x{DIRECTIONAL_BONUS:x}; // score = trunc(luma) + bonus when directional',
        f'constexpr std::int32_t range_threshold = 0x{RANGE_THRESHOLD:x}; // Dir slot: directional, or signed range > threshold',
        '// Selector weights (.rdata doubles), applied as trunc((green * G + red * R) + blue * B) in double precision.',
    ]
    for colour, rva, value in LUMA:
        lines.append(f'constexpr double luma_{colour} = {value!r}; // RVA 0x{rva:08x}')
    lines += [
        f'constexpr std::uint32_t collector_rva = 0x{COLLECTOR_RVA:08x}, selector_rva = 0x{SELECTOR_RVA:08x},'
        f' dir_rule_rva = 0x{DIR_RULE_RVA:08x}, dir_rule_end = 0x{DIR_RULE_END:08x}, lazy_flag_rva = 0x{LAZY_FLAG_RVA:08x};',
        f'constexpr std::uint32_t cockpit_init_rva = 0x{COCKPIT_INIT_RVA:08x}, cockpit_lights_rva = 0x{COCKPIT_LIGHTS_RVA:08x};'
        ' // census only: the HUD light scene',
        f'constexpr std::uint32_t collector_callers[3] = {{{", ".join(f"0x{c:08x}" for c in CALLERS[COLLECTOR_RVA])}}};'
        ' // cockpit, ordinary activation, cube face',
        '// Whole-instruction anchors. reloc_offset 0xff: no relocation; otherwise the disp32 at that offset is',
        '// relocated and must read module_base() + target_rva (bytes hold it zeroed).',
        'struct SunLightAnchor {',
        '    std::uint32_t rva;',
        '    std::uint8_t length, reloc_offset;',
        '    std::uint32_t target_rva;',
        f'    std::uint8_t bytes[{MAX_ANCHOR}];',
        '};',
        f'constexpr unsigned anchor_count = {len(d["anchors"])};',
        'constexpr SunLightAnchor anchors[anchor_count] = {',
    ]
    for a in d['anchors']:
        lines.append(f'    {{0x{a["rva"]:08x}, {a["length"]}, 0x{a["reloc_offset"]:02x}, 0x{a["target_rva"]:08x}, '
                     f'{{{hexes(a["bytes"])}}}}}, // {a["name"]}')
    lines += ['};', '// clang-format on']
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
        d['anchors'] = [{k: (v.hex() if isinstance(v, bytes) else v) for k, v in a.items()} for a in d['anchors']]
        print(json.dumps({'sha256': hashlib.sha256(data).hexdigest(), 'image_base': fields['image_base'], **d}))
        return 0
    text = fragment(data, fields)
    if args.check:
        same = args.output.exists() and args.output.read_text() == text
        print('fl_sun_light_constants', 'match' if same else 'differs', args.output)
        return 0 if same else 1
    args.output.write_text(text)
    print('wrote', args.output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
