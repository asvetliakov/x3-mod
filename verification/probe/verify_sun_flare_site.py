#!/usr/bin/env python3
"""Read-only qualification of the lens-flare collector fix site 0x0047e391.

src/proxy/sun_flare_fix.cpp claims the six bytes `SHRD EAX,EDX,16; CMP ECX,EAX`
(0f ac d0 10 3b c8) of the lens collector's horizontal off-screen test in the
render visit 0x0047d9c0 through engine_patch and pushes a saturating stub in
front of the tail (docs/reverse-engineering/field-of-view.md sections 9 and 9.1).
This checks X3AP.exe on the host: the structural identity, the 56-byte window
0x0047e365..0x0047e39c (both FixMuls, the compare and the JGE), the instruction
boundaries of the whole function and of the gate 0x0047e315..0x0047e402, the
gate's branch targets, the claimed span as two whole instructions without a
relative branch, the JGE after it reading the CMP's flags (the stub's flags are
dead: SHRD and CMP rewrite them), no direct branch of the function and no raw
rel8/rel32 encoding anywhere in .text landing in the window, no dword pointing
into it, the atomic-word rule for the five patch bytes, the patched span
decoded as a five-byte jmp with the JGE untouched, the stub bytes decoded as
the documented sequence, an emulation of the gate on the section 9.1 vectors
(vanilla vs the stub), no other DLL claim on the window, and that
src/proxy/sun_flare_fix_sites.h carries the same constants. Also the
`sun_flare_fix` log-line parsers. No Wine, no game launch.
"""
import argparse
import hashlib
import json
import math
import re
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

import verify_collide_sites as sites

common = sites.common
ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/sun_flare_fix_sites.h'
DEFAULT_EXE = common.DEFAULT_EXE
FUNCTION = (0x47d9c0, 0x47e617)  # ret 0xc at 0x47e614, int3 padding from 0x47e617
GATE_VA, GATE_END_VA = 0x47e315, 0x47e402
WINDOW_VA, SITE_VA, JGE_VA, Y_TEST_VA, OFF_SCREEN_VA = 0x47e365, 0x47e391, 0x47e397, 0x47e39d, 0x47e5b6
WINDOW = bytes.fromhex('8b44241c 8b542410 f7ea 0500800000 83d200 0facd010 89442410 8b442418 8b542410 f7ea 0500800000 83d200 0facd010 3bc8 0f8d19020000'
                       .replace(' ', ''))
SITE = bytes.fromhex('0facd0103bc8')
WINDOW_STARTS = [0x47e365, 0x47e369, 0x47e36d, 0x47e36f, 0x47e374, 0x47e377, 0x47e37b, 0x47e37f, 0x47e383, 0x47e387, 0x47e389, 0x47e38e,
                 0x47e391, 0x47e395, 0x47e397]
GATE_TARGETS = [0x47e315, 0x47e354, 0x47e356, 0x47e3b9, 0x47e3bb, 0x47e5b6]
STUB_CODE = bytes.fromhex('81fa00800000 7c0a baff7f0000 b8ffffffff ff25'.replace(' ', ''))
STUB_MNEMONICS = ['cmp', 'jl', 'mov', 'mov', 'jmp']
LOG_RE = re.compile(r'\bsun_flare_fix site=(?P<site>[0-9a-f]{8}) status=(?P<status>patched|patched_unverified|off|refused) reason=(?P<reason>\S+) '
                    r'mode=(?P<mode>on|off|-) setting=(?P<setting>[!-~]+) write=(?P<write>none|atomic|plain) stub=(?P<stub>[0-9a-f]{8})')
RESTORE_RE = re.compile(r'\bsun_flare_fix_restore site=(?P<site>[0-9a-f]{8}) status=(?P<status>restored|restore_not_owned|restore_failed) '
                        r'found=(?P<found>[0-9a-f]{12}|--) registered=(?P<registered>[01])')


def parse_log_line(line):
    """The one `sun_flare_fix` install line -> dict, or None."""
    match = LOG_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    row['site'], row['stub'] = int(row['site'], 16), int(row['stub'], 16)
    row['patched'] = row['status'] == 'patched'
    return row


def parse_restore_line(line):
    """The `sun_flare_fix_restore` row written by shutdown() on a dynamic unload -> dict, or None."""
    match = RESTORE_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'site': int(row['site'], 16), 'status': row['status'], 'found': None if row['found'] == '--' else bytes.fromhex(row['found']),
            'registered': row['registered'] == '1'}


def source_constants(text):
    """VAs, lengths and the byte arrays from sun_flare_fix_sites.h."""
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*[;,]', text)
        return int(match.group(1), 0) if match else None

    def array(name):
        match = re.search(rf'\b{name}\[\w+\]\s*=\s*\{{([^}}]*)\}}', text)
        return bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))) if match else b''
    spec = re.search(r'\bclaim_spec\s*=\s*\{\s*"(\w+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*\{([^}]*)\}\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}', text)
    return {name: value(name) for name in ('gate_va', 'gate_end_va', 'window_va', 'site_va', 'jge_va', 'y_test_va', 'off_screen_va',
                                           'window_length', 'site_offset', 'site_length', 'stub_code_length', 'stub_length')} | {
        'window': array('expected_window'), 'site': array('expected_site'), 'stub': array('stub_code'),
        'claim': (spec.group(1), int(spec.group(2), 16), bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', spec.group(3))),
                  int(spec.group(4)), int(spec.group(5)), int(spec.group(6))) if spec else None}


EXPECTED_CONSTANTS = {'gate_va': GATE_VA, 'gate_end_va': GATE_END_VA, 'window_va': WINDOW_VA, 'site_va': SITE_VA, 'jge_va': JGE_VA,
                      'y_test_va': Y_TEST_VA, 'off_screen_va': OFF_SCREEN_VA, 'window_length': len(WINDOW), 'site_offset': SITE_VA - WINDOW_VA,
                      'site_length': len(SITE), 'stub_code_length': len(STUB_CODE), 'stub_length': len(STUB_CODE) + 4,
                      'window': WINDOW, 'site': SITE, 'stub': STUB_CODE, 'claim': ('lens_collector_x_bound', SITE_VA, SITE, 6, 0, 0)}


# ---- the gate's arithmetic (section 9.1 vectors): vanilla vs the saturating stub ----
W_32_9, W_16_9, H = (0xC000 * 5120) // 1440, (0xC000 * 1920) // 1080, 0xC000


def tan16(focus):
    return int(round(math.tan(focus * math.pi / 65536) * 65536))


def _s32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


def _half(v):
    """cdq; sub eax,edx; sar eax,1: signed division by 2 toward zero."""
    return int(v / 2) if v >= 0 else -((-v) // 2)


def _fixmul(a, b, fixed=False):
    """imul; add 0x8000; adc edx,0; [stub]; shrd eax,edx,16 -> EAX as signed int32."""
    product = (a * b + 0x8000) & 0xffffffffffffffff
    hi, lo = product >> 32, product & 0xffffffff
    if fixed and _s32(hi) >= 0x8000:
        hi, lo = 0x7fff, 0xffffffff
    return _s32((lo >> 16) | (hi << 16))


def gate(w, tan, x, y, z, r=1000, fixed=False):
    """0x0047e315..0x0047e402: returns 'on' (0x0047e402) or 'off_z'/'off_x'/'off_y' (0x0047e5b6), and ECX at the exit."""
    if not z > 100:
        return 'off_z', None
    if not 2 * r < z:
        return 'off_z', None
    ecx = abs(_half(x))
    bound = _fixmul(w, _fixmul(tan, _half(z)), fixed=fixed)   # only the second FixMul carries the stub
    if ecx >= bound:
        return 'off_x', ecx
    ecx = abs(_half(y))
    return ('off_y' if ecx >= _fixmul(H, _fixmul(tan, _half(z))) else 'on'), ecx


def vectors():
    """The section 9.1 fixture table: (name, W, tan16, x, y, z, vanilla exit, fixed exit)."""
    rows = [('A_run309', W_32_9, 0x471c, 0, 0, 1_500_000_000, 'off_x', 'on'),
            ('B_below_zcrit', W_32_9, 0x471c, 0, 0, 1_200_000_000, 'on', 'on'),
            ('C_vanilla_bug', W_32_9, 0x4000, 0, 0, 1_700_000_000, 'off_x', 'on'),
            ('D_off_left', W_32_9, 0x471c, -1.05, 0, 500_000_000, 'off_x', 'off_x'),
            ('E_inside_edge', W_32_9, 0x471c, 0.99, 0, 500_000_000, 'on', 'on'),
            ('E2_largest_x', W_32_9, 0x471c, 2**31 - 1, 0, 1_500_000_000, 'off_x', 'on'),
            ('F_off_top', W_32_9, 0x471c, 0, 1_500_000_000, 1_500_000_000, 'off_x', 'off_y'),
            ('G_no_overflow', W_16_9, 0x3470, 0, 0, 2_100_000_000, 'on', 'on'),
            ('H_at_bound', W_32_9, 0x3470, 0, 0, 2_147_483_000, 'on', 'on')]
    out = []
    for name, w, focus, x, y, z, vanilla, fixed in rows:
        t = tan16(focus)
        if isinstance(x, float):
            x = int(x * z * (w / 65536) * (t / 65536))
        out.append((name, w, t, x, y, z, vanilla, fixed))
    return out


def emulate():
    rows = []
    for name, w, t, x, y, z, want_vanilla, want_fixed in vectors():
        vanilla, fixed = gate(w, t, x, y, z)[0], gate(w, t, x, y, z, fixed=True)[0]
        rows.append({'case': name, 'w': w, 'tan16': t, 'x': x, 'y': y, 'z': z, 'vanilla': vanilla, 'fixed': fixed,
                     'ok': (vanilla, fixed) == (want_vanilla, want_fixed)})
    return rows


def decode_blob(code, va):
    """objdump decode of raw bytes placed at va (a headerless blob, as objdump_window does)."""
    tool = shutil.which(common.OBJDUMP)
    if not tool:
        raise RuntimeError(f'{common.OBJDUMP} not found')
    with tempfile.TemporaryDirectory(prefix='x3-sun-flare-') as directory:
        blob = Path(directory) / 'stub.code'
        blob.write_bytes(code)
        run = subprocess.run([tool, '-D', '-b', 'binary', '-m', 'i386', '-Mintel', '--insn-width=16', f'--adjust-vma={va:#x}', str(blob)],
                             check=True, capture_output=True, text=True, timeout=60)
    return common.parse_objdump(run.stdout, va, va + len(code))


def raw_branch_sources(data, lo, hi):
    """(source VA, target VA) of every rel8/rel32 branch or call encoding in .text landing in [lo, hi) (a superset of real branches)."""
    image = common.Image(data)
    hits = []
    for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in exe_identity.section_table(data):
        if name != '.text':
            continue
        text = data[raw_pointer:raw_pointer + min(virtual_size, raw_size)]
        base = image.image_base + virtual_address
        for i in range(len(text) - 6):
            op = text[i]
            if op in (0xe8, 0xe9):
                target = base + i + 5 + struct.unpack_from('<i', text, i + 1)[0]
            elif op == 0x0f and 0x80 <= text[i + 1] <= 0x8f:
                target = base + i + 6 + struct.unpack_from('<i', text, i + 2)[0]
            elif 0x70 <= op <= 0x7f or op == 0xeb or 0xe0 <= op <= 0xe3:
                target = base + i + 2 + struct.unpack_from('<b', text, i + 1)[0]
            else:
                continue
            if lo <= target < hi:
                hits.append((base + i, target))
    return hits


def patched_image(data, dispatcher=0x10000000):
    """The image with the claim's jump at 0x0047e391 (e9 rel32 to `dispatcher`; the sixth byte left)."""
    image = common.Image(data)
    out = bytearray(data)
    for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in exe_identity.section_table(data):
        start = image.image_base + virtual_address
        if start <= SITE_VA < start + raw_size:
            offset = raw_pointer + SITE_VA - start
            out[offset:offset + 5] = b'\xe9' + struct.pack('<i', dispatcher - (SITE_VA + 5))
            return bytes(out)
    raise ValueError('site outside the image')


def inspect(data, instructions, patched_span, patched_jge, stub_decoded, core_text, claims):
    image = common.Image(data)
    by_va = {i.va: i for i in instructions}
    window_end = WINDOW_VA + len(WINDOW)
    incoming = sorted((i.va, t) for i in instructions for t in [common._is_direct_control(i)] if t is not None and WINDOW_VA <= t < window_end)
    gate_targets = sorted({t for i in instructions if GATE_VA <= i.va < GATE_END_VA for t in [common._is_direct_control(i)] if t is not None})
    interior = {va for i in instructions for va in range(i.va + 1, i.va + len(i.raw))}
    raw = raw_branch_sources(data, WINDOW_VA, window_end)
    raw_real = [(s, t) for s, t in raw if s not in interior]
    dword_refs = sum(data.count(struct.pack('<I', va)) for va in range(WINDOW_VA, window_end))
    shrd, cmp_, jge = by_va.get(SITE_VA), by_va.get(0x47e395), by_va.get(JGE_VA)
    stub_slot = 0x00123458
    emulation = emulate()
    # The claimed window must be clear of every other claim; other claims elsewhere in the gate (X3M_SUBMIT_PHASES' stamp at
    # its entry 0x0047e315) are disjoint spans and are only reported.
    overlapping = [(name, hex(address)) for name, address, length in claims if address < window_end and WINDOW_VA < address + length]
    in_gate = [(name, hex(address), length) for name, address, length in claims if address < GATE_END_VA and GATE_VA < address + length]
    checks = {
        'exe_identity': exe_identity.identity_ok(data),
        'preferred_base': image.image_base == common.IMAGE_BASE,
        'window_bytes': image.read(WINDOW_VA, len(WINDOW)) == WINDOW,
        'window_whole_instructions': [i.va for i in instructions if WINDOW_VA <= i.va < window_end] == WINDOW_STARTS and window_end in by_va,
        'gate_whole_instructions': GATE_VA in by_va and GATE_END_VA in by_va,
        'gate_branch_targets': gate_targets == GATE_TARGETS,
        'site_whole_instructions': (shrd is not None and shrd.raw == SITE[:4] and shrd.mnemonic == 'shrd' and shrd.operands.replace(' ', '') == 'eax,edx,0x10'
                                    and cmp_ is not None and cmp_.raw == SITE[4:] and cmp_.mnemonic == 'cmp' and cmp_.operands.replace(' ', '') == 'ecx,eax'),
        'site_no_relative_branch': all(common._is_direct_control(i) is None for i in (shrd, cmp_) if i is not None),
        # The JGE reads SF/OF of the CMP in the tail; the stub's flags are rewritten by SHRD and CMP before any reader.
        'jge_reads_cmp_flags': jge is not None and jge.mnemonic == 'jge' and common._is_direct_control(jge) == OFF_SCREEN_VA and by_va.get(Y_TEST_VA) is not None,
        'product_before_site': [by_va[v].mnemonic if v in by_va else None for v in (0x47e37f, 0x47e383, 0x47e387, 0x47e389, 0x47e38e)] == ['mov', 'mov', 'imul', 'add', 'adc'],
        'no_interior_branch': incoming == [],
        'no_raw_branch_into_window': raw_real == [],
        'no_dword_into_window': dword_refs == 0,
        'atomic_word': (SITE_VA & 7) + 5 <= 8,
        'patched_decode': (len(patched_span) == 1 and patched_span[0].mnemonic == 'jmp' and len(patched_span[0].raw) == 5
                           and common._is_direct_control(patched_span[0]) == 0x10000000
                           and len(patched_jge) == 1 and patched_jge[0].mnemonic == 'jge' and common._is_direct_control(patched_jge[0]) == OFF_SCREEN_VA),
        'stub_decode': ([i.mnemonic for i in stub_decoded] == STUB_MNEMONICS and stub_decoded[1].operands.startswith(hex(0x1000 + 0x12))
                        and stub_decoded[0].operands.replace(' ', '') == 'edx,0x8000' and stub_decoded[2].operands.replace(' ', '') == 'edx,0x7fff'
                        and stub_decoded[3].operands.replace(' ', '') == 'eax,0xffffffff' and f'{stub_slot:#x}' in stub_decoded[4].operands),
        'emulated_vectors': all(row['ok'] for row in emulation),
        'no_other_claim': overlapping == [],
        'source_constants': source_constants(core_text) == EXPECTED_CONSTANTS,
    }
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'exe_info': exe_identity.info(data), 'site': hex(SITE_VA),
            'site_bytes': (image.read(SITE_VA, len(SITE)) or b'').hex(), 'window_bytes': (image.read(WINDOW_VA, len(WINDOW)) or b'').hex(),
            'site_qword': hex(SITE_VA & ~7), 'incoming_window_branches': [(hex(a), hex(t)) for a, t in incoming],
            'gate_branch_targets': [hex(t) for t in gate_targets], 'raw_branch_hits': [(hex(s), hex(t)) for s, t in raw],
            'raw_branch_hits_not_interior': [(hex(s), hex(t)) for s, t in raw_real], 'dword_refs': dword_refs,
            'emulation': emulation, 'other_claims': len(claims), 'overlapping_claims': overlapping, 'other_claims_in_gate': in_gate,
            'function_instructions': len(instructions), 'exe_sha256': hashlib.sha256(data).hexdigest()}


def verify(exe=DEFAULT_EXE, core=CORE):
    data = common.image_bytes(exe)
    try:
        instructions = common.parse_objdump(common.objdump_window(exe, *FUNCTION, timeout=120), *FUNCTION)
        patched = patched_image(data)
        patched_span = common.parse_objdump(common.objdump_window(patched, SITE_VA, SITE_VA + 5), SITE_VA, SITE_VA + 5)
        patched_jge = common.parse_objdump(common.objdump_window(patched, JGE_VA, Y_TEST_VA), JGE_VA, Y_TEST_VA)
        stub_decoded = decode_blob(STUB_CODE + struct.pack('<I', 0x00123458), 0x1000)
        claims = sites.other_claims(ROOT, skip_prefix='sun_flare_fix', own_names=())
        return inspect(data, instructions, patched_span, patched_jge, stub_decoded, Path(core).read_text(), claims)
    except (ValueError, OSError, subprocess.SubprocessError, RuntimeError, KeyError, IndexError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': str(error)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--core', type=Path, default=CORE)
    args = parser.parse_args()
    report = verify(args.exe, args.core)
    print(json.dumps(report, indent=2, sort_keys=True))
    raise SystemExit(report['result'] != 'PASS')
