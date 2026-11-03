"""Shared reader for the FL loading quintet's site verifiers (verify_gz_buffer_sites_fl.py, verify_crypt_cache_sites_fl.py,
verify_mesh_adjacency_sites_fl.py): the checked-in identity windows (src/engine/fl/loading_windows_fl_inc.h) compared
with the windows regenerated from X3FL.exe (verification/results/fl-loading/generate_windows.py), the named-import
table, the relocation census of every instruction that references an import slot, and the AP refusal. Builds on
fl_music_sites_common.py (image, HIGHLOW census, Capstone). Read-only; no Wine, no game.
"""
import importlib.util
import re
import struct
from pathlib import Path

import fl_music_sites_common as music

ROOT = music.ROOT
BASE = music.BASE
DEFAULT_EXE = music.DEFAULT_EXE
AP_EXE = music.AP_EXE
FL_SHA256 = music.FL_SHA256
INC = ROOT / 'src/engine/fl/loading_windows_fl_inc.h'
_spec = importlib.util.spec_from_file_location('fl_loading_generate',
                                               ROOT / 'verification/results/fl-loading/generate_windows.py')
gen = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gen)
Image = music.Image
finish = music.finish
raw_branch_hits = music.raw_branch_hits


def declared_windows(table, inc=INC):
    """The checked-in table: [(name, rva, length, fnv, [(offset, target)])]."""
    text = Path(inc).read_text()
    body = re.search(r'constexpr CodeWindow %s_windows\[\] = \{(.*?)\n\};' % table, text, re.S).group(1)
    fx = re.search(r'constexpr Fixup %s_fixups\[\] = \{(.*?)\n\};' % table, text, re.S).group(1)
    fixups = [(int(a, 16), int(b, 16)) for a, b in re.findall(r'\{0x([0-9a-f]+), 0x([0-9a-f]+)\}', fx)]
    out = []
    for name, rva, length, first, count, h in re.findall(
            r'\{"([a-z_0-9]+)", 0x([0-9a-f]+), 0x([0-9a-f]+), (\d+), (\d+), 0x([0-9a-f]+)\}', body):
        first, count = int(first), int(count)
        out.append((name, int(rva, 16), int(length, 16), int(h, 16), fixups[first:first + count]))
    return out


def check_windows(im, table, checks, report, inc=INC):
    """The header's windows equal the exe's (masked hash, HIGHLOW census and targets, whole instructions)."""
    declared = declared_windows(table, inc)
    regenerated = {name: (rva, length) for t, name, rva, length in gen.WINDOWS if t == table}
    ok_all = len(declared) == len(regenerated) and len(declared) > 0
    failures = []
    for name, rva, length, h, fixups in declared:
        masked, fx, whole = gen.music.window(im, rva, length)
        same = regenerated.get(name) == (rva, length) and gen.music.fnv1a(masked) == h and fx == fixups and whole
        ok_all = ok_all and same
        if not same:
            failures.append(name)
    checks[f'{table}_windows_match_exe'] = ok_all
    report[f'{table}_window_count'] = len(declared)
    report[f'{table}_fixup_count'] = sum(len(d[4]) for d in declared)
    report[f'{table}_window_failures'] = failures
    return declared


def masked_matches(im, rva, length, fnv, fixups, base=BASE):
    raw = bytearray(im.read(rva, length))
    for off, target in fixups:
        if struct.unpack_from('<I', raw, off)[0] != (base + target) & 0xffffffff:
            return False
        raw[off:off + 4] = b'\0\0\0\0'
    return gen.music.fnv1a(bytes(raw)) == fnv


def mutation_refusals(im, table):
    """Single-byte flips (first, middle, last byte) and +0x10000 fixup changes of every window, each refused by the
    production decision (masked hash at the preferred base): (cases, refused)."""
    cases = refused = 0
    for name, rva, length, h, fixups in declared_windows(table):
        raw = bytearray(im.read(rva, length))
        variants = []
        for at in sorted({0, length // 2, length - 1}):
            v = bytearray(raw)
            v[at] ^= 0x01
            variants.append(v)
        for off, _ in fixups:
            v = bytearray(raw)
            struct.pack_into('<I', v, off, (struct.unpack_from('<I', v, off)[0] + 0x10000) & 0xffffffff)
            variants.append(v)
        for v in variants:
            cases += 1
            masked = bytearray(v)
            ok = True
            for off, target in fixups:
                if struct.unpack_from('<I', masked, off)[0] != BASE + target:
                    ok = False
                masked[off:off + 4] = b'\0\0\0\0'
            refused += not (ok and gen.music.fnv1a(bytes(masked)) == h)
    return cases, refused


def imports(im, dll):
    """{name: slot RVA} of one DLL's named imports (case-insensitive DLL name)."""
    for d, rows in im.pe.imports().items():
        if d.lower() == dll.lower():
            return {r['name']: int(r['iat_va'], 16) - BASE for r in rows}
    return {}


def slot_references(im, slot_rva):
    """Every HIGHLOW in .text holding BASE + slot_rva, decoded as the instruction that carries it: [(insn_rva,
    mnemonic, op_str)]. A relocated dword that is no instruction operand is reported as ('data', ...)."""
    lo, hi, _ = im.text
    out = []
    for f in sorted(im.fixups):
        if not lo <= f < hi or im.u32(f) != BASE + slot_rva:
            continue
        found = None
        for back in (2, 1):
            start = f - back
            insns = list(im.md.disasm(im.read(start, 8), BASE + start))
            if insns and insns[0].size == back + 4 and ('[0x%x]' % (BASE + slot_rva)) in insns[0].op_str:
                found = (start, insns[0].mnemonic, insns[0].op_str)
                break
        out.append(found or (f, 'data', ''))
    return out


def pointers_outside_text(im, slot_rva):
    """HIGHLOW dwords outside .text that hold BASE + slot_rva (a computed alias of the slot)."""
    lo, hi, _ = im.text
    return [f for f in im.fixups if not lo <= f < hi and im.u32(f) == BASE + slot_rva]


def ap_windows_admitted(table):
    if not AP_EXE.exists():
        return None
    ap = Image(AP_EXE)
    admitted = []
    for name, rva, length, h, fixups in declared_windows(table):
        try:
            if masked_matches(ap, rva, length, h, fixups):
                admitted.append(name)
        except (ValueError, struct.error):
            pass
    return admitted


def header_imports(path):
    """The sites header's {"name", 0xRVA} import table."""
    return {n: int(v, 16) for n, v in re.findall(r'\{"([A-Za-z_0-9]+)", (0x[0-9a-f]+)\}', Path(path).read_text())}
