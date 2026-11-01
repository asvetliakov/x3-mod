#!/usr/bin/env python3
"""X3FL.exe sun-probe and lens-bracket constants for src/engine/fl/sun_occlusion_fl.cpp (FL step 9).

Reads X3: Farnham's Legacy's X3FL.exe and writes src/engine/fl/sun_occlusion_fl_inc.h. AP contract:
docs/reverse-engineering/lens-flare-visibility.md sections 12-16 (src/proxy/own_sun_hook.h); FL findings:
docs/reverse-engineering/fl/sun-occlusion.md, "Verified hook sites and ABI (2026-11-01)". Decoded with capstone
as whole instructions at the preferred base:

  probe_walk       0x8aeb0 (AP 0x4715d0), the whole procedure: ECX = probing view -> EBX; holders root+0x18
                   minus root+0x64; owner views (EDI) with +0x270 & 1 and layer <= the probing view's; records
                   (ESI) from owner+0x2a0. Probe site 0x8af0a: mov edx,ebx; mov ecx,esi; call <probe>;
                   test eax,eax; je; mov [esi+0x30],0. EBP is the holder cursor (live), not AP's constant 0.
  probe_walk_call  0x8bb7e: cache reset view+0x37c; test view+0x270 & 0x1000; mov ecx,esi; call <probe_walk>
  probe_gates      [0xa3fd0, 0xa4063) 147 bytes: register ABI (ECX = record, EDX = view, no stack argument),
                   the prologue's two reads, then flares bit config+0xec & 0x8000 (clear: EAX=1), the signed
                   inclusive rectangle (outside: EAX=0), view+0x270 & 0x8000000 (EAX=1); the same three
                   tests in the same order as AP's 0x488720, with config +0xec instead of AP's +0xfc
  probe_visible    0xa42d3: pop edi; pop esi; xor eax,eax; pop ebx; mov esp,ebp; pop ebp; ret
  probe_fov        0xa40b5: view +0x298 (after the gates)
  probe_scale      0xa416a: view +0x300/+0x304, nonpositive -> both from [mode slot] +0x28/+0x2c
  lens_block       0x8bc54..0x8bcc9: lens holder root+0x64 (+0x18 & 1), ESI = its view +0x1c, body update
                   0x8af40, propagation, activation, selection, layer, sort, lens site 0x8bcb7: mov edx,esi;
                   call <draw list>; then the 2D overlay with root+0x6304 (outside the bracket)
  lens_target      0x97680, the whole procedure: EDX = view (-> EBX), walks root+0x40 (every selected body of
                   the active scene), instance 0xde570, material wrapper 0xe6ef0; one bare RET, no EBP, no stack
                   argument; ECX is read only as the padding word pushed for 0xde570, which reads [ebp+8] only
  record_*         collector 0x97259 (0x70 bytes), owner/source/accumulator/group stores, screen x/y/z,
                   visible = 1, size; per-pass reset 0x8b80f (record+0x30 = 0); accumulator 0x8aff0
  pass_order       inside the complete pass (0x8b6d0, caller 0x782cf): record reset < sorted loop head
                   0x8b917 <= compositor call 0x8b929 < probe walk call 0x8bb96 < loop back edge 0x8bba7 <
                   body update 0x8bc69 < lens site 0x8bcb7

Census (decode() raises on any difference): direct E8 callers of the probe (one: the site), of the probe walk (one),
of the draw list (three: ordinary 0x8ba8d, lens 0x8bcb7, cube 0x978ab: hook the call site, never the entry) and of
the pass (one); no rel32 branch anywhere in .text and no branch of the walk or pass lands inside either CALL; no base
relocation touches either CALL; no relocated word points at either target or into either CALL; the probe body
[0xa3fd0, 0xa42dc) has bare RETs only, reads no caller stack word and stores nothing at +0x30 (the caller owns
record+0x30); the lens body reads no stack word and never touches EBP.

Every base relocation inside an anchor sits on an absolute operand naming a documented slot; the anchor holds that
word zeroed and the runtime compares it with module_base() + slot RVA. The two CALL rel32 words are kept as in the
file (relative, base independent): compare the anchors before claiming; after claim_call the rel32 is the hook's.
Every value is an RVA or an offset; nothing is 0x400000 based.

usage: fl_sun_occlusion_constants.py [--exe PATH] [--output PATH] [--check] [--json]
  --check  exit 1 when the fragment on disk differs from the generated text
  --json   print the decoded constants and exit (verification/analysis/test_fl_sun_occlusion_constants.py)
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
DEFAULT_OUTPUT = ROOT / 'src/engine/fl/sun_occlusion_fl_inc.h'
_spec = importlib.util.spec_from_file_location('fl_identity_constants', ROOT / 'tools/analysis/fl_identity_constants.py')
identity = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(identity)

# Image slots (RVA, section).
SLOTS = {
    'root_slot_rva': (0x1aa100, b'.data'),     # AP 0x608518: render root (scene holders +0x18, lens holder +0x64)
    'config_slot_rva': (0x1a83b0, b'.data'),   # AP 0x606f34: config, video flags word at +0xec (AP +0xfc)
    'mode_slot_rva': (0x1a82f8, b'.data'),     # AP 0x606f38: fallback normalized scale pair at +0x28/+0x2c
}

VALUES = {
    # Code RVAs
    'probe_walk_rva': 0x08aeb0, 'probe_target_rva': 0x0a3fd0, 'body_update_rva': 0x08af40,
    'propagate_rva': 0x094b00, 'activate_rva': 0x095670, 'select_rva': 0x097720, 'layer_rva': 0x0978e0,
    'sort_rva': 0x0975c0, 'lens_target_rva': 0x097680, 'overlay_rva': 0x0e7310, 'instance_rva': 0x0de570,
    'material_wrapper_rva': 0x0e6ef0, 'allocator_rva': 0x131395, 'compositor_rva': 0x0e6770,
    'sorted_loop_head_rva': 0x08b917, 'probe_hidden_exit_rva': 0x0a4006, 'probe_visible_exit_rva': 0x0a42d3,
    'probe_gate1_pass_rva': 0x0a4012,
    # Render root and holders
    'root_holders': 0x18, 'root_draw_list': 0x40, 'root_lens_holder': 0x64, 'root_probe_node': 0x6c,
    'root_overlay': 0x6304, 'holder_flags': 0x18, 'holder_active': 1, 'holder_view': 0x1c,
    # Lens record (0x70 bytes, intrusive list)
    'record_bytes': 0x70, 'record_next': 0, 'record_prev': 4, 'record_owner': 8, 'record_source': 0xc,
    'record_accumulator': 0x10, 'record_x': 0x20, 'record_y': 0x24, 'record_z': 0x28, 'record_visible': 0x30,
    'record_size': 0x34, 'record_group': 0x38, 'collector_visible': 1, 'accumulator_step': 0x64,
    'accumulator_max': 0xc8, 'screen_offset': 0x8000,
    # View
    'view_scene': 0x1c, 'view_flags': 0x270, 'view_rect_top': 0x288, 'view_rect_bottom': 0x28c,
    'view_rect_left': 0x290, 'view_rect_right': 0x294, 'view_fov': 0x298, 'view_layer': 0x29c,
    'view_records': 0x2a0, 'view_records_tail': 0x2a4, 'view_records_tailpred': 0x2a8, 'view_scale_x': 0x300,
    'view_scale_y': 0x304, 'view_candidate_cache': 0x37c, 'view_flag_active': 1, 'view_flag_no_probe': 0x1000,
    'view_flag_probe_hidden': 0x8000000,
    # Config and mode
    'config_video_flags': 0xec, 'video_flag_flares': 0x8000, 'mode_scale_x': 0x28, 'mode_scale_y': 0x2c,
    'probe_hidden_answer': 1,
}

# (name, first RVA, end RVA (exclusive), relocated operand slots in span order, facts)
ANCHORS = (
    ('probe_walk', 0x08aeb0, 0x08af3b, ('root_slot_rva', 'root_slot_rva'),
     (('disp', 'root_holders', 0x08aeb9), ('disp', 'root_lens_holder', 0x08aec4), ('disp', 'holder_flags', 0x08aec9),
      ('imm', 'holder_active', 0x08aec9), ('disp', 'holder_view', 0x08aecf), ('disp', 'view_flags', 0x08aed7),
      ('imm', 'view_flag_active', 0x08aed7), ('disp', 'view_layer', 0x08aee0), ('disp', 'view_layer', 0x08aee6),
      ('disp', 'view_records', 0x08aeee), ('disp', 'record_visible', 0x08af00),
      ('target', 'probe_target_rva', 0x08af0a), ('disp', 'record_visible', 0x08af13),
      ('disp', 'record_next', 0x08af1a))),
    ('probe_walk_call', 0x08bb7e, 0x08bb9b, (),
     (('disp', 'view_candidate_cache', 0x08bb7e), ('disp', 'view_flags', 0x08bb88),
      ('imm', 'view_flag_no_probe', 0x08bb88), ('target', 'probe_walk_rva', 0x08bb96))),
    ('probe_gates', 0x0a3fd0, 0x0a4063, ('root_slot_rva', 'config_slot_rva'),
     (('disp', 'view_scene', 0x0a3fe2), ('disp', 'root_probe_node', 0x0a3fee), ('disp', 'config_video_flags', 0x0a3ffa),
      ('imm', 'video_flag_flares', 0x0a3ffa), ('target', 'probe_gate1_pass_rva', 0x0a4004),
      ('imm', 'probe_hidden_answer', 0x0a4006), ('disp', 'record_x', 0x0a4012), ('disp', 'view_rect_left', 0x0a4015),
      ('disp', 'screen_offset', 0x0a401b), ('target', 'probe_visible_exit_rva', 0x0a4023),
      ('disp', 'view_rect_right', 0x0a4029), ('target', 'probe_visible_exit_rva', 0x0a4031),
      ('imm', 'screen_offset', 0x0a4037), ('disp', 'record_y', 0x0a403c), ('disp', 'view_rect_top', 0x0a403f),
      ('target', 'probe_visible_exit_rva', 0x0a4045), ('disp', 'view_rect_bottom', 0x0a404b),
      ('target', 'probe_visible_exit_rva', 0x0a4051), ('disp', 'view_flags', 0x0a4057),
      ('imm', 'view_flag_probe_hidden', 0x0a4057), ('target', 'probe_hidden_exit_rva', 0x0a4061))),
    ('probe_visible', 0x0a42d3, 0x0a42dc, (), ()),
    ('probe_fov', 0x0a40b5, 0x0a40bb, (), (('disp', 'view_fov', 0x0a40b5),)),
    ('probe_scale', 0x0a416a, 0x0a418a, ('mode_slot_rva',),
     (('disp', 'view_scale_x', 0x0a416a), ('disp', 'view_scale_y', 0x0a4174), ('disp', 'mode_scale_x', 0x0a4184),
      ('disp', 'mode_scale_y', 0x0a4187))),
    ('lens_block', 0x08bc54, 0x08bcce, ('root_slot_rva',) * 6,
     (('disp', 'root_lens_holder', 0x08bc59), ('disp', 'holder_flags', 0x08bc5c), ('imm', 'holder_active', 0x08bc5c),
      ('disp', 'holder_view', 0x08bc66), ('target', 'body_update_rva', 0x08bc69),
      ('disp', 'root_lens_holder', 0x08bc75), ('target', 'propagate_rva', 0x08bc78),
      ('disp', 'root_lens_holder', 0x08bc84), ('target', 'activate_rva', 0x08bc87),
      ('disp', 'root_lens_holder', 0x08bc93), ('target', 'select_rva', 0x08bc96),
      ('disp', 'root_lens_holder', 0x08bca6), ('target', 'layer_rva', 0x08bca9), ('target', 'sort_rva', 0x08bcb0),
      ('target', 'lens_target_rva', 0x08bcb7), ('disp', 'root_overlay', 0x08bcc3), ('target', 'overlay_rva', 0x08bcc9))),
    ('lens_target', 0x097680, 0x097715, ('root_slot_rva', 'config_slot_rva'),
     (('disp', 'root_draw_list', 0x09768d), ('target', 'instance_rva', 0x0976a0),
      ('disp', 'config_video_flags', 0x0976e8), ('target', 'material_wrapper_rva', 0x097702),
      ('disp', 'record_next', 0x097707))),
    ('record_alloc', 0x097259, 0x097263, (), (('imm', 'record_bytes', 0x097259), ('target', 'allocator_rva', 0x09725b))),
    ('record_link', 0x0972dc, 0x097311, (),
     (('disp', 'record_owner', 0x0972dc), ('disp', 'record_source', 0x0972df), ('disp', 'record_accumulator', 0x0972e2),
      ('disp', 'record_group', 0x0972ef), ('disp', 'view_records_tailpred', 0x0972f2),
      ('disp', 'record_prev', 0x0972f8), ('disp', 'view_records_tail', 0x097303),
      ('disp', 'view_records_tailpred', 0x09730b))),
    ('record_size_store', 0x097468, 0x09746f, (), (('disp', 'record_size', 0x09746c),)),
    ('record_x_store', 0x0974fb, 0x0974fe, (), (('disp', 'record_x', 0x0974fb),)),
    ('record_screen', 0x09753b, 0x097550, (),
     (('disp', 'record_y', 0x09753b), ('disp', 'record_z', 0x09753e), ('imm', 'screen_offset', 0x09753e),
      ('disp', 'record_visible', 0x097549), ('imm', 'collector_visible', 0x097549))),
    ('record_reset', 0x08b80f, 0x08b84c, (),
     (('disp', 'root_holders', 0x08b80f), ('disp', 'holder_view', 0x08b817), ('disp', 'view_records', 0x08b820),
      ('disp', 'record_visible', 0x08b830), ('disp', 'record_next', 0x08b837))),
    ('accumulate', 0x08aff0, 0x08b019, (),
     (('disp', 'record_visible', 0x08aff0), ('disp', 'record_accumulator', 0x08aff6), ('imm', 'accumulator_step', 0x08aff6),
      ('imm', 'accumulator_max', 0x08affa), ('imm', 'accumulator_max', 0x08b003))),
    ('compositor_call', 0x08b917, 0x08b932, (),
     (('disp', 'view_layer', 0x08b920), ('target', 'compositor_rva', 0x08b929))),
    ('loop_back', 0x08bb9b, 0x08bbad, (), (('target', 'sorted_loop_head_rva', 0x08bba7),)),
)

PROBE_SITE_RVA = 0x08af0a
LENS_SITE_RVA = 0x08bcb7
PROBE_TARGET_END = 0x0a42dc            # one past the probe's last RET
LENS_TARGET_END = 0x097715             # one past the draw list's RET
INSTANCE_END = 0x0deb8f
PASS_RVA, PASS_END = 0x08b6d0, 0x08be31
PASS_CALLERS = [0x0782cf]
PROBE_WALK_CALLERS = [0x08bb96]
PROBE_TARGET_CALLERS = [PROBE_SITE_RVA]
LENS_TARGET_CALLERS = [0x08ba8d, LENS_SITE_RVA, 0x0978ab]
RECORD_RESET_STORE = 0x08b830
BACK_EDGE = 0x08bba7
GATES_FNV1A_FILE = 0x1abcc319ce5e4445  # the fl-codex note's figure, over the file bytes at the preferred base

PROBE_WINDOW_SHAPE = (('cmp', 'dword ptr [esi + 0x30], 0'), ('je', '0x8af1a'), ('mov', 'edx, ebx'), ('mov', 'ecx, esi'),
                      ('call', '0xa3fd0'), ('test', 'eax, eax'), ('je', '0x8af1a'), ('mov', 'dword ptr [esi + 0x30], 0'))
PROBE_ENTRY_SHAPE = (('push', 'ebp'), ('mov', 'ebp, esp'), ('and', 'esp, 0xfffffff0'), ('sub', 'esp, 0x34'),
                     ('push', 'ebx'), ('mov', 'ebx, edx'), ('mov', 'dword ptr [esp + 8], ecx'), ('push', 'esi'),
                     ('push', 'edi'))
PROBE_HIDDEN_SHAPE = (('mov', 'eax, 1'), ('pop', 'edi'), ('pop', 'esi'), ('pop', 'ebx'), ('mov', 'esp, ebp'),
                      ('pop', 'ebp'), ('ret', ''))
PROBE_VISIBLE_SHAPE = (('pop', 'edi'), ('pop', 'esi'), ('xor', 'eax, eax'), ('pop', 'ebx'), ('mov', 'esp, ebp'),
                       ('pop', 'ebp'), ('ret', ''))
LENS_WINDOW_SHAPE = (('mov', 'edx, esi'), ('call', '0x975c0'), ('mov', 'edx, esi'), ('call', '0x97680'))
LENS_CONTINUATION_SHAPE = (('mov', 'ecx, esi'),)  # after mov eax,[root]: ecx/edx/eax rewritten before any read
LENS_ENTRY_SHAPE = (('push', 'ebx'), ('push', 'esi'), ('mov', 'esi, dword ptr [0x5aa100]'), ('mov', 'ebx, edx'),
                    ('push', 'edi'), ('xor', 'edi, edi'))

REG_FAMILY = {
    'eax': {x86.X86_REG_EAX, x86.X86_REG_AX, x86.X86_REG_AL, x86.X86_REG_AH},
    'ecx': {x86.X86_REG_ECX, x86.X86_REG_CX, x86.X86_REG_CL, x86.X86_REG_CH},
    'edx': {x86.X86_REG_EDX, x86.X86_REG_DX, x86.X86_REG_DL, x86.X86_REG_DH},
    'ebp': {x86.X86_REG_EBP, x86.X86_REG_BP},
    'esi': {x86.X86_REG_ESI, x86.X86_REG_SI},
}


def read(data, fields, rva, n):
    off = identity.rva_to_offset(fields, rva)
    return data[off:off + n]


def section(fields, name):
    return next(s for s in fields['sections'] if s[0].rstrip(b'\0') == name)


def disasm(md, data, fields, lo, hi):
    """Whole instructions of [lo, hi) as RVAs; raises unless they tile the span exactly."""
    insns = list(md.disasm(read(data, fields, lo, hi - lo), lo))
    if not insns or sum(i.size for i in insns) != hi - lo or insns[-1].address + insns[-1].size != hi:
        raise ValueError(f'[0x{lo:x}, 0x{hi:x}) does not decode to whole instructions')
    return insns


def shape(insns):
    return tuple((i.mnemonic, i.op_str) for i in insns)


def fnv1a64(b):
    h = 0xcbf29ce484222325
    for x in b:
        h = ((h ^ x) * 0x100000001b3) & 0xffffffffffffffff
    return h


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
            got = mems[0].mem.disp & 0xffffffff
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


def direct_callers(data, fields, target_rva):
    """Every E8 rel32 in .text that resolves to target_rva (a byte scan: conservative, may over-count)."""
    _, vsize, va, raw_size, raw_ptr, _ = section(fields, b'.text')
    out = []
    for off in range(raw_ptr, raw_ptr + min(vsize, raw_size) - 4):
        if data[off] == 0xe8:
            rva = off - raw_ptr + va
            if (rva + 5 + struct.unpack_from('<i', data, off + 1)[0]) & 0xffffffff == target_rva:
                out.append(rva)
    return out


def rel32_into(data, fields, lo, hi):
    """Byte-scan .text for E8/E9/0F 8x rel32 whose target lies in (lo, hi) (an instruction interior)."""
    _, vsize, va, raw_size, raw_ptr, _ = section(fields, b'.text')
    out = []
    end = raw_ptr + min(vsize, raw_size)
    for off in range(raw_ptr, end - 5):
        b = data[off]
        if b in (0xe8, 0xe9):
            size = 5
        elif b == 0x0f and 0x80 <= data[off + 1] <= 0x8f:
            size = 6
        else:
            continue
        rva = off - raw_ptr + va
        target = (rva + size + struct.unpack_from('<i', data, off + size - 4)[0]) & 0xffffffff
        if lo < target < hi:
            out.append(rva)
    return out


def first_access(insns, family):
    """'read', 'write' or 'call' (a clobber of unknown use): the first access of a register family in linear order."""
    for i in insns:
        if i.group(x86.X86_GRP_CALL):
            return 'call', i.address
        read_regs, written = i.regs_access()
        if family & set(read_regs):
            return 'read', i.address
        if family & set(written):
            return 'write', i.address
    return None, None


def check_body(md, data, fields, lo, hi, name, frame):
    """Contiguous procedure: bare RETs, no jump leaves, no caller stack word read. frame='ebp': [ebp+d], d >= 8, is
    the caller's; frame='none': EBP untouched and no ESP-based memory operand. Returns (instructions, rets)."""
    body = disasm(md, data, fields, lo, hi)
    if body[-1].bytes != b'\xc3':
        raise ValueError(f'{name}: does not end in a bare RET')
    rets = [i.address for i in body if i.group(x86.X86_GRP_RET)]
    if any(i.bytes != b'\xc3' for i in body if i.group(x86.X86_GRP_RET)):
        raise ValueError(f'{name}: a RET pops stack arguments')
    leaving, stack = [], []
    for i in body:
        if i.group(x86.X86_GRP_JUMP):
            op = i.operands[0]
            if op.type != x86.X86_OP_IMM or not lo <= op.imm < hi:
                leaving.append(i.address)
        for op in i.operands:
            if op.type != x86.X86_OP_MEM:
                continue
            if frame == 'ebp' and op.mem.base == x86.X86_REG_EBP and op.mem.disp >= 8:
                stack.append(i.address)
            if frame == 'none' and op.mem.base == x86.X86_REG_ESP:
                stack.append(i.address)
        if frame == 'none':
            r, w = i.regs_access()
            if REG_FAMILY['ebp'] & (set(r) | set(w)):
                stack.append(i.address)
    if leaving or stack:
        raise ValueError(f'{name}: jumps out at {[hex(a) for a in leaving]} or reads the caller frame at '
                         f'{[hex(a) for a in stack]}')
    return body, rets


def decode(data, fields):
    relocated = identity.relocated_rvas(data, fields)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    anchors = [decode_anchor(data, fields, relocated, md, *a) for a in ANCHORS]
    by_name = {a['name']: a for a in anchors}
    witnessed = {}
    for a in anchors:
        for value_name, rvas in a['facts'].items():
            witnessed.setdefault(value_name, []).extend(rvas)
    missing = sorted(set(VALUES) - set(witnessed))
    if missing:
        raise ValueError(f'values without an operand witness: {missing}')

    # Probe site: whole-instruction window, register arguments, EAX tested, the caller's literal-zero store.
    window = disasm(md, data, fields, 0x08af00, 0x08af1a)
    if shape(window) != PROBE_WINDOW_SHAPE or window[4].address != PROBE_SITE_RVA or window[4].size != 5:
        raise ValueError(f'probe window decodes as {shape(window)}')
    # Probe entry: EDX -> EBX (view), ECX -> [esp+8] (record); EAX is written before it is read.
    entry = disasm(md, data, fields, 0x0a3fd0, 0x0a3fe2)
    if shape(entry) != PROBE_ENTRY_SHAPE:
        raise ValueError(f'probe entry decodes as {shape(entry)}')
    prologue = disasm(md, data, fields, 0x0a3fd0, 0x0a4004)
    access = {k: first_access(prologue, REG_FAMILY[k])[0] for k in ('eax', 'ecx', 'edx')}
    if access != {'eax': 'write', 'ecx': 'read', 'edx': 'read'}:
        raise ValueError(f'probe register arguments: {access}')
    hidden = disasm(md, data, fields, VALUES['probe_hidden_exit_rva'], VALUES['probe_gate1_pass_rva'])
    if shape(hidden) != PROBE_HIDDEN_SHAPE:
        raise ValueError(f'probe hidden exit decodes as {shape(hidden)}')
    if shape(disasm(md, data, fields, 0x0a42d3, PROBE_TARGET_END)) != PROBE_VISIBLE_SHAPE:
        raise ValueError('probe visible exit differs')
    probe_body, probe_rets = check_body(md, data, fields, VALUES['probe_target_rva'], PROBE_TARGET_END, 'probe', 'ebp')
    frame_writes = [i.address for i in probe_body[2:]
                    if REG_FAMILY['ebp'] & set(i.regs_access()[1]) and shape([i]) != (('pop', 'ebp'),)]
    if frame_writes:
        raise ValueError(f'probe rewrites its frame pointer at {[hex(a) for a in frame_writes]}')
    stores_30 = [i.address for i in probe_body for op in i.operands
                 if op.type == x86.X86_OP_MEM and op.access & capstone.CS_AC_WRITE and op.mem.disp == VALUES['record_visible']
                 and op.mem.base not in (x86.X86_REG_ESP, x86.X86_REG_EBP)]
    if stores_30:
        raise ValueError(f'probe stores at +0x30: {[hex(a) for a in stores_30]}')
    gates = by_name['probe_gates']
    gates_fnv_file = fnv1a64(gates['file_bytes'])
    if gates['length'] != 147 or gates_fnv_file != GATES_FNV1A_FILE:
        raise ValueError(f'gate interval {gates["length"]} bytes, FNV-1a64 0x{gates_fnv_file:016x}')

    # Lens site: mov edx,esi; call; then eax/ecx/edx are rewritten before any read, ESI is live.
    lens_window = disasm(md, data, fields, 0x08bcae, 0x08bcbc)
    if shape(lens_window) != LENS_WINDOW_SHAPE or lens_window[3].address != LENS_SITE_RVA or lens_window[3].size != 5:
        raise ValueError(f'lens window decodes as {shape(lens_window)}')
    continuation = disasm(md, data, fields, 0x08bcbc, 0x08bcc9)
    if shape(continuation)[1:2] != LENS_CONTINUATION_SHAPE:
        raise ValueError(f'lens continuation decodes as {shape(continuation)}')
    cont_access = {k: first_access(continuation, REG_FAMILY[k])[0] for k in ('eax', 'ecx', 'edx', 'esi')}
    if cont_access != {'eax': 'write', 'ecx': 'write', 'edx': 'write', 'esi': 'read'}:
        raise ValueError(f'lens continuation register use: {cont_access}')
    # ESI holds the lens view from 0x8bc66 to the site: no instruction of the block writes it.
    block = disasm(md, data, fields, 0x08bc69, LENS_SITE_RVA)
    if any(REG_FAMILY['esi'] & set(i.regs_access()[1]) for i in block if not i.group(x86.X86_GRP_CALL)):
        raise ValueError('the lens block rewrites ESI before the site')
    lens_entry = disasm(md, data, fields, 0x097680, 0x09768d)
    if shape(lens_entry)[:2] != LENS_ENTRY_SHAPE[:2] or shape(lens_entry)[3:] != LENS_ENTRY_SHAPE[3:] or \
            lens_entry[2].mnemonic != 'mov' or not lens_entry[2].op_str.startswith('esi, dword ptr ['):
        raise ValueError(f'lens entry decodes as {shape(lens_entry)}')
    lens_body, lens_rets = check_body(md, data, fields, VALUES['lens_target_rva'], LENS_TARGET_END, 'lens', 'none')
    lens_access = {k: first_access(lens_body, REG_FAMILY[k]) for k in ('eax', 'ecx', 'edx')}
    # EAX is not an input: the first instruction touching it is the instance call (a clobber) or, on the path that
    # skips it, the write at 0x976ce. The site discards EAX anyway.
    if lens_access['edx'] != ('read', 0x097688) or lens_access['eax'] != ('call', 0x0976a0) or \
            lens_access['ecx'] != ('read', 0x09769b):
        raise ValueError(f'lens register use: {lens_access}')
    pad = {i.address: i for i in lens_body}
    if shape([pad[0x09769b], pad[0x09769c], pad[0x09769f], pad[0x0976a0], pad[0x0976a5], pad[0x0976a8]]) != \
            (('push', 'ecx'), ('mov', 'ecx, dword ptr [esi + 0xc]'), ('push', 'ebx'), ('call', '0xde570'),
             ('mov', 'edi, dword ptr [esi + 0x10]'), ('add', 'esp, 8')):
        raise ValueError('the instance call does not pass (view, padding) with caller cleanup')
    instance = disasm(md, data, fields, VALUES['instance_rva'], INSTANCE_END)
    instance_args = sorted({op.mem.disp for i in instance for op in i.operands
                            if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_EBP and op.mem.disp >= 8})
    if instance_args != [8]:
        raise ValueError(f'instance helper reads caller stack words {instance_args}')

    # Census: callers, interior branches, relocations.
    census = {'probe_target_callers': direct_callers(data, fields, VALUES['probe_target_rva']),
              'probe_walk_callers': direct_callers(data, fields, VALUES['probe_walk_rva']),
              'lens_target_callers': direct_callers(data, fields, VALUES['lens_target_rva']),
              'pass_callers': direct_callers(data, fields, PASS_RVA)}
    for key, expected in (('probe_target_callers', PROBE_TARGET_CALLERS), ('probe_walk_callers', PROBE_WALK_CALLERS),
                          ('lens_target_callers', LENS_TARGET_CALLERS), ('pass_callers', PASS_CALLERS)):
        if census[key] != expected:
            raise ValueError(f'{key}: {[hex(c) for c in census[key]]}')
    for site in (PROBE_SITE_RVA, LENS_SITE_RVA):
        if [r for r in relocated if r < site + 5 and site < r + 4]:
            raise ValueError(f'a base relocation touches the call at 0x{site:x}')
        into = rel32_into(data, fields, site, site + 5)
        if into:
            raise ValueError(f'rel32 branches into the call at 0x{site:x}: {[hex(a) for a in into]}')
    walk = disasm(md, data, fields, VALUES['probe_walk_rva'], 0x08af3b)
    pass_insns = disasm(md, data, fields, PASS_RVA, PASS_END)
    local = [(i.address, i.operands[0].imm) for i in walk + pass_insns
             if (i.group(x86.X86_GRP_JUMP) or i.group(x86.X86_GRP_CALL)) and i.operands and
             i.operands[0].type == x86.X86_OP_IMM and
             any(s < i.operands[0].imm < s + 5 for s in (PROBE_SITE_RVA, LENS_SITE_RVA))]
    if local:
        raise ValueError(f'a local branch lands inside a call: {local}')
    image_base = fields['image_base']
    pointing = []
    for r in relocated:
        value = (struct.unpack_from('<I', data, identity.rva_to_offset(fields, r))[0] - image_base) & 0xffffffff
        if value in (VALUES['probe_target_rva'], VALUES['lens_target_rva'], VALUES['probe_walk_rva']) or \
                any(s <= value < s + 5 for s in (PROBE_SITE_RVA, LENS_SITE_RVA)):
            pointing.append(r)
    if pointing:
        raise ValueError(f'relocated words point at a target or a call: {[hex(r) for r in pointing]}')

    # Order inside the complete pass.
    order = [RECORD_RESET_STORE, VALUES['sorted_loop_head_rva'], 0x08b929, PROBE_WALK_CALLERS[0], BACK_EDGE,
             0x08bc69, LENS_SITE_RVA, 0x08bcc9]
    if order != sorted(order) or not PASS_RVA <= order[0] or not order[-1] < PASS_END:
        raise ValueError(f'pass order {[hex(a) for a in order]}')
    pass_at = {i.address for i in pass_insns}
    if any(a not in pass_at for a in order):
        raise ValueError('an ordered point is not an instruction boundary of the pass')

    return {'probe_site_rva': PROBE_SITE_RVA, 'lens_site_rva': LENS_SITE_RVA, 'slots': {k: s[0] for k, s in SLOTS.items()},
            'values': dict(VALUES), 'anchors': anchors, 'witnesses': witnessed,
            'probe_target_end': PROBE_TARGET_END, 'probe_target_instructions': len(probe_body), 'probe_rets': probe_rets,
            'lens_target_end': LENS_TARGET_END, 'lens_target_instructions': len(lens_body), 'lens_rets': lens_rets,
            'gates_length': gates['length'], 'gates_fnv1a_file': gates_fnv_file,
            'gates_fnv1a_masked': fnv1a64(gates['bytes']), 'probe_site_atomic': PROBE_SITE_RVA % 8 + 5 <= 8,
            'lens_site_atomic': LENS_SITE_RVA % 8 + 5 <= 8, 'pass_order': order, **census}


def hexes(b):
    return ', '.join(f'0x{x:02x}' for x in b)


GROUPS = (
    ('Code RVAs: the probe walk and its target, the lens block calls in native order, exits and branch targets',
     ('probe_walk_rva', 'probe_target_rva', 'probe_hidden_exit_rva', 'probe_visible_exit_rva', 'probe_gate1_pass_rva',
      'body_update_rva', 'propagate_rva', 'activate_rva', 'select_rva', 'layer_rva', 'sort_rva', 'lens_target_rva',
      'overlay_rva', 'instance_rva', 'material_wrapper_rva', 'allocator_rva', 'compositor_rva', 'sorted_loop_head_rva')),
    ('Render root ([root_slot]) and scene holders',
     ('root_holders', 'root_draw_list', 'root_lens_holder', 'root_probe_node', 'root_overlay', 'holder_flags',
      'holder_active', 'holder_view')),
    ('Lens record (record_bytes, intrusive list on the owner view): AP layout unchanged',
     ('record_bytes', 'record_next', 'record_prev', 'record_owner', 'record_source', 'record_accumulator', 'record_x',
      'record_y', 'record_z', 'record_visible', 'record_size', 'record_group', 'collector_visible', 'accumulator_step',
      'accumulator_max', 'screen_offset')),
    ('View: the probe gates read flags, the rectangle and (after the gates) fov/scale; layer orders the walk',
     ('view_scene', 'view_flags', 'view_rect_top', 'view_rect_bottom', 'view_rect_left', 'view_rect_right', 'view_fov',
      'view_layer', 'view_records', 'view_records_tail', 'view_records_tailpred', 'view_scale_x', 'view_scale_y',
      'view_candidate_cache', 'view_flag_active', 'view_flag_no_probe', 'view_flag_probe_hidden')),
    ('Config ([config_slot] + config_video_flags; AP +0xfc) and the fallback scale pair ([mode_slot] + 0x28/+0x2c)',
     ('config_video_flags', 'video_flag_flares', 'mode_scale_x', 'mode_scale_y', 'probe_hidden_answer')),
)


def fragment(data, fields):
    d = decode(data, fields)
    v = d['values']
    grouped = [n for _, names in GROUPS for n in names]
    if sorted(grouped) != sorted(v):
        raise ValueError('GROUPS does not list every value once')
    w = d['witnesses']
    lines = [
        '// Generated by tools/analysis/fl_sun_occlusion_constants.py from X3FL.exe; do not edit.',
        f'// X3FL.exe SHA-256 {hashlib.sha256(data).hexdigest()} (provenance, never a gate).',
        '// docs/reverse-engineering/fl/sun-occlusion.md, "Verified hook sites and ABI (2026-11-01)". Every slot and',
        '// code value is an RVA added to fl::module_base(); every other value is a field offset or a constant read',
        '// from a native operand. Nothing is 0x400000 based.',
        '//',
        '// Probe CALL (5 bytes, claim_call): ECX = record, EDX = probing view, no stack argument, plain RET, EAX = 1',
        '// "hide" (the caller then stores 0 at record+0x30). Live across it: EBX = probing view, ESI = record,',
        '// EDI = owner view, EBP = holder cursor; ECX/EDX/flags are dead after it. A forward must re-enter the',
        '// target with ECX/EDX intact and ESP at the return address. Its first five bytes lie in one aligned qword.',
        '// Lens CALL (5 bytes, claim_call): EDX = lens view (also ESI, live after it), no stack argument, plain',
        '// RET; EAX/ECX/EDX/flags dead after it. ECX on entry is pushed as an unread padding word. Draws every',
        '// selected body of the lens scene (all records), not one record; the record is not in a register.',
        '// Its five bytes straddle a qword: claim_call takes the plain-copy path (install window only).',
        '#ifndef X3M_SUN_OCCLUSION_FL_INC_H',
        '#define X3M_SUN_OCCLUSION_FL_INC_H',
        '#include <cstdint>',
        'namespace x3m::fl::sun_occlusion_fl {',
        '// clang-format off',
        f'constexpr std::uint32_t probe_site_rva = 0x{d["probe_site_rva"]:08x};  // call rel32 <probe_target_rva>',
        f'constexpr std::uint32_t lens_site_rva = 0x{d["lens_site_rva"]:08x};   // call rel32 <lens_target_rva>',
        f'constexpr bool probe_site_atomic = {"true" if d["probe_site_atomic"] else "false"}, '
        f'lens_site_atomic = {"true" if d["lens_site_atomic"] else "false"};',
        '// The probe prologue and its three early tests (anchor probe_gates): FNV-1a 64 of the span with the two',
        '// relocated slot words zeroed (the file-byte hash at the preferred base is in the RE note).',
        f'constexpr std::uint32_t probe_gates_length = {d["gates_length"]};',
        f'constexpr std::uint64_t probe_gates_fnv1a_masked = 0x{d["gates_fnv1a_masked"]:016x}ull;',
        '// Image slots',
    ]
    for name in SLOTS:
        lines.append(f'constexpr std::uint32_t {name} = 0x{d["slots"][name]:08x};')
    for title, names in GROUPS:
        lines.append(f'// {title}')
        for name in names:
            value = v[name]
            text = f'0x{value:x}' if value > 9 else str(value)
            if name.endswith('_rva'):
                text = f'0x{value:05x}'
            where = ', '.join(f'0x{r:06x}' for r in sorted(set(w[name]))[:3])
            lines.append(f'constexpr std::uint32_t {name} = {text}; // operand at RVA {where}')
    lines += [
        '// Anchor: `length` bytes at module_base() + rva; each relocated operand word (zeroed in `bytes`) must',
        '// equal module_base() + operand_rva. Whole instructions; every base relocation in the span is listed.',
        '// probe_walk and lens_block hold the two CALLs with their file rel32: compare before claiming.',
        'struct Anchor {',
        '    const char* name;',
        '    std::uint32_t rva, length, operands;',
        '    std::uint32_t operand_offset[6], operand_rva[6];',
        '    const std::uint8_t* bytes;',
        '};',
    ]
    for a in d['anchors']:
        lines.append(f'constexpr std::uint8_t {a["name"]}_bytes[{a["length"]}] = {{{hexes(a["bytes"])}}};')
    lines.append('constexpr Anchor anchors[] = {')
    for a in d['anchors']:
        offs = [o for o, _ in a['operands']] + [0] * (6 - len(a['operands']))
        rvas = [d['slots'][s] for _, s in a['operands']] + [0] * (6 - len(a['operands']))
        lines.append(f'    {{"{a["name"]}", 0x{a["rva"]:08x}, {a["length"]}, {len(a["operands"])}, '
                     f'{{{", ".join(str(o) for o in offs)}}}, {{{", ".join(f"0x{r:08x}" for r in rvas)}}}, '
                     f'{a["name"]}_bytes}},')
    lines += [
        '};',
        'constexpr unsigned anchor_count = sizeof anchors / sizeof anchors[0];',
        '// clang-format on',
        '} // namespace x3m::fl::sun_occlusion_fl',
        '#endif',
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
        out = dict(d)
        out['anchors'] = [{k: (x.hex() if isinstance(x, bytes) else x) for k, x in a.items()} for a in d['anchors']]
        print(json.dumps({'sha256': hashlib.sha256(data).hexdigest(), 'image_base': fields['image_base'], **out}))
        return 0
    text = fragment(data, fields)
    if args.check:
        same = args.output.exists() and args.output.read_text() == text
        print('fl_sun_occlusion_constants', 'match' if same else 'differs', args.output)
        return 0 if same else 1
    args.output.write_text(text)
    print('wrote', args.output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
