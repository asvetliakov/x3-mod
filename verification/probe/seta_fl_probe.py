"""Shared helpers of the two FL SETA site verifiers (verify_seta_manual_flight_sites_fl.py,
verify_seta_restore_sites_fl.py): the X3FL.exe image (verify_chase_camera_site.FlImage), the identity-window record in
the src/engine/fl/seta_windows_fl.h form (bytes with every relocated dword zeroed, FNV-1a 32, each fixup's target RVA),
its C++ row, the raw every-byte relative-branch census and the header row parser. Read-only; nothing is written."""
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_chase_camera_site import (FL_DEFAULT_EXE, FL_IMAGE_SIZE, FL_SHA256, FL_TIMESTAMP, FlImage,  # noqa: E402,F401
                                      fnv1a32)

BASE = 0x00400000


def window_record(img, rva, length):
    """(bytes with fixups zeroed, ((offset, target RVA), ...)) or None when a fixup straddles the window's edge."""
    raw = bytearray(img.rva(rva, length) or b'')
    if len(raw) != length:
        return None
    fix = []
    for f in sorted(img.fixups):
        if rva - 3 <= f < rva + length:
            if not (rva <= f and f + 4 <= rva + length):
                return None
            value = struct.unpack_from('<I', raw, f - rva)[0]
            fix.append((f - rva, value - BASE))
            raw[f - rva:f - rva + 4] = b'\0\0\0\0'
    return bytes(raw), tuple(fix)


def window_row(name, rva, length, raw, fix, extra=''):
    """The C++ row of seta_windows_fl.h's Window table (name, rva, length, fnv1a, fixups, {fixup...}, {bytes...})."""
    body = ', '.join(f'0x{b:02x}' for b in raw)
    fixups = ', '.join(f'{{{o}, 0x{t:06x}}}' for o, t in fix)
    return (f'    {{"{name}", 0x{rva:06x}, {length}, 0x{fnv1a32(raw):08x}u, {len(fix)}, {{{fixups}}},{extra}\n'
            f'     {{{body}}}}},')


def header_window_rows(text):
    """name -> (rva, length, fnv1a, ((offset, target), ...)) of every Window row in a sites header."""
    rows = {}
    pattern = r'\{"([a-z_0-9]+)", 0x([0-9a-f]+), (\d+), 0x([0-9a-f]{8})u, (\d+), \{(.*?)\}, \d+,\s*\n'
    for name, rva, length, h, count, fixups in re.findall(pattern, text):
        fix = tuple((int(o), int(t, 16)) for o, t in re.findall(r'\{(\d+), 0x([0-9a-f]+)\}', fixups))
        rows[name] = (int(rva, 16), int(length), int(h, 16), fix)
    return rows


def text_range(img):
    _, start, _, rp, rsize = next(s for s in img.sections if s[0] == '.text')
    return start - BASE, start - BASE + rsize, rp


def raw_branch_hits(img, lo, hi):
    """Every raw E8/E9/EB/7x/E0..E3/0F8x encoding in .text whose target lies strictly inside (lo, hi): [(source, target)]."""
    start, end, rp = text_range(img)
    data = img.data[rp:rp + (end - start)]
    hits = []
    for i in range(len(data) - 1):
        b, rva, target = data[i], start + i, None
        if b in (0xe8, 0xe9) and i + 5 <= len(data):
            target = (rva + 5 + struct.unpack_from('<i', data, i + 1)[0]) & 0xffffffff
        elif b == 0xeb or 0x70 <= b <= 0x7f or 0xe0 <= b <= 0xe3:  # jmp short, jcc, loop/loope/loopne/jecxz
            target = rva + 2 + struct.unpack_from('<b', data, i + 1)[0]
        elif b == 0x0f and 0x80 <= data[i + 1] <= 0x8f and i + 6 <= len(data):
            target = (rva + 6 + struct.unpack_from('<i', data, i + 2)[0]) & 0xffffffff
        if target is not None and lo < target < hi:
            hits.append((rva, target))
    return hits


def raw_callers(img, target):
    """Every raw E8 rel32 in .text that reaches target (RVA)."""
    start, end, rp = text_range(img)
    data = img.data[rp:rp + (end - start)]
    return [start + i for i in range(len(data) - 5)
            if data[i] == 0xe8 and (start + i + 5 + struct.unpack_from('<i', data, i + 1)[0]) & 0xffffffff == target]


def pointers(img):
    """Relocated dword value (as RVA) -> the fixup RVAs holding it."""
    out = {}
    for f in img.fixups:
        raw = img.rva(f, 4)
        if raw:
            out.setdefault(struct.unpack('<I', raw)[0] - BASE, []).append(f)
    return out


def starts(img, lo, hi):
    """Instruction starts of a linear decode from lo (a known instruction start) up to hi (RVAs)."""
    return {i.address - BASE for i in img.md.disasm(img.rva(lo, hi - lo) or b'', BASE + lo)}


def anchor_rows(img, anchors):
    rows = []
    for rva, text, what in anchors:
        got, _ = img.text_at(rva)
        rows.append({'rva': f'{rva:#08x}', 'ok': got == text, 'got': got, 'want': text, 'what': what})
    return {'ok': all(r['ok'] for r in rows), 'count': len(rows), 'failed': [r for r in rows if not r['ok']]}
