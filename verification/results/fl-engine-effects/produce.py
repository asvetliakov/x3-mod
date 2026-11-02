#!/usr/bin/env python3
"""FL engine-plumes native inputs, verified from X3FL.exe bytes (2026-11-02).

Owning note: docs/reverse-engineering/fl/engine-effects.md, section "Verified 2026-11-02 against X3FL.exe".
Decodes every anchor with Capstone over tools/analysis/inspect_x3.py's PE32 reader, asserts the operand
facts the FL engine-plumes adapter depends on, and writes contract.json beside this script: derived offsets,
RVAs, counts, booleans and window hashes only (no executable bytes, no disassembly text).

Window identity: bytes of [start, end) with every base-relocation HIGHLOW dword inside the window zeroed
(the convention of src/engine/fl/object_trace_fl_inc.h), hashed as FNV-1a 32 and SHA-256. A hook compares
the live window after zeroing the same dwords (or after rebasing them to module_base + target RVA).

usage: python3 verification/results/fl-engine-effects/produce.py [--exe X3FL.exe] [--ap X3AP.exe]
       [--game C:/X3FL] [--ap-game C:/X3AP-orig] [--check]
  --check  exit 1 when contract.json on disk differs from the regenerated text
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

import capstone

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'tools/effects'))
from inspect_x3 import PE32  # noqa: E402

FL_SHA256 = 'e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8'
AP_SHA256 = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
BASE = 0x400000
OUT = Path(__file__).resolve().parent / 'contract.json'


def fnv1a32(data):
    h = 0x811c9dc5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xffffffff
    return h


class Image:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        self.pe = PE32(self.data)
        assert self.pe.base == BASE
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        pe_off = self.pe.u32(0x3c)
        self.dll_characteristics = struct.unpack_from('<H', self.data, pe_off + 24 + 70)[0]
        reloc_rva, reloc_size = struct.unpack_from('<II', self.data, pe_off + 24 + 96 + 5 * 8)
        self.fixups = set()
        if reloc_size:
            o, end = self.pe.offset(reloc_rva), self.pe.offset(reloc_rva) + reloc_size
            while o < end:
                page, size = struct.unpack_from('<II', self.data, o)
                if size < 8:
                    break
                for k in range((size - 8) // 2):
                    e = struct.unpack_from('<H', self.data, o + 8 + 2 * k)[0]
                    if e >> 12 == 3:
                        self.fixups.add(BASE + page + (e & 0xfff))
                    else:
                        assert e >> 12 == 0, 'only HIGHLOW/ABSOLUTE fixups expected'
                o += size
        text = next(s for s in self.pe.sections if s['name'] == '.text')
        self.text = (BASE + text['rva'], BASE + text['rva'] + text['raw_size'], text['offset'])

    def read(self, va, n):
        o = self.pe.offset(va - BASE)
        return self.data[o:o + n]

    def insns(self, start, end):
        out = list(self.md.disasm(self.read(start, end - start), start))
        assert out and out[-1].address + out[-1].size == end, f'decode of {start:#x}..{end:#x} does not end on {end:#x}'
        return out

    def at(self, va, text):
        """Assert the whole instruction at va decodes as `mnemonic op_str` == text; return it."""
        i = next(self.md.disasm(self.read(va, 16), va))
        got = f'{i.mnemonic} {i.op_str}'.strip()
        assert got == text, f'{va:#x}: {got!r} != {text!r}'
        return i

    def window(self, start, end):
        b = bytearray(self.read(start, end - start))
        relocated = sorted(f for f in self.fixups if start <= f and f + 4 <= end)
        straddling = [f for f in self.fixups if start - 3 <= f < end and not (start <= f and f + 4 <= end)]
        assert not straddling, f'fixup straddles window {start:#x}'
        for f in relocated:
            b[f - start:f - start + 4] = b'\0\0\0\0'
        self.insns(start, end)  # whole instructions
        return dict(start_rva=start - BASE, end_rva=end - BASE, length=end - start,
                    relocated_dword_rvas=[f - BASE for f in relocated],
                    fnv1a32='%08x' % fnv1a32(bytes(b)), sha256=hashlib.sha256(bytes(b)).hexdigest())

    def direct_edges(self):
        """Linear-sweep census of direct call/jmp/jcc targets in .text (skipping undecodable bytes)."""
        start, end, off = self.text
        code = self.data[off:off + (end - start)]
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        edges = []
        pos = 0
        while pos < len(code):
            last = pos
            for i in md.disasm(code[pos:pos + 0x10000], start + pos):
                last = i.address + i.size - start
                if i.mnemonic == 'call' or i.mnemonic.startswith('j') or i.mnemonic.startswith('loop'):
                    if i.op_str.startswith('0x'):
                        edges.append((i.address, i.mnemonic, int(i.op_str, 16)))
            pos = last if last > pos else pos + 1  # an undecodable byte stops capstone: resume one byte later
        return edges


def qword_geometry(site):
    return dict(call_in_one_aligned_qword=(site & 7) + 5 <= 8, rel32_in_one_aligned_qword=((site + 1) & 7) + 4 <= 8)


def produce(fl_path, ap_path, game, ap_game):
    fl = Image(fl_path)
    ap = Image(ap_path)
    assert fl.sha256 == FL_SHA256 and ap.sha256 == AP_SHA256
    c = dict(schema=1, producer='verification/results/fl-engine-effects/produce.py',
             inputs=dict(fl_sha256=fl.sha256, ap_sha256=ap.sha256,
                         fl_dynamic_base=bool(fl.dll_characteristics & 0x40),
                         fl_dll_characteristics='%#06x' % fl.dll_characteristics,
                         fl_highlow_fixups=len(fl.fixups)))

    # ---- 1. suppression sites -------------------------------------------------------------------------
    f = fl
    f.at(0x414d90, 'push ebp')
    f.at(0x414da7, 'mov edi, ecx')                       # object arrives in ECX, kept in EDI
    f.at(0x414e07, 'movzx esi, word ptr [edi + 0x48]')   # class word
    f.at(0x414e12, 'cmp eax, 7')
    f.at(0x414e15, 'je 0x414e3a')
    f.at(0x414e17, 'cmp eax, 0xa')
    f.at(0x414e44, 'mov eax, dword ptr [0x5a83e4]')      # class 7 table = 0x5a83c8 + 7*4
    f.at(0x414e2f, 'mov eax, dword ptr [0x5a83f0]')      # class 10 table = 0x5a83c8 + 10*4
    f.at(0x414e60, 'mov eax, dword ptr [0x5a83c8]')      # class 0 table
    f.at(0x414e84, 'test si, si')                        # nonzero class -> child walk
    f.at(0x414eff, 'mov esi, dword ptr [esi + 0xc]')     # root +0xc first child
    f.at(0x414f10, 'mov eax, dword ptr [esi + 0x130]')
    f.at(0x414f16, 'and eax, 1')
    f.at(0x414f1f, 'test dword ptr [esi + 0x260], 0xffe')
    f.at(0x4150ba, 'mov esi, dword ptr [esi]')           # child +0 next
    f.at(0x4150b5, 'inc ebx')                             # k
    # A
    f.at(0x415054, 'test dword ptr [esi + 0x260], 0x4000')
    f.at(0x41505e, 'jne 0x41507d')
    f.at(0x415064, 'lea ecx, [esp + 0x48]')              # &pos (= [esp+0x40] before the two pushes)
    f.at(0x415068, 'mov edx, ebx')                        # EDX = k
    f.at(0x415071, 'push edi')                            # obj
    f.at(0x415072, 'push eax')                            # effect id
    f.at(0x415073, 'xor ecx, ecx')                        # ECX = 0 (key component)
    f.at(0x415075, 'call 0x4150e0')
    f.at(0x41507a, 'add esp, 0x20')
    f.at(0x41507d, 'mov ecx, dword ptr [esp + 0x14]')    # ECX reloaded (trail id)
    # A callee: register args and plain RET
    f.at(0x415101, 'mov eax, ecx')
    f.at(0x415107, 'mov ebx, edx')
    f.at(0x41510c, 'cmp dword ptr [esi], 0')             # first flags writer, nothing reads flags before
    f.at(0x415111, 'mov ecx, dword ptr [ebp + 0xc]')     # obj (stack arg 2)
    f.at(0x415119, 'cmp dword ptr [esi + 0x20], eax')
    f.at(0x41511e, 'cmp dword ptr [esi + 0x24], ebx')
    f.at(0x4150e5, 'push 0x54cb52')                       # SEH frame
    f.at(0x41526a, 'ret')
    a_rets = [i for i in f.insns(0x4150e0, 0x41526b) if i.mnemonic == 'ret']
    assert [(i.address, i.op_str) for i in a_rets] == [(0x41526a, '')]
    a_flag_prefix = [i.mnemonic for i in f.insns(0x4150e0, 0x41510c)]
    assert not set(a_flag_prefix) & {'adc', 'sbb', 'jb', 'jae', 'je', 'jne', 'cmovl', 'setb', 'pushfd', 'rcl', 'rcr'}
    # B
    f.at(0x415085, 'test dword ptr [esi + 0x260], 0x2000')
    f.at(0x41508f, 'jne 0x4150b5')
    f.at(0x415091, 'mov eax, dword ptr [0x5a83b0]')      # cfg (relocated disp32)
    f.at(0x415096, 'test dword ptr [eax + 0xec], 0x40000000')
    f.at(0x4150a0, 'je 0x4150b5')
    f.at(0x4150a2, 'lea eax, [esp + 0x40]')
    f.at(0x4150a6, 'push eax'); f.at(0x4150a7, 'push edi'); f.at(0x4150a8, 'push ecx'); f.at(0x4150a9, 'push ebx')
    f.at(0x4150aa, 'call 0x413640')
    f.at(0x4150af, 'jmp 0x4150b5')
    f.at(0x413640, 'sub esp, 0xc')
    f.at(0x413656, 'mov ecx, dword ptr [eax]')           # ECX written before any read
    f.at(0x413658, 'mov edi, dword ptr [esp + 0x28]')    # obj (stack arg 3)
    b_rets = [i for i in f.insns(0x413640, 0x41381a) if i.mnemonic == 'ret']
    assert [(i.address, i.op_str) for i in b_rets] == [(0x41371e, '0x10'), (0x413816, '0x10')]
    f.at(0x4645bb, 'mov ecx, ebx'); f.at(0x4645bd, 'call 0x414d90')
    f.at(0x4645ad, 'mov eax, dword ptr [0x5a82ec]'); f.at(0x4645b2, 'cmp dword ptr [eax], 0')
    # bit-0-only JET writer: the effect-instance scene builder 0x415490 (callers include 0x4153ed inside the
    # instance constructor 0x415340 that A's callee calls at 0x41514a); AP twin 0x41514e in 0x414cf0
    f.at(0x41591b, 'mov ecx, dword ptr [0x5a8314]'); f.at(0x41592c, 'call 0x50d240')
    f.at(0x415939, 'or dword ptr [esi + 0x130], 1'); f.at(0x41514a, 'call 0x415340')

    edges = f.direct_edges()
    def callers(t):
        return sorted(a for a, m, x in edges if x == t and m == 'call')
    windows = dict(a=(0x415054, 0x41507d), b=(0x415085, 0x4150af))
    inside = {k: sorted((a, x) for a, m, x in edges if s < x < e) for k, (s, e) in windows.items()}
    assert inside == {'a': [], 'b': []}, inside
    image_dwords = {k: [] for k in windows}
    for k, (s, e) in windows.items():
        for v in range(s + 1, e):
            for m in re.finditer(re.escape(struct.pack('<I', v)), f.data):
                image_dwords[k].append((f.pe.va(m.start()), v))
    # the one known hit is the ASCII ".CRT$XPA" substring (0x415058 = b'XPA\0'); require nothing else
    assert all(f.data[f.pe.offset(va - BASE) - 5:f.pe.offset(va - BASE) - 1] == b'.CRT' for va, _ in sum(image_dwords.values(), []))
    c['suppression'] = dict(
        routine=dict(entry_rva=0x414d90 - BASE, end_rva=0x4150d1 - BASE, object_register='ecx', object_kept_in='edi',
                     direct_callers_rva=[a - BASE for a in callers(0x414d90)]),
        class_test=dict(offset=0x48, width='int16', ship=7, other_child_walk_class=10, dispatch_rva=0x414e07 - BASE,
                        window=f.window(0x414e07, 0x414e1c),
                        type_table_base_rva=0x5a83c8 - BASE, ap_type_table_base_va=0x606fb8),
        a=dict(window=f.window(*windows['a']), call_rva=0x415075 - BASE, target_rva=0x4150e0 - BASE,
               return_rva=0x41507a - BASE, join_rva=0x41507d - BASE, **qword_geometry(0x415075),
               ecx='0 (effect-list key component; AP stack arg 1)', edx='k, jet ordinal (AP stack arg 2)',
               stack_at_entry={'+4': 'effect id', '+8': 'object', '+0xc..+0x14': '0', '+0x18': '&pos',
                               '+0x1c..+0x20': '0'},
               object_slot_at_entry=8, caller_pop=0x20, callee_ret='ret',
               target_direct_callers_rva=[a - BASE for a in callers(0x4150e0)]),
        b=dict(window=f.window(*windows['b']), call_rva=0x4150aa - BASE, target_rva=0x413640 - BASE,
               return_rva=0x4150af - BASE, join_rva=0x4150b5 - BASE, **qword_geometry(0x4150aa),
               stack_at_entry={'+4': 'k', '+8': 'trail id', '+0xc': 'object', '+0x10': '&pos'},
               object_slot_at_entry=0xc, callee_ret='ret 0x10', ret_rvas=[0x41371e - BASE, 0x413816 - BASE],
               target_direct_callers_rva=[a - BASE for a in callers(0x413640)]),
        live_across_calls=['ebx (k)', 'esi (child node)', 'edi (object)', 'ebp', 'esp-relative locals'],
        dead_after_calls=['eax', 'ecx', 'edx', 'status flags', 'xmm0'],
        branch_targets_strictly_inside_windows=0, direct_edges_censused=len(edges),
        image_dwords_into_windows=sum(len(v) for v in image_dwords.values()),
        image_dwords_into_windows_classified='.CRT$XPA section-name substring')
    assert c['suppression']['routine']['direct_callers_rva'] == [0x4645bd - BASE]
    # the redirects claim call sites, not targets: the targets' other direct callers stay native
    assert {0x414eaa - BASE, 0x415075 - BASE} <= set(c['suppression']['a']['target_direct_callers_rva'])
    assert {0x414ee5 - BASE, 0x4150aa - BASE} <= set(c['suppression']['b']['target_direct_callers_rva'])

    # ---- 2. JET flag ------------------------------------------------------------------------------------
    f.at(0x43de93, 'mov edx, dword ptr [0x5a8314]')
    f.at(0x43de9f, 'mov esi, dword ptr [ecx + 0x144]')
    f.at(0x43deb3, 'or dword ptr [ecx + 0x130], 0x4000001')
    f.at(0x43debd, 'mov edx, dword ptr [0x5a832c]')
    f.at(0x43dede, 'or dword ptr [ecx + 0x130], 0x4000000')
    f.at(0x43dee8, 'mov dword ptr [ecx + 0x1d8], 5')
    f.at(0x43ddd0, 'mov edx, dword ptr [0x5a82fc]')
    f.at(0x43f6fb, 'push 0x588544')                       # "addon2\types\Bodies"
    o = f.pe.offset(0x588544 - BASE)
    assert f.data[o:f.data.index(b'\0', o)] == b'addon2\\types\\Bodies'
    sbtype = {}
    o = f.pe.offset(0x576c68 - BASE)
    for k in range(13):
        p, idx = struct.unpack_from('<II', f.data, o + 8 * k)
        so = f.pe.offset(p - BASE)
        sbtype[f.data[so:f.data.index(b'\0', so)].decode()] = idx
    assert sbtype['SBTYPE_JET'] == 6 and sbtype['SBTYPE_SMALLJET'] == 12
    assert 0x5a82fc + 4 * 6 == 0x5a8314 and 0x5a82fc + 4 * 12 == 0x5a832c
    def or130_census(img):
        t0, t1, toff = img.text
        code = img.data[toff:toff + t1 - t0]
        rows = []
        for m in re.finditer(rb'[\x81\x83][\x80-\x87\x88-\x8f]\x30\x01\x00\x00', code):
            va = t0 + m.start()
            i = next(img.md.disasm(code[m.start():m.start() + 12], va), None)
            if i is not None and i.mnemonic == 'or' and '+ 0x130]' in i.op_str:
                rows.append((va, int(i.op_str.rsplit(', ', 1)[1], 16) & 0xffffffff))
        return rows
    fl_or = or130_census(f)
    ap_or = or130_census(ap)
    def bits(rows, mask):
        return sorted(va - BASE for va, v in rows if v & mask)
    c['jet_flag'] = dict(
        writer_rva=0x43deb3 - BASE, ap_writer_va=0x434708, offset=0x130, value=0x4000001,
        classifier_entry_rva=0x43ddd0 - BASE, model_offset=0x144, list_array_rva=0x5a82fc - BASE,
        sbtype_jet=6, sbtype_smalljet=12, jet_list_slot_rva=0x5a8314 - BASE, smalljet_list_slot_rva=0x5a832c - BASE,
        smalljet_writer_rva=0x43dede - BASE, smalljet_threshold=dict(offset=0x1d8, value=5),
        bodies_loader_string_rva=0x588544 - BASE, bodies_loader_push_rva=0x43f6fb - BASE,
        or_writers=dict(fl=len(fl_or), ap=len(ap_or)),
        writers_setting_bit0=dict(fl=bits(fl_or, 1), ap=[va for va in bits(ap_or, 1)]),
        writers_setting_0x4000000=dict(fl=bits(fl_or, 0x4000000), ap=bits(ap_or, 0x4000000)),
        window=f.window(0x43de93, 0x43debd))

    # ---- 3. throttle, size, SETA --------------------------------------------------------------------------
    f.at(0x4646a6, 'test byte ptr [esi + 0x130], 1')
    f.at(0x4646c2, 'mov edi, 0x28f')
    f.at(0x4646c7, 'push 0x20000'); f.at(0x4646cc, 'push 0x4000')
    f.at(0x464932, 'mov edx, dword ptr [esi + 0x88]')
    f.at(0x46493c, 'imul ecx, dword ptr [esp + 0x44], 0x106')
    f.at(0x464950, 'imul eax, dword ptr [esp + 0x44], 0x106')
    f.at(0x4649be, 'call 0x4a3ae0')
    f.at(0x464880, 'or dword ptr [esi + 0x12c], 0x100000')
    f.at(0x4a3af8, 'mov dword ptr [esi + 0x80], edi')
    f.at(0x4a3b01, 'mov dword ptr [esi + 0x84], ebx')
    f.at(0x4a3b09, 'mov dword ptr [esi + 0x88], ebp')
    f.at(0x4a3b1b, 'mov dword ptr [esi + 0xa4], 0xffffffff')
    f.at(0x4a3b28, 'imul dword ptr [esi + 0x70]')
    f.at(0x4a3b37, 'mov dword ptr [esi + 0xa0], eax')
    f.at(0x4a3b70, 'ret 0xc')
    f.at(0x46301c, 'mov edi, dword ptr [eax + 0x708]')
    f.at(0x46319d, 'sub eax, dword ptr [ebx + 0xa8]')
    f.at(0x4631a7, 'cmp eax, 0x3e8')
    f.at(0x4631c3, 'mov dword ptr [esp + 0x44], eax')
    f.at(0x4ffed5, 'mov ecx, dword ptr [0x5a83b0]')
    f.at(0x4fff26, 'mov eax, dword ptr [ecx + 0xbc]')
    f.at(0x4fff2c, 'imul dword ptr [ecx + 0xc0]')
    f.at(0x4fff4c, 'mov edx, 0xc8')
    f.at(0x4fff67, 'add dword ptr [ecx + 0x708], eax')
    ap.at(0x4d1ef0, 'mov edx, dword ptr [ecx + 0xd0]')
    ap.at(0x4d1ef6, 'mov eax, dword ptr [ecx + 0xcc]')
    ap.at(0x4d1f5c, 'add dword ptr [ecx + 0x718], eax')
    ap.at(0x45b01a, 'imul eax, eax, 0x106')
    c['throttle'] = dict(
        node=dict(base_scale=0x70, scale_x=0x80, scale_y=0x84, scale_z=0x88, max_scaled=0xa0, radius_cache=0xa4,
                  flags12c_hidden_bit=0x100000, model=0x144, ap_model=0x140),
        drive=dict(range_rva=[0x4646a6 - BASE, 0x4649c3 - BASE], ap_range_va=[0x45ad8b, 0x45b03c],
                   endpoints_q16=[0x4000, 0x20000], hide_threshold_q16=0x28f, ramp_q16_per_game_ms=0x106,
                   setter_rva=0x4a3ae0 - BASE, setter_call_rva=0x4649be - BASE, setter_ret='ret 0xc'),
        game_time=dict(cfg_slot_rva=0x5a83b0 - BASE, clock_offset=0x708, ap_clock_offset=0x718,
                       object_last_offset=0xa8, dt_cap_ms=1000, dt_negative_becomes_ms=1000,
                       dt_read_rva=0x46319d - BASE),
        seta=dict(site=f.window(0x4fff26, 0x4fff32), warp_offset=0xbc, mult_offset=0xc0, ap_warp_offset=0xcc,
                  ap_mult_offset=0xd0, cfg_load_rva=0x4ffed5 - BASE, real_ms_clamp=[1, 200],
                  clock_add_rva=0x4fff67 - BASE))

    # ---- 4. body table ----------------------------------------------------------------------------------
    f.at(0x4867c1, 'mov edi, dword ptr [0x5aa100]')
    f.at(0x4867e9, 'add esi, dword ptr [edi + 0xb4]')
    f.at(0x4867f3, 'mov eax, dword ptr [edi + 0xb8]')
    f.at(0x486803, 'mov eax, dword ptr [edi + 0xbc]')
    f.at(0x486809, 'lea ecx, [esi*8]'); f.at(0x486810, 'sub ecx, esi')
    f.at(0x486812, 'mov eax, dword ptr [eax + ecx*4 + 0xc]')
    f.at(0x48a5f7, 'mov dword ptr [edi + 0xb4], 0x2af8')
    ap.at(0x46df69, 'mov ecx, dword ptr [0x608518]')
    ap.at(0x46dfbd, 'mov eax, dword ptr [ecx + edi*4 + 0xc]')
    c['body_table'] = dict(global_rva=0x5aa100 - BASE, ap_global_va=0x608518, fixed_count_offset=0xb4,
                           dynamic_count_offset=0xb8, slots_offset=0xbc, slot_stride=0x1c, name_offset=0xc,
                           fixed_count=11000, fixed_count_writer_rva=0x48a5f7 - BASE,
                           id_to_slot_rva=0x4867c0 - BASE, ap_id_to_slot_va=0x46df60,
                           id_map='id<1000: id; <20000: id-9000; else fixed+id-20000 (same as AP)',
                           window=f.window(0x4867c1, 0x486816))
    c['body_table']['bodies_txt'] = bodies(game, ap_game)

    # ---- 5. cull site (brief) ---------------------------------------------------------------------------
    f.at(0x4961d3, 'mov eax, dword ptr [esi + 0x18]')
    f.at(0x4961d6, 'mov edx, dword ptr [esi + 0x1d8]')
    f.at(0x4961ef, 'cmp ecx, edx')
    f.at(0x4961f3, 'and dword ptr [esi + 0x12c], 0xfffffffd')
    f.at(0x496207, 'test eax, 0x4000000')
    f.at(0x49621c, 'mov ecx, dword ptr [esi + 0x144]')
    ap.at(0x47d2a2, 'mov ecx, dword ptr [edi + 0x18]')
    ap.at(0x47d2c3, 'and dword ptr [edi + 0x12c], 0xfffffffd')
    f.at(0x48bb24, 'mov eax, dword ptr [0x5aa100]'); f.at(0x48bb29, 'mov ecx, dword ptr [eax + 0x64]')
    c['cull'] = dict(site_rva=0x4961d3 - BASE, ap_site_va=0x47d2a2, size_cull_rva=0x4961f3 - BASE,
                     ap_size_cull_va=0x47d2c3, node='esi', measure='ecx', model_read_rva=0x49621c - BASE,
                     lens_scene_read_rva=0x48bb24 - BASE, window=f.window(0x4961d3, 0x49621c),
                     qualified=False)
    return c


def bodies(game, ap_game):
    import catalogue_mounts
    import engine_bodies as eb
    from sector_fog_census import Assets
    out = {}
    lists = {}
    for label, root, ed, key in (('fl', game, 'fl', 'addon2/types/bodies.txt'), ('ap', ap_game, 'ap', 'addon/types/bodies.txt')):
        root = Path(root)
        cats = [p.relative_to(root).as_posix() for p in catalogue_mounts.mounted_catalogues(root, ed)]
        a = Assets(root, catalogues=cats)
        entry = a.entries[key][-1]
        data = a.read_entry(entry)
        bl = eb.body_lists(data.decode('latin1'))
        jb = eb.jet_bodies(bl)
        lists[label] = jb
        rec = dict(member=entry['path'], catalogue=entry['source'], decoded_sha256=hashlib.sha256(data).hexdigest(),
                   jet=bl['jet']['declared'], smalljet=bl['smalljet']['declared'], smalljet_tokens=bl['smalljet']['tokens'],
                   unique_bodies=len(jb), numeric=sum(v['id'] is not None for v in jb.values()),
                   forward_slash_names=sum('/' in k for k in jb))
        if label == 'fl':
            src = {}
            for k, v in jb.items():
                if v['id'] is not None:
                    continue
                p = 'objects/' + k.replace('\\', '/').lower()
                hits = [e for ext in ('.bob', '.bod') for e in a.entries.get(p + ext, [])]
                s = hits[-1]['source'] if hits else 'none'
                src[s] = src.get(s, 0) + 1
            rec['named_body_file_source'] = dict(sorted(src.items()))
            root_entry = a.entries['types/bodies.txt'][-1]
            rec['root_types_bodies_shadow'] = dict(catalogue=root_entry['source'],
                                                   jet=eb.body_lists(a.read_entry(root_entry).decode('latin1'))['jet']['declared'])
        out[label] = rec
    # what tools/effects/engine_bodies.py's default view (Assets(root): root + physical addon/) picks on the FL root
    _, prov = Assets(Path(game)).get('types/Bodies.txt')
    out['engine_bodies_default_view_on_fl_root'] = dict(catalogue=prov['source'], member=prov['member'],
                                                         decoded_sha256=prov['decoded_sha256'])
    fl_keys, ap_keys = set(lists['fl']), set(lists['ap'])
    out['only_fl'] = sorted(fl_keys - ap_keys)
    out['only_ap'] = sorted(ap_keys - fl_keys)
    out['common'] = len(fl_keys & ap_keys)
    out['casefold_only_fl'] = len({k.lower() for k in fl_keys} - {k.lower() for k in ap_keys})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default='/mnt/c/X3FL/X3FL.exe')
    ap.add_argument('--ap', default='/mnt/c/X3AP-orig/X3AP.exe')
    ap.add_argument('--game', default='/mnt/c/X3FL')
    ap.add_argument('--ap-game', default='/mnt/c/X3AP-orig')
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    text = json.dumps(produce(a.exe, a.ap, a.game, a.ap_game), indent=1, sort_keys=True) + '\n'
    assert json.loads(text)
    if a.check:
        same = OUT.is_file() and OUT.read_text() == text
        print('contract.json', 'matches' if same else 'differs')
        return 0 if same else 1
    OUT.write_text(text)
    print('wrote', OUT.relative_to(ROOT), len(text), 'bytes')
    return 0


if __name__ == '__main__':
    sys.exit(main())
