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

usage: verify_chase_camera_site.py [--exe PATH] [--json]   (exit 0 = PASS)
"""
import argparse
import json
import struct
import sys
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

DEFAULT_EXE = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe'
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


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--json', action='store_true', help='print the full report')
    args = parser.parse_args()
    if not args.exe.is_file():
        print(json.dumps({'result': 'SKIP', 'reason': f'{args.exe} not found'}))
        return 2
    data = args.exe.read_bytes()
    report = verify(data)
    report['exe'] = str(args.exe)
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(json.dumps({'result': report['result'], 'exe': report['exe'], 'exe_info': report['exe_info'],
                          **{k: v['ok'] for k, v in report['checks'].items()}}))
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
