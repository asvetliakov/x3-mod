#!/usr/bin/env python3
"""Read-only qualification of the Terran-station LOD site 0x0047d01c.

src/proxy/terran_station_lod.cpp rewrites the bit-31 reader's `je 0x0047d023`
(74 05) in the cull/LOD pass 0x0047cfe0 into `jmp 0x0047d023` (eb 05), so Terran
station subtrees select their LOD by screen size instead of the fixed-distance
branch (docs/reverse-engineering/lod-selection.md, "Terran stations and bit 31
of node+0x12c", section 3). This checks X3AP.exe on the host: the structural
identity, the 17-byte window 0x0047d012..0x0047d022 and its context (the two
parent/flag tests before it, the flag-dead `mov`/`test` after it), the
instruction boundaries of the whole pass, that no direct branch in the pass
and no raw rel8/rel32 encoding or aligned dword anywhere in the image lands
inside the window, the three sources of 0x0047d023, the patched window decoded
as `test; jmp 0x0047d023; mov` with unchanged boundaries, the atomic-word rule,
no other DLL claim on the window, and that src/proxy/terran_lod_sites.h carries
the same constants. Also the `terran_station_lod` log-line parser the host test
exercises. No Wine, no game launch.
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
CORE = ROOT / 'src/proxy/terran_lod_sites.h'
DEFAULT_EXE = common.DEFAULT_EXE
FUNCTION = (0x47cfe0, 0x47d552)
WINDOW_VA, SITE_VA, TARGET_VA = 0x47d012, 0x47d01c, 0x47d023
WINDOW = bytes.fromhex('f7872c010000 00000080 7405 c644241801'.replace(' ', ''))
SITE, PATCHED = bytes.fromhex('7405'), bytes.fromhex('eb05')
# Before the window: jne 0x47d023 (propagated flag); cmp [edi+0x18],edx; jne 0x47d023 (node has a parent).
BEFORE_VA, BEFORE = 0x47d00b, bytes.fromhex('7516 395718 7511'.replace(' ', ''))
# After it: mov eax,[edi+0x12c] (no flag access); test eax,0x100000 (writes every flag the `test` at the window set).
AFTER = bytes.fromhex('8b872c010000 a900001000'.replace(' ', ''))
WINDOW_STARTS = [0x47d012, 0x47d01c, 0x47d01e]
TARGET_SOURCES = [0x47d00b, 0x47d010, 0x47d01c]
LOG_RE = re.compile(r'\bterran_station_lod site=(?P<site>[0-9a-f]{8}) status=(?P<status>patched|patched_unverified|off|refused) reason=(?P<reason>\S+) '
                    r'mode=(?P<mode>size|distance|-) setting=(?P<setting>[!-~]+) write=(?P<write>none|atomic|plain)')


def parse_log_line(line):
    """The one `terran_station_lod` install line -> dict, or None."""
    match = LOG_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    row['site'] = int(row['site'], 16)
    row['patched'] = row['status'] == 'patched'
    return row


RESTORE_RE = re.compile(r'\bterran_station_lod_restore site=(?P<site>[0-9a-f]{8}) status=(?P<status>restored|restore_not_owned|restore_failed) '
                        r'found=(?P<found>[0-9a-f]{4}|--) registered=(?P<registered>[01])')


def parse_restore_line(line):
    """The `terran_station_lod_restore` row written by shutdown() on a dynamic unload -> dict, or None."""
    match = RESTORE_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'site': int(row['site'], 16), 'status': row['status'], 'found': None if row['found'] == '--' else bytes.fromhex(row['found']),
            'registered': row['registered'] == '1'}


def source_constants(text):
    """VAs, lengths and the byte arrays from terran_lod_sites.h."""
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*[;,]', text)
        return int(match.group(1), 0) if match else None

    def array(name):
        match = re.search(rf'\b{name}\[\w+\]\s*=\s*\{{([^}}]*)\}}', text)
        return bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))) if match else b''
    return {name: value(name) for name in ('function_va', 'function_end_va', 'window_va', 'site_va', 'target_va', 'window_length',
                                           'site_offset', 'site_length', 'flags_offset')} | {
        'window': array('expected_window'), 'site': array('expected_site'), 'patched': array('patched_site')}


EXPECTED_CONSTANTS = {'function_va': FUNCTION[0], 'function_end_va': FUNCTION[1], 'window_va': WINDOW_VA, 'site_va': SITE_VA,
                      'target_va': TARGET_VA, 'window_length': len(WINDOW), 'site_offset': SITE_VA - WINDOW_VA, 'site_length': 2,
                      'flags_offset': 0x12c, 'window': WINDOW, 'site': SITE, 'patched': PATCHED}


def raw_branches_into(data, lo, hi):
    """VAs in .text whose bytes, read as a rel8/rel32 branch or call, land in [lo, hi) (a superset of real branches)."""
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
                hits.append(base + i)
    return hits


def patched_image(data):
    image = common.Image(data)
    out = bytearray(data)
    for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in exe_identity.section_table(data):
        start = image.image_base + virtual_address
        if start <= SITE_VA < start + raw_size:
            offset = raw_pointer + SITE_VA - start
            out[offset:offset + len(PATCHED)] = PATCHED
            return bytes(out)
    raise ValueError('site outside the image')


def decode(exe):
    return common.parse_objdump(common.objdump_window(exe, *FUNCTION, timeout=60), *FUNCTION)


def inspect(data, instructions, patched_instructions, core_text, claims):
    image = common.Image(data)
    by_va = {i.va: i for i in instructions}
    window_end = WINDOW_VA + len(WINDOW)
    incoming = sorted((i.va, t) for i in instructions for t in [common._is_direct_control(i)] if t is not None and WINDOW_VA <= t < window_end)
    target_sources = sorted(i.va for i in instructions if common._is_direct_control(i) == TARGET_VA)
    raw_hits = [va for va in raw_branches_into(data, WINDOW_VA + 1, window_end) if va not in TARGET_SOURCES]
    dword_refs = sum(data.count(struct.pack('<I', va)) for va in range(WINDOW_VA, window_end))
    site, after, flags_writer = by_va.get(SITE_VA), by_va.get(TARGET_VA), by_va.get(TARGET_VA + 6)
    patched_by_va = {i.va: i for i in patched_instructions}
    patched_site = patched_by_va.get(SITE_VA)
    overlapping = [(name, hex(address)) for name, address, length in claims if address < window_end and WINDOW_VA < address + length]
    checks = {
        'exe_identity': exe_identity.identity_ok(data),
        'preferred_base': image.image_base == common.IMAGE_BASE,
        'window_bytes': image.read(WINDOW_VA, len(WINDOW)) == WINDOW,
        'context_before': image.read(BEFORE_VA, len(BEFORE)) == BEFORE and [by_va[v].mnemonic if v in by_va else None for v in (0x47d00b, 0x47d00d, 0x47d010)] == ['jne', 'cmp', 'jne'],
        'context_after': image.read(TARGET_VA, len(AFTER)) == AFTER,
        'window_whole_instructions': [i.va for i in instructions if WINDOW_VA <= i.va < window_end] == WINDOW_STARTS and TARGET_VA in by_va,
        'site_whole_instruction': site is not None and site.raw == SITE and site.mnemonic == 'je' and common._is_direct_control(site) == TARGET_VA,
        'flags_dead_after_target': after is not None and after.mnemonic == 'mov' and flags_writer is not None and flags_writer.mnemonic == 'test',
        'no_interior_branch': incoming == [],
        'target_sources': target_sources == TARGET_SOURCES,
        'no_raw_branch_into_window': raw_hits == [],
        'no_dword_into_window': dword_refs == 0,
        'patched_decode': (patched_site is not None and patched_site.raw == PATCHED and patched_site.mnemonic == 'jmp'
                           and common._is_direct_control(patched_site) == TARGET_VA
                           and [i.va for i in patched_instructions if WINDOW_VA <= i.va < window_end] == WINDOW_STARTS
                           and [(i.va, i.raw) for i in patched_instructions if i.va != SITE_VA] == [(i.va, i.raw) for i in instructions if i.va != SITE_VA]),
        'atomic_word': (SITE_VA & 7) + len(SITE) <= 8,
        'no_other_claim': overlapping == [],
        'source_constants': source_constants(core_text) == EXPECTED_CONSTANTS,
    }
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'exe_info': exe_identity.info(data), 'site': hex(SITE_VA),
            'site_bytes': (image.read(SITE_VA, 2) or b'').hex(), 'window_bytes': (image.read(WINDOW_VA, len(WINDOW)) or b'').hex(),
            'incoming_window_branches': [(hex(a), hex(t)) for a, t in incoming], 'target_sources': [hex(a) for a in target_sources],
            'raw_branch_hits': [hex(a) for a in raw_hits], 'dword_refs': dword_refs, 'other_claims': len(claims), 'overlapping_claims': overlapping,
            'function_instructions': len(instructions), 'exe_sha256': hashlib.sha256(data).hexdigest()}


def verify(exe=DEFAULT_EXE, core=CORE):
    data = common.image_bytes(exe)
    try:
        instructions = decode(exe)
        patched = common.parse_objdump(common.objdump_window(patched_image(data), *FUNCTION, timeout=60), *FUNCTION)
        claims = sites.other_claims(ROOT, skip_prefix='terran_', own_names=())
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
