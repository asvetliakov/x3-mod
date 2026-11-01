#!/usr/bin/env python3
"""X3FL.exe fixed-point scaler constants for src/engine/fl/scale_overflow_fix_fl.cpp.

Reads X3: Farnham's Legacy's X3FL.exe and writes src/engine/fl/scale_overflow_fix_fl_inc.h
(docs/architecture/farnhams-legacy-scale-overflow.md). The helper at RVA 0x12d10 computes, per component,
out[i] = c ? (int64)a[i] * b / c : 0 with `imul edx; idiv ecx` (T(n,d) in gate-motion.md section 2);
the 32-bit idiv traps (c0000095) when the quotient does not fit. ABI, decoded from the body:

  ECX = const int a[3], EDX = const int *b, [esp+4] = int out[3], [esp+8] = const int *c;
  returns EAX = out with a plain RET (the caller removes the two words); EBX/ESI/EDI/EBP preserved.

Checked here, all fail closed: the 16 entry bytes (whole instructions), the body's shape (three
`imul edx; idiv ecx` pairs, the [ebp+8]/[ebp+0xc] operands, the two `mov eax,esi; pop edi; pop esi;
mov esp,ebp; pop ebp; ret` exits, int3 padding after the last), no base relocation inside the body, no
relocated word pointing into it, no rel8/rel32 branch from outside it into its interior (a byte scan;
a candidate whose opcode byte is the first byte of a relocated dword is an absolute operand, not an
instruction), and the documented eleven direct callers. The fragment carries the entry bytes and an
FNV-1a 32 of the body (the DLL compares both before patching); the body bytes themselves stay untracked.

usage: fl_scale_overflow_constants.py [--exe PATH] [--output PATH] [--check] [--json] [--body PATH]
  --check  exit 1 when the fragment on disk differs from the generated text
  --json   print the decoded constants and exit (verification/analysis/test_fl_scale_overflow.py)
  --body   write the helper's body bytes to PATH (the native fixture's input; keep it untracked)
"""
import argparse
import hashlib
import importlib.util
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = Path('/mnt/c/X3FL/X3FL.exe')
DEFAULT_OUTPUT = ROOT / 'src/engine/fl/scale_overflow_fix_fl_inc.h'
_spec = importlib.util.spec_from_file_location('fl_identity_constants', ROOT / 'tools/analysis/fl_identity_constants.py')
identity = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(identity)

HELPER_RVA = 0x012d10
HELPER_LENGTH = 0xbc  # through the second RET at 0x12dcb; int3 padding follows
# push ebp; mov ebp,esp; and esp,-8; sub esp,0x10; push esi; mov esi,[ebp+0xc]; push edi; mov edi,edx
ENTRY = bytes.fromhex('558bec83e4f883ec10568b750c578bfa')
DISPLACED = 6  # push ebp; mov ebp,esp; and esp,-8: the whole instructions the 5-byte JMP covers
# (body offset, bytes) anchors of the arithmetic and the ABI
SHAPE = (
    (0x10, bytes.fromhex('894c2408')),   # mov [esp+8],ecx      a pointer kept
    (0x14, bytes.fromhex('8b36')),       # mov esi,[esi]        c = *[ebp+0xc]
    (0x1a, bytes.fromhex('8b07')),       # mov eax,[edi]        b = *edx
    (0x20, bytes.fromhex('8b01')),       # mov eax,[ecx]        a[0]
    (0x3a, bytes.fromhex('f7eaf7f9')),   # imul edx; idiv ecx   component 0
    (0x3e, bytes.fromhex('8b75088906')),  # mov esi,[ebp+8]; mov [esi],eax   out = first stack word
    (0x71, bytes.fromhex('f7eaf7f9')),   # component 1
    (0x77, bytes.fromhex('894604')),     # mov [esi+4],eax
    (0x96, bytes.fromhex('895608')),     # mov [esi+8],edx      c == 0: out[2] = 0
    (0x99, bytes.fromhex('8bc65f5e8be55dc3')),  # mov eax,esi; pop edi; pop esi; mov esp,ebp; pop ebp; ret
    (0xad, bytes.fromhex('f7eaf7f9')),   # component 2 (the idiv at RVA 0x12dbf of FL run 3)
    (0xb1, bytes.fromhex('894608')),     # mov [esi+8],eax
    (0xb4, bytes.fromhex('8bc65f5e8be55dc3')),
)
PADDING = b'\xcc' * 4
CALLERS = (0x170f6, 0x22dd4, 0x22e2a, 0x4fc00, 0x4fd07, 0x64075, 0x64f3d, 0x84805, 0x84870, 0xa4d55, 0xa4da5)


def fnv1a(data):
    h = 0x811c9dc5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xffffffff
    return h


def read(data, fields, rva, n):
    off = identity.rva_to_offset(fields, rva)
    return data[off:off + n]


def text_section(fields):
    return next(s for s in fields['sections'] if s[0].rstrip(b'\0') == b'.text')


def direct_branches(data, fields, target_rva, opcode):
    """Every rel32 E8 (call) or E9 (jmp) in .text resolving to target_rva (byte scan)."""
    _, vsize, va, raw_size, raw_ptr, _ = text_section(fields)
    out = []
    for off in range(raw_ptr, raw_ptr + min(vsize, raw_size) - 4):
        if data[off] == opcode:
            rva = off - raw_ptr + va
            if (rva + 5 + struct.unpack_from('<i', data, off + 1)[0]) & 0xffffffff == target_rva:
                out.append(rva)
    return out


def interior_branches(data, fields, start, length, relocated):
    """Byte-scan candidates outside [start, start+length) for E8/E9 rel32, 0F 8x rel32, EB/7x/E0-E3 rel8 that
    land in (start, start+length). Returns (hits, operands): candidates whose opcode byte begins a relocated
    dword are absolute operands of another instruction, not branches."""
    _, vsize, va, raw_size, raw_ptr, _ = text_section(fields)
    starts = set(relocated)
    hits, operands = [], []
    for off in range(raw_ptr, raw_ptr + min(vsize, raw_size) - 6):
        rva = off - raw_ptr + va
        if start <= rva < start + length:
            continue
        b = data[off]
        forms = []
        if b in (0xe8, 0xe9):
            forms.append((5, struct.unpack_from('<i', data, off + 1)[0]))
        if b == 0x0f and 0x80 <= data[off + 1] <= 0x8f:
            forms.append((6, struct.unpack_from('<i', data, off + 2)[0]))
        if b == 0xeb or 0x70 <= b <= 0x7f or 0xe0 <= b <= 0xe3:
            forms.append((2, struct.unpack_from('<b', data, off + 1)[0]))
        for n, disp in forms:
            if start < (rva + n + disp) & 0xffffffff < start + length:
                (operands if rva in starts else hits).append(rva)
    return hits, operands


def decode(data, fields):
    relocated = identity.relocated_rvas(data, fields)
    body = read(data, fields, HELPER_RVA, HELPER_LENGTH + len(PADDING))
    if body[:len(ENTRY)] != ENTRY:
        raise ValueError(f'helper entry is {body[:len(ENTRY)].hex()}')
    for offset, expected in SHAPE:
        if body[offset:offset + len(expected)] != expected:
            raise ValueError(f'helper +0x{offset:x} is {body[offset:offset + len(expected)].hex()}, not {expected.hex()}')
    if body[HELPER_LENGTH:] != PADDING:
        raise ValueError('no int3 padding after the helper')
    body = body[:HELPER_LENGTH]
    if [r for r in relocated if r < HELPER_RVA + HELPER_LENGTH and HELPER_RVA < r + 4]:
        raise ValueError('a base relocation touches the helper')
    pointers = [r for r in relocated
                if HELPER_RVA < struct.unpack_from('<I', data, identity.rva_to_offset(fields, r))[0] - fields['image_base']
                < HELPER_RVA + HELPER_LENGTH]
    if pointers:
        raise ValueError(f'relocated words point into the helper: {[hex(r) for r in pointers]}')
    hits, operands = interior_branches(data, fields, HELPER_RVA, HELPER_LENGTH, relocated)
    if hits:
        raise ValueError(f'branches into the helper interior: {[hex(r) for r in hits]}')
    callers = direct_branches(data, fields, HELPER_RVA, 0xe8)
    if tuple(callers) != CALLERS:
        raise ValueError(f'direct callers: {[hex(c) for c in callers]}')
    jumps = direct_branches(data, fields, HELPER_RVA, 0xe9)
    if jumps:
        raise ValueError(f'direct jumps to the helper: {[hex(c) for c in jumps]}')
    return {'helper_rva': HELPER_RVA, 'helper_length': HELPER_LENGTH, 'entry': ENTRY, 'displaced': DISPLACED,
            'fnv1a': fnv1a(body), 'body': bytes(body), 'callers': callers, 'operand_candidates': operands}


def hexes(b):
    return ', '.join(f'0x{x:02x}' for x in b)


def fragment(data, fields):
    d = decode(data, fields)
    return '\n'.join([
        '// Generated by tools/analysis/fl_scale_overflow_constants.py from X3FL.exe; do not edit.',
        f'// X3FL.exe SHA-256 {hashlib.sha256(data).hexdigest()} (provenance, never a gate).',
        '// The fixed-point scaler (RVA, added to fl::module_base()): ECX = a[3], EDX = &b, [esp+4] = out[3],',
        '// [esp+8] = &c; plain RET, EAX = out. No base relocation inside the body, no relocated word and no branch',
        '// from outside into its interior; direct callers (RVAs): ' + ', '.join(f'0x{c:x}' for c in d['callers']) + '.',
        '// clang-format off',
        f'constexpr std::uint32_t helper_rva = 0x{d["helper_rva"]:08x}, helper_length = 0x{d["helper_length"]:x};',
        '// push ebp; mov ebp,esp; and esp,-8; sub esp,0x10; push esi; mov esi,[ebp+0xc]; push edi; mov edi,edx',
        f'constexpr std::uint8_t helper_entry[{len(d["entry"])}] = {{{hexes(d["entry"])}}};',
        f'constexpr unsigned displaced_length = {d["displaced"]}; // push ebp; mov ebp,esp; and esp,-8',
        f'constexpr std::uint32_t helper_fnv1a = 0x{d["fnv1a"]:08x}; // FNV-1a 32 of the helper_length body bytes',
        '// clang-format on',
    ]) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    ap.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--body', type=Path)
    args = ap.parse_args()
    data = args.exe.read_bytes()
    fields = identity.parse(data)
    if args.body:
        args.body.write_bytes(decode(data, fields)['body'])
        return 0
    if args.json:
        d = decode(data, fields)
        d.pop('body')
        out = {k: (v.hex() if isinstance(v, bytes) else v) for k, v in d.items()}
        print(json.dumps({'sha256': hashlib.sha256(data).hexdigest(), 'image_base': fields['image_base'], **out}))
        return 0
    text = fragment(data, fields)
    if args.check:
        same = args.output.exists() and args.output.read_text() == text
        print('fl_scale_overflow_constants', 'match' if same else 'differs', args.output)
        return 0 if same else 1
    args.output.write_text(text)
    print('wrote', args.output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
