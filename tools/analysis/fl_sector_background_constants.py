#!/usr/bin/env python3
"""X3FL.exe sector-background constants for src/engine/fl/sector_background_fl.cpp.

Reads X3: Farnham's Legacy's X3FL.exe and writes src/engine/fl/sector_background_fl_inc.h: the image
slots, object/record/camera offsets and byte anchors the shared value-only reader
(src/proxy/sector_background.h, AP proof docs/reverse-engineering/sector-fog.md section 11) needs on FL.
Findings, AP correspondences and the safe read window: docs/reverse-engineering/fl/sector-fog.md section 6.

Every anchor is a run of whole instructions decoded with capstone at the preferred base:

  registry_lookup     inline active-cockpit lookup (AP 0x40f85b): [registry]+0x10 handle, [registry]+0 map,
                      map {buckets +0, capacity +4}, (capacity-1)&handle, link {next +0, key +4, cockpit +8}
  active_control_set  INS_SetActiveControlCockpit (AP 0x42ebea): registry +0x10 is the active handle
  ref_object_set      the reference-object command: cockpit +0xc store (resolved through the object manager)
  sector_space_set    INS_CockpitSetSectorSpace's callee 0x421c70 (AP stores at 0x42d670): cockpit +0x54,
                      class word +0x48 == 1, sector +0x13c, stride 0xdb8, table slot
  fog_copy            cockpit updater 0x421f50 (AP 0x421533): row +0x148/+0x14c -> sector camera
                      (cockpit +0x58) +0x36c/+0x370, camera +0x270 |= 0x10000
  name/rates/dust/fog_near/fog_far/stardust getters (SA_GetBgType*, AP 0x46775f..0x4679f2): count slot bound,
                      table slot, row offsets +0x44/+0x114[8]/+0x134/+0x148/+0x14c/+0x150
  setter_rebase/name/rates/fields  SA_SetBgTypeData (AP 0x467ac3): R = table + 0x44 + index*0xdb8 and the
                      rebased R offsets +0/+0xd0/+0xf0/+0x104/+0x108/+0x10c (must equal row offset - 0x44)
  far_floor           fog consumer (AP 0x4c2c1a): [config]+0x758 view distance, floors 500e6 / 100e6
  parent_walk         docked parent hop (AP 0x423813): object +0x54 parent, +0x48 class 5/6 -> parent, class 1
  sector_by_id        0x443db0 (AP 0x43a560): object manager +0x14 map, class word +0x48 == 1
  sector_constructor  0x45c110 inline object insert (AP 0x43f93b): object +8 id, sector +0x13c/+0x140/+0x144 init
  sector_bodies       background change 0x45c2a0 (AP 0x4525b0): sector +0x140 -> row +0x4c[] (stars),
                      sector +0x144 -> row +0x90[] (background nebula)
  loader_count/table  type loader 0x43e710 (AP 0x434e40): indexed publication; TBackgrounds is type 2

Every base relocation inside an anchor must sit on an absolute operand the anchor names; the anchor's
bytes hold that word zeroed and the runtime compares it with module_base() + slot RVA. Field offsets are
read from the named instructions' operands and must equal the documented values. Every value in the
fragment is an RVA or an offset; nothing is 0x400000 based.

usage: fl_sector_background_constants.py [--exe PATH] [--output PATH] [--check] [--json]
  --check  exit 1 when the fragment on disk differs from the generated text
  --json   print the decoded constants and exit (verification/analysis/test_fl_sector_background_constants.py)
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
DEFAULT_OUTPUT = ROOT / 'src/engine/fl/sector_background_fl_inc.h'
_spec = importlib.util.spec_from_file_location('fl_identity_constants', ROOT / 'tools/analysis/fl_identity_constants.py')
identity = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(identity)

# Image slots (RVAs in .data). The background table and count are element 2 of the type-table arrays.
SLOTS = {
    'cockpit_registry_slot_rva': 0x1a82e4,   # AP 0x608504
    'object_manager_slot_rva': 0x1a82ec,     # AP 0x60850c (census: sector_by_id, ref_object_set)
    'background_count_slot_rva': 0x1a8338,   # AP 0x607040
    'config_slot_rva': 0x1a83b0,             # AP 0x606f34
    'background_table_slot_rva': 0x1a83d0,   # AP 0x606fc0
    'script_vm_slot_rva': 0x1aa174,          # census only (commands report through it)
    'type_count_array_rva': 0x1a8330,        # loader: count of type t at array + 4t
    'type_table_array_rva': 0x1a83c8,        # loader: table of type t at array + 4t
}
RUNTIME_SLOTS = ('cockpit_registry_slot_rva', 'background_table_slot_rva', 'background_count_slot_rva', 'config_slot_rva')

# Documented values; each is re-read from at least one instruction operand below.
VALUES = {
    'registry_map': 0x0, 'registry_active_handle': 0x10, 'map_buckets': 0x0, 'map_capacity': 0x4,
    'link_next': 0x0, 'link_key': 0x4, 'link_value': 0x8,
    'cockpit_ref_object': 0xc, 'cockpit_sector': 0x54, 'cockpit_camera': 0x58,
    'object_id': 0x8, 'object_class': 0x48, 'object_parent': 0x54, 'sector_class': 1,
    'docked_class_a': 5, 'docked_class_b': 6,
    'object_manager_map': 0x14,
    'sector_background_index': 0x13c, 'sector_stars': 0x140, 'sector_nebula': 0x144,
    'record_stride': 0xdb8, 'record_stride_dwords': 0x36e, 'record_rebase': 0x44,
    'row_name': 0x44, 'row_star_count': 0x48, 'row_star_bodies': 0x4c, 'row_nebula_count': 0x8c,
    'row_nebula_bodies': 0x90, 'row_rates': 0x114, 'row_dust': 0x134, 'row_fog_near': 0x148,
    'row_fog_far': 0x14c, 'row_stardust': 0x150, 'rate_count': 8,
    'record_name': 0x0, 'record_rates': 0xd0, 'record_dust': 0xf0, 'record_fog_near': 0x104,
    'record_fog_far': 0x108, 'record_stardust': 0x10c,
    'camera_flags': 0x270, 'camera_fog_flag': 0x10000, 'camera_fog_near': 0x36c, 'camera_fog_far': 0x370,
    'config_view_distance': 0x758, 'view_distance_high': 3, 'view_distance_mid': 2,
    'far_floor_high': 500000000, 'far_floor_mid': 100000000,
    'background_type': 2,
    'cockpit_resolver_rva': 0x01e420, 'object_resolver_rva': 0x043d30, 'name_helper_rva': 0x10b670,
}
RECORD_SPAN = 0x120  # bytes the shared reader copies from R (sector_background.h read_record)

# (name, first RVA, end RVA (exclusive), relocated operand slots in span order, facts)
# A fact is (kind, value name, instruction RVA): 'disp' = the instruction's one memory displacement,
# 'imm' = its immediate, 'target' = its branch target as an RVA.
ANCHORS = (
    ('registry_lookup', 0x0cf46f, 0x0cf4a6, ('cockpit_registry_slot_rva',),
     (('disp', 'registry_active_handle', 0x0cf474), ('disp', 'registry_map', 0x0cf477),
      ('disp', 'map_buckets', 0x0cf479), ('disp', 'map_capacity', 0x0cf483), ('disp', 'link_key', 0x0cf494),
      ('disp', 'link_next', 0x0cf499), ('disp', 'link_value', 0x0cf4a3))),
    ('active_control_set', 0x031fab, 0x031fd3, ('cockpit_registry_slot_rva', 'script_vm_slot_rva'),
     (('target', 'cockpit_resolver_rva', 0x031fba), ('disp', 'registry_active_handle', 0x031fd0))),
    ('ref_object_set', 0x0308d6, 0x030914, ('cockpit_registry_slot_rva', 'object_manager_slot_rva', 'script_vm_slot_rva'),
     (('target', 'cockpit_resolver_rva', 0x0308e2), ('target', 'object_resolver_rva', 0x0308f9),
      ('disp', 'cockpit_ref_object', 0x030911))),
    ('sector_space_set', 0x021c70, 0x021c9d, ('background_table_slot_rva',),
     (('disp', 'cockpit_sector', 0x021c77), ('disp', 'object_class', 0x021c86), ('imm', 'sector_class', 0x021c86),
      ('disp', 'sector_background_index', 0x021c8d), ('imm', 'record_stride', 0x021c8d))),
    ('fog_copy', 0x022e3f, 0x022e89, ('background_table_slot_rva',),
     (('disp', 'cockpit_sector', 0x022e3f), ('disp', 'cockpit_camera', 0x022e42),
      ('disp', 'sector_background_index', 0x022e45), ('imm', 'record_stride', 0x022e45),
      ('disp', 'row_fog_far', 0x022e54), ('disp', 'row_fog_near', 0x022e5b),
      ('disp', 'camera_fog_near', 0x022e66), ('disp', 'camera_fog_far', 0x022e6c),
      ('disp', 'camera_flags', 0x022e74), ('imm', 'camera_fog_flag', 0x022e74))),
    ('name_getter', 0x0722be, 0x0722f6, ('background_count_slot_rva', 'background_table_slot_rva'),
     (('imm', 'record_stride', 0x0722e6), ('disp', 'row_name', 0x0722f2))),
    ('rates_getter', 0x0723ec, 0x07242e, ('background_count_slot_rva', 'background_table_slot_rva'),
     (('imm', 'rate_count', 0x072411), ('imm', 'record_stride_dwords', 0x07241a), ('disp', 'row_rates', 0x072427))),
    ('dust_getter', 0x07243f, 0x07246b, ('background_count_slot_rva', 'background_table_slot_rva'),
     (('imm', 'record_stride', 0x072459), ('disp', 'row_dust', 0x072464))),
    ('fog_near_getter', 0x07250c, 0x072538, ('background_count_slot_rva', 'background_table_slot_rva'),
     (('imm', 'record_stride', 0x072526), ('disp', 'row_fog_near', 0x072531))),
    ('fog_far_getter', 0x072549, 0x072575, ('background_count_slot_rva', 'background_table_slot_rva'),
     (('imm', 'record_stride', 0x072563), ('disp', 'row_fog_far', 0x07256e))),
    ('stardust_getter', 0x072586, 0x0725b2, ('background_count_slot_rva', 'background_table_slot_rva'),
     (('imm', 'record_stride', 0x0725a0), ('disp', 'row_stardust', 0x0725ab))),
    ('setter_rebase', 0x072703, 0x072714, ('background_table_slot_rva',),
     (('imm', 'record_stride', 0x072703), ('imm', 'record_rebase', 0x07270f))),
    ('setter_name', 0x072723, 0x07272e, (),
     (('disp', 'record_name', 0x072723), ('target', 'name_helper_rva', 0x072727), ('disp', 'record_name', 0x07272c))),
    ('setter_rates', 0x07274b, 0x072778, (),
     (('disp', 'record_rates', 0x07274b), ('imm', 'rate_count', 0x072773))),
    ('setter_fields', 0x072783, 0x0727d8, (),
     (('disp', 'record_dust', 0x072786), ('disp', 'record_fog_near', 0x0727c0), ('disp', 'record_fog_far', 0x0727c9),
      ('disp', 'record_stardust', 0x0727d2))),
    ('far_floor', 0x0e4bc1, 0x0e4bf9, ('config_slot_rva',),
     (('disp', 'camera_fog_far', 0x0e4bc6), ('disp', 'config_view_distance', 0x0e4bcc),
      ('imm', 'view_distance_high', 0x0e4bd5), ('imm', 'far_floor_high', 0x0e4bda),
      ('imm', 'view_distance_mid', 0x0e4be6), ('imm', 'far_floor_mid', 0x0e4beb))),
    ('parent_walk', 0x025031, 0x025062, (),
     (('disp', 'object_parent', 0x025031), ('disp', 'object_class', 0x02503c), ('imm', 'docked_class_a', 0x025040),
      ('imm', 'docked_class_b', 0x025046), ('disp', 'object_parent', 0x02504c), ('disp', 'object_class', 0x025057),
      ('imm', 'sector_class', 0x025057))),
    ('sector_by_id', 0x043db0, 0x043df4, ('object_manager_slot_rva',),
     (('disp', 'object_manager_map', 0x043db5), ('disp', 'link_key', 0x043dd0), ('disp', 'link_value', 0x043de0),
      ('disp', 'object_class', 0x043de9), ('imm', 'sector_class', 0x043de9))),
    ('sector_constructor', 0x05c14d, 0x05c1ab, ('object_manager_slot_rva', 'config_slot_rva'),
     (('disp', 'object_manager_map', 0x05c179), ('disp', 'object_id', 0x05c181),
      ('disp', 'sector_background_index', 0x05c199), ('disp', 'sector_stars', 0x05c19f),
      ('disp', 'sector_nebula', 0x05c1a5))),
    ('sector_bodies', 0x05c320, 0x05c383, (),
     (('disp', 'sector_stars', 0x05c320), ('disp', 'row_star_bodies', 0x05c32c), ('disp', 'row_star_count', 0x05c336),
      ('disp', 'sector_nebula', 0x05c351), ('disp', 'row_nebula_bodies', 0x05c357),
      ('disp', 'row_nebula_count', 0x05c364))),
    ('loader_count', 0x0405b3, 0x0405ba, ('type_count_array_rva',), ()),
    ('loader_table', 0x040637, 0x04063e, ('type_table_array_rva',), ()),
)
# The 0xdb8 imul with a memory source carries both the displacement and the immediate.
REBASED = (('record_name', 'row_name'), ('record_rates', 'row_rates'), ('record_dust', 'row_dust'),
           ('record_fog_near', 'row_fog_near'), ('record_fog_far', 'row_fog_far'), ('record_stardust', 'row_stardust'))


def read(data, fields, rva, n):
    off = identity.rva_to_offset(fields, rva)
    return data[off:off + n]


def section(fields, name):
    return next(s for s in fields['sections'] if s[0].rstrip(b'\0') == name)


def decode_anchor(data, fields, relocated, md, name, lo, hi, slots, facts):
    span = read(data, fields, lo, hi - lo)
    insns = list(md.disasm(span, fields['image_base'] + lo))
    if not insns or insns[-1].address + insns[-1].size != fields['image_base'] + hi or \
            sum(i.size for i in insns) != hi - lo:
        raise ValueError(f'{name}: [0x{lo:x}, 0x{hi:x}) does not decode to whole instructions')
    by_rva = {i.address - fields['image_base']: i for i in insns}
    touching = sorted(r for r in relocated if r < hi and lo < r + 4)
    if len(touching) != len(slots):
        raise ValueError(f'{name}: relocations {[hex(r) for r in touching]}, expected {len(slots)}')
    image_base = fields['image_base']
    data_section = section(fields, b'.data')
    operands = []
    for r, slot in zip(touching, slots):
        if not lo <= r <= hi - 4:
            raise ValueError(f'{name}: relocation at 0x{r:x} straddles the span')
        owner = max(a for a in by_rva if a <= r)
        insn = by_rva[owner]
        value = struct.unpack_from('<I', span, r - lo)[0]
        absolute = [op for op in insn.operands
                    if (op.type == x86.X86_OP_MEM and op.mem.base == 0 and (op.mem.disp & 0xffffffff) == value) or
                    (op.type == x86.X86_OP_IMM and (op.imm & 0xffffffff) == value)]
        if not absolute:
            raise ValueError(f'{name}: relocated word at 0x{r:x} is not an absolute operand of 0x{owner:x}')
        if value - image_base != SLOTS[slot]:
            raise ValueError(f'{name}: operand at 0x{r:x} names RVA 0x{value - image_base:x}, documented {slot} '
                             f'0x{SLOTS[slot]:x}')
        if not data_section[2] <= SLOTS[slot] < data_section[2] + data_section[1] - 4:
            raise ValueError(f'{name}: {slot} outside .data')
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
            if not (insn.group(x86.X86_GRP_CALL) or insn.group(x86.X86_GRP_JUMP)) or insn.operands[0].type != x86.X86_OP_IMM:
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


def decode(data, fields):
    relocated = identity.relocated_rvas(data, fields)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    anchors = [decode_anchor(data, fields, relocated, md, *a) for a in ANCHORS]
    witnessed = {}
    for a in anchors:
        for value_name, rvas in a['facts'].items():
            witnessed.setdefault(value_name, []).extend(rvas)
    # Cross-checks between independently read operands.
    for rebased, row in REBASED:
        if VALUES[rebased] != VALUES[row] - VALUES['record_rebase']:
            raise ValueError(f'{rebased} is not {row} - record_rebase')
    if VALUES['record_stride_dwords'] * 4 != VALUES['record_stride']:
        raise ValueError('dword stride disagrees with the byte stride')
    t = VALUES['background_type']
    if SLOTS['type_table_array_rva'] + 4 * t != SLOTS['background_table_slot_rva'] or \
            SLOTS['type_count_array_rva'] + 4 * t != SLOTS['background_count_slot_rva']:
        raise ValueError('the background slots are not element background_type of the loader arrays')
    # background_type is slot arithmetic over the loader's indexed publications, not one operand.
    witnessed['background_type'] = [a['rva'] for a in anchors if a['name'] in ('loader_count', 'loader_table')]
    missing = sorted(set(VALUES) - set(witnessed))
    if missing:
        raise ValueError(f'values without an operand witness: {missing}')
    last = max(VALUES[r] for r, _ in REBASED) + 4
    if not last <= RECORD_SPAN or VALUES['record_rebase'] + RECORD_SPAN > VALUES['record_stride']:
        raise ValueError('the reader span does not cover the fields or leaves the row')
    if VALUES['record_rates'] + 4 * VALUES['rate_count'] > VALUES['record_dust']:
        raise ValueError('the rates overlap the dust count')
    return {'slots': dict(SLOTS), 'runtime_slots': list(RUNTIME_SLOTS), 'values': dict(VALUES),
            'record_span': RECORD_SPAN, 'anchors': anchors, 'witnesses': witnessed}


def hexes(b):
    return ', '.join(f'0x{x:02x}' for x in b)


def fragment(data, fields):
    d = decode(data, fields)
    v = d['values']
    lines = [
        '// Generated by tools/analysis/fl_sector_background_constants.py from X3FL.exe; do not edit.',
        f'// X3FL.exe SHA-256 {hashlib.sha256(data).hexdigest()} (provenance, never a gate).',
        '// docs/reverse-engineering/fl/sector-fog.md section 6. Every slot is an RVA added to fl::module_base(); every',
        '// other value is a field offset or a constant read from a native operand. Nothing is 0x400000 based.',
        '#ifndef X3M_SECTOR_BACKGROUND_FL_INC_H',
        '#define X3M_SECTOR_BACKGROUND_FL_INC_H',
        '#include <cstdint>',
        'namespace x3m::fl::sector_background_fl {',
        '// clang-format off',
        '// Image slots (.data). The reader dereferences the first four; the rest are census anchors only.',
    ]
    for name in RUNTIME_SLOTS + tuple(n for n in SLOTS if n not in RUNTIME_SLOTS):
        lines.append(f'constexpr std::uint32_t {name} = 0x{d["slots"][name]:08x};')
    groups = (
        ('Cockpit registry: [slot] -> registry; map {buckets, capacity}; link {next, key, cockpit}',
         ('registry_map', 'registry_active_handle', 'map_buckets', 'map_capacity', 'link_next', 'link_key', 'link_value')),
        ('Cockpit and simulation objects', ('cockpit_ref_object', 'cockpit_sector', 'cockpit_camera', 'object_id',
                                            'object_class', 'object_parent', 'sector_class', 'docked_class_a',
                                            'docked_class_b', 'object_manager_map')),
        ('Sector: background index; body selections (+0x140 selects row +0x4c[] star bodies, +0x144 row +0x90[] '
         'nebula bodies)', ('sector_background_index', 'sector_stars', 'sector_nebula')),
        ('TBackgrounds (type 2): R = [table] + index * record_stride + record_rebase; offsets below are from R',
         ('background_type', 'record_stride', 'record_rebase', 'record_name', 'record_rates', 'rate_count',
          'record_dust', 'record_fog_near', 'record_fog_far', 'record_stardust')),
        ('Sector camera (cockpit +0x58) raw fog pair and the flag the fog copy always sets',
         ('camera_flags', 'camera_fog_flag', 'camera_fog_near', 'camera_fog_far')),
        ('Effective far: [config]+config_view_distance >= high -> max(far, far_floor_high), == mid -> far_floor_mid',
         ('config_view_distance', 'view_distance_high', 'view_distance_mid', 'far_floor_high', 'far_floor_mid')),
    )
    for title, names in groups:
        lines.append(f'// {title}')
        for name in names:
            rvas = ', '.join(f'0x{r:06x}' for r in sorted(set(d['witnesses'][name]))[:3])
            value = v[name]
            text = f'0x{value:x}' if value >= 10 else str(value)
            lines.append(f'constexpr std::uint32_t {name} = {text}; // operand at RVA {rvas}')
    lines.append(f'constexpr std::uint32_t record_span = 0x{d["record_span"]:x}; // one row copy from R')
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
              '} // namespace x3m::fl::sector_background_fl', '#endif']
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
        print('fl_sector_background_constants', 'match' if same else 'differs', args.output)
        return 0 if same else 1
    args.output.write_text(text)
    print('wrote', args.output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
