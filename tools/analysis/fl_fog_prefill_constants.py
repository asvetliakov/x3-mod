#!/usr/bin/env python3
"""X3FL.exe constants for the fog transit prefill (the FL counterpart of src/proxy/fog_prefill.h).

Reads X3: Farnham's Legacy's X3FL.exe and writes src/engine/fl/fog_prefill_fl_inc.h: the object-manager
slot, the parentless-object list header at M+8, the node/object/sector offsets the backward walk reads,
the native transit order around it and byte anchors for each. AP proof: docs/reverse-engineering/
sector-transit-order.md sections 1, 2 and 5; FL findings: docs/reverse-engineering/fl/fog-prefill.md,
section "Verified constants (2026-11-01)". The TBackgrounds recipe after the walk (count/table slots, row
offsets) is src/engine/fl/sector_background_fl_inc.h's; the two slots are re-emitted here and must equal
that generator's.

Every anchor is a run of whole instructions decoded with capstone at the preferred base:

  manager_publish        startup 0x477895 (AP 0x403565): M = new manager (ctor 0x43dc30) -> [manager slot]
  list_init              manager ctor 0x43dc85 (AP not decompiled): map +0x14; header M+8 = {head M+0xc,
                         tail 0, tailpred M+8}
  sector_append          sector ctor 0x45c148 (AP 0x43f93b + 0x4524d0): common init 0x449910, AddTail at
                         [M+0x10], id map insert -> +8, scene +0x130 = 0, subtype +0x4a test, index +0x13c = 0
  object_class_marker    common init 0x44995b (AP 0x43ffa0): class word +0x48, eax = 0xcafe
  object_subtype_marker  common init 0x4499f2: subtype word +0x4a, parent +0x54 = 0, marker +0x9c = ax (0xcafe;
                         the generator proves eax is not written in between)
  object_unlink          destruction 0x449602 (AP 0x43fc0b): unlink next +0 / prev +4, marker +0x9c = 0xefac
  scene_set              SA_SetSpaceScene callee 0x45c260 (AP 0x452570): scene +0x130 store, index +0x13c,
                         stride 0xdb8, table slot
  background_command     native case 0x133 0x46eaac (AP 0x4644ad): sector_by_id 0x443db0, index +0x13c store
  scene_command          native case 0x128 0x46e909: sector_by_id, scene setter 0x45c260
  alloc_command          native case 0 0x46a6a3 (AP 0x460630 case 0): main class 1 -> sector allocation
  sector_allocate        0x46a71e: 0x180-byte object, sector ctor 0x45c110
  native_dispatch        0x46a691: case bound 0x179, case table (cases 0/0x128/0x133 checked statically)
  cleanup_walk           SA_CleanUpObjects 0x443cd0 (AP 0x43a460): forward walk from [M+8], class word 1 kept
  restore_append         save-load post-pass 0x44bea0 (AP 0x442c31): AddTail of a parentless restored object
  task_runner_call       outer loop 0x477ebf -> KC ready-task runner 0x4788b0 (the transit's script runs here)
  frame_calls            0x4782ca -> cockpit update 0x41e550, 0x4782cf -> render pass 0x48b6d0
  loop_back              0x478591 -> loop head 0x477e80
  recovery_guard         allocator 0x4d9007: guard slot set/cleared around the recovery callback (census)

Every base relocation inside an anchor must sit on an absolute operand the anchor names; the anchor's
bytes hold that word zeroed and the runtime compares it with module_base() + slot RVA. Field offsets are
read from the named instructions' operands and must equal the documented values. Every value in the
fragment is an RVA or an offset; nothing is 0x400000 based.

usage: fl_fog_prefill_constants.py [--exe PATH] [--output PATH] [--check] [--json]
  --check  exit 1 when the fragment on disk differs from the generated text
  --json   print the decoded constants and exit (verification/analysis/test_fl_fog_prefill_constants.py)
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
DEFAULT_OUTPUT = ROOT / 'src/engine/fl/fog_prefill_fl_inc.h'
_spec = importlib.util.spec_from_file_location('fl_identity_constants', ROOT / 'tools/analysis/fl_identity_constants.py')
identity = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(identity)
_spec = importlib.util.spec_from_file_location('fl_sector_background_constants',
                                               ROOT / 'tools/analysis/fl_sector_background_constants.py')
background = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(background)

# Image slots (RVAs). (rva, section the slot must lie in)
SLOTS = {
    'object_manager_slot_rva': (0x1a82ec, b'.data'),     # AP 0x60850c
    'background_count_slot_rva': (0x1a8338, b'.data'),   # AP 0x607040 (anchored in sector_background_fl_inc.h)
    'background_table_slot_rva': (0x1a83d0, b'.data'),   # AP 0x606fc0
    'recovery_guard_slot_rva': (0x22de10, b'.data'),     # FL census: nonzero while the allocator's recovery runs
    'config_slot_rva': (0x1a83b0, b'.data'),             # census (sector_append)
    'script_vm_slot_rva': (0x1aa174, b'.data'),          # census (background_command)
    'recovery_callback_slot_rva': (0x1aae24, b'.data'),  # census (recovery_guard)
    'native_command_table_rva': (0x073dc4, b'.text'),    # census (native_dispatch)
}
RUNTIME_SLOTS = ('object_manager_slot_rva', 'background_count_slot_rva', 'background_table_slot_rva')
# Slots and values that must equal tools/analysis/fl_sector_background_constants.py's.
SHARED_SLOTS = ('object_manager_slot_rva', 'background_count_slot_rva', 'background_table_slot_rva',
                'config_slot_rva', 'script_vm_slot_rva')
SHARED_VALUES = ('object_id', 'object_class', 'object_parent', 'sector_class', 'object_manager_map',
                 'sector_background_index', 'record_stride')

VALUES = {
    # Manager and its parentless-object list (Exec-style header at M + list_header)
    'object_manager_map': 0x14, 'list_header': 0x8, 'header_tail': 0x4, 'header_tailpred': 0x8,
    'list_tail': 0xc, 'list_tailpred': 0x10, 'link_next': 0x0, 'link_prev': 0x4,
    # Engine object
    'object_id': 0x8, 'object_class': 0x48, 'object_subtype': 0x4a, 'object_parent': 0x54,
    'object_marker': 0x9c, 'alive_marker': 0xcafe, 'dead_marker': 0xefac,
    # Sector object (class 1)
    'sector_class': 1, 'sector_object_size': 0x180, 'sector_scene': 0x130, 'sector_background_index': 0x13c,
    'record_stride': 0xdb8,
    # Native command dispatch
    'native_command_max': 0x179, 'guard_set': 1, 'guard_clear': 0,
    # Code RVAs (branch targets)
    'manager_constructor_rva': 0x03dc30, 'object_initializer_rva': 0x049910, 'id_map_insert_rva': 0x10cfe0,
    'sector_constructor_rva': 0x05c110, 'sector_by_id_rva': 0x043db0, 'scene_setter_rva': 0x05c260,
    'object_allocate_rva': 0x0d9120, 'sector_allocate_rva': 0x06a71e, 'task_runner_rva': 0x0788b0,
    'cockpit_update_rva': 0x01e550, 'render_pass_rva': 0x08b6d0, 'loop_head_rva': 0x077e80,
}
# Native command cases whose handlers the anchors name: case -> handler RVA (table entry, checked statically).
COMMAND_CASES = {0x000: 0x06a6a3, 0x128: 0x06e909, 0x133: 0x06eaac}
# eax holds alive_marker from its load to the marker store: no instruction in between writes it.
PRESERVED_EAX = (0x049963, 0x049a20)

# (name, first RVA, end RVA (exclusive), relocated operand slots in span order, facts)
ANCHORS = (
    ('manager_publish', 0x077895, 0x0778ae, ('object_manager_slot_rva',),
     (('target', 'manager_constructor_rva', 0x077897),)),
    ('list_init', 0x03dc85, 0x03dc9b, (),
     (('disp', 'object_manager_map', 0x03dc85), ('disp', 'list_header', 0x03dc88), ('disp', 'header_tail', 0x03dc8b),
      ('disp', 'header_tailpred', 0x03dc8e), ('disp', 'link_next', 0x03dc91), ('disp', 'link_next', 0x03dc95))),
    ('sector_append', 0x05c148, 0x05c1b7, ('object_manager_slot_rva', 'config_slot_rva'),
     (('target', 'object_initializer_rva', 0x05c148), ('disp', 'list_tailpred', 0x05c155),
      ('disp', 'link_prev', 0x05c158), ('disp', 'list_tailpred', 0x05c15b), ('disp', 'link_next', 0x05c15e),
      ('disp', 'list_tail', 0x05c160), ('disp', 'link_next', 0x05c163), ('disp', 'list_tailpred', 0x05c16a),
      ('disp', 'object_manager_map', 0x05c179), ('target', 'id_map_insert_rva', 0x05c17c),
      ('disp', 'object_id', 0x05c181), ('disp', 'sector_scene', 0x05c18b), ('disp', 'object_subtype', 0x05c195),
      ('disp', 'sector_background_index', 0x05c199))),
    ('object_class_marker', 0x04995b, 0x049968, (),
     (('disp', 'object_class', 0x04995f), ('imm', 'alive_marker', 0x049963))),
    ('object_subtype_marker', 0x0499f2, 0x049a27, (),
     (('disp', 'object_subtype', 0x0499f2), ('disp', 'object_parent', 0x0499f6), ('disp', 'object_marker', 0x049a20))),
    ('object_unlink', 0x049602, 0x049625, (),
     (('disp', 'link_prev', 0x049602), ('disp', 'link_next', 0x049605), ('disp', 'link_next', 0x04960e),
      ('disp', 'link_next', 0x049610), ('disp', 'link_prev', 0x049612), ('disp', 'link_prev', 0x049616),
      ('imm', 'dead_marker', 0x049619), ('disp', 'object_marker', 0x04961e))),
    ('scene_set', 0x05c260, 0x05c29c, ('background_table_slot_rva',),
     (('disp', 'sector_scene', 0x05c264), ('disp', 'sector_background_index', 0x05c26e),
      ('imm', 'record_stride', 0x05c26e))),
    ('background_command', 0x06eaac, 0x06eadc, ('script_vm_slot_rva',),
     (('target', 'sector_by_id_rva', 0x06eab2), ('disp', 'sector_background_index', 0x06eac5))),
    ('scene_command', 0x06e909, 0x06e939, (),
     (('target', 'sector_by_id_rva', 0x06e90f), ('target', 'scene_setter_rva', 0x06e934))),
    ('alloc_command', 0x06a6a3, 0x06a6b6, (),
     (('imm', 'sector_class', 0x06a6b1), ('target', 'sector_allocate_rva', 0x06a6b4))),
    ('sector_allocate', 0x06a71e, 0x06a741, (),
     (('imm', 'sector_object_size', 0x06a71e), ('target', 'object_allocate_rva', 0x06a723),
      ('target', 'sector_constructor_rva', 0x06a73c))),
    ('native_dispatch', 0x06a691, 0x06a6a3, ('native_command_table_rva',),
     (('imm', 'native_command_max', 0x06a691),)),
    ('cleanup_walk', 0x043cd0, 0x043d06, ('object_manager_slot_rva',),
     (('disp', 'list_header', 0x043cd6), ('disp', 'link_next', 0x043cd9), ('disp', 'object_class', 0x043ce0),
      ('imm', 'sector_class', 0x043ce0), ('disp', 'link_next', 0x043ce5), ('disp', 'object_parent', 0x043ce9))),
    ('restore_append', 0x04bea0, 0x04beb9, ('object_manager_slot_rva',),
     (('disp', 'list_tailpred', 0x04bea6), ('disp', 'link_prev', 0x04bea9), ('disp', 'list_tailpred', 0x04beac),
      ('disp', 'link_next', 0x04beaf), ('disp', 'list_tail', 0x04beb1), ('disp', 'link_next', 0x04beb4),
      ('disp', 'list_tailpred', 0x04beb6))),
    ('task_runner_call', 0x077ebf, 0x077ec4, (), (('target', 'task_runner_rva', 0x077ebf),)),
    ('frame_calls', 0x0782ca, 0x0782d4, (),
     (('target', 'cockpit_update_rva', 0x0782ca), ('target', 'render_pass_rva', 0x0782cf))),
    ('loop_back', 0x078591, 0x078596, (), (('target', 'loop_head_rva', 0x078591),)),
    ('recovery_guard', 0x0d9007, 0x0d9026,
     ('recovery_callback_slot_rva', 'recovery_guard_slot_rva', 'recovery_guard_slot_rva'),
     (('imm', 'guard_set', 0x0d900c), ('imm', 'guard_clear', 0x0d901c))),
)


def read(data, fields, rva, n):
    off = identity.rva_to_offset(fields, rva)
    return data[off:off + n]


def section(fields, name):
    return next(s for s in fields['sections'] if s[0].rstrip(b'\0') == name)


def decode_anchor(data, fields, relocated, md, name, lo, hi, slots, facts):
    span = read(data, fields, lo, hi - lo)
    image_base = fields['image_base']
    insns = list(md.disasm(span, image_base + lo))
    if not insns or insns[-1].address + insns[-1].size != image_base + hi or sum(i.size for i in insns) != hi - lo:
        raise ValueError(f'{name}: [0x{lo:x}, 0x{hi:x}) does not decode to whole instructions')
    by_rva = {i.address - image_base: i for i in insns}
    touching = sorted(r for r in relocated if r < hi and lo < r + 4)
    if len(touching) != len(slots):
        raise ValueError(f'{name}: relocations {[hex(r) for r in touching]}, expected {len(slots)}')
    operands = []
    for r, slot in zip(touching, slots):
        if not lo <= r <= hi - 4:
            raise ValueError(f'{name}: relocation at 0x{r:x} straddles the span')
        insn = by_rva[max(a for a in by_rva if a <= r)]
        value = struct.unpack_from('<I', span, r - lo)[0]
        absolute = [op for op in insn.operands
                    if (op.type == x86.X86_OP_MEM and op.mem.base == 0 and (op.mem.disp & 0xffffffff) == value) or
                    (op.type == x86.X86_OP_IMM and (op.imm & 0xffffffff) == value)]
        if not absolute:
            raise ValueError(f'{name}: relocated word at 0x{r:x} is not an absolute operand')
        rva, where = SLOTS[slot]
        if value - image_base != rva:
            raise ValueError(f'{name}: operand at 0x{r:x} names RVA 0x{value - image_base:x}, documented {slot} 0x{rva:x}')
        s = section(fields, where)
        if not s[2] <= rva <= s[2] + s[1] - 4:
            raise ValueError(f'{name}: {slot} outside {where.decode()}')
        operands.append((r - lo, slot))
    found = {}
    for kind, value_name, rva in facts:
        insn = by_rva.get(rva)
        if insn is None:
            raise ValueError(f'{name}: no instruction boundary at 0x{rva:x}')
        if kind == 'disp':
            mems = [op for op in insn.operands if op.type == x86.X86_OP_MEM]
            if len(mems) != 1 or mems[0].mem.base == 0:
                raise ValueError(f'{name}: 0x{rva:x} has no single based memory operand')
            got = mems[0].mem.disp
        elif kind == 'imm':
            imms = [op for op in insn.operands if op.type == x86.X86_OP_IMM]
            if len(imms) != 1:
                raise ValueError(f'{name}: 0x{rva:x} has no single immediate')
            got = imms[0].imm & 0xffffffff
        elif kind == 'target':
            if not (insn.group(x86.X86_GRP_CALL) or insn.group(x86.X86_GRP_JUMP)) or \
                    insn.operands[0].type != x86.X86_OP_IMM:
                raise ValueError(f'{name}: 0x{rva:x} is not a direct branch')
            got = insn.operands[0].imm - image_base
        else:
            raise ValueError(kind)
        if got != VALUES[value_name]:
            raise ValueError(f'{name}: {value_name} at 0x{rva:x} reads 0x{got:x}, documented 0x{VALUES[value_name]:x}')
        found.setdefault(value_name, []).append(rva)
    masked = bytearray(span)
    for off, _ in operands:
        masked[off:off + 4] = b'\0\0\0\0'
    return {'name': name, 'rva': lo, 'length': hi - lo, 'operands': operands, 'bytes': bytes(masked),
            'file_bytes': bytes(span), 'instructions': len(insns), 'facts': found}


def check_preserved_eax(data, fields, md):
    lo, hi = PRESERVED_EAX
    span = read(data, fields, lo, hi - lo)
    insns = list(md.disasm(span, fields['image_base'] + lo))
    if sum(i.size for i in insns) != hi - lo:
        raise ValueError('the marker interval does not decode to whole instructions')
    eax = {x86.X86_REG_EAX, x86.X86_REG_AX, x86.X86_REG_AL, x86.X86_REG_AH}
    for i in insns[1:]:
        _, written = i.regs_access()
        if eax & set(written) or i.group(x86.X86_GRP_CALL) or i.group(x86.X86_GRP_JUMP):
            raise ValueError(f'0x{i.address - fields["image_base"]:x} breaks eax between the marker load and store')
    first = insns[0]
    if first.mnemonic != 'mov' or first.operands[0].type != x86.X86_OP_REG or first.operands[0].reg != x86.X86_REG_EAX:
        raise ValueError('the marker interval does not start by loading eax')
    return len(insns)


def check_command_table(data, fields, relocated):
    table = SLOTS['native_command_table_rva'][0]
    rel = set(relocated)
    for case, handler in COMMAND_CASES.items():
        at = table + 4 * case
        if at not in rel:
            raise ValueError(f'case 0x{case:x}: table entry is not relocated')
        got = struct.unpack_from('<I', read(data, fields, at, 4))[0] - fields['image_base']
        if got != handler:
            raise ValueError(f'case 0x{case:x}: handler 0x{got:x}, documented 0x{handler:x}')
        if case > VALUES['native_command_max']:
            raise ValueError(f'case 0x{case:x} above the dispatch bound')


def decode(data, fields):
    relocated = identity.relocated_rvas(data, fields)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    anchors = [decode_anchor(data, fields, relocated, md, *a) for a in ANCHORS]
    witnessed = {}
    for a in anchors:
        for value_name, rvas in a['facts'].items():
            witnessed.setdefault(value_name, []).extend(rvas)
    gap = check_preserved_eax(data, fields, md)
    check_command_table(data, fields, relocated)
    v = VALUES
    # The header's own offsets name the absolute list offsets the walk and every AddTail use.
    if v['list_header'] + v['header_tail'] != v['list_tail'] or v['list_header'] + v['header_tailpred'] != v['list_tailpred']:
        raise ValueError('list_tail / list_tailpred are not the header offsets')
    # The walk reads one dword at object_class: class word low, subtype word high.
    if v['object_subtype'] != v['object_class'] + 2:
        raise ValueError('the subtype word does not follow the class word')
    # The walk reads 0x20 bytes at sector_scene; the index is its fourth dword.
    if v['sector_background_index'] - v['sector_scene'] != 3 * 4:
        raise ValueError('the background index is not dword 3 of the scene block')
    if v['sector_scene'] + 0x20 > v['sector_object_size'] or v['object_marker'] + 2 > v['sector_object_size']:
        raise ValueError('a walk read leaves the sector object')
    # Cross-check with the sector-background generator (the record recipe after the walk).
    for name in SHARED_SLOTS:
        if SLOTS[name][0] != background.SLOTS[name]:
            raise ValueError(f'{name} differs from fl_sector_background_constants')
    for name in SHARED_VALUES:
        if v[name] != background.VALUES[name]:
            raise ValueError(f'{name} differs from fl_sector_background_constants')
    background.decode(data, fields)  # the shared slots are anchored there too
    witnessed['background_count_slot_rva'] = ['fl_sector_background_constants']
    missing = sorted(set(VALUES) - set(witnessed))
    if missing:
        raise ValueError(f'values without an operand witness: {missing}')
    return {'slots': {k: s[0] for k, s in SLOTS.items()}, 'runtime_slots': list(RUNTIME_SLOTS), 'values': dict(VALUES),
            'command_cases': {hex(k): h for k, h in COMMAND_CASES.items()}, 'preserved_eax_instructions': gap,
            'anchors': anchors, 'witnesses': witnessed}


def hexes(b):
    return ', '.join(f'0x{x:02x}' for x in b)


def fragment(data, fields):
    d = decode(data, fields)
    v = d['values']
    lines = [
        '// Generated by tools/analysis/fl_fog_prefill_constants.py from X3FL.exe; do not edit.',
        f'// X3FL.exe SHA-256 {hashlib.sha256(data).hexdigest()} (provenance, never a gate).',
        '// docs/reverse-engineering/fl/fog-prefill.md, "Verified constants (2026-11-01)". The FL inputs of the',
        '// transit prefill walk (src/proxy/fog_prefill.h). Every slot is an RVA added to fl::module_base(); every',
        '// other value is a field offset or a constant read from a native operand. Nothing is 0x400000 based.',
        '#ifndef X3M_FOG_PREFILL_FL_INC_H',
        '#define X3M_FOG_PREFILL_FL_INC_H',
        '#include <cstdint>',
        'namespace x3m::fl::fog_prefill_fl {',
        '// clang-format off',
        '// Image slots. The walk dereferences the first three (the record recipe after it is',
        "// sector_background_fl's: the same count/table slots); the rest are census anchors only.",
    ]
    for name in RUNTIME_SLOTS + tuple(n for n in SLOTS if n not in RUNTIME_SLOTS):
        lines.append(f'constexpr std::uint32_t {name} = 0x{d["slots"][name]:08x};')
    groups = (
        ('Manager M = [object_manager_slot]; parentless-object list header at M + list_header: {head, tail 0, '
         'tailpred}; the head sentinel of a backward walk is M + list_header',
         ('object_manager_map', 'list_header', 'header_tail', 'header_tailpred', 'list_tail', 'list_tailpred',
          'link_next', 'link_prev')),
        ('Engine object: the walk reads the dword at object_class (class word | subtype word << 16)',
         ('object_id', 'object_class', 'object_subtype', 'object_parent', 'object_marker', 'alive_marker',
          'dead_marker')),
        ('Sector object (class 1, subtype 0 for a flown sector): one 0x20-byte read at sector_scene, index = dword 3',
         ('sector_class', 'sector_object_size', 'sector_scene', 'sector_background_index', 'record_stride')),
        ('Native command dispatch and the allocator recovery guard (census)',
         ('native_command_max', 'guard_set', 'guard_clear')),
        ('Code RVAs: constructor/setter chain and the outer-loop order (task runner before cockpit update and render)',
         ('manager_constructor_rva', 'object_initializer_rva', 'id_map_insert_rva', 'sector_constructor_rva',
          'sector_by_id_rva', 'scene_setter_rva', 'object_allocate_rva', 'sector_allocate_rva', 'task_runner_rva',
          'cockpit_update_rva', 'render_pass_rva', 'loop_head_rva')),
    )
    for title, names in groups:
        lines.append(f'// {title}')
        for name in names:
            rvas = ', '.join(f'0x{r:06x}' for r in sorted(set(d['witnesses'][name]))[:3])
            value = v[name]
            text = f'0x{value:x}' if value >= 10 else str(value)
            lines.append(f'constexpr std::uint32_t {name} = {text}; // operand at RVA {rvas}')
    lines += [
        '// Anchor: `length` bytes at module_base() + rva; each relocated operand word (zeroed in `bytes`) must',
        '// equal module_base() + operand_rva. Whole instructions; every base relocation in the span is listed.',
        'struct Anchor {',
        '    const char* name;',
        '    std::uint32_t rva, length, operands;',
        '    std::uint32_t operand_offset[3], operand_rva[3];',
        '    const std::uint8_t* bytes;',
        '};',
    ]
    for a in d['anchors']:
        lines.append(f'constexpr std::uint8_t {a["name"]}_bytes[{a["length"]}] = {{{hexes(a["bytes"])}}};')
    lines.append('constexpr Anchor anchors[] = {')
    for a in d['anchors']:
        offs = [o for o, _ in a['operands']] + [0] * (3 - len(a['operands']))
        rvas = [d['slots'][s] for _, s in a['operands']] + [0] * (3 - len(a['operands']))
        lines.append(f'    {{"{a["name"]}", 0x{a["rva"]:08x}, {a["length"]}, {len(a["operands"])}, '
                     f'{{{", ".join(str(o) for o in offs)}}}, {{{", ".join(f"0x{r:08x}" for r in rvas)}}}, '
                     f'{a["name"]}_bytes}},')
    lines += ['};', 'constexpr unsigned anchor_count = sizeof anchors / sizeof anchors[0];', '// clang-format on',
              '} // namespace x3m::fl::fog_prefill_fl', '#endif']
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
        for a in d['anchors']:
            a['bytes'] = a['bytes'].hex()
            a['file_bytes'] = a['file_bytes'].hex()
        print(json.dumps({'sha256': hashlib.sha256(data).hexdigest(), 'image_base': fields['image_base'], **d}))
        return 0
    text = fragment(data, fields)
    if args.check:
        same = args.output.exists() and args.output.read_text() == text
        print('fl_fog_prefill_constants', 'match' if same else 'differs', args.output)
        return 0 if same else 1
    args.output.write_text(text)
    print('wrote', args.output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
