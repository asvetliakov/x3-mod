#!/usr/bin/env python3
"""Read-only qualification of the FL oversized-disc sites in X3FL.exe (src/engine/fl/circle_fill_sites_fl.h,
circle_fill_fl.cpp; docs/reverse-engineering/fl/circle-fill.md, "Port (2026-11-03)"). Replaces the 2026-10-28 static
study verify_fl_circle_fill.py (fl-codex), whose region and operand evidence it keeps.

Checks, on the installed exe (default /mnt/c/X3FL/X3FL.exe, SHA-256 e7f3aad8..., preferred base 0x400000, every value
an RVA):

- identity: the hash, base 0x400000, HIGHLOW relocations only;
- every window of the header: its bytes with the .reloc census zeroed hash (FNV-1a 32) to the header's value, the
  census is exactly the header's relocation offsets and each points at the declared target RVA; the raw SHA-256 of the
  thirteen regions of the 2026-10-28 study (rasterizer, class, lock, five leaves, upstream caller, converter, format
  mapper, CRT fill, lookup) and the float 2^-16;
- the claimed entry span: `push ebp; mov ebp,esp; and esp,-8` (1 + 2 + 3 bytes, whole instructions, no relocation,
  no relative operand), continuation `sub esp,0x64`, the five patch bytes in one aligned qword; no raw branch encoding,
  decoded edge or relocated pointer into 0x10c941..45; the only direct caller 0x0a7b19 and the three upstream callers;
- the register convention: the caller loads ECX = surface ([esp+0x1c]) and EDX = cx (mov edx, ebx) after four pushes
  (aspect, colour, r, cy) and pops 16 bytes after the call (`add esp,0x10`), ESI live after it (`mov ecx, esi`); the
  rasterizer copies EDX to EBX and ECX to ESI before any other read of them and writes EAX first at 0x10c949; both
  exits are plain `ret`; the twelve plain returns of the study;
- the helper entries: the lock call (ECX = backing from [esi+0x30], EDX = out, push 0, add esp,4), the class call
  (EDX = surface, ECX = 0, cwde), the class table (index bytes, jump table, mov ebx, leaf) mapping {0,5,10,15,24} to the
  header's leaves, the unsupported-class diagnostic (push message, call 0x075110, add esp,4), the B conversion sequence
  0x10ca29..58, the live width/height reads of the emitter, the class function and CRT fill call-free;
- arithmetic: max |d| at r 813 < 2^31 <= at 814 (verify_circle_fill_site.max_abs_d), the B rounding witness
  (r 814, aspect 1051682: 2572 native, 2573 exact) and the directed-rounding bound behind the routing (seven radii x
  65535 q);
- mutation guards: copies of the image with ret 16 at an exit, the caller's EDX taken from ECX, the caller's ECX load
  changed, a truncated AND prologue, a class/lock/leaf body byte, and the header's entry moved to the AP address
  (0x0ef620) all FAIL.

Writes one JSON report to stdout (result PASS/FAIL). No Wine, no game launch, nothing written to the exe.

usage: python3 verification/probe/verify_circle_fill_site_fl.py [X3FL.exe] [--out report.json]
"""
import argparse
import hashlib
import json
import math
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_fov_site_fl import Image, BASE, raw_branch_hits, direct_callers  # noqa: E402
from verify_circle_fill_site import max_abs_d  # noqa: E402

SITES = ROOT / 'src/engine/fl/circle_fill_sites_fl.h'
DEFAULT_EXE = Path('/mnt/c/X3FL/X3FL.exe')
FL_SHA256 = 'e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8'
# The 2026-10-28 study's regions (VA, length, SHA-256 of the raw file bytes).
STUDY_REGIONS = [
    (0x50c940, 0x491, 'b492976ea6bf7e39ad02f96902c3955743e605af7ac19eee4977ef4ab68ee458'),
    (0x50ba70, 0x199, '8b995ae2965909ff1401d74f75bae7c5f96d0dec67564c8c3ab759b5051fc41a'),
    (0x4f5a90, 0xdc, '09d884aa8bd34eca958479efadc5b6f4d15c88ba515e7aef73aa597d55983eea'),
    (0x4d42a0, 0x4b, '6cea64a37e6a598bcfb3cc225f06ccd143a48176e60b5df69a5f8fa7eee691d6'),
    (0x4d42f0, 0x200, 'fa6deb86d1022aedc3bdc6c4c7bd354be8bc9a1a9b88cb3b9e6eabf62209ffca'),
    (0x4d44f0, 0x8f, '8490fb04cc16bb541067423c269f46747e0c3d6a800a4ad25d29480a3fdfd60e'),
    (0x4d4580, 0x60, '402d55f67217bfb312083d2ab92dc59a76851f519d3db93bd713d507c944ee4c'),
    (0x4d45e0, 0x70, '6555a3ae0ad6d742cb1eb0cc655519910e7ae21005522bba19fde6da6ae257c3'),
    (0x4a79a0, 0x18f, '8ef3b4418854532f3655a25993ee8d4b1d293b8aa86e8174cb9966e802bce841'),
    (0x4f4e90, 0x124, '45acc01c108ad0a59524848849d9bf3c86b1575351e1784571b0047a0ab6aa0a'),
    (0x4ea770, 0x6e6, '045b2e885b79dce9cbf85cbee8d01818c4a9f82b2a396ff98f3bbb65f9fa4f4d'),
    (0x521920, 0x15a, 'b5696ddc443d4d4a579cae54bebe7733a0fc7e9a734207dbc8534e3137dbab7b'),
    (0x5123a0, 0x6a, 'cb98a1392d2c9a4fe6182a637ca3303becfcc19caeb1e86a821af9db59bd1f41')]
UPSTREAM = 0x0a79a0
UPSTREAM_CALLERS = [0x090f90, 0x0a7a48, 0x0b2525]
# (rva, mnemonic, operands) the port relies on; VAs at the preferred base.
OPERANDS = [
    (0x0a7b0c, 'push', 'dword ptr [esp + 0x28]'), (0x0a7b10, 'mov', 'ecx, dword ptr [esp + 0x1c]'),
    (0x0a7b14, 'push', 'edx'), (0x0a7b15, 'push', 'edi'), (0x0a7b16, 'push', 'ebp'), (0x0a7b17, 'mov', 'edx, ebx'),
    (0x0a7b19, 'call', '0x50c940'), (0x0a7b1e, 'add', 'esp, 0x10'), (0x0a7b22, 'mov', 'ecx, esi'),
    (0x0a7a73, 'mov', 'dword ptr [esp + 0x18], edx'),
    (0x10c946, 'sub', 'esp, 0x64'), (0x10c949, 'mov', 'eax, dword ptr [ebp + 0xc]'), (0x10c94e, 'mov', 'ebx, edx'),
    (0x10c958, 'movsx', 'edx, ax'), (0x10c95b, 'mov', 'esi, ecx'), (0x10c975, 'movsx', 'eax, word ptr [esi + 4]'),
    (0x10c98a, 'movsx', 'eax, word ptr [esi + 6]'), (0x10c9a9, 'mov', 'ecx, dword ptr [esi + 0x30]'),
    (0x10c9b4, 'test', 'dword ptr [esi + 0x10], 0x200000'), (0x10c9c1, 'cmp', 'dword ptr [esi], 0'),
    (0x10c9c6, 'push', '0'), (0x10c9c8, 'lea', 'edx, [esp + 0x54]'), (0x10c9cc, 'call', '0x4f5a90'),
    (0x10c9d1, 'add', 'esp, 4'), (0x10c9d4, 'mov', 'dword ptr [esi], eax'), (0x10c9de, 'or', 'dword ptr [esi + 0x10], 2'),
    (0x10c9e2, 'mov', 'edx, esi'), (0x10c9e4, 'xor', 'ecx, ecx'), (0x10c9e6, 'call', '0x50ba70'), (0x10c9eb, 'cwde', ''),
    (0x10c9ec, 'cmp', 'eax, 0x18'), (0x10c9ef, 'ja', '0x50cd89'),
    (0x10ca24, 'mov', 'eax, dword ptr [ebp + 0x14]'), (0x10ca27, 'imul', 'eax'),
    (0x10ca29, 'movd', 'xmm1, dword ptr [esp + 0x20]'), (0x10ca2f, 'cvtdq2pd', 'xmm1, xmm1'),
    (0x10ca33, 'add', 'eax, 0x8000'), (0x10ca38, 'adc', 'edx, 0'), (0x10ca3b, 'shrd', 'eax, edx, 0x10'),
    (0x10ca42, 'movd', 'xmm0, eax'), (0x10ca46, 'cvtdq2ps', 'xmm0, xmm0'),
    (0x10ca49, 'mulss', 'xmm0, dword ptr [0x591994]'), (0x10ca51, 'cvtps2pd', 'xmm0, xmm0'),
    (0x10ca54, 'divsd', 'xmm1, xmm0'), (0x10ca58, 'cvttsd2si', 'eax, xmm1'), (0x10caa6, 'cvttsd2si', 'ecx, xmm1'),
    (0x10cb1d, 'movzx', 'ecx, word ptr [esi + 4]'), (0x10ccb2, 'movzx', 'eax, word ptr [esi + 4]'),
    (0x10cb4a, 'cmp', 'ax, word ptr [esi + 6]'), (0x10cb72, 'cmp', 'ax, word ptr [esi + 6]'),
    (0x10ccd6, 'cmp', 'ax, word ptr [esi + 6]'), (0x10ccf8, 'cmp', 'ax, word ptr [esi + 6]'),
    (0x10cd89, 'push', '0x590fc0'), (0x10cd8e, 'call', '0x475110'), (0x10cd93, 'add', 'esp, 4'),
    (0x0f5a95, 'mov', 'ebx, ecx'), (0x0f5a97, 'mov', 'dword ptr [esp + 0xc], edx'),
    (0x0f5aa5, 'call', 'dword ptr [eax + 0x30]'), (0x0f5adf, 'call', 'dword ptr [eax + 0x34]'),
    (0x0f5b10, 'call', 'eax'), (0x0f5b21, 'cmp', 'edi, 1'), (0x0f5b60, 'mov', 'dword ptr [ecx + 0x18], eax'),
    (0x0f4ebe, 'call', 'dword ptr [eax + 0x44]'), (0x0f4f4e, 'call', 'dword ptr [eax + 0x30]'),
    (0x1123a0 + 0x60, 'mov', 'eax, dword ptr [eax + esi*8 + 8]'),
    (0x0d42a0, 'mov', 'edx, dword ptr [esp + 4]')]
PLAIN_RETURNS = [0x10cd88, 0x10cd9c, 0x0f5acd, 0x0f5b54, 0x0f5b6b, 0x0d42ea, 0x0d43b3, 0x0d443a, 0x0d44ef, 0x0d457e,
                 0x0d45df, 0x0d464f]
CALL_FREE = [(0x10ba70, 0x199), (0x121920, 0x15a)]


def header(text):
    """The header's constants and window table."""
    values = {}
    for name, value in re.findall(r'\b([a-z_0-9]+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\b', text):
        values.setdefault(name, int(value, 0))
    entry = bytes(int(v, 16) for v in re.search(r'expected_entry\[site_length\] = \{([^}]*)\}', text).group(1)
                  .replace('0x', ' ').split(','))

    def num(token):
        token = token.strip()
        return int(token, 0) if re.match(r'^(0x[0-9a-fA-F]+|\d+)$', token) else values[token]
    windows = []
    for name, rva, length, fnv, count, offsets, targets in re.findall(
            r'\{"([a-z0-9_]+)", ([a-z0-9_x]+), (0x[0-9a-f]+|\d+), 0x([0-9a-f]+)u, (\d+),\s*\{([^}]*)\},\s*\{([^}]*)\}\}',
            text, re.S):
        offs = [num(x) for x in offsets.split(',') if x.strip()]
        tgts = [num(x) for x in targets.split(',') if x.strip()]
        windows.append(dict(name=name, rva=num(rva), length=int(length, 0), fnv=int(fnv, 16), count=int(count),
                            offsets=offs, targets=tgts))
    return values, entry, windows


def fnv1a(data):
    h = 0x811c9dc5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xffffffff
    return h


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def i32(x):
    return (x + 2 ** 31) % 2 ** 32 - 2 ** 31


def native_b(r, aspect):
    q = i32((aspect * aspect + 0x8000) >> 16)
    den = f32(f32(q) * 2.0 ** -16)
    v = r * r / den
    return math.trunc(v) if -2 ** 31 <= v < 2 ** 31 else -2 ** 31


def ins_at(im, rva):
    return next(im.md.disasm(im.read(rva, 15), BASE + rva))


def target(im, rva):
    """The immediate operand of the instruction at rva as an RVA, None when it has none."""
    op = ins_at(im, rva).op_str
    return int(op, 16) - BASE if re.match(r'^0x[0-9a-f]+$', op) else None


def static_checks(im, values, entry, windows):
    """The window, operand and table checks (fast: used for the mutation guards too)."""
    v = values
    checks, rows = {}, {}
    ok_windows = True
    for w in windows:
        live = bytearray(im.read(w['rva'], w['length']))
        census = [f - w['rva'] for f in im.fixups_in(w['rva'] - 3, w['rva'] + w['length'])]
        pointed = [im.u32(w['rva'] + o) - BASE for o in census if 0 <= o <= w['length'] - 4]
        for o in census:
            live[max(o, 0):o + 4] = b'\0' * (4 - max(-o, 0))
        h = fnv1a(bytes(live))
        ok = (h == w['fnv'] and census == w['offsets'] and pointed == w['targets'] and len(census) == w['count'])
        rows[w['name']] = dict(rva=hex(w['rva']), length=w['length'], fnv1a=hex(h), relocs=len(census))
        ok_windows = ok_windows and ok
        checks['window_' + w['name'].replace('_mismatch', '')] = ok
    checks['windows_all'] = ok_windows and len(windows) == 11
    study = [hashlib.sha256(im.read(va - BASE, n)).hexdigest() == h for va, n, h in STUDY_REGIONS]
    checks['study_regions_sha256'] = all(study) and len(study) == 13
    checks['scale_is_2_pow_minus_16'] = im.read(v['scale_rva'], 4) == struct.pack('<f', 2.0 ** -16)
    # The entry span.
    span = list(im.md.disasm(im.read(v['entry_rva'], 9), BASE + v['entry_rva']))
    checks['entry_span'] = (im.read(v['entry_rva'], v['site_length']) == entry and
                            [(i.mnemonic, i.op_str, i.size) for i in span[:4]] ==
                            [('push', 'ebp', 1), ('mov', 'ebp, esp', 2), ('and', 'esp, 0xfffffff8', 3),
                             ('sub', 'esp, 0x64', 3)] and
                            v['entry_rva'] + v['site_length'] == v['continue_rva'] and
                            not im.fixups_in(v['entry_rva'] - 3, v['continue_rva']) and
                            (v['entry_rva'] & 7) + 5 <= 8)
    # Named operands, plain returns, call-free bodies.
    bad = []
    for rva, mnemonic, ops in OPERANDS:
        i = ins_at(im, rva)
        if (i.mnemonic, i.op_str) != (mnemonic, ops):
            bad.append([hex(rva), i.mnemonic, i.op_str])
    checks['named_operands'] = not bad
    checks['plain_returns'] = all(im.read(r, 1) == b'\xc3' for r in PLAIN_RETURNS)
    import capstone
    checks['call_free_bodies'] = all(not any(i.group(capstone.CS_GRP_CALL) for i in im.md.disasm(im.read(r, n), BASE + r))
                                     for r, n in CALL_FREE)
    # The register convention: EDX and ECX read first by the two copies; EAX written first at 0x10c949.
    body = list(im.md.disasm(im.read(v['entry_rva'], 0x30), BASE + v['entry_rva']))
    first_reads = {}
    for i in body:
        regs_read, regs_write = i.regs_access()
        for r in regs_read:
            first_reads.setdefault(i.reg_name(r), i.address - BASE)
        for r in regs_write:
            first_reads.setdefault('w:' + i.reg_name(r), i.address - BASE)
    checks['register_convention'] = (first_reads.get('edx') == 0x10c94e and first_reads.get('ecx') == 0x10c95b and
                                     first_reads.get('w:eax') == 0x10c949 and 'eax' not in
                                     {k for k, a in first_reads.items() if a < 0x10c949})
    # Helper entries the header names.
    checks['helper_entries'] = (target(im, 0x10c9cc) == v['lock_rva'] and target(im, 0x10c9e6) == v['class_rva'] and
                                target(im, 0x10cd8e) == v['diagnostic_rva'] and target(im, 0x10cd89) == v['message_rva'] and
                                im.read(v['message_rva'], 31) == b'FilledCircle: Unknown Blittype\n')
    jumps = struct.unpack('<6I', im.read(v['jump_table_rva'], 24))
    indices = im.read(v['class_index_rva'], 25)
    cmap = {}
    for cls, idx in enumerate(indices):
        i = ins_at(im, jumps[idx] - BASE)
        if i.mnemonic == 'mov' and re.match(r'^ebx, 0x[0-9a-f]+$', i.op_str):
            cmap[cls] = int(i.op_str.split(', ')[1], 16) - BASE
    want = {0: v['leaf_class0_rva'], 5: v['leaf_class5_rva'], 10: v['leaf_class10_rva'], 15: v['leaf_class15_rva'],
            24: v['leaf_class24_rva']}
    checks['class_table'] = cmap == want and jumps[5] - BASE == 0x10cd89
    return checks, rows, bad, cmap


def verify(exe=DEFAULT_EXE, sites_path=SITES, sha256=FL_SHA256, header_text=None, data=None, full=True):
    values, entry, windows = header(header_text if header_text is not None else Path(sites_path).read_text())
    im = Image(exe)
    if data is not None:
        im.data = data
    report = {'exe': str(exe), 'sha256': hashlib.sha256(im.data).hexdigest()}
    checks, rows, bad, cmap = static_checks(im, values, entry, windows)
    checks['identity'] = (sha256 is None or report['sha256'] == sha256) and im.pe.base == BASE and im.other_fixup_types == 0
    report.update(windows=rows, bad_operands=bad, class_map={str(k): hex(t) for k, t in cmap.items()})
    if full:
        v = values
        callers = direct_callers(im, v['entry_rva'])
        upstream = direct_callers(im, UPSTREAM)
        hits = raw_branch_hits(im, v['entry_rva'], v['continue_rva'])
        pointers = [f for f in im.fixups if v['entry_rva'] < im.u32(f) - BASE < v['continue_rva']]
        literals = sum(im.data.count(struct.pack('<I', BASE + r)) for r in range(v['entry_rva'] + 1, v['continue_rva']))
        report.update(direct_callers=[hex(c) for c in callers], upstream_callers=[hex(c) for c in upstream],
                      interior_raw_hits=[[hex(a), hex(b)] for a, b in hits], interior_pointers=[hex(p) for p in pointers],
                      interior_literals=literals)
        checks['only_caller'] = callers == [v['caller_rva']] and v['caller_rva'] + 5 == v['caller_return_rva']
        checks['upstream_callers'] = upstream == UPSTREAM_CALLERS
        checks['no_entry_into_span'] = not hits and not pointers and not literals
        # Arithmetic: the 813 threshold, the B witness, the directed-rounding bound of the routing proof.
        checks['threshold_813'] = max_abs_d(813) < 2 ** 31 <= max_abs_d(814) and v['stock_radius_max'] == 813
        checks['b_witness'] = native_b(814, 1051682) == 2572 and (814 * 814 * 65536) // i32((1051682 ** 2 + 0x8000) >> 16) == 2573
        bounds = 0
        rounding_ok = True
        for r in (1, 300, 813, 814, 1640, 32766, 32767):
            N = r * r << 16
            for q in range(1, 65536):
                B, rem = divmod(N, q)
                val = N / q
                if rem:
                    rounding_ok &= math.floor(math.nextafter(val, -math.inf)) == B == math.floor(math.nextafter(val, math.inf))
                else:
                    rounding_ok &= val == B
                bounds += 1
        checks['directed_rounding_bound'] = rounding_ok and bounds == 7 * 65535
        report['directed_rounding_bounds'] = bounds
        report['decision_max'] = {'813': max_abs_d(813), '814': max_abs_d(814)}
        # Mutation guards: each named change must fail the static checks.
        guards = {}
        text = Path(sites_path).read_text() if header_text is None else header_text
        for label, rva, new in [('ret16_exit', 0x10cd88, b'\xc2\x10\x00'), ('caller_edx_from_ecx', 0x0a7b17, b'\x8b\xd1'),
                                ('caller_ecx_load', 0x0a7b10, b'\x8b\x54'), ('and_prologue_truncated', 0x10c943, b'\x83\xe4\xf0'),
                                ('class_body', 0x10ba85, b'\x90'), ('lock_body', 0x0f5b21, b'\x83\xff\x00'),
                                ('leaf24_body', 0x0d4590, b'\xcc')]:
            mutated = bytearray(im.data)
            off = im.pe.offset(rva)
            mutated[off:off + len(new)] = new
            m = Image(exe)
            m.data = bytes(mutated)
            mc, _, _, _ = static_checks(m, values, entry, windows)
            guards[label] = not all(mc.values())
        stale_values, stale_entry, stale_windows = header(text.replace('entry_rva = 0x10c940', 'entry_rva = 0x0ef620'))
        mc, _, _, _ = static_checks(im, stale_values, stale_entry, stale_windows)
        guards['stale_ap_entry'] = not all(mc.values())
        report['mutation_guards'] = guards
        checks['mutation_guards'] = all(guards.values()) and len(guards) == 8
    report['checks'] = checks
    report['result'] = 'PASS' if all(checks.values()) else 'FAIL'
    report['failed'] = [k for k, ok in checks.items() if not ok]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('exe_path', nargs='?', type=Path)
    parser.add_argument('--exe', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    report = verify(args.exe_path or args.exe or DEFAULT_EXE)
    text = json.dumps(report, indent=1, sort_keys=True)
    if args.out:
        args.out.write_text(text + '\n')
    print(text)
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
