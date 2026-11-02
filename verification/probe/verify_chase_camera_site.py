#!/usr/bin/env python3
"""Read-only check of the chase camera's hook site against the installed X3AP.exe.

Mirrors what src/proxy/chase_camera.cpp will do in the process, on the file
instead (docs/architecture/chase-camera.md, "Verification plan"): the exact
executable identity (exe_identity.py: structure and global anchors; the raw
SHA-256 is INFO against the known list), the ten bytes at 0x00420e06, the
relocated-prologue rules the trampoline relies on (whole instructions, exactly
one relative branch and it is the declared jz whose rel32 the tail re-bases, no
other branch in the function lands inside the displaced span), the cockpit
update's prologue and the main loop's cockpit-update -> frame-routine call
order the design depends on. Nothing is written; the game directory is only read.

--edition fl checks the FL seam of the same module against X3FL.exe instead (docs/reverse-engineering/fl/
chase-camera.md, "Verified 2026-11-02"; constants src/engine/fl/chase_camera_sites_fl.h): the link stamp and image
size, Capstone decoding of the ten-byte span and its neighbours (last native position copy, continuation, first
camera-relative load, branch target), the exact bytes, the .reloc census (no HIGHLOW inside the span; the plane-slot
fixup), the image-wide direct-edge census (the seven inbound edges at the span start, none into its interior), the
qword crossing, EBX = cockpit up to the seam, the field anchors the shared handler relies on and the three identity
windows' FNV-1a with relocated dwords zeroed.

usage: verify_chase_camera_site.py [--edition ap|fl] [--exe PATH] [--json]   (exit 0 = PASS)
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

DEFAULT_EXE = exe_identity.DEFAULT_EXE  # the X3 bottle, or the native install on a host without CrossOver
EXPECTED_SHA256 = exe_identity.SHIPPED_SHA256  # provenance only: reported as INFO, never a check
EXPECTED_SIZE = exe_identity.FILE_SIZE
IMAGE_BASE = 0x00400000
SITE_VA = 0x00420e06
SITE_BYTES = bytes.fromhex('837b54000f8409020000')
SITE_LENGTH = 10
REL32_OFFSET = 6
SITE_BRANCH_TARGET = 0x00421019     # the jz target after the object loop: push ebx; call FUN_004216e0
FUNCTION_START, FUNCTION_END = 0x004205e0, 0x004216dc   # FUN_004205e0 body (Ghidra)
FUNCTION_PROLOGUE = bytes.fromhex('558bec83e4f0')       # push ebp; mov ebp,esp; and esp,-16
MAIN_LOOP_CALLS = ((0x00403f2f, 0x0041cde0), (0x00403f34, 0x00471f50))  # cockpit update loop, then the frame routine


class Image:
    """Minimal PE section mapper for VA -> file bytes (read-only)."""

    def __init__(self, data):
        self.data = data
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('not a PE image')
        count = struct.unpack_from('<H', data, pe + 6)[0]
        optional = struct.unpack_from('<H', data, pe + 20)[0]
        self.image_base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
        self.sections = []
        for i in range(count):
            s = pe + 24 + optional + i * 40
            name = data[s:s + 8].rstrip(b'\0').decode('ascii', 'replace')
            vsize, va, rsize, rp = struct.unpack_from('<IIII', data, s + 8)
            self.sections.append((name, self.image_base + va, vsize, rp, rsize))

    def read(self, va, n):
        for name, base, vsize, rp, rsize in self.sections:
            if base <= va < base + vsize:
                off = va - base
                if off + n > rsize:
                    return None
                return self.data[rp + off:rp + off + n]
        return None

    def text_range(self):
        for name, base, vsize, rp, rsize in self.sections:
            if name == '.text':
                return base, base + vsize
        return None


def decode_lengths(code, va):
    """Instruction lengths of the displaced span: a deliberately tiny decoder
    that knows the forms this site uses and refuses anything else (fail closed).
    Returns [(va, length, mnemonic, rel32_field_offset_or_None, branch_target)]."""
    out, i = [], 0
    while i < len(code):
        b = code[i]
        if b == 0x83 and i + 1 < len(code):          # 83 /r ib: op r/m32, imm8
            modrm = code[i + 1]
            mod, rm = modrm >> 6, modrm & 7
            n = 2
            if mod != 3 and rm == 4:
                n += 1                                 # sib
            n += {0: 4 if rm == 5 else 0, 1: 1, 2: 4, 3: 0}[mod]
            n += 1                                     # imm8
            names = {7: 'cmp', 0: 'add', 5: 'sub', 4: 'and', 1: 'or'}
            out.append((va + i, n, names.get((modrm >> 3) & 7, 'grp1'), None, None))
        elif b == 0x0f and i + 1 < len(code) and 0x80 <= code[i + 1] <= 0x8f:   # jcc rel32
            if i + 6 > len(code):
                raise ValueError('truncated jcc')
            rel = struct.unpack_from('<i', code, i + 2)[0]
            out.append((va + i, 6, 'jcc', i + 2, va + i + 6 + rel))
            n = 6
        elif b in (0xe8, 0xe9):
            if i + 5 > len(code):
                raise ValueError('truncated call/jmp')
            rel = struct.unpack_from('<i', code, i + 1)[0]
            out.append((va + i, 5, 'call' if b == 0xe8 else 'jmp', i + 1, va + i + 5 + rel))
            n = 5
        elif 0x70 <= b <= 0x7f or b == 0xeb:
            raise ValueError(f'short branch at {va + i:#x}: not relocatable by the tail copy')
        elif b in (0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57, 0x9c, 0x60):
            out.append((va + i, 1, 'push', None, None)); n = 1
        else:
            raise ValueError(f'undecoded opcode {b:#04x} at {va + i:#x}')
        i += n
    return out


def branches_into(image, lo, hi, interior_lo, interior_hi):
    """Heuristic scan of [lo, hi) for rel8/rel32 branch encodings whose target
    lies in [interior_lo, interior_hi). Data bytes can alias; the expectation is
    an empty list and any hit is reported for a human to look at."""
    code = image.read(lo, hi - lo)
    hits = []
    for i in range(len(code)):
        b = code[i]
        if b in (0xe8, 0xe9) and i + 5 <= len(code):
            target = lo + i + 5 + struct.unpack_from('<i', code, i + 1)[0]
        elif b == 0x0f and i + 6 <= len(code) and 0x80 <= code[i + 1] <= 0x8f:
            target = lo + i + 6 + struct.unpack_from('<i', code, i + 2)[0]
        elif (0x70 <= b <= 0x7f or b == 0xeb) and i + 2 <= len(code):
            target = lo + i + 2 + struct.unpack_from('<b', code, i + 1)[0]
        else:
            continue
        if interior_lo <= target < interior_hi:
            hits.append({'at': f'{lo + i:#010x}', 'target': f'{target:#010x}'})
    return hits


def verify(data):
    report = {'result': 'FAIL', 'checks': {}}
    checks = report['checks']
    report['exe_info'] = exe_identity.info(data)  # INFO only
    checks['size'] = {'ok': len(data) == EXPECTED_SIZE, 'value': len(data)}
    checks['exe_identity'] = {'ok': exe_identity.identity_ok(data)}
    try:
        image = Image(data)
    except (ValueError, struct.error) as e:
        checks['pe'] = {'ok': False, 'error': str(e)}
        return report
    checks['pe'] = {'ok': image.image_base == IMAGE_BASE, 'image_base': f'{image.image_base:#010x}'}
    site = image.read(SITE_VA, SITE_LENGTH)
    checks['site_bytes'] = {'ok': site == SITE_BYTES, 'value': site.hex() if site else None, 'expected': SITE_BYTES.hex(), 'va': f'{SITE_VA:#010x}'}
    if site:
        try:
            decoded = decode_lengths(site, SITE_VA)
            covered = sum(n for _, n, _, _, _ in decoded)
            branches = [d for d in decoded if d[3] is not None]
            ok = (covered == SITE_LENGTH and len(branches) == 1 and branches[0][3] == REL32_OFFSET and branches[0][2] == 'jcc'
                  and branches[0][4] == SITE_BRANCH_TARGET and FUNCTION_START <= branches[0][4] < FUNCTION_END and SITE_LENGTH >= 5)
            checks['relocation'] = {'ok': ok, 'instructions': [{'va': f'{v:#010x}', 'length': n, 'mnemonic': m, 'rel32_offset': r, 'target': f'{t:#010x}' if t else None} for v, n, m, r, t in decoded],
                                    'covered': covered, 'declared_rel32_offset': REL32_OFFSET}
        except ValueError as e:
            checks['relocation'] = {'ok': False, 'error': str(e)}
    # A jump into the middle of the displaced span would execute a torn instruction after the patch.
    hits = branches_into(image, FUNCTION_START, FUNCTION_END, SITE_VA + 1, SITE_VA + SITE_LENGTH)
    checks['interior_branches'] = {'ok': not hits, 'hits': hits}
    prologue = image.read(FUNCTION_START, len(FUNCTION_PROLOGUE))
    checks['function_prologue'] = {'ok': prologue == FUNCTION_PROLOGUE, 'value': prologue.hex() if prologue else None}
    calls = []
    for va, target in MAIN_LOOP_CALLS:
        code = image.read(va, 5)
        actual = va + 5 + struct.unpack_from('<i', code, 1)[0] if code and code[0] == 0xe8 else None
        calls.append({'va': f'{va:#010x}', 'expected': f'{target:#010x}', 'actual': f'{actual:#010x}' if actual else None, 'ok': actual == target})
    checks['main_loop_order'] = {'ok': all(c['ok'] for c in calls), 'calls': calls}
    # The five-byte jmp straddles the qword boundary at 0x00420e08: engine_patch takes the plain copy, covered by the install window.
    checks['atomic_write'] = {'ok': True, 'qword_aligned_span': (SITE_VA & 7) + 5 <= 8, 'note': 'plain copy inside the install window when false'}
    report['result'] = 'PASS' if all(c.get('ok') for c in checks.values()) else 'FAIL'
    return report


# ---------------------------------------------------------------- FL (X3FL.exe, relocatable; VAs at the preferred base)
FL_DEFAULT_EXE = Path('/mnt/c/X3FL/X3FL.exe')
FL_SHA256 = 'e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8'  # provenance only (INFO)
FL_TIMESTAMP, FL_IMAGE_SIZE = 0x618416c8, 0x00391000   # executable_identity_fl_inc.h
FL_HIGHLOW_COUNT = 38327
FL_SITE_RVA, FL_SITE_BYTES = 0x0227ad, bytes.fromhex('837b54000f8471010000')
FL_CONTINUATION_RVA, FL_BRANCH_TARGET_RVA = 0x0227b7, 0x022928
FL_UPDATER_RVA, FL_UPDATER_END_RVA = 0x021f50, 0x022fb0
FL_INBOUND_EDGES = (0x022141, 0x022597, 0x0225a6, 0x0225af, 0x0225b8, 0x0225c1, 0x0225ca)
FL_PLANE_SLOT_RVA = 0x1a82f8
# (start RVA, length, FNV-1a with relocated dwords zeroed, relocated dword RVAs): chase_camera_sites_fl.h.
FL_WINDOWS = {'seam': (0x0227a4, 66, 0x391b90a2, ()), 'target': (0x022928, 7, 0x466a42e2, ()),
              'plane': (0x0dec8c, 11, 0x2df2f1ce, (0x0dec8d,))}
# Whole instructions at the seam and around it (RVA, Capstone text at the preferred base).
FL_SEAM = ((0x0227a4, 'movups xmm0, xmmword ptr [esp + 0x20]'), (0x0227a9, 'movups xmmword ptr [ecx + 0x30], xmm0'),
           (0x0227ad, 'cmp dword ptr [ebx + 0x54], 0'), (0x0227b1, 'je 0x422928'), (0x0227b7, 'xor ecx, ecx'),
           (0x0227c6, 'mov eax, dword ptr [ebx + 0x54]'), (0x0227e3, 'mov ecx, dword ptr [ebx + 0x58]'),
           (0x022928, 'mov ecx, ebx'), (0x02292a, 'call 0x422fb0'), (0x02293f, 'mov dword ptr [ebx + 0x1fc], 0'))
# EBX = cockpit: ECX -> EBX at entry, one store to [esp+0x1c]; every other EBX write before the seam lies in the loop
# [0x02268d, 0x0226d8) whose only exit falls through to the reload.
FL_EBX = ((0x021f5e, 'mov ebx, ecx'), (0x021f63, 'mov dword ptr [esp + 0x1c], ebx'),
          (0x0226d8, 'mov ebx, dword ptr [esp + 0x1c]'))
FL_EBX_LOOP = (0x02268d, 0x0226d8)
# Field anchors of every offset the shared handler reads or writes (chase_camera.cpp, chase_camera_native.h).
FL_FIELDS = (
    (0x02241c, 'mov esi, dword ptr [ebx + 0xc]', 'cockpit+0x0c reference object (native follow)'),
    (0x0224d0, 'mov ecx, dword ptr [ebx + 0x10]', 'cockpit+0x10 view object'),
    (0x0224d7, 'cmp ecx, dword ptr [ebx + 0xc]', 'view object == reference object'),
    (0x022102, 'mov eax, dword ptr [ebx + 8]', 'cockpit+0x08 HUD scene camera (scene_fix)'),
    (0x02210c, 'add eax, 0x40', 'scene camera +0x40 basis'),
    (0x0224f9, 'mov eax, dword ptr [ebx + 0x58]', 'cockpit+0x58 sector camera'),
    (0x0224fe, 'add eax, 0x40', 'camera +0x40 basis (native multiply output)'),
    (0x022539, 'movups xmm0, xmmword ptr [ebx + 0xf0]', 'cockpit+0xf0 view_rel row 0'),
    (0x022540, 'movups xmmword ptr [eax + 0x40], xmm0', 'camera basis row 0'),
    (0x022544, 'movups xmm0, xmmword ptr [ebx + 0x100]', 'view_rel row 1 (stride 0x10)'),
    (0x02254b, 'movups xmmword ptr [eax + 0x50], xmm0', 'camera basis row 1'),
    (0x02254f, 'movups xmm0, xmmword ptr [ebx + 0x110]', 'view_rel row 2'),
    (0x022556, 'movups xmmword ptr [eax + 0x60], xmm0', 'camera basis row 2'),
    (0x022151, 'cmp dword ptr [ebx + 0x150], 1', 'cockpit+0x150 view mode'),
    (0x0224c5, 'mov eax, dword ptr [ebx + 0x1c0]', 'cockpit+0x1c0 connect mode'),
    (0x0224e1, 'test byte ptr [ebx + 0x1a0], 4', 'cockpit+0x1a0 flags'),
    (0x022578, 'cmp dword ptr [ebx + 0x160], 0', 'cockpit+0x160 boom tested before the seam'),
    (0x022c30, 'mov eax, dword ptr [ebx + 0x1d8]', 'cockpit+0x1d8 aim gun'),
    (0x03106f, 'mov eax, dword ptr [eax + 0x1e0]', 'cockpit+0x1e0 tracked object'),
    (0x03107d, 'push dword ptr [eax + 8]', 'tracked object +8 id'),
    (0x030fc4, 'cmp word ptr [eax + 0x1e4], 4', 'cockpit+0x1e4 tracking short'),
    (0x0229a8, 'mov dword ptr [ebx + 0x1fc], eax', 'cockpit+0x1fc sector identity (after the seam)'),
    (0x03073d, 'mov dword ptr [esi + 0x58], eax', 'cockpit+0x58 assignment'),
    (0x03074a, 'mov dword ptr [eax + 0x298], ecx', 'camera+0x298 FOV'),
    (0x022cc5, 'mov dword ptr [eax + 0x298], esi', 'later FOV change (after the seam)'),
    (0x0dec76, 'mov edi, dword ptr [edx + 0x300]', 'camera+0x300 plane width'),
    (0x0dec82, 'mov esi, dword ptr [edx + 0x304]', 'camera+0x304 plane height'),
    (0x0dec8c, 'mov eax, dword ptr [0x5a82f8]', 'default plane slot RVA 0x1a82f8'),
    (0x0dec91, 'mov edi, dword ptr [eax + 0x28]', 'default plane +0x28 width'),
    (0x0dec94, 'mov esi, dword ptr [eax + 0x2c]', 'default plane +0x2c height'),
    (0x0dec97, 'movd xmm0, dword ptr [edx + 0x298]', 'projection reads camera+0x298'),
    (0x059780, 'mov eax, dword ptr [ecx + 0x54]', 'reference+0x54 owner'),
    (0x059787, 'cmp word ptr [eax + 0x48], 1', 'owner+0x48 type 1 = base domain'),
    (0x022441, 'mov ecx, dword ptr [esi + 0x70]', 'reference+0x70 node'),
    (0x022447, 'mov eax, dword ptr [ecx + 0x30]', 'node+0x30 base-domain position'),
    (0x02246f, 'mov esi, dword ptr [esi + 0x70]', 'reference+0x70 node (render branch)'),
    (0x022478, 'lea edx, [esi + 0xc0]', 'node+0xc0 render basis'),
    (0x022483, 'mov eax, dword ptr [esi + 0xb0]', 'node+0xb0 render position'),
    (0x059fc0, 'cmp word ptr [ecx + 0x48], 7', 'reference+0x48 type 7 = ship'),
    (0x059fc7, 'mov eax, dword ptr [ecx + 0x50]', 'reference+0x50 simulation data'),
    (0x059fca, 'add eax, 0x870', 'data+0x870 ship basis'),
    (0x059fd0, 'mov eax, dword ptr [ecx + 0x70]', 'node (non-ship)'),
    (0x059fd3, 'add eax, 0x40', 'node+0x40 basis'),
)


def fnv1a32(data):
    h = 0x811c9dc5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xffffffff
    return h


class FlImage(Image):
    """Image plus header fields, the HIGHLOW census and Capstone (FL checks only)."""

    def __init__(self, data):
        super().__init__(data)
        import capstone  # FL only; the AP path keeps its dependency-free decoder
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        self.timestamp = struct.unpack_from('<I', data, pe + 8)[0]
        self.image_size = struct.unpack_from('<I', data, pe + 24 + 56)[0]
        self.dll_characteristics = struct.unpack_from('<H', data, pe + 24 + 70)[0]
        reloc_rva, reloc_size = struct.unpack_from('<II', data, pe + 24 + 96 + 5 * 8)
        self.fixups, self.other_fixups = set(), 0
        o = self.file_offset(self.image_base + reloc_rva) if reloc_size else None
        end = o + reloc_size if o is not None else 0
        while o is not None and o + 8 <= end:
            page, size = struct.unpack_from('<II', data, o)
            if size < 8:
                break
            for k in range((size - 8) // 2):
                e = struct.unpack_from('<H', data, o + 8 + 2 * k)[0]
                if e >> 12 == 3:
                    self.fixups.add(page + (e & 0xfff))
                elif e >> 12:
                    self.other_fixups += 1
            o += size
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True

    def file_offset(self, va):
        for name, base, vsize, rp, rsize in self.sections:
            if base <= va < base + rsize:
                return rp + va - base
        return None

    def rva(self, rva, n):
        return self.read(self.image_base + rva, n)

    def text_at(self, rva):
        code = self.rva(rva, 16)
        i = next(self.md.disasm(code, self.image_base + rva), None) if code else None
        return (f'{i.mnemonic} {i.op_str}'.strip(), i.size) if i else (None, 0)

    def insns(self, start_rva, end_rva):
        """Linear decode of [start, end) at the preferred base; None unless it ends exactly on end."""
        out = list(self.md.disasm(self.rva(start_rva, end_rva - start_rva) or b'', self.image_base + start_rva))
        return out if out and out[-1].address + out[-1].size == self.image_base + end_rva else None

    def direct_edges(self):
        """Linear-sweep census of direct call/jmp/jcc/loop targets in .text (resuming after undecodable bytes)."""
        import capstone
        _, start, _, rp, rsize = next(s for s in self.sections if s[0] == '.text')
        code = self.data[rp:rp + rsize]
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        edges, pos = [], 0
        while pos < len(code):
            last = pos
            for i in md.disasm(code[pos:pos + 0x10000], start + pos):
                last = i.address + i.size - start
                if (i.mnemonic == 'call' or i.mnemonic.startswith('j') or i.mnemonic.startswith('loop')) and \
                        i.op_str.startswith('0x'):
                    edges.append((i.address - self.image_base, i.mnemonic, int(i.op_str, 16) - self.image_base))
            pos = last if last > pos else pos + 1
        return edges


def verify_fl(data):
    report = {'result': 'FAIL', 'edition': 'fl', 'checks': {}}
    checks = report['checks']
    report['exe_info'] = {'sha256': hashlib.sha256(data).hexdigest(), 'known': hashlib.sha256(data).hexdigest() == FL_SHA256}
    try:
        img = FlImage(data)
    except (ValueError, struct.error, StopIteration) as e:
        checks['pe'] = {'ok': False, 'error': str(e)}
        return report
    checks['fl_identity'] = {'ok': img.timestamp == FL_TIMESTAMP and img.image_size == FL_IMAGE_SIZE,
                             'timestamp': f'{img.timestamp:#010x}', 'image_size': f'{img.image_size:#010x}'}
    checks['relocatable'] = {'ok': bool(img.dll_characteristics & 0x40) and img.image_base == IMAGE_BASE,
                             'dll_characteristics': f'{img.dll_characteristics:#06x}', 'highlow': len(img.fixups),
                             'highlow_expected': FL_HIGHLOW_COUNT, 'other_types': img.other_fixups}
    site = img.rva(FL_SITE_RVA, 10)
    checks['site_bytes'] = {'ok': site == FL_SITE_BYTES, 'value': site.hex() if site else None, 'rva': f'{FL_SITE_RVA:#08x}'}
    seam = []
    for rva, text in FL_SEAM:
        got, _ = img.text_at(rva)
        seam.append({'rva': f'{rva:#08x}', 'ok': got == text, 'got': got})
    span = img.insns(FL_SITE_RVA, FL_SITE_RVA + 10)
    span_ok = bool(span) and [i.size for i in span] == [4, 6] and span[1].bytes[:2] == b'\x0f\x84' and \
        FL_SITE_RVA + 10 + struct.unpack_from('<i', site, 6)[0] == FL_BRANCH_TARGET_RVA
    window = img.insns(0x0227a4, 0x0227e6)
    checks['span_decode'] = {'ok': span_ok and bool(window) and all(s['ok'] for s in seam), 'seam': seam,
                             'span_lengths': [i.size for i in span] if span else None, 'rel32_offset': 6,
                             'window_whole_instructions': bool(window)}
    inside = sorted(f for f in img.fixups if FL_SITE_RVA - 3 <= f < FL_SITE_RVA + 10)
    checks['reloc_span'] = {'ok': not inside, 'fixups_touching_span': [f'{f:#08x}' for f in inside]}
    plane = img.rva(0x0dec8d, 4)
    plane_va = struct.unpack('<I', plane)[0] if plane else None
    checks['reloc_plane_slot'] = {'ok': 0x0dec8d in img.fixups and plane_va == IMAGE_BASE + FL_PLANE_SLOT_RVA,
                                  'value': f'{plane_va:#010x}' if plane_va is not None else None}
    windows = {}
    for name, (start, length, expected, relocated) in FL_WINDOWS.items():
        raw = bytearray(img.rva(start, length) or b'')
        fixups = tuple(sorted(f for f in img.fixups if start - 3 <= f < start + length))
        for f in fixups:
            if start <= f and f + 4 <= start + length:
                raw[f - start:f - start + 4] = b'\0\0\0\0'
        windows[name] = {'ok': len(raw) == length and fnv1a32(raw) == expected and fixups == relocated,
                         'fnv1a32': f'{fnv1a32(raw):08x}', 'relocated': [f'{f:#08x}' for f in fixups]}
    checks['windows'] = {'ok': all(w['ok'] for w in windows.values()), **windows}
    edges = img.direct_edges()
    to_start = tuple(sorted(a for a, _, t in edges if t == FL_SITE_RVA))
    interior = [(a, t) for a, _, t in edges if FL_SITE_RVA < t < FL_SITE_RVA + 10]
    to_target = sorted(a for a, _, t in edges if t == FL_BRANCH_TARGET_RVA)
    callers = sorted(a for a, m, t in edges if t == FL_UPDATER_RVA)
    checks['inbound_edges'] = {'ok': to_start == FL_INBOUND_EDGES and not interior,
                               'to_start': [f'{a:#08x}' for a in to_start],
                               'interior': [f'{a:#08x}->{t:#08x}' for a, t in interior], 'direct_edges': len(edges),
                               'to_branch_target': [f'{a:#08x}' for a in to_target],
                               'updater_callers': [f'{a:#08x}' for a in callers]}
    # Raw rel8/rel32 byte-position scan of the updater (the AP heuristic): any encoding landing inside the span. A hit
    # at a byte that is not an instruction start of the updater's linear decode is an alias inside another
    # instruction's operand (reported, not a failure); a hit at an instruction start fails.
    hits = branches_into(img, IMAGE_BASE + FL_UPDATER_RVA, IMAGE_BASE + FL_UPDATER_END_RVA,
                         IMAGE_BASE + FL_SITE_RVA + 1, IMAGE_BASE + FL_SITE_RVA + 10)
    updater = img.insns(FL_UPDATER_RVA, FL_UPDATER_END_RVA)
    starts = {i.address for i in updater} if updater else set()
    real = [h for h in hits if int(h['at'], 16) in starts]
    checks['interior_branches_raw'] = {'ok': bool(updater) and not real, 'updater_whole_instructions': bool(updater),
                                       'updater_instructions': len(updater) if updater else 0,
                                       'at_instruction_start': real,
                                       'aliased_in_operands': [h for h in hits if h not in real]}
    # EBX = cockpit from the updater's entry to the seam (linear decode of [entry, seam)).
    body = img.insns(FL_UPDATER_RVA, FL_CONTINUATION_RVA)
    anchors = [{'rva': f'{rva:#08x}', 'ok': img.text_at(rva)[0] == text} for rva, text in FL_EBX]
    writes, slot_stores, loop_exits = [], [], []
    if body:
        lo, hi = (IMAGE_BASE + r for r in FL_EBX_LOOP)
        for i in body:
            if i.address <= IMAGE_BASE + FL_EBX[0][0]:
                continue
            _, written = i.regs_access()
            if any(i.reg_name(r) in ('ebx', 'bx', 'bl', 'bh') for r in written):
                writes.append(i.address - IMAGE_BASE)
            if i.op_str.startswith('dword ptr [esp + 0x1c],') and i.mnemonic == 'mov':
                slot_stores.append(i.address - IMAGE_BASE)
            if lo <= i.address < hi and i.mnemonic.startswith('j') and i.op_str.startswith('0x'):
                t = int(i.op_str, 16)
                if not lo <= t < hi:
                    loop_exits.append(i.address - IMAGE_BASE)
            if i.mnemonic.startswith('j') and i.op_str.startswith('0x') and not lo <= i.address < hi:
                if lo < int(i.op_str, 16) < hi:
                    loop_exits.append(i.address - IMAGE_BASE)  # an entry past the clobber start would need a proof too
    reload = IMAGE_BASE + FL_EBX[2][0]
    checks['ebx_cockpit'] = {
        'ok': bool(body) and all(a['ok'] for a in anchors) and slot_stores == [FL_EBX[1][0]] and not loop_exits and
        all(FL_EBX_LOOP[0] <= w < FL_EBX_LOOP[1] or w == FL_EBX[2][0] for w in writes),
        'anchors': anchors, 'ebx_writes': [f'{w:#08x}' for w in writes], 'slot_stores': [f'{s:#08x}' for s in slot_stores],
        'foreign_loop_edges': [f'{e:#08x}' for e in loop_exits], 'reload': f'{reload - IMAGE_BASE:#08x}',
        'stack_depth_equal': 'inferred: the engine itself uses EBX as the cockpit after the seam on both branches'}
    fields = []
    for rva, text, what in FL_FIELDS:
        got, _ = img.text_at(rva)
        fields.append({'rva': f'{rva:#08x}', 'ok': got == text, 'got': got, 'field': what})
    checks['field_anchors'] = {'ok': all(f['ok'] for f in fields), 'count': len(fields),
                               'failed': [f for f in fields if not f['ok']]}
    checks['atomic_write'] = {'ok': (FL_SITE_RVA & 7) + 5 > 8, 'qword_aligned_span': (FL_SITE_RVA & 7) + 5 <= 8,
                              'site_mod_8': FL_SITE_RVA & 7,
                              'note': 'crosses a qword: plain copy inside the install window (64 KB base keeps RVA & 7)'}
    report['result'] = 'PASS' if all(c.get('ok') for c in checks.values()) else 'FAIL'
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--edition', choices=('ap', 'fl'), default='ap')
    parser.add_argument('--exe', type=Path, default=None)
    parser.add_argument('--json', action='store_true', help='print the full report')
    args = parser.parse_args()
    exe = args.exe or (FL_DEFAULT_EXE if args.edition == 'fl' else DEFAULT_EXE)
    if not exe.is_file():
        print(json.dumps({'result': 'SKIP', 'reason': f'{exe} not found'}))
        return 2
    args.exe = exe
    data = args.exe.read_bytes()
    report = verify_fl(data) if args.edition == 'fl' else verify(data)
    report['exe'] = str(args.exe)
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(json.dumps({'result': report['result'], 'exe': report['exe'], 'exe_info': report['exe_info'],
                          **{k: v['ok'] for k, v in report['checks'].items()}}))
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
