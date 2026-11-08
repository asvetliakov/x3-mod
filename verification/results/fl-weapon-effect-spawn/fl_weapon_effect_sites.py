#!/usr/bin/env python3
"""FL counterparts of the AP weapon-effect call sites (read-only; RVAs, window witnesses, relocations, counts).

Owning note: docs/reverse-engineering/fl/weapon-effect-spawn.md. AP sites: src/proxy/weapon_effects_sites.h,
weapon_effects_bolt_sites.h, weapon_effects_death_sites.h (docs/reverse-engineering/weapon-effect-spawn.md section 6).

  python3 -I verification/results/fl-weapon-effect-spawn/fl_weapon_effect_sites.py \
      [--fl /mnt/c/X3FL/X3FL.exe] [--ap /mnt/c/X3AP-orig/X3AP.exe] [--game /mnt/c/X3FL] \
      > verification/results/fl-weapon-effect-spawn/fl_weapon_effect_sites_out.txt

Capstone over tools/analysis/inspect_x3.py's PE32 reader. For every site: the call bytes and decoded target, the AP
twin's call and target, the window [start, end) decoded as whole instructions, its bytes with every base-relocation
HIGHLOW dword zeroed (the code_window_fl.h convention) and FNV-1a 32 over them, the relocated dwords (window offset,
target RVA), whether the five call bytes / the rel32 lie in one aligned qword, direct branches (linear sweep of .text)
landing strictly inside the call bytes and at instruction boundaries inside the window, image dwords equal to an
address in the call bytes 1..4, and the E8 callers of the target. Then every frame and layout anchor below is asserted
as one whole instruction (`mnemonic op_str`); the output prints the counts and any failure. The last block is a data
census of the FL installation's TBullets / TShips columns the AP classifier reads (catalogue readers of
tools/analysis/sector_fog_census.py). Addresses are printed as RVAs (VA - 0x400000; the image is DYNAMIC_BASE).
"""
import argparse
import collections
import hashlib
import pathlib
import re
import struct
import sys

import capstone

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from inspect_x3 import PE32  # noqa: E402

FL_SHA256 = 'e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8'
AP_SHA256 = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
BASE = 0x400000


def fnv1a32(data):
    h = 0x811c9dc5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xffffffff
    return h


class Image:
    def __init__(self, path, sha):
        self.data = pathlib.Path(path).read_bytes()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        assert self.sha256 == sha, f'{path}: sha256 {self.sha256} != {sha}'
        self.pe = PE32(self.data)
        assert self.pe.base == BASE
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        pe_off = self.pe.u32(0x3c)
        self.dll_characteristics = struct.unpack_from('<H', self.data, pe_off + 24 + 70)[0]
        reloc_rva, reloc_size = struct.unpack_from('<II', self.data, pe_off + 24 + 96 + 5 * 8)
        self.fixups = set()
        o, end = (self.pe.offset(reloc_rva), self.pe.offset(reloc_rva) + reloc_size) if reloc_size else (0, 0)
        while o < end:
            page, size = struct.unpack_from('<II', self.data, o)
            if size < 8:
                break
            for k in range((size - 8) // 2):
                e = struct.unpack_from('<H', self.data, o + 8 + 2 * k)[0]
                if e >> 12 == 3:
                    self.fixups.add(BASE + page + (e & 0xfff))
            o += size
        text = next(s for s in self.pe.sections if s['name'] == '.text')
        self.text = (BASE + text['rva'], BASE + text['rva'] + text['raw_size'], text['offset'])
        self.fails = []
        self.checks = collections.Counter()

    def read(self, va, n):
        o = self.pe.offset(va - BASE)
        return self.data[o:o + n]

    def insns(self, start, end):
        out = list(self.md.disasm(self.read(start, end - start), start))
        assert out and out[-1].address + out[-1].size == end, f'decode of {start:#x}..{end:#x} does not end on {end:#x}'
        return out

    def at(self, group, va, text):
        i = next(self.md.disasm(self.read(va, 16), va))
        got = f'{i.mnemonic} {i.op_str}'.strip()
        self.checks[group] += 1
        if got != text:
            self.fails.append(f'{group} {va - BASE:#08x}: {got!r} != {text!r}')
        return i

    def direct_edges(self):
        start, end, off = self.text
        code = self.data[off:off + (end - start)]
        edges, pos = [], 0
        while pos < len(code):
            last = pos
            for i in self.md.disasm(code[pos:pos + 0x10000], start + pos):
                last = i.address + i.size - start
                if (i.mnemonic == 'call' or i.mnemonic.startswith('j') or i.mnemonic.startswith('loop')) \
                        and i.op_str.startswith('0x'):
                    edges.append((i.address, i.mnemonic, int(i.op_str, 16)))
            pos = last if last > pos else pos + 1
        return edges

    def dword_index(self):
        idx = collections.defaultdict(int)
        for s in self.pe.sections:
            raw = self.data[s['offset']:s['offset'] + s['raw_size']]
            for k in range(0, max(0, len(raw) - 3)):
                v = struct.unpack_from('<I', raw, k)[0]
                if 0x401000 <= v < 0x600000:
                    idx[v] += 1
        return idx


# name, AP site, AP target, FL site, FL target, FL window [start, end) (preferred VAs)
SITES = [
    ('impact', 0x449d85, 0x413c40, 0x453390, 0x414440, 0x45338b, 0x4533b6),
    ('tap1', 0x413da8, 0x414a70, 0x414583, 0x415270, 0x414572, 0x41458d),
    ('tap2', 0x413e79, 0x414a70, 0x414650, 0x415270, 0x41463f, 0x41465a),
    ('flight', 0x4146c0, 0x4148a0, 0x414eaa, 0x4150e0, 0x414e95, 0x414eb2),
    ('object', 0x45ac7d, 0x414590, 0x4645bd, 0x414d90, 0x4645ad, 0x4645cc),
    ('muzzle', 0x446268, 0x413c00, 0x44f639, 0x4150e0, 0x44f603, 0x44f641),
    ('death', 0x44ac64, 0x413610, 0x4543b3, 0x413ed0, 0x4543ac, 0x4543c2),  # AP area site; AP 0x44acb2 joins below
]
AP_DEATH_OTHER = 0x44acb2
# Frame witnesses (read only, compared at install like the AP prologue/writer witnesses): name, [start, end)
WITNESSES = [
    ('impact prologue 0x453070 (sub esp,8 .. mov [esp+0x10],ebx)', 0x453070, 0x453089),
    ('impact shield-walk total, absorb path (mov [esp+0x10],ebx)', 0x45318d, 0x453191),
    ('impact absorbed -> ESI = 1 (xor ebp,ebp; mov [esp+0x14],ebp; lea esi,[ebp+1])', 0x453194, 0x45319d),
    ('impact source reload + flag 0x20 test (mov ebx,[esp+0x1c] .. jne 0x4532e3)', 0x4531a1, 0x4531d6),
    ('impact hull add (add ebp,esi)', 0x4532a9, 0x4532ab),
    ('impact total reload + piloted clamp (mov ebp,[esp+0x10] .. cmovg ebp,ecx)', 0x4532e3, 0x453300),
    ('muzzle gun slot (movsx eax,[eax+4]; mov [ebp-0x88],eax)', 0x44e4a4, 0x44e4ae),
    ('muzzle bullet index (movzx ecx,[edx+eax+0x4c]; mov [ebp-0x8c],ecx)', 0x44e4f9, 0x44e504),
    ('death row chooser call (push [esi+0x40] .. call 0x4138e0)', 0x413f3d, 0x413f4b),
]


def rva(va):
    return f'{va - BASE:#08x}'


def call_target(img, site):
    b = img.read(site, 5)
    return b, (site + 5 + struct.unpack_from('<i', b, 1)[0]) & 0xffffffff if b[0] == 0xe8 else None


def anchors(f):
    a = f.at
    # ---- impact: the damage routine 0x453070 (ECX = victim, [esp+4] source, [esp+8] shield, [esp+0xc] hull, ret 0xc)
    g = 'impact-frame'
    for va, t in [
        (0x453070, 'sub esp, 8'), (0x453073, 'push ebx'), (0x453074, 'push ebp'),
        (0x453075, 'mov ebp, dword ptr [esp + 0x18]'), (0x45307b, 'push esi'), (0x45307c, 'push edi'),
        (0x45307d, 'mov edi, ecx'), (0x45307f, 'mov dword ptr [esp + 0x14], ebp'),
        (0x453085, 'mov dword ptr [esp + 0x10], ebx'), (0x45308d, 'cmp cx, 7'), (0x453096, 'shr eax, 0x14'),
        (0x45309d, 'cmp cx, 1'), (0x4530a3, 'cmp cx, 0x12'), (0x4530b9, 'mov esi, dword ptr [esi]'),
        (0x4530bd, 'mov eax, dword ptr [edi + 0x50]'), (0x4530c0, 'mov esi, dword ptr [eax + 0x6c]'),
        (0x4530cb, 'cmp dword ptr [esi], 0'), (0x4530d4, 'mov eax, dword ptr [esi + 0x50]'),
        (0x4530d7, 'mov ecx, dword ptr [eax + 4]'), (0x4530de, 'movsx eax, word ptr [esi + 0x4a]'),
        (0x4530e2, 'mov edx, dword ptr [0x5a83ec]'), (0x4530ee, 'mov eax, dword ptr [eax + edx + 0x48]'),
        (0x453102, 'mov edx, 0x3e80000'), (0x45311d, 'jg 0x453161'),
        (0x453151, 'mov dword ptr [esp + 0x10], ebx'), (0x453155, 'mov dword ptr [ecx + 4], 0'),
        (0x45318d, 'mov dword ptr [esp + 0x10], ebx'), (0x453191, 'sub dword ptr [ecx + 4], eax'),
        (0x453194, 'xor ebp, ebp'), (0x453196, 'mov dword ptr [esp + 0x14], ebp'), (0x45319a, 'lea esi, [ebp + 1]'),
        (0x45319f, 'xor esi, esi'), (0x4531a1, 'mov ebx, dword ptr [esp + 0x1c]'),
        (0x4531be, 'test byte ptr [eax + ecx + 0x58], 0x20'), (0x4531c5, 'mov ecx, 1'), (0x4531cc, 'xor ecx, ecx'),
        (0x4531ce, 'test esi, esi'), (0x4531d0, 'jne 0x4532e3'), (0x4531d6, 'mov esi, dword ptr [esp + 0x24]'),
        (0x4531dc, 'je 0x4532e3'), (0x4531fb, 'test byte ptr [eax + edx + 0x58], 0xa0'),
        (0x45324f, 'test byte ptr [edx + ecx + 0x58], 0x80'), (0x45325f, 'call 0x4584b0'),
        (0x453264, 'mov ebp, dword ptr [esp + 0x10]'), (0x453268, 'test esi, esi'), (0x45326a, 'jle 0x4532e7'),
        (0x4532a4, 'call 0x4584b0'), (0x4532a9, 'add ebp, esi'), (0x4532bd, 'mov ebp, dword ptr [esp + 0x10]'),
        (0x4532e3, 'mov ebp, dword ptr [esp + 0x10]'), (0x4532ea, 'shr eax, 1'), (0x4532ec, 'test al, 1'),
        (0x4532f6, 'mov ecx, 0xc350'), (0x4532fb, 'cmp ebp, ecx'), (0x4532fd, 'cmovg ebp, ecx'),
        (0x453307, 'mov dword ptr [esp + 0x24], ebp'), (0x45333b, 'call 0x42f7f0'), (0x42f906, 'ret 8'),
        (0x42f81e, 'push esi'), (0x42f901, 'pop esi'),
        (0x453340, 'mov eax, dword ptr [edi + 0x70]'), (0x453347, 'test dword ptr [eax + 0x12c], 0x100000'),
        (0x453356, 'test eax, 0x40000000'), (0x45335d, 'cmp word ptr [ebx + 0x48], 0'),
        (0x45336c, 'call 0x449690'), (0x453375, 'sub esp, 8'), (0x453379, 'call 0x41e460'), (0x41e543, 'ret 0xc'),
        (0x453382, 'test byte ptr [eax + 0x150], 1'), (0x453389, 'jne 0x453398'),
        (0x45338b, 'push ecx'), (0x45338c, 'mov edx, ebx'), (0x45338e, 'mov ecx, edi'), (0x453390, 'call 0x414440'),
        (0x453395, 'add esp, 4'), (0x453398, 'mov ecx, edi'), (0x45339a, 'call 0x459d80'),
        (0x4533a3, 'mov edx, ebx'), (0x4533a7, 'call 0x417a40'), (0x4533b0, 'add esp, 8'), (0x4533b3, 'ret 0xc'),
        # the impact function: ECX = victim, EDX = source; reads no stack argument (frame realigned); plain ret
        (0x414440, 'push ebp'), (0x414443, 'and esp, 0xfffffff0'), (0x41444b, 'mov ebx, ecx'),
        (0x41444e, 'mov edi, edx'), (0x414458, 'mov esi, dword ptr [ebx + 0x70]'),
        (0x414463, 'cmp dword ptr [esi + 0x1c], 0'), (0x41448a, 'mov eax, dword ptr [ebx + 0xa4]'),
        (0x41449d, 'lea edx, [esi + 0x40]'), (0x4144a0, 'lea ecx, [esi + 0x180]'), (0x4144ab, 'lea eax, [esi + 0x30]'),
        (0x4144d5, 'test ax, ax'), (0x4144de, 'cmp ax, 0xa'),
        (0x41454b, 'mov eax, dword ptr [0x5a83c8]'), (0x414550, 'mov esi, dword ptr [ecx + eax + 0x78]'),
        (0x41455c, 'mov eax, dword ptr [0x5a83f0]'), (0x414561, 'mov esi, dword ptr [ecx + eax + 0x68]'),
        (0x41456d, 'call 0x414c10'), (0x4145af, 'mov dword ptr [ecx + 0x14], edi'),
        (0x4145c4, 'ret'), (0x41468d, 'ret'), (0x414696, 'ret'),
        # the AP twin's frame (weapon_effects_sites.h): the same guards before the call
        ]:
        a(g, va, t)
    # ---- inner taps: 0x415270 (ECX = kind 3, EDX = projectile id, [esp+4] eff, +8 hit, +0xc &pos, +0x10 0; ret)
    g = 'taps'
    for va, t in [
        (0x414572, 'mov edx, dword ptr [edi + 8]'), (0x414575, 'lea eax, [esp + 0x18]'), (0x414579, 'push 0'),
        (0x41457b, 'push eax'), (0x41457c, 'push ebx'), (0x41457d, 'push esi'), (0x41457e, 'mov ecx, 3'),
        (0x414583, 'call 0x415270'), (0x414588, 'mov ecx, eax'), (0x41458a, 'add esp, 0x18'),
        (0x41458d, 'test ecx, ecx'),
        (0x41463f, 'mov edx, dword ptr [ebx + 8]'), (0x414642, 'lea eax, [esp + 0x18]'), (0x414646, 'push 0'),
        (0x414648, 'push eax'), (0x414649, 'push edi'), (0x41464a, 'push esi'), (0x41464b, 'mov ecx, 3'),
        (0x414650, 'call 0x415270'), (0x414655, 'mov ecx, eax'), (0x414657, 'add esp, 0x18'),
        (0x41465a, 'test ecx, ecx'), (0x414678, 'mov dword ptr [ecx + 0x14], ebx'),
        (0x415270, 'push ecx'), (0x41527b, 'cmp ecx, 3'), (0x415284, 'mov eax, dword ptr [esp + 0x18]'),
        (0x41528b, 'mov ebx, dword ptr [eax*4 + 0x61c8a0]'), (0x415296, 'mov edi, dword ptr [eax*4 + 0x61c8b0]'),
        (0x41529d, 'mov ebp, dword ptr [0x5a82ec]'), (0x4152a3, 'add edi, 0x3c'),
        (0x4152a6, 'test dword ptr [edi], 0x10000000'), (0x4152c4, 'cmp dword ptr [eax + 4], edx'),
        (0x4152d1, 'add edi, 0x70'), (0x415301, 'ret'), (0x415330, 'ret'),
        (0x4152f4, 'call 0x4150e0'), (0x415323, 'call 0x4150e0')]:
        a(g, va, t)
    # ---- the spawner 0x4150e0 (ECX = kind, EDX = k, 8 stack dwords: eff, obj, a5, a6, a7, &pos, t0, &orient)
    g = 'spawner'
    for va, t in [(0x4150e5, 'push 0x54cb52'), (0x415101, 'mov eax, ecx'), (0x415107, 'mov ebx, edx'),
                  (0x415111, 'mov ecx, dword ptr [ebp + 0xc]'), (0x415119, 'cmp dword ptr [esi + 0x20], eax'),
                  (0x41511e, 'cmp dword ptr [esi + 0x24], ebx'), (0x41514a, 'call 0x415340'), (0x41526a, 'ret'),
                  (0x41534a, 'lea eax, [eax*4 + 0x61c8a0]'), (0x415351, 'mov dword ptr [esi + 8], eax'),
                  (0x41535c, 'mov edx, 0x14'), (0x41536a, 'call 0x4d9180'), (0x415395, 'ret 4')]:
        a(g, va, t)
    # ---- flight effect (inside the per-object routine 0x414d90, class-0 branch; EDI = the bullet)
    g = 'flight'
    for va, t in [
        (0x414e07, 'movzx esi, word ptr [edi + 0x48]'), (0x414e56, 'movsx eax, word ptr [edi + 0x4a]'),
        (0x414e5a, 'imul ecx, eax, 0xdb8'), (0x414e60, 'mov eax, dword ptr [0x5a83c8]'),
        (0x414e65, 'mov edx, dword ptr [ecx + eax + 0x74]'), (0x414e69, 'mov ebx, dword ptr [ecx + eax + 0x84]'),
        (0x414e84, 'test si, si'), (0x414e8c, 'movaps xmmword ptr [esp + 0x40], xmm0'), (0x414e91, 'test edx, edx'),
        (0x414e93, 'jle 0x414eb2'), (0x414e99, 'lea eax, [esp + 0x48]'), (0x414e9d, 'xor ecx, ecx'),
        (0x414ea6, 'push edi'), (0x414ea7, 'push edx'), (0x414ea8, 'xor edx, edx'), (0x414eaa, 'call 0x4150e0'),
        (0x414eaf, 'add esp, 0x20'), (0x414eb2, 'test ebx, ebx'), (0x414eba, 'mov eax, dword ptr [0x5a83b0]'),
        (0x414edc, 'lea eax, [esp + 0x40]'), (0x4150c7, 'mov eax, 1')]:
        a(g, va, t)
    # ---- object tap (sector update, per object; ECX = obj, no stack argument)
    g = 'object'
    for va, t in [(0x4645ad, 'mov eax, dword ptr [0x5a82ec]'), (0x4645b2, 'cmp dword ptr [eax], 0'),
                  (0x4645b5, 'je 0x464d3a'), (0x4645bb, 'mov ecx, ebx'), (0x4645bd, 'call 0x414d90'),
                  (0x4645c2, 'movzx esi, word ptr [ebx + 0x48]'), (0x4645c6, 'movsx eax, si'),
                  (0x4645c9, 'cmp eax, 0xa'), (0x414d90, 'push ebp'), (0x414da7, 'mov edi, ecx'),
                  (0x414db1, 'mov esi, dword ptr [edi + 0x70]'), (0x414db4, 'mov eax, dword ptr [esi + 0x100]'),
                  (0x4150d0, 'ret')]:
        a(g, va, t)
    # ---- muzzle (fire routine 0x44e310: ECX = ship -> EDI / [ebp-0x3c]; the wrapper 0x413c00 is inlined)
    g = 'muzzle'
    for va, t in [
        (0x44e310, 'push ebx'), (0x44e324, 'mov ebp, esp'), (0x44e334, 'mov dword ptr fs:[0], esp'),
        (0x44e34e, 'mov edi, ecx'), (0x44e350, 'mov dword ptr [ebp - 0x3c], edi'),
        (0x44e35b, 'cmp word ptr [edi + 0x48], 7'), (0x44e375, 'mov eax, dword ptr [ecx + eax + 0x180]'),
        (0x44e467, 'mov eax, dword ptr [edi + 0x50]'), (0x44e46a, 'mov esi, dword ptr [eax + 0x60]'),
        (0x44e46d, 'mov dword ptr [ebp - 0x18], esi'), (0x44e4a4, 'movsx eax, word ptr [eax + 4]'),
        (0x44e4a8, 'mov dword ptr [ebp - 0x88], eax'), (0x44e4ca, 'movzx ecx, word ptr [esi + 0x4a]'),
        (0x44e4f4, 'mov eax, dword ptr [0x5a83e8]'), (0x44e4f9, 'movzx ecx, word ptr [edx + eax + 0x4c]'),
        (0x44e4fe, 'mov dword ptr [ebp - 0x8c], ecx'), (0x44e510, 'cmp ecx, dword ptr [0x5a8330]'),
        (0x44e51c, 'mov edx, dword ptr [edx + eax + 0x44]'), (0x44e525, 'imul ecx, ecx, 0xdb8'),
        (0x44e548, 'mov dword ptr [ebp - 0x24], ecx'), (0x44f427, 'je 0x44f5cf'),
        (0x44f51d, 'mov eax, dword ptr [eax + ecx + 0x58]'), (0x44f521, 'test al, 2'), (0x44f586, 'test al, 4'),
        (0x44f603, 'mov eax, dword ptr [0x5a83c8]'), (0x44f60b, 'mov esi, dword ptr [ebp - 0x24]'),
        (0x44f60e, 'mov eax, dword ptr [esi + eax + 0x7c]'), (0x44f612, 'test eax, eax'), (0x44f614, 'je 0x44f641'),
        (0x44f616, 'mov edx, dword ptr [ebp - 0x88]'), (0x44f61c, 'lea ecx, [ebp - 0x2e0]'), (0x44f622, 'push ecx'),
        (0x44f623, 'push 0'), (0x44f625, 'lea ecx, [ebp - 0x120]'), (0x44f62b, 'push ecx'), (0x44f632, 'push edi'),
        (0x44f633, 'push eax'), (0x44f634, 'mov ecx, 4'), (0x44f639, 'call 0x4150e0'), (0x44f63e, 'add esp, 0x20'),
        (0x44f641, 'mov eax, dword ptr [ebp - 0x28]'), (0x44f644, 'cmp dword ptr [eax + 0x10], 0'),
        (0x44f64e, 'mov edx, dword ptr [0x5a83c0]'), (0x44f738, 'mov edi, dword ptr [ebp - 0x3c]'),
        (0x44f73b, 'mov esi, dword ptr [ebp - 0x24]'), (0x44f740, 'cmp word ptr [ebp - 0x8c], 0x1d'),
        # the bullet extension filled by the fire routine (owner +4, laser +0x70, laser ext +0xc = bullet)
        (0x44f985, 'mov dword ptr [eax], ecx'), (0x44f98a, 'mov ecx, dword ptr [ebp - 0x3c]'),
        (0x44f98d, 'mov dword ptr [eax + 4], ecx'), (0x44f97b, 'mov edx, dword ptr [ebp - 0x18]'),
        (0x44f99b, 'mov dword ptr [eax + 0x70], edx'), (0x44f9a1, 'mov dword ptr [eax + 0xc], edi')]:
        a(g, va, t)
    # ---- death: kill routine 0x4542f0 (ECX = obj; [ebp+8] reason word, [ebp+0xc] killer, [ebp+0x10] arg4; ret 0xc)
    g = 'death'
    for va, t in [
        (0x4542f0, 'push ebp'), (0x4542f1, 'mov ebp, esp'), (0x4542f3, 'and esp, 0xfffffff8'),
        (0x4542f9, 'mov esi, ecx'), (0x4542fc, 'mov edx, dword ptr [esi + 0x40]'), (0x4542ff, 'test edx, 0x8000000'),
        (0x45430b, 'mov ax, word ptr [ebp + 8]'), (0x45430f, 'or edx, 0x8000000'), (0x454319, 'cmp eax, 0xe'),
        (0x454327, 'cmp eax, 0x15'), (0x454330, 'mov ebx, dword ptr [ebp + 0xc]'),
        (0x454333, 'and edx, 0x40000000'), (0x45434a, 'test dword ptr [ecx + eax + 0x58], 0x100'),
        (0x454354, 'mov dword ptr [esi + 0x10], 0'), (0x454362, 'jmp 0x454368'), (0x454364, 'test edx, edx'),
        (0x454366, 'jne 0x4543cd'), (0x4543a5, 'cmp word ptr [eax + 0x48], 1'), (0x4543aa, 'jne 0x4543cd'),
        (0x4543ac, 'push dword ptr [ebp + 0x10]'), (0x4543af, 'mov edx, ebx'), (0x4543b1, 'mov ecx, esi'),
        (0x4543b3, 'call 0x413ed0'), (0x4543b8, 'add esp, 4'), (0x4543bb, 'mov ecx, esi'),
        (0x4543bd, 'call 0x459d80'), (0x4543c8, 'call 0x417960'), (0x4543cd, 'movzx edx, word ptr [esi + 0x48]'),
        (0x45440c, 'push ebx'), (0x454459, 'ret 0xc'), (0x459d80, 'mov ecx, dword ptr [ecx + 0x54]'),
        # area damage 0x468db0: called by the kill routine, calls the damage routine and the kill routine back
        (0x454445, 'call 0x468db0'), (0x468f41, 'call 0x453070'), (0x469003, 'call 0x4542f0'),
        (0x459dad, 'ret'),
        # the death function 0x413ed0 (ECX = obj, EDX = killer, [esp+4] arg4; plain ret; SEH frame)
        (0x413ed0, 'push ebx'), (0x413ef4, 'mov dword ptr fs:[0], esp'), (0x413f00, 'mov esi, ecx'),
        (0x413f02, 'mov dword ptr [ebp - 0x20], edx'), (0x413f23, 'cmp word ptr [esi + 0x48], 0x1c'),
        (0x413f3d, 'push dword ptr [esi + 0x40]'), (0x413f40, 'movzx edx, word ptr [esi + 0x4a]'),
        (0x413f44, 'mov ecx, edi'), (0x413f46, 'call 0x4138e0'), (0x413f50, 'mov dword ptr [ebp - 0x14], edx'),
        (0x413f53, 'cmp di, 0xa'), (0x413f59, 'test dword ptr [esi + 0x40], 0x1000000'),
        (0x413f62, 'cmp dword ptr [ebp - 0x20], 0'), (0x413f68, 'mov eax, 1'),
        (0x413f95, 'test dword ptr [ecx + eax + 0x58], 0x100'), (0x413fa2, 'mov dword ptr [esi + 0x14], 0'),
        (0x41400a, 'mov ecx, dword ptr [ecx + 0x144]'), (0x414016, 'or ecx, 0x1b0000'), (0x41401f, 'call 0x449110'),
        (0x414045, 'test byte ptr [edx + 0x61c8a4], 0x20'), (0x414111, 'test byte ptr [edx + 0x61c8a4], 8'),
        (0x4141a6, 'test byte ptr [ecx + 0x61c8a4], 2'), (0x4142b2, 'mov ecx, 1'),
        (0x4142bb, 'push dword ptr [ebx + 8]'), (0x4142c3, 'call 0x4150e0'), (0x414303, 'mov ecx, 1'),
        (0x414308, 'push dword ptr [ebx + 8]'), (0x414310, 'call 0x4150e0'), (0x413eb7, 'ret'),
        (0x4142e2, 'ret'), (0x41432f, 'ret'),
        # the row chooser 0x4138e0 (ECX = class, EDX = subtype, [esp+4] obj+0x40; default 48)
        (0x4138e4, 'mov esi, 0x30'), (0x413909, 'mov eax, dword ptr [0x5a83e4]'),
        (0x41390e, 'mov eax, dword ptr [ecx + eax + 0xbc]'), (0x41392a, 'mov eax, dword ptr [0x5a83e0]'),
        (0x41392f, 'mov eax, dword ptr [ecx + eax + 0x60]'), (0x413944, 'mov eax, dword ptr [0x5a83dc]'),
        (0x413949, 'mov eax, dword ptr [ecx + eax + 0x60]'), (0x41395e, 'mov eax, dword ptr [0x5a840c]'),
        (0x413963, 'mov eax, dword ptr [ecx + eax + 0x48]'), (0x41396f, 'cmp dword ptr [esp + 8], 0'),
        (0x41397f, 'mov eax, dword ptr [0x5a83f0]'), (0x413984, 'mov eax, dword ptr [ecx + eax + 0x7c]'),
        (0x41399e, 'test dword ptr [ecx + eax + 0x58], 0x100'), (0x4139a8, 'mov esi, dword ptr [ecx + eax + 0x78]'),
        (0x4139af, 'ret')]:
        a(g, va, t)
    # ---- hull-copy fade (FADEOUTBODY), the Effects table, its loader, reset and allocator
    g = 'fade'
    for va, t in [
        (0x416ead, 'test dword ptr [ecx + eax + 0x3c], 0x800'), (0x416ebb, 'mov edi, dword ptr [ecx + eax + 0x40]'),
        (0x415576, 'mov eax, dword ptr [ebx + 0x10]'), (0x415579, 'cmp word ptr [eax + 0x48], 0x1b'),
        (0x41558c, 'mov eax, dword ptr [0x5a83b0]'), (0x415591, 'mov edx, dword ptr [eax + 0x708]'),
        (0x415597, 'sub edx, dword ptr [ebx + 0x28]'), (0x41559a, 'sub edx, edi'), (0x4155a4, 'mov eax, 0xff'),
        (0x416ec8, 'cmp edx, 0x3e8'), (0x416ed0, 'mov eax, 1'), (0x416ef6, 'mov ecx, dword ptr [ecx + 0x70]'),
        (0x416ef9, 'mov dword ptr [ecx + 0x13c], eax'), (0x41554c, 'test eax, 0x20000'),
        (0x4155ae, 'test eax, 0x80000'), (0x4155b8, 'call 0x4585a0'), (0x4155bd, 'cmp eax, 0x64'),
        (0x416b3f, 'test dword ptr [eax + edi + 0x3c], 0x4000000'),
        (0x416dc8, 'test dword ptr [eax + edi + 0x3c], 0x4000000'), (0x416c4b, 'test cl, 0x10'),
        (0x412de8, 'mov eax, 0x61c8a0'), (0x412e00, 'cmp eax, 0x6216c0'), (0x412eb4, 'cmp eax, 0x3e7'),
        (0x412ed5, 'mov dword ptr [edi*4 + 0x61c8a0], eax'), (0x412ee4, 'mov dword ptr [edi*4 + 0x61c8a4], eax'),
        (0x412ef2, 'mov dword ptr [edi*4 + 0x61c8a8], eax'), (0x412f48, 'imul ecx, dword ptr [edi*4 + 0x61c8a0], 0x70'),
        (0x412f50, 'xor edx, edx'), (0x412f52, 'mov dword ptr [edi*4 + 0x61c8ac], ebx'), (0x412f59, 'call 0x4d8fc0'),
        (0x412f66, 'mov dword ptr [edi*4 + 0x61c8b0], eax'), (0x412f9c, 'mov dword ptr [ebx + ecx + 0x3c], eax'),
        (0x412fae, 'mov dword ptr [ebx + ecx + 4], eax'), (0x412fb9, 'test dword ptr [ebx + eax + 0x3c], 0x2000'),
        (0x412fc3, 'or dword ptr [edi*4 + 0x61c8a4], 1'), (0x412fcb, 'test dword ptr [ebx + eax + 0x3c], 0x4000'),
        (0x412fdb, 'or dword ptr [edi*4 + 0x61c8a4], 2'), (0x412fe6, 'mov dword ptr [ebx + eax], 0xffffffff'),
        (0x412ffc, 'mov dword ptr [ebx + ecx + 0x44], eax'), (0x41308a, 'mov dword ptr [ebx + ecx + 0x60], edx'),
        (0x41316d, 'mov dword ptr [ebx + ecx], eax'), (0x4131cc, 'mov dword ptr [ebx + ecx + 8], eax'),
        (0x41322f, 'mov dword ptr [ebx + ecx + 0xc], eax'), (0x413241, 'mov dword ptr [ebx + ecx + 0x10], eax'),
        (0x41349d, 'mov dword ptr [ebx + ecx + 0x38], eax'), (0x4134db, 'mov dword ptr [ebx + ecx + 0x40], eax'),
        # reset: free + the memory-accounting counters the loader's allocator added to
        (0x4135c7, 'mov esi, 0x61c8a0'), (0x4135dd, 'imul edi, eax, 0x70'), (0x4135e5, 'call 0x52f4e5'),
        (0x4135ea, 'sub dword ptr [0x5aae20], edi'), (0x4135f3, 'sub dword ptr [0x5aaa18], edi'),
        (0x413609, 'cmp esi, 0x6216c0'),
        # the loader's allocator 0x4d8fc0 (ECX = size, EDX = flags; flags 0: malloc 0x531395 + accounting + zero)
        (0x4d8fc2, 'mov ebx, edx'), (0x4d8fc5, 'mov edi, ecx'), (0x4d8fc7, 'test bl, 1'), (0x4d9041, 'call 0x531395'),
        (0x4d9089, 'test bl, 0xc'), (0x4d908e, 'add dword ptr [0x5aae20], edi'),
        (0x4d90a0, 'add dword ptr [ebx*4 + 0x5aaa18], edi'), (0x4d90b1, 'call 0x521920'), (0x4d90be, 'ret'),
        # FL malloc / free are thunks (the AP malloc prologue witness 55 8b 6c 24 08 does not apply)
        (0x531395, 'mov edi, edi'), (0x53139a, 'pop ebp'), (0x53139b, 'jmp 0x5337fd'), (0x52f4e5, 'jmp 0x533b75')]:
        a(g, va, t)
    # ---- record and object layout readers (weapon_effects_core.h layout::, bolt_layout::)
    g = 'layout'
    for va, t in [
        # the TBullets case of the type loader: col 7 +0x44 .. col 23 +0x84, consecutive (version gates 0x33/0x35/0x37)
        (0x44074d, 'call 0x507940'), (0x440754, 'mov dword ptr [esi + 0x44], eax'),
        (0x440786, 'mov dword ptr [esi + 0x58], eax'), (0x440790, 'mov dword ptr [esi + 0x5c], eax'),
        (0x44079a, 'mov dword ptr [esi + 0x60], eax'), (0x4407a4, 'mov dword ptr [esi + 0x64], eax'),
        (0x4407cc, 'mov dword ptr [esi + 0x74], eax'), (0x4407d6, 'mov dword ptr [esi + 0x78], eax'),
        (0x4407e0, 'mov dword ptr [esi + 0x7c], eax'), (0x4407ea, 'mov dword ptr [esi + 0x80], eax'),
        (0x4407f7, 'mov dword ptr [esi + 0x84], eax'), (0x44081c, 'cmp dword ptr [ebp - 0x1c], 0x33'),
        # collision damage call: TBullets +0x44 shield, +0x80 hull, +0x58 flags (&6, 0x2000)
        (0x468105, 'add ecx, dword ptr [0x5a83c8]'), (0x46810b, 'test byte ptr [ecx + 0x58], 6'),
        (0x468112, 'mov edx, dword ptr [ecx + 0x44]'), (0x468115, 'mov eax, dword ptr [ecx + 0x80]'),
        (0x468168, 'test dword ptr [esp + 0x48], 0x2000'), (0x468183, 'mov ecx, edi'), (0x468185, 'call 0x453070'),
        # light colour (the flash 0x44dbd0, ECX = bullet)
        (0x44dbd2, 'mov esi, ecx'), (0x44dbef, 'cmp dword ptr [edi + eax + 0x5c], 0'),
        (0x44dbf6, 'cmp dword ptr [edi + eax + 0x60], 0'), (0x44dbfd, 'cmp dword ptr [edi + eax + 0x64], 0'),
        # TShips col 52 class id (+0xc8) through the OBJ_SHIP table
        (0x4413c8, 'push 0x5763d8'), (0x4413cd, 'call 0x507960'), (0x4413d2, 'mov dword ptr [esi + 0xc8], eax'),
        # bullet creation: ext+0 expiry (col27 + col10 + clock), ext+4 = 0, ext+8 created, clock cfg+0x708
        (0x449b08, 'mov dword ptr [eax + 4], 0'), (0x449b24, 'mov ecx, dword ptr [edx + eax + 0x94]'),
        (0x449b2b, 'add ecx, dword ptr [edx + eax + 0x50]'), (0x449b34, 'add ecx, dword ptr [eax + 0x708]'),
        (0x449b3d, 'mov dword ptr [eax], ecx'), (0x449b47, 'mov eax, dword ptr [eax + 0x708]'),
        (0x449b4d, 'mov dword ptr [ecx + 8], eax'), (0x449b64, 'mov dword ptr [esi + 0x40], 0x40004801'),
        (0x449b1d, 'mov dword ptr [ebp - 0x20], 0x20800000'),
        # beam length ext+0x78 = TBullets +0x70 (flags 4 / 8)
        (0x44a762, 'mov eax, dword ptr [edx + edi + 0x70]'), (0x44a766, 'mov dword ptr [ecx + 0x78], eax'),
        # node handle +0x28 (object_trace_fl_inc.h), node rows +0x40..+0x6c (impact copy)
        (0x4a22a8, 'mov dword ptr [edi + 0x28], eax'), (0x4145ab, 'movups xmm0, xmmword ptr [eax + 0x60]'),
        # node velocity +0x100 (an area bullet's is zeroed in the death function), node body id +0x144 (AP +0x140)
        (0x413fb0, 'mov dword ptr [eax + 0x100], 0'), (0x413fce, 'mov dword ptr [eax + 0x10c], 0'),
        (0x413f2e, 'mov ecx, dword ptr [ecx + 0x144]'),
        # body cache root
        (0x4867c1, 'mov edi, dword ptr [0x5aa100]'),
        # type-table count slots 0x5a8330 + 4 class (paired with the table 0x5a83c8 + 4 class)
        (0x43d8a5, 'cmp eax, dword ptr [0x5a834c]'), (0x43d8ad, 'mov edx, dword ptr [0x5a83e4]'),
        (0x43d8c6, 'cmp eax, dword ptr [0x5a8348]'), (0x43d8ce, 'mov edx, dword ptr [0x5a83e0]'),
        (0x43d8da, 'cmp eax, dword ptr [0x5a8344]'), (0x43d8e2, 'mov edx, dword ptr [0x5a83dc]'),
        (0x46f732, 'cmp eax, dword ptr [0x5a8354]'), (0x443e85, 'cmp edx, dword ptr [0x5a8358]'),
        # fixed-point rotate 0x50e0c0 (AP 0x4f0da0): imul, + 0x8000, adc, shrd 16
        (0x50e0dc, 'imul dword ptr [edx + 0x10]'), (0x50e0e5, 'add ecx, 0x8000'), (0x50e0eb, 'adc edx, 0'),
        (0x50e0ee, 'shrd ecx, edx, 0x10')]:
        a(g, va, t)


def bubble_anchors(fl, ap):
    """The shield-hit bubble's hull walk (weapon_effects_bubble.h; render-node-bounds.md 6.1): FL reader and AP twin."""
    pairs = [
        # body id -> model: H = [[root]+0x14], buckets [H], size [H+4], key id+1, entries {next, key, model}
        ((0x499870, 'mov eax, dword ptr [0x5aa100]'), (0x4863e2, 'mov eax, dword ptr [0x608518]')),
        ((0x499875, 'inc ecx'), (0x4863f8, 'lea edx, [edi + 1]')),
        ((0x499876, 'mov eax, dword ptr [eax + 0x14]'), (0x4863e7, 'mov eax, dword ptr [eax + 0x14]')),
        ((0x499879, 'mov edx, dword ptr [eax]'), (0x4863ea, 'mov ecx, dword ptr [eax]')),
        ((0x49987f, 'mov eax, dword ptr [eax + 4]'), (0x4863fd, 'mov eax, dword ptr [eax + 4]')),
        ((0x499883, 'and eax, ecx'), (0x486403, 'and eax, edx')),
        ((0x499885, 'mov eax, dword ptr [edx + eax*4]'), (0x486405, 'mov eax, dword ptr [ecx + eax*4]')),
        ((0x499890, 'cmp dword ptr [eax + 4], ecx'), (0x486410, 'cmp dword ptr [eax + 4], edx')),
        ((0x499895, 'mov eax, dword ptr [eax]'), (0x486415, 'mov eax, dword ptr [eax]')),
        ((0x49989e, 'mov eax, dword ptr [eax + 8]'), (0x486464, 'mov eax, dword ptr [eax + 8]')),
        ((0x4998a1, 'ret'), None),
        # model -> LOD records: count int16 +0x10, array +0xc, L_i = [arr + 4 i]; extents L+0x20.. stored at load
        ((0x4a17eb, 'cmp ax, word ptr [edi + 0x10]'), (0x4861c9, 'movsx edx, word ptr [esi + 0x10]')),
        ((0x4a17f1, 'mov eax, dword ptr [edi + 0xc]'), (0x4c6c59, 'mov eax, dword ptr [ecx + 0xc]')),
        ((0x4a17f6, 'mov esi, dword ptr [eax + ecx*4]'), (0x4c6c60, 'mov edx, dword ptr [eax]')),
        ((0x4a182a, 'call 0x498560'), (0x48619b, 'call 0x47f350')),
        ((0x4a183f, 'movups xmmword ptr [esi + 0x20], xmm0'), (0x4861b2, 'mov dword ptr [edi + 0x20], ecx')),
        ((0x4a1843, 'movsx eax, word ptr [edi + 0x10]'), None),
        # placement: child list [n+0xc], offset +0x30 through the parent's world basis +0xc0, x parent +0x80.. unless
        # parent +0x12c & 0x10000, x parent +0x70 when child +0x12c & 0x40
        ((0x494382, 'mov esi, dword ptr [ebx + 0xc]'), None),
        ((0x494385, 'cmp dword ptr [esi], 0'), None),
        ((0x49438e, 'lea eax, [ebx + 0xc0]'), (0x47b56d, 'lea ecx, [ebx + 0xc0]')),
        ((0x49439d, 'lea ecx, [esi + 0x30]'), (0x47b56a, 'lea esi, [eax + 0x30]')),
        ((0x4943a8, 'test dword ptr [ebx + 0x12c], 0x10000'), (0x47b578, 'test dword ptr [ebx + 0x12c], 0x10000')),
        ((0x4943b6, 'imul dword ptr [ebx + 0x80]'), None),
        ((0x4943d2, 'imul dword ptr [ebx + 0x84]'), None),
        ((0x4943ee, 'imul dword ptr [ebx + 0x88]'), None),
        ((0x49440f, 'test byte ptr [esi + 0x12c], 0x40'), None),
        ((0x49441c, 'mov ecx, dword ptr [ebx + 0x70]'), None),
        # no collision tree: node +0x12c & 0x8000000 (build test / set, narrow-phase filter)
        ((0x497b5c, 'test dword ptr [edi + 0x12c], 0x8000000'), (0x47ebd8, 'test dword ptr [edi + 0x12c], 0x8000000')),
        ((0x497c5e, 'or dword ptr [edi + 0x12c], 0x8000000'), (0x47ecf5, 'or dword ptr [edi + 0x12c], 0x8000000')),
        ((0x4a6836, 'test dword ptr [edi + 0x12c], 0x8000000'), (0x48ae1f, 'test dword ptr [edi + 0x12c], 0x8000000')),
        # node body id +0x144 (AP +0x140): the cull-pass reject of id <= 0 in AP, the death carrier's body in FL
        ((0x413f2e, 'mov ecx, dword ptr [ecx + 0x144]'), (0x4c6aee, 'cmp dword ptr [ebx + 0x140], 0')),
    ]
    for f, a in pairs:
        fl.at('bubble', *f)
        if a:
            ap.at('bubble-ap', *a)


def sweep_writes(img, start, end, operand):
    """Instructions in [start, end) whose first operand is `operand` and that write it (mov/lea/add/sub/...)."""
    out = []
    for i in img.insns(start, end):
        if i.op_str.startswith(operand + ',') and i.mnemonic not in ('cmp', 'test'):
            out.append(i.address)
    return out


def census(game):
    sys.path.insert(0, str(ROOT / 'tools/analysis'))
    from sector_fog_census import Assets
    a = Assets(pathlib.Path(game), edition='fl')

    def table(name):
        data, prov = a.get(f'{a.expansion}/types/{name}.txt')
        lines = [l.split('//')[0].strip() for l in data.decode('latin1').splitlines()]
        lines = [l for l in lines if l]
        head = lines[0].split(';')
        return int(head[0]), [l.split(';') for l in lines[1:]], prov['source']

    def num(r, c):
        try:
            return int(r[c].strip())
        except (ValueError, IndexError):
            return None

    ver, rows, src = table('TBullets')
    print(f'TBullets {src}: file version {ver}, {len(rows)} rows, columns per row '
          f'{sorted(collections.Counter(len(r) for r in rows).items())}')
    flags = [num(r, 12) or 0 for r in rows]
    bits = collections.Counter(b for f in flags for b in range(16) if f >> b & 1)
    print('flag values (col 12): ' + ', '.join(f'{v:#x}x{n}' for v, n in sorted(collections.Counter(flags).items())))
    print('bits present (rows): ' + ' '.join(f'{1 << b:#x}:{n}' for b, n in sorted(bits.items())))
    known = 0x2 | 0x4 | 0x8 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 | 0x400 | 0x1000
    print(f'bits outside the classifier\'s cues (beam 0x6, wave 0x8, 0x20, 0x40, 0x80, burst 0x1100, on-hit 0x600): '
          f'{sorted(hex(1 << b) for b in bits if not (known >> b & 1))}')
    for c, label in ((19, 'flight'), (20, 'impact'), (21, 'muzzle'), (23, 'trail')):
        v = [num(r, c) or 0 for r in rows]
        print(f'col {c} {label}: non-zero {sum(1 for x in v if x)}, zero {sum(1 for x in v if not x)}, '
              f'distinct {len({x for x in v if x})}')
    edata, eprov = a.get(f'{a.expansion}/types/Effects.txt')
    eff_ids = {int(m.group(1)) for m in re.finditer(r'(?m)^\s*(\d+);\s*\d+;\s*[A-Z_|0-9]+;\s*-?\d+;\s*-?\d+\.\d*;',
                                                     edata.decode('latin1'))}
    for c, label in ((19, 'flight'), (20, 'impact'), (21, 'muzzle')):
        missing = sorted({num(r, c) for r in rows if num(r, c)} - eff_ids)
        print(f'col {c} {label} rows missing from Effects ({eprov["source"]}, {len(eff_ids)} rows): {missing or "none"}')
    light = sum(1 for r in rows if any(num(r, c) for c in (13, 14, 15)))
    print(f'light colour cols 13-15 non-zero: {light} / {len(rows)}')
    zero_muzzle = [r[48].strip() if len(r) > 48 else '?' for r in rows if not num(r, 21)]
    print(f'rows with muzzle col 21 = 0 (no FL muzzle-site call): {zero_muzzle}')
    _, ships, ssrc = table('TShips')
    known_ids = {2023, 2024, 2025, 2060, 2062, 2064, 2065, 2066, 2067, 2068, 2070, 2080, 2092, 2131, 2143, 2144,
                 2145, 2146, 2147, 2150, 2151, 2026, 2030, 2031, 2035, 2039, 2141, 2142, 2011, 2020, 2021, 2022,
                 2027, 2032, 2036, 2149, 2152, 2153}  # patch_tier_of_ship (weapon_effects_core.h)
    names = {'OBJ_RELAYBEACON': 2063}
    out = collections.Counter()
    for r in ships:
        tok = r[52].strip() if len(r) > 52 else ''
        cid = int(tok) if tok[:1].isdigit() else names.get(tok)
        if cid is None and tok.startswith('OBJ_'):
            continue  # a named id the AP/FL tables share (class_id_tables_out.txt); all of them are in known_ids
        if cid not in known_ids:
            out[(tok, r[5].strip(), r[47].strip())] += 1
    print(f'TShips {ssrc} rows whose col 52 id falls outside patch_tier_of_ship (col 52, col 5, death row col 47): '
          + ', '.join(f'{k} x{v}' for k, v in sorted(out.items())))
    fl_new = collections.Counter((r[52].strip(), r[47].strip()) for r in ships if len(r) > 52 and r[52].strip() in
                                 ('OBJ_SHIP_SPECIALDRONE', 'OBJ_SHIP_DRONECARRIER', 'OBJ_SHIP_MISSILE_FRIGATE', '2153'))
    print('FL-only ship ids (col 52, death row col 47): ' + ', '.join(f'{k} x{v}' for k, v in sorted(fl_new.items())))
    rows_missing = sorted({r[47].strip() for r in ships if len(r) > 47 and r[47].strip().isdigit()
                           and int(r[47]) and int(r[47]) not in eff_ids})
    print(f'TShips death rows (col 47) missing from Effects: {rows_missing or "none"}')


def flag_test_census(img):
    """Immediates of `test [reg(+reg)+0x58], imm` in .text: the flag-word tests a TBullets reader can make."""
    start, end, off = img.text
    pat = re.compile(r'^(?:byte|dword) ptr \[[a-z]{3}(?: \+ [a-z]{3}(?:\*\d)?)? \+ 0x58\], (0x[0-9a-f]+|\d+)$')
    out = collections.Counter()
    sites = collections.defaultdict(list)
    pos, code = 0, img.data[off:off + (end - start)]
    while pos < len(code):
        last = pos
        for i in img.md.disasm(code[pos:pos + 0x10000], start + pos):
            last = i.address + i.size - start
            if i.mnemonic == 'test':
                m = pat.match(i.op_str)
                if m:
                    out[int(m.group(1), 0)] += 1
                    sites[int(m.group(1), 0)].append(i.address)
        pos = last if last > pos else pos + 1
    return out, sites


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--fl', default='/mnt/c/X3FL/X3FL.exe')
    p.add_argument('--ap', default='/mnt/c/X3AP-orig/X3AP.exe')
    p.add_argument('--game', default='/mnt/c/X3FL')
    a = p.parse_args()
    fl, ap = Image(a.fl, FL_SHA256), Image(a.ap, AP_SHA256)
    print(f'X3FL.exe sha256 {fl.sha256}; DllCharacteristics {fl.dll_characteristics:#06x} '
          f'(DYNAMIC_BASE {bool(fl.dll_characteristics & 0x40)}); HIGHLOW fixups {len(fl.fixups)}')
    print(f'X3AP.exe sha256 {ap.sha256}')
    edges = fl.direct_edges()
    print(f'FL direct edges in .text (linear sweep): {len(edges)}')
    by_target = collections.defaultdict(list)
    for src, m, dst in edges:
        by_target[dst].append((src, m))
    dwords = fl.dword_index()
    ap_by_target = collections.defaultdict(list)
    for src, m, dst in ap.direct_edges():
        ap_by_target[dst].append((src, m))
    print()
    print('site | FL call RVA -> target RVA | AP twin VA -> target | window RVA, length | relocated dwords (off: target '
          'RVA) | call in one qword / rel32 in one qword | branches into call bytes 1..4 | branch targets inside window '
          '(RVA: sources) | image dwords into bytes 1..4 | E8 callers of target (FL / AP)')
    for name, aps, apt, s, t, w0, w1 in SITES:
        b, dst = call_target(fl, s)
        ab, adst = call_target(ap, aps)
        ok = dst == t and adst == apt
        insns = fl.insns(w0, w1)
        assert any(i.address == s for i in insns), f'{name}: call not on an instruction boundary of its window'
        raw = bytearray(fl.read(w0, w1 - w0))
        rel = sorted(f for f in fl.fixups if w0 <= f and f + 4 <= w1)
        assert not [f for f in fl.fixups if w0 - 3 <= f < w1 and not (w0 <= f and f + 4 <= w1)], f'{name}: straddle'
        assert not [f for f in rel if s <= f < s + 5 or f <= s < f + 4], f'{name}: a fixup inside the call'
        reloc_desc = []
        for f in rel:
            v = struct.unpack_from('<I', raw, f - w0)[0]
            reloc_desc.append(f'+{f - w0}: {v - BASE:#08x}')
            raw[f - w0:f - w0 + 4] = b'\0\0\0\0'
        inside_call = [(src, m) for x in range(s + 1, s + 5) for src, m in by_target.get(x, [])]
        starts = {i.address for i in insns}
        in_window = {x: by_target[x] for x in sorted(starts) if w0 <= x < w1 and x in by_target}
        ptrs = sum(dwords.get(x, 0) for x in range(s + 1, s + 5))
        fl_callers = [src for src, m in by_target.get(t, []) if m == 'call']
        ap_callers = [src for src, m in ap_by_target.get(apt, []) if m == 'call']
        wdesc = '; '.join(f'{rva(x)}: ' + ','.join(f'{m} {rva(src)}' for src, m in v) for x, v in in_window.items())
        print(f'{name} | {rva(s)} {b.hex(" ")} -> {rva(dst)} ({"ok" if ok else "MISMATCH"}) | {aps:#010x} -> '
              f'{adst:#010x} | {rva(w0)}, {w1 - w0} | {", ".join(reloc_desc) or "none"} | '
              f'{(s & 7) + 5 <= 8} / {((s + 1) & 7) + 4 <= 8} | {inside_call or "none"} | {wdesc or "none"} | '
              f'{ptrs} | {len(fl_callers)} [{" ".join(rva(c) for c in sorted(fl_callers))}] / {len(ap_callers)}')
        print(f'  window bytes (relocated dwords zeroed) {raw.hex(" ")}; FNV-1a 32 {fnv1a32(raw):08x}; '
              f'SHA-256 {hashlib.sha256(bytes(raw)).hexdigest()[:16]}; call offset {s - w0}')
    fl.at('layout', 0x44073d, 'movzx eax, byte ptr [ecx + 0x442b60]')  # ECX = [ebp-0x28], the class
    fl.at('layout', 0x440744, 'jmp dword ptr [eax*4 + 0x442b1c]')
    idx = fl.read(0x442b60, 1)[0]
    jt = struct.unpack_from('<I', fl.read(0x442b1c + 4 * idx, 4))[0]
    print(f'type loader class dispatch: byte table {rva(0x442b60)}[class 0] = {idx}, jump table {rva(0x442b1c)} '
          f'(relocated: {0x442b1c + 4 * idx in fl.fixups}) -> {rva(jt)} ({"ok" if jt == 0x44074b else "MISMATCH"}: '
          f'the TBullets case)')
    for t, label in ((0x453070, 'damage routine'), (0x4542f0, 'kill routine'), (0x413ed0, 'death function'),
                     (0x468db0, 'area damage'), (0x44e310, 'fire routine'), (0x4138e0, 'death row chooser')):
        cs = sorted(src for src, m in by_target.get(t, []) if m == 'call')
        print(f'E8 callers of {label} {rva(t)}: {len(cs)} [{" ".join(rva(c) for c in cs)}]')
    for wname, w0, w1 in WITNESSES:
        fl.insns(w0, w1)
        raw = bytearray(fl.read(w0, w1 - w0))
        rel = sorted(f for f in fl.fixups if w0 <= f and f + 4 <= w1)
        assert not [f for f in fl.fixups if w0 - 3 <= f < w1 and f not in rel], f'{wname}: straddling fixup'
        rdesc = []
        for f in rel:
            rdesc.append(f'+{f - w0}: {struct.unpack_from("<I", raw, f - w0)[0] - BASE:#08x}')
            raw[f - w0:f - w0 + 4] = b'\0\0\0\0'
        print(f'witness {rva(w0)}, {w1 - w0}: {raw.hex(" ")}; relocated {", ".join(rdesc) or "none"}; '
              f'FNV-1a 32 {fnv1a32(raw):08x}  {wname}')
    b, adst = call_target(ap, AP_DEATH_OTHER)
    print(f'AP death B {AP_DEATH_OTHER:#010x} -> {adst:#010x}: FL has no second site (one E8 caller of '
          f'{rva(0x413ed0)}: {[rva(src) for src, m in by_target.get(0x413ed0, [])]})')
    other_refs = {t: [(rva(src), m) for src, m in by_target.get(t, []) if m != 'call'] for t in
                  (0x414440, 0x415270, 0x4150e0, 0x414d90, 0x413ed0)}
    print('non-call direct edges to the targets: ' + '; '.join(f'{rva(t)}: {v or "none"}' for t, v in other_refs.items()))
    print()
    anchors(fl)
    bubble_anchors(fl, ap)
    print('anchors: ' + ', '.join(f'{g} {n}' for g, n in fl.checks.items()) +
          f'; total {sum(fl.checks.values())}, failures {len(fl.fails)}; AP twins {sum(ap.checks.values())}, '
          f'failures {len(ap.fails)}')
    for x in fl.fails + ap.fails:
        print('  FAIL ' + x)
    # single writers of the muzzle frame slots and of the impact routine's locals
    w88 = sweep_writes(fl, 0x44e310, 0x44ff46, 'dword ptr [ebp - 0x88]')
    w24 = sweep_writes(fl, 0x44e310, 0x44ff46, 'dword ptr [ebp - 0x24]')
    w18 = sweep_writes(fl, 0x44e310, 0x44ff46, 'dword ptr [ebp - 0x18]')
    print(f'fire routine writers: [ebp-0x88] (gun slot) {[rva(x) for x in w88]}, [ebp-0x24] (bullet index * 0xdb8) '
          f'{[rva(x) for x in w24]}, [ebp-0x18] (laser) {[rva(x) for x in w18]}')
    l10 = sweep_writes(fl, 0x453070, 0x4533b6, 'dword ptr [esp + 0x10]')
    l14 = sweep_writes(fl, 0x453070, 0x4533b6, 'dword ptr [esp + 0x14]')
    ebp_w = [i.address for i in fl.insns(0x453070, 0x4533b6) if i.op_str.startswith('ebp,')
             and i.mnemonic not in ('cmp', 'test')]
    esi_w = [i.address for i in fl.insns(0x453070, 0x4533b6) if i.op_str.startswith('esi,')
             and i.mnemonic not in ('cmp', 'test')]
    print(f'damage routine writers: [esp+0x10] {[rva(x) for x in l10]}, [esp+0x14] {[rva(x) for x in l14]}, '
          f'EBP {[rva(x) for x in ebp_w]}, ESI {[rva(x) for x in esi_w]}')
    fc, fsites = flag_test_census(fl)
    ac, _ = flag_test_census(ap)
    print('test [..+0x58], imm (immediate: AP count / FL count): ' +
          ', '.join(f'{k:#x}: {ac.get(k, 0)}/{fc.get(k, 0)}' for k in sorted(set(fc) | set(ac))))
    print('FL-only immediates and their sites: ' + '; '.join(f'{k:#x}: {[rva(x) for x in fsites[k]]}'
                                                            for k in sorted(set(fc) - set(ac))))
    print()
    census(a.game)


if __name__ == '__main__':
    main()
