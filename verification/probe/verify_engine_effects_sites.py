#!/usr/bin/env python3
"""Read-only qualification of the engine-effects call sites 0x004147eb (A) and 0x0041482c (B).

src/proxy/engine_effects_patch.cpp redirects the two calls of the per-ship engine effect routine 0x00414590
(`call 0x004148a0`, the effect instance; `call 0x00412d70`, the Particles3 trail generator) to stubs that skip
them for ships (docs/reverse-engineering/engine-effects.md section 7). This checks X3AP.exe on the host: the
structural identity; the 47-byte window A 0x004147c4..0x004147f2 and the 50-byte window B 0x004147ff..0x00414830,
the trail test between them, the guard before A and the `jmp 0x0041487c` after B; the routine 0x00414590..0x00414899
decoded without gaps, both windows whole instructions, both sites whole `call` instructions to their callees, A
followed by `add esp,0x28`; the obj slot (`mov esi,[ebp+0x8]`, the 16-bit class load `movzx edx,WORD PTR
[esi+0x48]` and the class 7/10 dispatch); callee A 0x004148a0..0x00414a62 returning only with plain `ret` (cdecl)
and callee B 0x00412d70..0x00412f79 only with `ret 0x10` (stdcall); the status flags dead at both callee entries
(only push/mov/lea before the first flag writer); no direct branch of the decoded routine and callees landing
strictly inside either window; every raw rel8/rel32 encoding in .text landing strictly inside a window starts
mid-instruction of the decoded range; no image dword pointing into either window; the patched decode (both rel32s
redirected) with unchanged boundaries; the atomic-word rule (A inside one aligned qword, B crossing one); no other
DLL claim on either window; and that src/proxy/engine_effects_sites.h carries the same constants. Also the
`engine_effects_patch` log-row parser the runner and the host test use. No Wine, no game launch.
"""
import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

import verify_collide_sites as sites
import verify_lod_occlusion_site as lod

common = sites.common
ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/engine_effects_sites.h'
DEFAULT_EXE = common.DEFAULT_EXE
ROUTINE = (0x414590, 0x41489a)          # ret at 0x414899
CALLEE_A = (0x4148a0, 0x414a63)         # ret at 0x414a62
CALLEE_B = (0x412d70, 0x412f7a)         # ret 0x10 at 0x412e5e and 0x412f77
WINDOW_A_VA, SITE_A_VA, TARGET_A_VA = 0x4147c4, 0x4147eb, 0x4148a0
WINDOW_B_VA, SITE_B_VA, TARGET_B_VA = 0x4147ff, 0x41482c, 0x412d70
JOIN_A_VA, JOIN_B_VA = 0x4147f3, 0x41487c
WINDOW_A = bytes.fromhex('f78360020000004000007523 8b5508 6a00 6a00 8d4c2428 51 6a00 6a00 6a00 52 50 8b442430 50 6a00 e8b0000000 83c428'
                         .replace(' ', ''))
WINDOW_B = bytes.fromhex('f78360020000002000007571 8b0d346f6000 f781fc00000000000040 745f 8b4d08 8d542420 52 8b542414 51 50 52 e83fe5ffff'
                         .replace(' ', ''))
GAP = bytes.fromhex('8b442418 85c0 0f8e7d000000'.replace(' ', ''))   # 0x004147f3: trail > 0, else jle 0x0041487c
BEFORE_A_VA, BEFORE_A = 0x4147bc, bytes.fromhex('8b442414 85c0 7e2f'.replace(' ', ''))  # eff > 0, else jle 0x004147f3
AFTER_B = bytes.fromhex('eb49')                                              # 0x00414831 jmp 0x0041487c
WINDOW_A_STARTS = [0x4147c4, 0x4147ce, 0x4147d0, 0x4147d3, 0x4147d5, 0x4147d7, 0x4147db, 0x4147dc, 0x4147de, 0x4147e0, 0x4147e2,
                   0x4147e3, 0x4147e4, 0x4147e8, 0x4147e9, 0x4147eb, 0x4147f0]
WINDOW_B_STARTS = [0x4147ff, 0x414809, 0x41480b, 0x414811, 0x41481b, 0x41481d, 0x414820, 0x414824, 0x414825, 0x414829, 0x41482a,
                   0x41482b, 0x41482c]
FLAG_WRITERS = ('and', 'test', 'cmp', 'sub', 'add', 'xor', 'or')
LOG_RE = re.compile(r'\bengine_effects_patch site=(?P<site>[AB]) va=(?P<va>[0-9a-f]{8}) state=(?P<state>\S+) reason=(?P<reason>\S+) '
                    r'mode=(?P<mode>native|off|plumes|-) setting=(?P<setting>[!-~]+) write=(?P<write>none|atomic|plain)\s*$')


def parse_log_line(line):
    """One `engine_effects_patch` row -> dict, or None."""
    match = LOG_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    row['va'] = int(row['va'], 16)
    row['active'] = row['state'] == 'active'
    return row


def source_constants(text):
    """VAs, lengths and the two window arrays from engine_effects_sites.h."""
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*[;,]', text)
        return int(match.group(1), 0) if match else None

    def array(name):
        match = re.search(rf'\b{name}\[\w+\]\s*=\s*\{{([^}}]*)\}}', text)
        return bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))) if match else b''
    names = ('routine_va', 'routine_end_va', 'window_a_va', 'a_site_va', 'target_a_va', 'return_a_va', 'join_a_va', 'window_b_va',
             'b_site_va', 'target_b_va', 'return_b_va', 'join_b_va', 'd3d_flags_slot_va', 'window_a_length', 'window_b_length',
             'site_a_offset', 'site_b_offset', 'obj_arg_a', 'obj_arg_b', 'caller_pop_a', 'callee_pop_b', 'class_offset', 'ship_class')
    return {name: value(name) for name in names} | {'window_a': array('expected_window_a'), 'window_b': array('expected_window_b')}


EXPECTED_CONSTANTS = {'routine_va': ROUTINE[0], 'routine_end_va': ROUTINE[1], 'window_a_va': WINDOW_A_VA, 'a_site_va': SITE_A_VA,
                      'target_a_va': TARGET_A_VA, 'return_a_va': SITE_A_VA + 5, 'join_a_va': JOIN_A_VA, 'window_b_va': WINDOW_B_VA,
                      'b_site_va': SITE_B_VA, 'target_b_va': TARGET_B_VA, 'return_b_va': SITE_B_VA + 5, 'join_b_va': JOIN_B_VA,
                      'd3d_flags_slot_va': 0x606f34, 'window_a_length': len(WINDOW_A), 'window_b_length': len(WINDOW_B),
                      'site_a_offset': SITE_A_VA - WINDOW_A_VA, 'site_b_offset': SITE_B_VA - WINDOW_B_VA, 'obj_arg_a': 0x10,
                      'obj_arg_b': 0x0c, 'caller_pop_a': 0x28, 'callee_pop_b': 0x10, 'class_offset': 0x48, 'ship_class': 7,
                      'window_a': WINDOW_A, 'window_b': WINDOW_B}


def patched_image(data, stub_a=0x10000000, stub_b=0x10000020):
    """The image with both rel32s redirected as claim_call writes them (to stand-in stub addresses)."""
    image = common.Image(data)
    out = bytearray(data)
    for site, stub in ((SITE_A_VA, stub_a), (SITE_B_VA, stub_b)):
        for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in exe_identity.section_table(data):
            start = image.image_base + virtual_address
            if start <= site < start + raw_size:
                offset = raw_pointer + site - start
                out[offset + 1:offset + 5] = struct.pack('<I', (stub - (site + 5)) & 0xffffffff)
                break
        else:
            raise ValueError('site outside the image')
    return bytes(out)


def decode(source, start, end):
    return common.parse_objdump(common.objdump_window(source, start, end, timeout=120), start, end)


def flags_dead_at_entry(instructions):
    """True when only push/mov/lea run before the callee's first status-flag writer."""
    for i in instructions:
        if i.mnemonic in FLAG_WRITERS:
            return True
        if i.mnemonic not in ('push', 'mov', 'lea'):
            return False
    return False


def inspect(data, routine, callee_a, callee_b, patched_routine, core_text, claims):
    image = common.Image(data)
    by_va = {i.va: i for i in routine}
    a_end, b_end = WINDOW_A_VA + len(WINDOW_A), WINDOW_B_VA + len(WINDOW_B)
    windows = ((WINDOW_A_VA, a_end), (WINDOW_B_VA, b_end))
    decoded = routine + callee_a + callee_b
    incoming = sorted((i.va, t) for i in decoded for t in [common._is_direct_control(i)] if t is not None
                      and any(lo < t < hi for lo, hi in windows))
    interior = {va for i in decoded for va in range(i.va + 1, i.va + len(i.raw))}
    raw = lod.raw_branch_sources(data, WINDOW_A_VA + 1, a_end) + lod.raw_branch_sources(data, WINDOW_B_VA + 1, b_end)
    raw = [(s, t) for s, t in raw if any(lo < t < hi for lo, hi in windows)]
    raw_real = [(s, t) for s, t in raw if s not in interior]
    dword_refs = sorted({va for lo, hi in windows for va in range(lo, hi) if data.count(struct.pack('<I', va))})
    site_a, site_b = by_va.get(SITE_A_VA), by_va.get(SITE_B_VA)
    rets_a = [(i.va, i.operands) for i in callee_a if i.mnemonic.startswith('ret')]
    rets_b = [(i.va, i.operands) for i in callee_b if i.mnemonic.startswith('ret')]
    patched_by_va = {i.va: i for i in patched_routine}
    overlapping = [(name, hex(address)) for name, address, length in claims for lo, hi in windows if address < hi and lo < address + length]
    checks = {
        'exe_identity': exe_identity.identity_ok(data),
        'preferred_base': image.image_base == common.IMAGE_BASE,
        'window_a_bytes': image.read(WINDOW_A_VA, len(WINDOW_A)) == WINDOW_A,
        'window_b_bytes': image.read(WINDOW_B_VA, len(WINDOW_B)) == WINDOW_B,
        'context': (image.read(BEFORE_A_VA, len(BEFORE_A)) == BEFORE_A and image.read(JOIN_A_VA, len(GAP)) == GAP
                    and image.read(b_end, len(AFTER_B)) == AFTER_B and JOIN_A_VA + len(GAP) == WINDOW_B_VA),
        'windows_whole_instructions': ([i.va for i in routine if WINDOW_A_VA <= i.va < a_end] == WINDOW_A_STARTS and a_end in by_va
                                       and [i.va for i in routine if WINDOW_B_VA <= i.va < b_end] == WINDOW_B_STARTS and b_end in by_va),
        'site_a_call': site_a is not None and site_a.mnemonic == 'call' and len(site_a.raw) == 5 and common._is_direct_control(site_a) == TARGET_A_VA,
        'site_b_call': site_b is not None and site_b.mnemonic == 'call' and len(site_b.raw) == 5 and common._is_direct_control(site_b) == TARGET_B_VA,
        'caller_pops_a': by_va.get(SITE_A_VA + 5) is not None and by_va[SITE_A_VA + 5].raw == bytes.fromhex('83c428'),
        'obj_slot_and_class_dispatch': (by_va.get(0x4145a9) is not None and by_va[0x4145a9].raw == bytes.fromhex('8b7508')
                                        and by_va.get(0x41460c) is not None and by_va[0x41460c].raw == bytes.fromhex('0fb75648')
                                        and by_va.get(0x414617) is not None and by_va[0x414617].raw == bytes.fromhex('83f807')
                                        and by_va.get(0x41461c) is not None and by_va[0x41461c].raw == bytes.fromhex('83f80a')),
        'callee_a_cdecl': rets_a == [(0x414a62, '')],
        'callee_b_stdcall_0x10': [va for va, _ in rets_b] == [0x412e5e, 0x412f77] and all(o == '0x10' for _, o in rets_b),
        'callee_flags_dead_at_entry': flags_dead_at_entry(callee_a) and flags_dead_at_entry(callee_b),
        'no_interior_branch': incoming == [],
        'no_raw_branch_into_windows': raw_real == [],
        'no_dword_into_windows': dword_refs == [],
        'patched_decode': ([i.va for i in patched_routine] == [i.va for i in routine]
                           and [(i.va, i.raw) for i in patched_routine if i.va not in (SITE_A_VA, SITE_B_VA)]
                           == [(i.va, i.raw) for i in routine if i.va not in (SITE_A_VA, SITE_B_VA)]
                           and patched_by_va[SITE_A_VA].mnemonic == 'call' and patched_by_va[SITE_B_VA].mnemonic == 'call'
                           and common._is_direct_control(patched_by_va[SITE_A_VA]) == 0x10000000
                           and common._is_direct_control(patched_by_va[SITE_B_VA]) == 0x10000020),
        'atomic_word_a_plain_b': (SITE_A_VA & 7) + 5 <= 8 and (SITE_B_VA & 7) + 5 > 8,
        'no_other_claim': overlapping == [],
        'source_constants': source_constants(core_text) == EXPECTED_CONSTANTS,
    }
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'exe_info': exe_identity.info(data),
            'sites': [hex(SITE_A_VA), hex(SITE_B_VA)], 'window_a_bytes': (image.read(WINDOW_A_VA, len(WINDOW_A)) or b'').hex(),
            'window_b_bytes': (image.read(WINDOW_B_VA, len(WINDOW_B)) or b'').hex(),
            'incoming_window_branches': [(hex(a), hex(t)) for a, t in incoming], 'raw_branch_hits': [(hex(s), hex(t)) for s, t in raw],
            'raw_branch_hits_not_interior': [(hex(s), hex(t)) for s, t in raw_real], 'dword_refs': [hex(v) for v in dword_refs],
            'callee_returns': {'a': [(hex(v), o) for v, o in rets_a], 'b': [(hex(v), o) for v, o in rets_b]},
            'other_claims': len(claims), 'overlapping_claims': overlapping,
            'instructions': {'routine': len(routine), 'callee_a': len(callee_a), 'callee_b': len(callee_b)},
            'exe_sha256': hashlib.sha256(data).hexdigest()}


def verify(exe=DEFAULT_EXE, core=CORE):
    """exe is a path or the image bytes."""
    data = common.image_bytes(exe)
    try:
        routine = decode(exe, *ROUTINE)
        callee_a = decode(exe, *CALLEE_A)
        callee_b = decode(exe, *CALLEE_B)
        patched = decode(patched_image(data), *ROUTINE)
        claims = sites.other_claims(ROOT, skip_prefix='engine_effects', own_names=())
        return inspect(data, routine, callee_a, callee_b, patched, Path(core).read_text(), claims)
    except (ValueError, OSError, subprocess.SubprocessError, RuntimeError, KeyError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': str(error)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--core', type=Path, default=CORE)
    args = parser.parse_args()
    report = verify(args.exe, args.core)
    print(json.dumps(report, indent=2, sort_keys=True))
    raise SystemExit(report['result'] != 'PASS')
