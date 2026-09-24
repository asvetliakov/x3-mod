#!/usr/bin/env python3
"""Read-only qualification of the field-of-view sites 0x0041c9d9 and 0x0042dbf8.

src/proxy/fov.cpp replaces the imm32 of the cockpit registry constructor's
`MOV dword [ESI+0x24],0x4000` (c7 46 24 00 40 00 00) with F'(N), the remap of
the game's N degrees to "N horizontal on 16:9", and claims INS_SetFocus's
`MOV EDX,[0x00608504]` (8b 15 04 85 60 00) at 0x0042dbf8 for the stub that
remaps the menu's incoming F (docs/reverse-engineering/field-of-view.md
sections 5 and 7.3). This checks X3AP.exe on the host. The second site: the
31-byte case window 0x0042dbed..0x0042dc0b and the callee prefix at
0x004a47f0, the whole-instruction decode of the case body (the claimed MOV one
instruction, the jmp's five bytes in one aligned qword), the dispatcher's jump
table (entry 0x21 is the case start, no entry lands inside), no raw branch
encoding and no dword into the case body, dead EFLAGS/ECX after the store
(PUSH/MOV/CALL follow; the callee writes both before reading them), no other
claim on it, and the remap table (N 50..130, rounding recovers N from the
script's truncated F). The first site: the structural identity, the 28-byte window 0x0041c9cc..0x0041c9e7,
the instruction boundaries of the whole constructor 0x0041c960..0x0041cc14,
that the site decodes as one `mov DWORD PTR [esi+0x24],0x4000`, that ESI is the
constructor's argument (`mov esi,[esp+0x38]` at 0x0041c97b) and is not written
again before the site, the constructor's direct callers, that no direct branch
in the constructor lands inside the window, that every raw rel8/rel32 encoding
in the image landing inside the window starts in the middle of a decoded
instruction of the constructor (none on the immediate), that no dword anywhere
in the image points into the window, the patched window (F = 0x3470) decoded
with unchanged boundaries and only the immediate changed, the atomic-word rule
for the four-byte write, the reader contract (`mov edx,[0x00608504]; mov
esi,[edx+0x24]` at 0x00421148, `mov [edx+0x24],ecx` at 0x0042dc04), no other
DLL claim on the window, the conversion table and bounds, and that
src/proxy/fov_sites.h carries the same constants. Also the `fov`,
`fov_restore` and `fov_confirm` log-line parsers the host test exercises. No
Wine, no game launch.
"""
import argparse
import hashlib
import json
import math
import re
import struct
import subprocess
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

import verify_collide_sites as sites
import verify_lod_occlusion_site as occlusion  # raw_branch_sources

common = sites.common
ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/fov_sites.h'
DEFAULT_EXE = common.DEFAULT_EXE
FUNCTION = (0x41c960, 0x41cc14)  # ret 4 at 0x41cc11, int3 padding from 0x41cc14
WINDOW_VA, SITE_VA, WRITE_VA = 0x41c9cc, 0x41c9d9, 0x41c9dc
WINDOW = bytes.fromhex('895e20 c6461901 885e1a 895e1c c7462400400000 897c2430 895c242c'.replace(' ', ''))
SITE, WRITE = bytes.fromhex('c7462400400000'), bytes.fromhex('00400000')
WINDOW_STARTS = [0x41c9cc, 0x41c9cf, 0x41c9d3, 0x41c9d6, 0x41c9d9, 0x41c9e0, 0x41c9e4]
ESI_LOAD_VA = 0x41c97b  # mov esi,[esp+0x38]: the registry (the constructor's stack argument)
READER_VA, READER = 0x421148, bytes.fromhex('8b1504856000 8b7224'.replace(' ', ''))
SETFOCUS_VA, SETFOCUS = 0x42dc04, bytes.fromhex('894a24')
CASE_VA, CASE = 0x42dbed, bytes.fromhex('8b4518 8b4801 a1e4856000 8b1504856000 6a00 50 8b450c 894a24 e8e46b0700'.replace(' ', ''))
CASE_STARTS = [0x42dbed, 0x42dbf0, 0x42dbf3, 0x42dbf8, 0x42dbfe, 0x42dc00, 0x42dc01, 0x42dc04, 0x42dc07, 0x42dc0c]
CASE_DECODE_END = 0x42dc11  # through the case's closing jmp 0x0042f04c
SETFOCUS_SITE_VA, SETFOCUS_SITE, SETFOCUS_SITE_OFFSET, SETFOCUS_RETURN_VA = 0x42dbf8, bytes.fromhex('8b1504856000'), 11, 0x42dbfe
CALLEE_VA, CALLEE = 0x4a47f0, bytes.fromhex('56 8bf0 57 8d7e28 66c746200100 803f08 7207 8bcf e8373a0000 8b4c240c'.replace(' ', ''))
CALLEE_MNEMONICS = ['push', 'mov', 'push', 'lea', 'mov', 'cmp', 'jb', 'mov', 'call', 'mov']
JUMP_TABLE_VA, JUMP_TABLE_ENTRIES, SETFOCUS_CASE = 0x42f064, 0x71, 0x21
REMAP_FIRST, REMAP_COUNT, REMAP_FOCUS_MIN, REMAP_FOCUS_MAX = 50, 81, 0x2334, 0x5ccc
STUB_LENGTH = 45
REGISTRY_SLOT_VA, REGISTRY_FOCUS_OFFSET = 0x608504, 0x24
ENGINE_FOCUS, NEAR_PLANE_FOCUS = 0x4000, 0x2147
SETTING_MIN, SETTING_MAX, SETTING_DEFAULT, PLANE_HEIGHT = 70.0, 100.0, 90.0, 0.75
PATCH_SAMPLE = 0x3470  # the launcher default N = 90 (90 deg horizontal on 16:9)
LOG_RE = re.compile(r'\bfov site=(?P<site>[0-9a-f]{8}) status=(?P<status>patched|patched_unverified|off|refused) reason=(?P<reason>\S+) '
                    r'value=0x(?P<value>[0-9a-f]{4,8}) vertical_deg=(?P<vertical>[0-9.]+) setting=(?P<setting>[!-~]+) write=(?P<write>none|atomic|plain) '
                    r'registry=(?P<registry>written|absent|skipped) registry_before=(?P<before>0x[0-9a-f]+|-) '
                    r'setfocus=(?P<setfocus>[a-z_]+) setfocus_write=(?P<setfocus_write>none|atomic|plain)')
RESTORE_RE = re.compile(r'\bfov_restore site=(?P<site>[0-9a-f]{8}) status=(?P<status>restored|restore_not_owned|restore_failed|none) '
                        r'found=(?P<found>[0-9a-f]{8}|--) registered=(?P<registered>[01]) setfocus=(?P<setfocus>[a-z_]+)')
CONFIRM_RE = re.compile(r'\bfov_confirm frame=(?P<frame>\d+) registry=(?P<registry>[0-9a-f]{8}|absent) focus=(?P<focus>0x[0-9a-f]+|-) '
                        r'expected=0x(?P<expected>[0-9a-f]+) match=(?P<match>[01]) vertical_deg=(?P<vertical>[0-9.]+|-) camera=(?P<camera>\S+)')


def remap_focus(focus):
    """F' with tan(F'/2) = 0.75 * tan(F/2), rounded; 0 outside (0, 0x8000)."""
    if not 0 < focus < 0x8000:
        return 0
    return int(math.floor(65536 / math.pi * math.atan(PLANE_HEIGHT * math.tan(focus * math.pi / 65536)) + 0.5))


def game_focus(degrees):
    """The script's F for N degrees: (N << 16) / 360, truncated; 0 outside (0, 180)."""
    if not math.isfinite(degrees) or not 0 < degrees < 180:
        return 0
    return int(math.floor(degrees * 65536 / 360))


def focus_for_degrees(degrees):
    return remap_focus(game_focus(degrees))


def vertical_for_focus(focus):
    return math.degrees(2 * math.atan(PLANE_HEIGHT * math.tan(focus * math.pi / 65536)))


def horizontal_for_vertical(degrees, aspect):
    return math.degrees(2 * math.atan(math.tan(math.radians(degrees) / 2) * aspect))


def remap_index(focus):
    """round(F * 360 / 65536) as the stub computes it."""
    return (focus * 360 + 0x8000) >> 16


def remap_table():
    return [remap_focus(((REMAP_FIRST + i) << 16) // 360) for i in range(REMAP_COUNT)]


def remap_lookup(focus):
    """What the stub stores for an incoming F: the table value in range, else F unchanged."""
    if not REMAP_FOCUS_MIN <= focus <= REMAP_FOCUS_MAX:
        return focus
    return remap_table()[remap_index(focus) - REMAP_FIRST]


# The game's N -> F' (field-of-view.md section 7.3 table); 90 is the launcher default.
CONVERSIONS = {70: 0x2768, 75: 0x2a8d, 80: 0x2dc5, 85: 0x3110, 90: 0x3470, 95: 0x37e4, 100: 0x3b6f}


def parse_log_line(line):
    """The one `fov` install line -> dict, or None."""
    match = LOG_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'site': int(row['site'], 16), 'status': row['status'], 'reason': row['reason'], 'value': int(row['value'], 16),
            'vertical_deg': float(row['vertical']), 'setting': row['setting'], 'write': row['write'], 'registry': row['registry'],
            'registry_before': None if row['before'] == '-' else int(row['before'], 16), 'setfocus': row['setfocus'],
            'setfocus_write': row['setfocus_write'], 'patched': row['status'] == 'patched'}


def parse_restore_line(line):
    """The `fov_restore` row written by shutdown() on a dynamic unload -> dict, or None."""
    match = RESTORE_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'site': int(row['site'], 16), 'status': row['status'], 'found': None if row['found'] == '--' else bytes.fromhex(row['found']),
            'registered': row['registered'] == '1', 'setfocus': row['setfocus']}


def parse_confirm_line(line):
    """The `fov_confirm` row of the first Present -> dict, or None."""
    match = CONFIRM_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'frame': int(row['frame']), 'registry': None if row['registry'] == 'absent' else int(row['registry'], 16),
            'focus': None if row['focus'] == '-' else int(row['focus'], 16), 'expected': int(row['expected'], 16),
            'match': row['match'] == '1', 'vertical_deg': None if row['vertical'] == '-' else float(row['vertical']), 'camera': row['camera']}


def source_constants(text):
    """VAs, lengths and the byte arrays from fov_sites.h."""
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+(?:\.\d+)?)\s*[;,]', text)
        return (float(match.group(1)) if '.' in match.group(1) else int(match.group(1), 0)) if match else None

    def array(name):
        match = re.search(rf'\b{name}\[\w+\]\s*=\s*\{{([^}}]*)\}}', text)
        return bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))) if match else b''
    return {name: value(name) for name in ('function_va', 'function_end_va', 'window_va', 'site_va', 'write_va', 'window_length', 'site_offset',
                                           'site_length', 'write_offset', 'write_length', 'reader_va', 'setfocus_va', 'registry_slot_va',
                                           'registry_focus_offset', 'engine_focus', 'near_plane_focus', 'setting_min', 'setting_max',
                                           'setting_default', 'plane_height', 'setfocus_case_va', 'setfocus_site_va', 'setfocus_return_va',
                                           'setfocus_callee_va', 'setfocus_case_length', 'setfocus_site_offset', 'setfocus_site_length',
                                           'setfocus_callee_length', 'remap_first', 'remap_count', 'stub_length')} | {
        'window': array('expected_window'), 'site': array('expected_site'), 'write': array('expected_write'),
        'reader': array('expected_reader'), 'setfocus': array('expected_setfocus'), 'case': array('expected_setfocus_case'),
        'setfocus_site': array('expected_setfocus_site'), 'callee': array('expected_setfocus_callee')}


EXPECTED_CONSTANTS = {'function_va': FUNCTION[0], 'function_end_va': FUNCTION[1], 'window_va': WINDOW_VA, 'site_va': SITE_VA, 'write_va': WRITE_VA,
                      'window_length': len(WINDOW), 'site_offset': SITE_VA - WINDOW_VA, 'site_length': len(SITE), 'write_offset': WRITE_VA - WINDOW_VA,
                      'write_length': len(WRITE), 'reader_va': READER_VA, 'setfocus_va': SETFOCUS_VA, 'registry_slot_va': REGISTRY_SLOT_VA,
                      'registry_focus_offset': REGISTRY_FOCUS_OFFSET, 'engine_focus': ENGINE_FOCUS, 'near_plane_focus': NEAR_PLANE_FOCUS,
                      'setting_min': SETTING_MIN, 'setting_max': SETTING_MAX, 'setting_default': SETTING_DEFAULT, 'plane_height': PLANE_HEIGHT,
                      'setfocus_case_va': CASE_VA, 'setfocus_site_va': SETFOCUS_SITE_VA, 'setfocus_return_va': SETFOCUS_RETURN_VA,
                      'setfocus_callee_va': CALLEE_VA, 'setfocus_case_length': len(CASE), 'setfocus_site_offset': SETFOCUS_SITE_OFFSET,
                      'setfocus_site_length': len(SETFOCUS_SITE), 'setfocus_callee_length': len(CALLEE), 'remap_first': REMAP_FIRST,
                      'remap_count': REMAP_COUNT, 'stub_length': STUB_LENGTH,
                      'window': WINDOW, 'site': SITE, 'write': WRITE, 'reader': READER, 'setfocus': SETFOCUS, 'case': CASE,
                      'setfocus_site': SETFOCUS_SITE, 'callee': CALLEE}


def patched_image(data, focus=PATCH_SAMPLE):
    image = common.Image(data)
    out = bytearray(data)
    for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in exe_identity.section_table(data):
        start = image.image_base + virtual_address
        if start <= WRITE_VA < start + raw_size:
            offset = raw_pointer + WRITE_VA - start
            out[offset:offset + 4] = struct.pack('<I', focus)
            return bytes(out)
    raise ValueError('site outside the image')


def decode(exe):
    return common.parse_objdump(common.objdump_window(exe, *FUNCTION, timeout=120), *FUNCTION)


def callers(data, target):
    """Source VAs of every `call rel32` (e8) in .text whose target is `target` (raw scan, a superset of real calls)."""
    image = common.Image(data)
    hits = []
    for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in exe_identity.section_table(data):
        if name != '.text':
            continue
        text = data[raw_pointer:raw_pointer + min(virtual_size, raw_size)]
        base = image.image_base + virtual_address
        for i in range(len(text) - 5):
            if text[i] == 0xe8 and base + i + 5 + struct.unpack_from('<i', text, i + 1)[0] == target:
                hits.append(base + i)
    return hits


def conversion_ok():
    """The N -> F' table, the 16:9 horizontal of F'(N) equal to N, the bounds, and the stub's table and rounding."""
    table = all(focus_for_degrees(n) == f for n, f in CONVERSIONS.items())
    horizontal = all(abs(horizontal_for_vertical(vertical_for_focus(remap_focus(game_focus(n))), 16 / 9) - n) < 0.01 for n in range(70, 101))
    bounds = focus_for_degrees(SETTING_MIN) >= NEAR_PLANE_FOCUS and focus_for_degrees(SETTING_MAX) < ENGINE_FOCUS
    recovers = all(remap_index((n << 16) // 360) == n for n in range(0, 181))
    limits = (remap_index(REMAP_FOCUS_MIN) == REMAP_FIRST and remap_index(REMAP_FOCUS_MIN - 1) == REMAP_FIRST - 1
              and remap_index(REMAP_FOCUS_MAX) == REMAP_FIRST + REMAP_COUNT - 1 and remap_index(REMAP_FOCUS_MAX + 1) == REMAP_FIRST + REMAP_COUNT)
    lookup = all(remap_lookup(game_focus(n)) == CONVERSIONS[n] for n in CONVERSIONS) and remap_lookup(0x2000) == 0x2000
    return table and horizontal and bounds and recovers and limits and lookup


def jump_table(data):
    image = common.Image(data)
    raw = image.read(JUMP_TABLE_VA, 4 * JUMP_TABLE_ENTRIES)
    return list(struct.unpack(f'<{JUMP_TABLE_ENTRIES}I', raw)) if raw else []


def inspect_setfocus(data, case_instructions, callee_instructions, claims):
    """The INS_SetFocus site checks (keys prefixed setfocus_) and their report fields."""
    image = common.Image(data)
    case_end = CASE_VA + len(CASE)
    by_va = {i.va: i for i in case_instructions}
    site = by_va.get(SETFOCUS_SITE_VA)
    table = jump_table(data)
    into_table = [hex(k) for k, target in enumerate(table) if CASE_VA < target < case_end]
    raw = occlusion.raw_branch_sources(data, CASE_VA + 1, case_end)
    dword_refs = sum(data.count(struct.pack('<I', va)) for va in range(CASE_VA + 1, case_end))
    after = [i.mnemonic for i in case_instructions if SETFOCUS_RETURN_VA <= i.va < case_end]
    overlapping = [(name, hex(address)) for name, address, length in claims if address < case_end and CASE_VA < address + length]
    callee_overlap = [(name, hex(address)) for name, address, length in claims if address < CALLEE_VA + len(CALLEE) and CALLEE_VA < address + length]
    checks = {
        'setfocus_case_bytes': image.read(CASE_VA, len(CASE)) == CASE,
        'setfocus_callee_bytes': image.read(CALLEE_VA, len(CALLEE)) == CALLEE,
        'setfocus_whole_instructions': [i.va for i in case_instructions] == CASE_STARTS,
        'setfocus_site_whole_instruction': (site is not None and site.raw == SETFOCUS_SITE and site.mnemonic == 'mov'
                                            and site.operands.replace(' ', '').startswith('edx,DWORDPTR') and site.operands.endswith('0x608504')),
        'setfocus_site_atomic_qword': SETFOCUS_SITE_VA % 8 == 0 and (SETFOCUS_SITE_VA + 4) // 8 == SETFOCUS_SITE_VA // 8,
        'setfocus_case_closes': (CASE_DECODE_END - 5) in by_va and by_va[CASE_DECODE_END - 5].mnemonic == 'jmp',
        'setfocus_jump_table': len(table) == JUMP_TABLE_ENTRIES and table[SETFOCUS_CASE] == CASE_VA and into_table == [],
        'setfocus_no_raw_branch_into_case': raw == [],
        'setfocus_no_dword_into_case': dword_refs == 0,
        'setfocus_flags_ecx_dead_after': (after == ['push', 'push', 'mov', 'mov', 'call']
                                          and [i.mnemonic for i in callee_instructions] == CALLEE_MNEMONICS),
        'setfocus_no_other_claim': overlapping == [] and callee_overlap == [],
    }
    report = {'setfocus_case_bytes': (image.read(CASE_VA, len(CASE)) or b'').hex(), 'setfocus_site_bytes': (image.read(SETFOCUS_SITE_VA, 6) or b'').hex(),
              'setfocus_callee_bytes': (image.read(CALLEE_VA, len(CALLEE)) or b'').hex(), 'setfocus_case_starts': [hex(i.va) for i in case_instructions],
              'setfocus_jump_table_entry': hex(table[SETFOCUS_CASE]) if len(table) > SETFOCUS_CASE else None, 'setfocus_table_entries_inside': into_table,
              'setfocus_raw_branch_hits': [(hex(s), hex(t)) for s, t in raw], 'setfocus_dword_refs': dword_refs, 'setfocus_overlapping_claims': overlapping + callee_overlap,
              'remap_table': [hex(v) for v in remap_table()]}
    return checks, report


def inspect(data, instructions, patched_instructions, core_text, claims, caller_sites, case_instructions=(), callee_instructions=()):
    image = common.Image(data)
    by_va = {i.va: i for i in instructions}
    window_end = WINDOW_VA + len(WINDOW)
    incoming = sorted((i.va, t) for i in instructions for t in [common._is_direct_control(i)] if t is not None and WINDOW_VA < t < window_end)
    interior = {va for i in instructions for va in range(i.va + 1, i.va + len(i.raw))}
    raw = occlusion.raw_branch_sources(data, WINDOW_VA + 1, window_end)
    raw_real = [(s, t) for s, t in raw if s not in interior]
    raw_into_imm = [(s, t) for s, t in raw if SITE_VA < t < window_end and t <= WRITE_VA + 3]
    dword_refs = sum(data.count(struct.pack('<I', va)) for va in range(WINDOW_VA, window_end))
    site = by_va.get(SITE_VA)
    esi_load = by_va.get(ESI_LOAD_VA)
    esi_writes = [hex(i.va) for i in instructions if ESI_LOAD_VA < i.va < SITE_VA and re.match(r'(esi|si)\b', i.operands or '')
                  and i.mnemonic not in ('cmp', 'test', 'push')]
    patched_by_va = {i.va: i for i in patched_instructions}
    patched_site = patched_by_va.get(SITE_VA)
    overlapping = [(name, hex(address)) for name, address, length in claims if address < window_end and WINDOW_VA < address + length]
    reader_bytes = image.read(READER_VA, len(READER))
    setfocus_bytes = image.read(SETFOCUS_VA, len(SETFOCUS))
    checks = {
        'exe_identity': exe_identity.identity_ok(data),
        'preferred_base': image.image_base == common.IMAGE_BASE,
        'window_bytes': image.read(WINDOW_VA, len(WINDOW)) == WINDOW,
        'window_whole_instructions': [i.va for i in instructions if WINDOW_VA <= i.va < window_end] == WINDOW_STARTS and window_end in by_va,
        'function_ends_ret4': (FUNCTION[1] - 3) in by_va and by_va[FUNCTION[1] - 3].raw == bytes.fromhex('c20400'),
        'site_whole_instruction': (site is not None and site.raw == SITE and site.mnemonic == 'mov'
                                   and site.operands.replace(' ', '') == 'DWORDPTR[esi+0x24],0x4000'),
        'esi_is_registry_argument': (esi_load is not None and esi_load.mnemonic == 'mov' and esi_load.operands.replace(' ', '') == 'esi,DWORDPTR[esp+0x38]'
                                     and esi_writes == []),
        'constructor_callers': sorted(caller_sites) == [0x403a26, 0x4050f4],  # the two registry creations (field-of-view.md section 3)
        'no_interior_branch': incoming == [],
        'no_raw_branch_into_window': raw_real == [],
        'no_raw_branch_into_imm32': raw_into_imm == [],
        'no_dword_into_window': dword_refs == 0,
        'patched_decode': (patched_site is not None and patched_site.raw == SITE[:3] + struct.pack('<I', PATCH_SAMPLE) and patched_site.mnemonic == 'mov'
                           and patched_site.operands.replace(' ', '') == f'DWORDPTR[esi+0x24],{PATCH_SAMPLE:#x}'
                           and [i.va for i in patched_instructions if WINDOW_VA <= i.va < window_end] == WINDOW_STARTS
                           and [(i.va, i.raw) for i in patched_instructions if i.va != SITE_VA] == [(i.va, i.raw) for i in instructions if i.va != SITE_VA]),
        'atomic_word': (WRITE_VA & 7) + len(WRITE) <= 8,
        'reader_contract': reader_bytes == READER and setfocus_bytes == SETFOCUS,
        'conversion': conversion_ok(),
        'no_other_claim': overlapping == [],
        'source_constants': source_constants(core_text) == EXPECTED_CONSTANTS,
    }
    setfocus_checks, setfocus_report = inspect_setfocus(data, list(case_instructions), list(callee_instructions), claims)
    checks.update(setfocus_checks)
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'exe_info': exe_identity.info(data), 'site': hex(SITE_VA),
            'setfocus_site': hex(SETFOCUS_SITE_VA), **setfocus_report,
            'site_bytes': (image.read(SITE_VA, len(SITE)) or b'').hex(), 'window_bytes': (image.read(WINDOW_VA, len(WINDOW)) or b'').hex(),
            'reader_bytes': (reader_bytes or b'').hex(), 'setfocus_bytes': (setfocus_bytes or b'').hex(),
            'constructor_callers': [hex(a) for a in sorted(caller_sites)], 'esi_writes_before_site': esi_writes,
            'incoming_window_branches': [(hex(a), hex(t)) for a, t in incoming],
            'raw_branch_hits': [(hex(s), hex(t)) for s, t in raw], 'raw_branch_hits_not_interior': [(hex(s), hex(t)) for s, t in raw_real],
            'dword_refs': dword_refs, 'other_claims': len(claims), 'overlapping_claims': overlapping,
            'conversions': {f'{v:g}': hex(focus_for_degrees(v)) for v in CONVERSIONS},
            'function_instructions': len(instructions), 'exe_sha256': hashlib.sha256(data).hexdigest()}


def verify(exe=DEFAULT_EXE, core=CORE):
    data = common.image_bytes(exe)
    try:
        instructions = decode(exe)
        patched = common.parse_objdump(common.objdump_window(patched_image(data), *FUNCTION, timeout=120), *FUNCTION)
        claims = sites.other_claims(ROOT, skip_prefix='fov', own_names=())
        case = common.parse_objdump(common.objdump_window(exe, CASE_VA, CASE_DECODE_END, timeout=60), CASE_VA, CASE_DECODE_END)
        callee_end = CALLEE_VA + len(CALLEE)
        callee = common.parse_objdump(common.objdump_window(exe, CALLEE_VA, callee_end, timeout=60), CALLEE_VA, callee_end)
        return inspect(data, instructions, patched, Path(core).read_text(), claims, callers(data, FUNCTION[0]), case, callee)
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
