#!/usr/bin/env python3
"""Generates src/engine/fl/music_windows_fl_inc.h, the FL music family's identity windows, from X3FL.exe.

Every window is a whole-instruction span (linear Capstone decode from its first byte ends exactly at its end) of the
installed X3FL.exe; its identity is FNV-1a 32 over the bytes with every base-relocation HIGHLOW dword zeroed, and each
such dword is recorded with the RVA it must hold at run time (base + target). The windows, their roles and the sites
inside them: docs/reverse-engineering/fl/media-cue.md, music-keep.md, music-alert.md ("Port (2026-11-03)").

usage: python3 verification/results/fl-music/generate_windows.py [--exe X3FL.exe] [--check]
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

OUT = ROOT / 'src/engine/fl/music_windows_fl_inc.h'
DEFAULT_EXE = Path('/mnt/c/X3FL/X3FL.exe')
FL_SHA256 = 'e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8'
BASE = 0x400000

# (table, name, rva, length). Names are the refusal reasons (<name>_mismatch in the rows).
WINDOWS = [
    # media_cue: the allocator (whole), its one wrapper (whole), the sector selector's creation call and kind store,
    # the video consumer's head and its destination LockRect / UnlockRect calls.
    ('media', 'allocator', 0x0b4080, 0x124),
    ('media', 'wrapper', 0x0b46b0, 0x79),
    ('media', 'selector_create', 0x06611d, 0x0e),
    ('media', 'selector_kind', 0x0662f2, 0x26),
    ('media', 'video_head', 0x0fed10, 0x1d),
    ('media', 'video_lock', 0x0fede4, 0x0f),
    ('media', 'video_unlock', 0x0ff54f, 0x1b),
    # music_keep: stop-all (T1 entry, Patch A, the bookkeeping label, the loop continue), the update manager (Patch D
    # call, the inline position reader), retirement, the free wrapper, MOV_StopMovie (T3), the play routine (T2, C, E),
    # the fallback pause, the seek / Run / pump / status heads and exits, the Run state writes, the save's close wrapper
    # and gzwrite call, the VM's KC loader stores and jump handler, the alert record slot witness.
    ('keep', 'stop_all', 0x0b4210, 0xc1),
    ('keep', 'manager', 0x0b42e0, 0x1ab),
    ('keep', 'retirement', 0x0b4490, 0x7b),
    ('keep', 'free_wrapper', 0x0b4640, 0x64),
    ('keep', 'stop_movie', 0x0b47a0, 0xbd),
    ('keep', 'play', 0x0b4c50, 0x1b3),
    ('keep', 'pause', 0x0b41b0, 0x53),
    ('keep', 'seek_head', 0x0fe5f0, 0x25),
    ('keep', 'run_head', 0x0ff8b0, 0x1b),
    ('keep', 'run_state', 0x0ffa01, 0x0e),
    ('keep', 'pump_head', 0x0fe7d0, 0x37),
    ('keep', 'pump_drain', 0x0fece1, 0x2e),
    ('keep', 'status_head', 0x0ff580, 0x24),
    ('keep', 'status_ended', 0x0ff631, 0x0d),
    ('keep', 'status_ds_tail', 0x0ff859, 0x0e),
    ('keep', 'close_wrapper', 0x106940, 0x3a),
    ('keep', 'gzwrite_call', 0x106871, 0x06),
    ('keep', 'kc_build_store', 0x0b90af, 0x06),
    ('keep', 'kc_code_stores', 0x0b9122, 0x1c),
    ('keep', 'kc_jump', 0x0bfb21, 0x0a),
    ('keep', 'mov_dispatch', 0x0b5921, 0x7b),
    ('keep', 'alert_slot', 0x01e550, 0x10),
    # music_alert: the alert routine (whole), the registry update's call, the clock frame, TI_GetAbsTime, stance and
    # relation (whole), the cockpit resolver (whole), and the layout witnesses (collision clear, missile shot counter,
    # hit counter, attack order, selected target copy, group setter).
    ('alert', 'alert', 0x02f910, 0x2d7),
    ('alert', 'registry_update', 0x01e550, 0x10),
    ('alert', 'clock_frame', 0x10b600, 0x66),
    ('alert', 'ti_abs_time', 0x0ce057, 0x20),
    ('alert', 'stance', 0x05a300, 0x90),
    ('alert', 'relation', 0x05a390, 0x5f),
    ('alert', 'cockpit_resolver', 0x01e420, 0x36),
    ('alert', 'collide_clear', 0x066e20, 0x0e),
    ('alert', 'missile_counter', 0x052961, 0x0a),
    ('alert', 'hit_counter', 0x06831e, 0x0c),
    ('alert', 'attack_order', 0x06b588, 0x10),
    ('alert', 'cockpit_target', 0x04e3f7, 0x09),
    ('alert', 'group_set', 0x05a1d0, 0x10),
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
    lines = ['// Generated by verification/results/fl-music/generate_windows.py from X3FL.exe; do not edit.',
             f'// X3FL.exe SHA-256 {im.sha256} (provenance, never a gate).',
             '// Each window: RVA, length, FNV-1a 32 of the bytes with every HIGHLOW dword zeroed, and those dwords',
             '// (offset in the window, the RVA base + which they must hold). Included by code_window_fl.h only.',
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
