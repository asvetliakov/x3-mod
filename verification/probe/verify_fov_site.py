#!/usr/bin/env python3
"""Read-only qualification of the field-of-view site 0x0041c9d9.

src/proxy/fov.cpp replaces the imm32 of the cockpit registry constructor's
`MOV dword [ESI+0x24],0x4000` (c7 46 24 00 40 00 00) with the requested focus
F, so every registry creation starts from the user's FOV
(docs/reverse-engineering/field-of-view.md section 5). This checks X3AP.exe on
the host: the structural identity, the 28-byte window 0x0041c9cc..0x0041c9e7,
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
REGISTRY_SLOT_VA, REGISTRY_FOCUS_OFFSET = 0x608504, 0x24
ENGINE_FOCUS, NEAR_PLANE_FOCUS = 0x4000, 0x2147
VERTICAL_MIN, VERTICAL_MAX, PLANE_HEIGHT = 36.0, 120.0, 0.75
PATCH_SAMPLE = 0x3470  # the launcher default (90 deg horizontal on 16:9)
LOG_RE = re.compile(r'\bfov site=(?P<site>[0-9a-f]{8}) status=(?P<status>patched|patched_unverified|off|refused) reason=(?P<reason>\S+) '
                    r'value=0x(?P<value>[0-9a-f]{4,8}) vertical_deg=(?P<vertical>[0-9.]+) setting=(?P<setting>[!-~]+) write=(?P<write>none|atomic|plain) '
                    r'registry=(?P<registry>written|absent|skipped) registry_before=(?P<before>0x[0-9a-f]+|-)')
RESTORE_RE = re.compile(r'\bfov_restore site=(?P<site>[0-9a-f]{8}) status=(?P<status>restored|restore_not_owned|restore_failed) '
                        r'found=(?P<found>[0-9a-f]{8}|--) registered=(?P<registered>[01])')
CONFIRM_RE = re.compile(r'\bfov_confirm frame=(?P<frame>\d+) registry=(?P<registry>[0-9a-f]{8}|absent) focus=(?P<focus>0x[0-9a-f]+|-) '
                        r'expected=0x(?P<expected>[0-9a-f]+) match=(?P<match>[01]) vertical_deg=(?P<vertical>[0-9.]+|-) camera=(?P<camera>\S+)')


def focus_for_vertical(degrees):
    """F = round(65536/pi * atan(tan(v/2) / 0.75)); 0 outside (0, 180)."""
    if not math.isfinite(degrees) or not 0 < degrees < 180:
        return 0
    return int(math.floor(65536 / math.pi * math.atan(math.tan(math.radians(degrees) / 2) / PLANE_HEIGHT) + 0.5))


def vertical_for_focus(focus):
    return math.degrees(2 * math.atan(PLANE_HEIGHT * math.tan(focus * math.pi / 65536)))


def horizontal_for_vertical(degrees, aspect):
    return math.degrees(2 * math.atan(math.tan(math.radians(degrees) / 2) * aspect))


# Conversion table (requested vertical degrees -> F); the launcher's default is the exact 90-on-16:9 value 2*atan(9/16).
CONVERSIONS = {36.0: 0x2150, 58.7155: 0x3470, 58.72: 0x3471, 59.0: 0x34aa, 73.74: 0x4000, 120.0: 0x5eb4}


def parse_log_line(line):
    """The one `fov` install line -> dict, or None."""
    match = LOG_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'site': int(row['site'], 16), 'status': row['status'], 'reason': row['reason'], 'value': int(row['value'], 16),
            'vertical_deg': float(row['vertical']), 'setting': row['setting'], 'write': row['write'], 'registry': row['registry'],
            'registry_before': None if row['before'] == '-' else int(row['before'], 16), 'patched': row['status'] == 'patched'}


def parse_restore_line(line):
    """The `fov_restore` row written by shutdown() on a dynamic unload -> dict, or None."""
    match = RESTORE_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'site': int(row['site'], 16), 'status': row['status'], 'found': None if row['found'] == '--' else bytes.fromhex(row['found']),
            'registered': row['registered'] == '1'}


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
                                           'registry_focus_offset', 'engine_focus', 'near_plane_focus', 'vertical_min', 'vertical_max', 'plane_height')} | {
        'window': array('expected_window'), 'site': array('expected_site'), 'write': array('expected_write'),
        'reader': array('expected_reader'), 'setfocus': array('expected_setfocus')}


EXPECTED_CONSTANTS = {'function_va': FUNCTION[0], 'function_end_va': FUNCTION[1], 'window_va': WINDOW_VA, 'site_va': SITE_VA, 'write_va': WRITE_VA,
                      'window_length': len(WINDOW), 'site_offset': SITE_VA - WINDOW_VA, 'site_length': len(SITE), 'write_offset': WRITE_VA - WINDOW_VA,
                      'write_length': len(WRITE), 'reader_va': READER_VA, 'setfocus_va': SETFOCUS_VA, 'registry_slot_va': REGISTRY_SLOT_VA,
                      'registry_focus_offset': REGISTRY_FOCUS_OFFSET, 'engine_focus': ENGINE_FOCUS, 'near_plane_focus': NEAR_PLANE_FOCUS,
                      'vertical_min': VERTICAL_MIN, 'vertical_max': VERTICAL_MAX, 'plane_height': PLANE_HEIGHT,
                      'window': WINDOW, 'site': SITE, 'write': WRITE, 'reader': READER, 'setfocus': SETFOCUS}


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
    table = all(focus_for_vertical(v) == f for v, f in CONVERSIONS.items())
    exact_default = focus_for_vertical(math.degrees(2 * math.atan(9 / 16))) == 0x3470
    bounds = focus_for_vertical(VERTICAL_MIN) >= NEAR_PLANE_FOCUS and focus_for_vertical(VERTICAL_MAX) < 0x8000
    near_plane = vertical_for_focus(NEAR_PLANE_FOCUS) < VERTICAL_MIN
    return table and exact_default and bounds and near_plane


def inspect(data, instructions, patched_instructions, core_text, claims, caller_sites):
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
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'exe_info': exe_identity.info(data), 'site': hex(SITE_VA),
            'site_bytes': (image.read(SITE_VA, len(SITE)) or b'').hex(), 'window_bytes': (image.read(WINDOW_VA, len(WINDOW)) or b'').hex(),
            'reader_bytes': (reader_bytes or b'').hex(), 'setfocus_bytes': (setfocus_bytes or b'').hex(),
            'constructor_callers': [hex(a) for a in sorted(caller_sites)], 'esi_writes_before_site': esi_writes,
            'incoming_window_branches': [(hex(a), hex(t)) for a, t in incoming],
            'raw_branch_hits': [(hex(s), hex(t)) for s, t in raw], 'raw_branch_hits_not_interior': [(hex(s), hex(t)) for s, t in raw_real],
            'dword_refs': dword_refs, 'other_claims': len(claims), 'overlapping_claims': overlapping,
            'conversions': {f'{v:g}': hex(focus_for_vertical(v)) for v in CONVERSIONS},
            'function_instructions': len(instructions), 'exe_sha256': hashlib.sha256(data).hexdigest()}


def verify(exe=DEFAULT_EXE, core=CORE):
    data = common.image_bytes(exe)
    try:
        instructions = decode(exe)
        patched = common.parse_objdump(common.objdump_window(patched_image(data), *FUNCTION, timeout=120), *FUNCTION)
        claims = sites.other_claims(ROOT, skip_prefix='fov', own_names=())
        return inspect(data, instructions, patched, Path(core).read_text(), claims, callers(data, FUNCTION[0]))
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
