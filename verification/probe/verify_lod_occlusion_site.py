#!/usr/bin/env python3
"""Read-only qualification of the LOD occlusion site 0x004c34f7.

src/proxy/lod_occlusion.cpp sets the rel32 of the occlusion gate's
`jne 0x004c35c6` (0f 85 c9 00 00 00) in the material submission 0x004c0150 to
0, so the branch lands on the next instruction and every LOD record binds its
material's t_OcclusionTexture instead of the NONE_OCCL_DECAL placeholder
(docs/reverse-engineering/texture-lookup.md section 12). This checks X3AP.exe
on the host: the structural identity, the 31-byte window 0x004c34e7..0x004c3505
and its context (the handle test before it, the `jl` after it), the
instruction boundaries of the whole function, that the placeholder bind
0x004c35c6 is reached only from this jne, that the LOD-0 path (every
instruction reachable from 0x004c34fd before the join 0x004c35d6, including the
placeholder-bind tail 0x004c35c9..0x004c35d3) stays inside that range and never
reads node+0x14c, that the flags of the gate's
`cmp` are dead on both paths once the jne falls through, that no direct branch
in the function lands inside the window, that every raw rel8/rel32 encoding in
the image landing inside the window starts in the middle of a decoded
instruction of the function (none on 0x004c34f8..0x004c34fc), that no dword
anywhere in the image points into the window, the patched window decoded as
`jne 0x004c34fd` with unchanged boundaries, the atomic-word rule for the
four-byte write, no other DLL claim on the window, and that
src/proxy/lod_occlusion_sites.h carries the same constants. Also the
`lod_occlusion` log-line parsers the host test exercises. No Wine, no game launch.
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

common = sites.common
ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/lod_occlusion_sites.h'
DEFAULT_EXE = common.DEFAULT_EXE
FUNCTION = (0x4c0150, 0x4c40fc)  # ret at 0x4c40fb, int3 padding from 0x4c40fc
WINDOW_VA, SITE_VA, WRITE_VA, LOD0_VA, PLACEHOLDER_VA = 0x4c34e7, 0x4c34f7, 0x4c34f9, 0x4c34fd, 0x4c35c6
BIND_END_VA = 0x4c35d6  # the join after the placeholder bind; the LOD-0 path is what 0x4c34fd reaches before it
WINDOW = bytes.fromhex('8b4d0c 83b94c01000000 8b15746f6000 0f85c9000000 837c247400 89542418'.replace(' ', ''))
SITE, WRITE, PATCHED = bytes.fromhex('0f85c9000000'), bytes.fromhex('c9000000'), bytes.fromhex('00000000')
# Before the window: mov eax,[esp+0x28]; mov esi,[eax+0x74] (the group's t_OcclusionTexture handle); test esi,esi; je 0x4c35d6.
BEFORE_VA, BEFORE = 0x4c34d8, bytes.fromhex('8b442428 8b7074 85f6 0f84ef000000'.replace(' ', ''))
# After it: jl 0x4c35d6 (occlusion id < 0: nothing bound).
AFTER = bytes.fromhex('0f8cca000000')
WINDOW_STARTS = [0x4c34e7, 0x4c34ea, 0x4c34f1, 0x4c34f7, 0x4c34fd, 0x4c3502]
LOG_RE = re.compile(r'\blod_occlusion site=(?P<site>[0-9a-f]{8}) status=(?P<status>patched|patched_unverified|off|refused) reason=(?P<reason>\S+) '
                    r'mode=(?P<mode>record0|all|-) setting=(?P<setting>[!-~]+) write=(?P<write>none|atomic|plain)')
RESTORE_RE = re.compile(r'\blod_occlusion_restore site=(?P<site>[0-9a-f]{8}) status=(?P<status>restored|restore_not_owned|restore_failed) '
                        r'found=(?P<found>[0-9a-f]{8}|--) registered=(?P<registered>[01])')


def parse_log_line(line):
    """The one `lod_occlusion` install line -> dict, or None."""
    match = LOG_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    row['site'] = int(row['site'], 16)
    row['patched'] = row['status'] == 'patched'
    return row


def parse_restore_line(line):
    """The `lod_occlusion_restore` row written by shutdown() on a dynamic unload -> dict, or None."""
    match = RESTORE_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'site': int(row['site'], 16), 'status': row['status'], 'found': None if row['found'] == '--' else bytes.fromhex(row['found']),
            'registered': row['registered'] == '1'}


def source_constants(text):
    """VAs, lengths and the byte arrays from lod_occlusion_sites.h."""
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*[;,]', text)
        return int(match.group(1), 0) if match else None

    def array(name):
        match = re.search(rf'\b{name}\[\w+\]\s*=\s*\{{([^}}]*)\}}', text)
        return bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))) if match else b''
    return {name: value(name) for name in ('function_va', 'function_end_va', 'window_va', 'site_va', 'write_va', 'lod0_va', 'placeholder_va',
                                           'window_length', 'site_offset', 'site_length', 'write_offset', 'write_length', 'lod_offset')} | {
        'window': array('expected_window'), 'site': array('expected_site'), 'write': array('expected_write'), 'patched': array('patched_write')}


EXPECTED_CONSTANTS = {'function_va': FUNCTION[0], 'function_end_va': FUNCTION[1], 'window_va': WINDOW_VA, 'site_va': SITE_VA, 'write_va': WRITE_VA,
                      'lod0_va': LOD0_VA, 'placeholder_va': PLACEHOLDER_VA, 'window_length': len(WINDOW), 'site_offset': SITE_VA - WINDOW_VA,
                      'site_length': len(SITE), 'write_offset': WRITE_VA - WINDOW_VA, 'write_length': len(WRITE), 'lod_offset': 0x14c,
                      'window': WINDOW, 'site': SITE, 'write': WRITE, 'patched': PATCHED}


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


def patched_image(data):
    image = common.Image(data)
    out = bytearray(data)
    for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in exe_identity.section_table(data):
        start = image.image_base + virtual_address
        if start <= WRITE_VA < start + raw_size:
            offset = raw_pointer + WRITE_VA - start
            out[offset:offset + len(PATCHED)] = PATCHED
            return bytes(out)
    raise ValueError('site outside the image')


def decode(exe):
    return common.parse_objdump(common.objdump_window(exe, *FUNCTION, timeout=120), *FUNCTION)


def lod0_path(instructions):
    """Every instruction reachable from LOD0_VA before the join BIND_END_VA (calls fall through, a direct jump is followed,
    ret or an indirect jump ends a branch), and the control transfers that leave [LOD0_VA, BIND_END_VA] (should be none)."""
    by_va = {i.va: i for i in instructions}
    seen, todo, exits = {}, [LOD0_VA], []
    while todo:
        va = todo.pop()
        if va == BIND_END_VA or va in seen:
            continue
        i = by_va.get(va)
        if i is None or not LOD0_VA <= va < BIND_END_VA:
            exits.append(va)
            continue
        seen[va] = i
        target = common._is_direct_control(i)
        if i.mnemonic.startswith('ret') or (i.mnemonic == 'jmp' and target is None):
            exits.append(va)
            continue
        if i.mnemonic != 'call' and target is not None:
            todo.append(target)
        if i.mnemonic != 'jmp':
            todo.append(i.va + len(i.raw))
    return [seen[va] for va in sorted(seen)], sorted(exits)


def inspect(data, instructions, patched_instructions, core_text, claims):
    image = common.Image(data)
    by_va = {i.va: i for i in instructions}
    window_end = WINDOW_VA + len(WINDOW)
    incoming = sorted((i.va, t) for i in instructions for t in [common._is_direct_control(i)] if t is not None and WINDOW_VA < t < window_end)
    placeholder_sources = sorted(i.va for i in instructions if common._is_direct_control(i) == PLACEHOLDER_VA)
    # A raw encoding is harmless when its first byte sits strictly inside a decoded instruction of this function:
    # the byte is never fetched as an opcode. Anything else landing in the window (a real branch or an undecoded byte) fails.
    interior = {va for i in instructions for va in range(i.va + 1, i.va + len(i.raw))}
    raw = raw_branch_sources(data, WINDOW_VA + 1, window_end)
    raw_real = [(s, t) for s, t in raw if s not in interior]
    raw_into_rel32 = [(s, t) for s, t in raw if SITE_VA < t < LOD0_VA]
    dword_refs = sum(data.count(struct.pack('<I', va)) for va in range(WINDOW_VA, window_end))
    lod_path, path_exits = lod0_path(instructions)
    lod_reads = [hex(i.va) for i in lod_path if '0x14c' in i.operands]
    site, fall, gate = by_va.get(SITE_VA), by_va.get(LOD0_VA), by_va.get(0x4c34ea)
    patched_by_va = {i.va: i for i in patched_instructions}
    patched_site = patched_by_va.get(SITE_VA)
    overlapping = [(name, hex(address)) for name, address, length in claims if address < window_end and WINDOW_VA < address + length]
    checks = {
        'exe_identity': exe_identity.identity_ok(data),
        'preferred_base': image.image_base == common.IMAGE_BASE,
        'window_bytes': image.read(WINDOW_VA, len(WINDOW)) == WINDOW,
        'context_before': image.read(BEFORE_VA, len(BEFORE)) == BEFORE and [by_va[v].mnemonic if v in by_va else None for v in (0x4c34d8, 0x4c34dc, 0x4c34df, 0x4c34e1)] == ['mov', 'mov', 'test', 'je'],
        'context_after': image.read(window_end, len(AFTER)) == AFTER and window_end in by_va and by_va[window_end].mnemonic == 'jl',
        'window_whole_instructions': [i.va for i in instructions if WINDOW_VA <= i.va < window_end] == WINDOW_STARTS and window_end in by_va,
        'gate_cmp': gate is not None and gate.mnemonic == 'cmp' and '[ecx+0x14c]' in gate.operands,
        'site_whole_instruction': site is not None and site.raw == SITE and site.mnemonic == 'jne' and common._is_direct_control(site) == PLACEHOLDER_VA,
        # The gate's flags reach only the jne: mov edx between them writes none, and the fall-through `cmp` rewrites them all.
        'flags_dead_after_site': (by_va.get(0x4c34f1) is not None and by_va[0x4c34f1].mnemonic == 'mov' and fall is not None and fall.mnemonic == 'cmp'),
        'placeholder_only_from_site': placeholder_sources == [SITE_VA],
        'lod0_path_closed': lod_path != [] and path_exits == [],
        'lod0_path_no_lod_read': lod_path != [] and lod_reads == [],
        'no_interior_branch': incoming == [],
        'no_raw_branch_into_window': raw_real == [],
        'no_raw_branch_into_rel32': raw_into_rel32 == [],
        'no_dword_into_window': dword_refs == 0,
        'patched_decode': (patched_site is not None and patched_site.raw == SITE[:2] + PATCHED and patched_site.mnemonic == 'jne'
                           and common._is_direct_control(patched_site) == LOD0_VA
                           and [i.va for i in patched_instructions if WINDOW_VA <= i.va < window_end] == WINDOW_STARTS
                           and [(i.va, i.raw) for i in patched_instructions if i.va != SITE_VA] == [(i.va, i.raw) for i in instructions if i.va != SITE_VA]),
        'atomic_word': (WRITE_VA & 7) + len(WRITE) <= 8,
        'no_other_claim': overlapping == [],
        'source_constants': source_constants(core_text) == EXPECTED_CONSTANTS,
    }
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'exe_info': exe_identity.info(data), 'site': hex(SITE_VA),
            'site_bytes': (image.read(SITE_VA, len(SITE)) or b'').hex(), 'window_bytes': (image.read(WINDOW_VA, len(WINDOW)) or b'').hex(),
            'incoming_window_branches': [(hex(a), hex(t)) for a, t in incoming], 'placeholder_sources': [hex(a) for a in placeholder_sources],
            'raw_branch_hits': [(hex(s), hex(t)) for s, t in raw], 'raw_branch_hits_not_interior': [(hex(s), hex(t)) for s, t in raw_real],
            'dword_refs': dword_refs, 'lod0_path_instructions': len(lod_path), 'lod0_path_range': [hex(lod_path[0].va), hex(lod_path[-1].va)] if lod_path else None,
            'lod0_path_calls': sorted({hex(common._is_direct_control(i)) for i in lod_path if i.mnemonic == 'call'}), 'other_claims': len(claims), 'overlapping_claims': overlapping,
            'function_instructions': len(instructions), 'exe_sha256': hashlib.sha256(data).hexdigest()}


def verify(exe=DEFAULT_EXE, core=CORE):
    data = common.image_bytes(exe)
    try:
        instructions = decode(exe)
        patched = common.parse_objdump(common.objdump_window(patched_image(data), *FUNCTION, timeout=120), *FUNCTION)
        claims = sites.other_claims(ROOT, skip_prefix='lod_occlusion', own_names=())
        return inspect(data, instructions, patched, Path(core).read_text(), claims)
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
