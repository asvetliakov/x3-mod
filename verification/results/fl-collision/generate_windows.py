#!/usr/bin/env python3
"""Generates src/engine/fl/collide_windows_fl_inc.h, the identity windows of the FL collision trio, from X3FL.exe.

Every window is a whole-instruction span (linear Capstone decode from its first byte ends exactly at its end) of the
installed X3FL.exe; its identity is FNV-1a 32 over the bytes with every base-relocation HIGHLOW dword zeroed, and each
such dword is recorded with the RVA it must hold at run time (base + target). Roles and sites:
docs/reverse-engineering/fl/collision-sat.md, collision-memo.md, collision-box.md ("Port (2026-11-03)"); checked by
verification/probe/verify_collide_sat_fl.py, verify_collide_memo_sites_fl.py and verify_collide_box_cull_sites_fl.py.

usage: python3 verification/results/fl-collision/generate_windows.py [--exe X3FL.exe] [--check]
  --check  compares the generated text with the checked-in header (exit 1 on a difference); writes nothing.
"""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

import capstone

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from inspect_x3 import PE32  # noqa: E402

OUT = ROOT / 'src/engine/fl/collide_windows_fl_inc.h'
DEFAULT_EXE = Path('/mnt/c/X3FL/X3FL.exe')
FL_SHA256 = 'e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8'
BASE = 0x400000

# (table, name, rva, length). Names are the refusal reasons (<name>_mismatch in the rows).
WINDOWS = [
    # collide_sat_sse2: the native SAT (whole) and the descent's call/return span (the scaled-B store, the call, the
    # stack drop and the zero test).
    ('sat', 'sat_body', 0x11c6d0, 0x67b),
    ('sat', 'sat_call', 0x11a73e, 0x12),
    # collide_memo: the transform caller (whole, holds the call site 0x9853a), the query, its descent, triangle leaf,
    # triangle test and its projection helper, the SAT, the two matrix products (the closure), and the model mutators
    # the built-state guard rests on (reset, insert, finish, destroy, construct).
    ('memo', 'caller', 0x0982d0, 0x284),
    ('memo', 'query', 0x11aae0, 0x580),
    ('memo', 'descent', 0x11a6c0, 0x418),
    ('memo', 'leaf', 0x11a260, 0x45d),
    ('memo', 'triangle', 0x11b2a0, 0x1430),
    ('memo', 'projection', 0x11b160, 0x135),
    ('memo', 'sat', 0x11c6d0, 0x67b),
    ('memo', 'transpose_product', 0x116b70, 0x182),
    ('memo', 'matrix_product', 0x11a0e0, 0x171),
    ('memo', 'reset_model', 0x117c10, 0xab),
    ('memo', 'insert_triangle', 0x117cc0, 0x17e),
    ('memo', 'finish_model', 0x11b060, 0x4c),
    ('memo', 'destroy_model', 0x11b0f0, 0x64),
    ('memo', 'construct_model', 0x11cd50, 0x5d),
    # collide_box_cull: the nine windows of collision-box.md (radius source, class-7 entry, P1 distance, P1 class-7
    # rejection, P1 continuation, P2 inbound, P2 distance, P2 continuation, the scalar sqrt helper).
    ('box', 'radius', 0x06703d, 0x14),
    ('box', 'class7_entry', 0x0671b1, 0x8e),
    ('box', 'p1_distance', 0x06723f, 0x70),
    ('box', 'p1_class7_reject', 0x0673ae, 0x15),
    ('box', 'p1_continue', 0x067e7e, 0x14),
    ('box', 'p2_inbound', 0x066833, 0x26),
    ('box', 'p2_distance', 0x066859, 0x72),
    ('box', 'p2_continue', 0x066a1b, 0x2e),
    ('box', 'sqrt_helper', 0x012a80, 0x0e),
]


class Image:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        self.pe = PE32(self.data)
        pe_off = self.pe.u32(0x3c)
        reloc_rva, reloc_size = struct.unpack_from('<II', self.data, pe_off + 24 + 96 + 5 * 8)
        self.fixups = set()
        if reloc_rva:
            o, end = self.pe.offset(reloc_rva), self.pe.offset(reloc_rva) + reloc_size
            while o < end:
                page, size = struct.unpack_from('<II', self.data, o)
                if size < 8:
                    break
                for k in range((size - 8) // 2):
                    e = struct.unpack_from('<H', self.data, o + 8 + 2 * k)[0]
                    if e >> 12 == 3:
                        self.fixups.add(page + (e & 0xfff))
                o += size
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True

    def read(self, rva, n):
        o = self.pe.offset(rva)
        return self.data[o:o + n]

    def u32(self, rva):
        return struct.unpack_from('<I', self.read(rva, 4))[0]


def fnv1a(data):
    h = 0x811c9dc5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xffffffff
    return h


def window(im, rva, length):
    """(masked bytes, [(offset, target_rva)], whole_instructions) of one window."""
    raw = bytearray(im.read(rva, length))
    fixups = []
    for f in sorted(x for x in im.fixups if rva - 3 <= x < rva + length):
        if f < rva or f + 4 > rva + length:
            raise SystemExit(f'window {rva:#x}+{length:#x}: a relocation straddles its edge at {f:#x}')
        fixups.append((f - rva, (im.u32(f) - BASE) & 0xffffffff))
        raw[f - rva:f - rva + 4] = b'\0\0\0\0'
    insns = list(im.md.disasm(im.read(rva, length), BASE + rva))
    whole = bool(insns) and insns[-1].address + insns[-1].size == BASE + rva + length
    return bytes(raw), fixups, whole


def generate(exe):
    im = Image(exe)
    lines = ['// Generated by verification/results/fl-collision/generate_windows.py from X3FL.exe; do not edit.',
             f'// X3FL.exe SHA-256 {im.sha256} (provenance, never a gate).',
             '// Each window: RVA, length, FNV-1a 32 of the bytes with every HIGHLOW dword zeroed, and those dwords',
             '// (offset in the window, the RVA base + which they must hold). Included by collide_windows_fl.h only.',
             '// clang-format off']
    tables = {}
    for table, name, rva, length in WINDOWS:
        masked, fixups, whole = window(im, rva, length)
        if not whole:
            raise SystemExit(f'{name}: {rva:#x}+{length:#x} is not a whole-instruction span')
        tables.setdefault(table, []).append((name, rva, length, fnv1a(masked), fixups))
    for table, rows in tables.items():
        fx = []
        lines.append(f'constexpr CodeWindow {table}_windows[] = {{')
        for name, rva, length, h, fixups in rows:
            lines.append(f'    {{"{name}", 0x{rva:06x}, 0x{length:03x}, {len(fx)}, {len(fixups)}, 0x{h:08x}}},')
            fx.extend(fixups)
        lines.append('};')
        lines.append(f'constexpr Fixup {table}_fixups[] = {{')
        body = ', '.join(f'{{0x{o:03x}, 0x{t:06x}}}' for o, t in fx) or '{0, 0}'
        while body:
            cut = body[:110].rfind('}, ') + 2 if len(body) > 110 else len(body)
            lines.append('    ' + body[:cut].strip())
            body = body[cut:].strip()
        lines.append('};')
        lines.append(f'constexpr unsigned {table}_fixup_count = {len(fx)};')
    lines.append('// clang-format on')
    return '\n'.join(lines) + '\n', im


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    text, im = generate(args.exe)
    if im.sha256 != FL_SHA256:
        print(f'warning: {args.exe} SHA-256 {im.sha256} is not the measured image', file=sys.stderr)
    if args.check:
        same = OUT.exists() and OUT.read_text() == text
        print('match' if same else 'differs')
        return 0 if same else 1
    OUT.write_text(text)
    print(f'wrote {OUT.relative_to(ROOT)} ({len(WINDOWS)} windows)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
