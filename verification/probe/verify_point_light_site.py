#!/usr/bin/env python3
"""Read-only qualification of the point-light admission site 0x004c27af.

src/proxy/point_light_admission.cpp replaces the six-byte `jg 0x004c29f5` at
0x004c27af with `jmp detour; nop` (docs/reverse-engineering/camera-and-lights.md,
"Point-light admission site"). This checks the installed X3AP.exe on the host:
exact identity, the 28-byte window 0x004c27a1..0x004c27bc, that the site is one
whole 6-byte instruction whose target is 0x004c29f5, that both branch targets
start on decoded instruction boundaries with the expected byte prefixes, that
no direct branch anywhere in the containing function 0x004c0150..0x004c40fb
lands inside the window other than the known label 0x004c27b7, and that
src/proxy/point_light_admission_core.h carries the same constants. Also the
detour/site encoders and the `point_light_root_admission`, `point_light_admission_frame`
and `point_light_node` log-line parsers the host test exercises. No Wine, no game launch.
"""
import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

import verify_chase_aim_sites as common

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/point_light_admission_core.h'
DEFAULT_EXE = common.DEFAULT_EXE
FUNCTION = (0x4c0150, 0x4c40fc)
WINDOW_VA, SITE_VA, ADMIT_VA, REJECT_VA = 0x4c27a1, 0x4c27af, 0x4c27b5, 0x4c29f5
WINDOW = bytes.fromhex('2b8658010000 8b4d0c 2b4170 85c0 0f8f40020000 8bc6 8bb06c010000'.replace(' ', ''))
SITE = WINDOW[SITE_VA - WINDOW_VA:SITE_VA - WINDOW_VA + 6]
REJECT_PREFIX = bytes.fromhex('8b44245c 8b1518856000'.replace(' ', ''))
KNOWN_LABEL = 0x4c27b7          # the directional-light bypass from 0x4c2737 joins the admit path here
KNOWN_LABEL_SOURCE = 0x4c2737
DETOUR_LENGTH, DETOUR_COUNTED_OFFSET = 43, 32
FRAME_RE = re.compile(r'\bpoint_light_admission_frame device=(?P<device>\d+) frame=(?P<frame>\d+) tests=(?P<tests>\d+) fast_admit=(?P<fast_admit>\d+) '
                      r'reject=(?P<reject>\d+) walks=(?P<walks>\d+) memo_hits=(?P<memo_hits>\d+) root_admit=(?P<root_admit>\d+) root_reject=(?P<root_reject>\d+) '
                      r'chain_unreadable=(?P<chain_unreadable>\d+) chain_too_deep=(?P<chain_too_deep>\d+) chain_cycle=(?P<chain_cycle>\d+) node_is_root=(?P<node_is_root>\d+) '
                      r'root_unreadable=(?P<root_unreadable>\d+) light_unreadable=(?P<light_unreadable>\d+) reach_negative=(?P<reach_negative>\d+) samples=(?P<samples>\d+)')
NODE_RE = re.compile(r'\bpoint_light_node device=(?P<device>\d+) frame=(?P<frame>\d+) node=(?P<node>[0-9a-f]{8}) root=(?P<root>[0-9a-f]{8}) depth=(?P<depth>\d+) '
                     r'dist=(?P<dist>-?\d+) reach=(?P<reach>-?\d+) root_dist=(?P<root_dist>-?\d+) root_reach=(?P<root_reach>-?\d+) verdict=(?P<verdict>\w+) '
                     r'node_scale=(?P<node_scale>-?\d+) root_scale=(?P<root_scale>-?\d+)')
WALK_OUTCOMES = ('root_admit', 'root_reject', 'chain_unreadable', 'chain_too_deep', 'chain_cycle', 'node_is_root', 'root_unreadable', 'light_unreadable', 'reach_negative')
LOG_RE = re.compile(r'\bpoint_light_root_admission requested=(?P<requested>[01]) patched=(?P<patched>[01]) reason=(?P<reason>\S+) write=(?P<write>none|atomic|plain) '
                    r'site=0x(?P<site>[0-9a-f]{8}) detour=0x(?P<detour>[0-9a-f]{8}) handler=0x(?P<handler>[0-9a-f]{8})')


def encode_site_patch(site, detour):
    """`jmp rel32; nop`: the C++ encoder's contract."""
    for value in (site, detour):
        if not 0 <= value <= 0xffffffff:
            raise ValueError('addresses must be 32-bit VAs')
    return b'\xe9' + struct.pack('<I', (detour - (site + 5)) & 0xffffffff) + b'\x90'


def encode_detour(at, handler, admit, reject, counter):
    """JLE counted; push eax; push esi; push [ebp+0xc]; call handler; add esp,12; test eax,eax; jnz admit; jmp reject;
    counted: inc dword [counter]; jmp admit."""
    rel = lambda offset, target: struct.pack('<I', (target - (at + offset + 4)) & 0xffffffff)
    code = (b'\x0f\x8e' + rel(2, at + DETOUR_COUNTED_OFFSET) + b'\x50\x56' + b'\xff\x75\x0c' + b'\xe8' + rel(12, handler) + b'\x83\xc4\x0c' + b'\x85\xc0'
            + b'\x0f\x85' + rel(23, admit) + b'\xe9' + rel(28, reject) + b'\xff\x05' + struct.pack('<I', counter) + b'\xe9' + rel(39, admit))
    assert len(code) == DETOUR_LENGTH
    return code


def parse_frame_line(line):
    """One `point_light_admission_frame` line -> dict of ints with the sums checked, or None."""
    match = FRAME_RE.search(line)
    if not match:
        return None
    row = {k: int(v) for k, v in match.groupdict().items()}
    row['sums_ok'] = (row['tests'] == row['fast_admit'] + row['reject'] and row['reject'] == row['walks'] + row['memo_hits']
                      and row['walks'] == sum(row[k] for k in WALK_OUTCOMES))
    return row


def parse_node_line(line):
    """One `point_light_node` sample line -> dict, or None."""
    match = NODE_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {k: (int(v, 16) if k in ('node', 'root') else v if k == 'verdict' else int(v)) for k, v in row.items()}


def parse_log_line(line):
    """The one `point_light_root_admission` install line -> dict, or None."""
    match = LOG_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'requested': row['requested'] == '1', 'patched': row['patched'] == '1', 'reason': row['reason'], 'write': row['write'],
            'site': int(row['site'], 16), 'detour': int(row['detour'], 16), 'handler': int(row['handler'], 16)}


def source_constants(text):
    """VAs, lengths and the expected byte arrays from point_light_admission_core.h."""
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*[;,]', text)
        return int(match.group(1), 0) if match else None

    def array(name):
        match = re.search(rf'{name}\[\w+\]\s*=\s*\{{([^}}]*)\}}', text)
        return bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))) if match else b''
    return {name: value(name) for name in ('function_va', 'function_end_va', 'window_va', 'site_va', 'admit_va', 'reject_va',
                                           'window_length', 'site_offset', 'site_length', 'reject_prefix_length', 'site_rel32',
                                           'parent_offset', 'scale_offset', 'position_offset', 'range_offset', 'max_hops', 'detour_length', 'detour_counted_offset')} | {
        'window': array('expected_window'), 'site': array('expected_site'), 'reject_prefix': array('expected_reject_prefix')}


EXPECTED_CONSTANTS = {'function_va': FUNCTION[0], 'function_end_va': FUNCTION[1], 'window_va': WINDOW_VA, 'site_va': SITE_VA,
                      'admit_va': ADMIT_VA, 'reject_va': REJECT_VA, 'window_length': len(WINDOW), 'site_offset': SITE_VA - WINDOW_VA,
                      'site_length': 6, 'reject_prefix_length': 4, 'site_rel32': REJECT_VA - (SITE_VA + 6),
                      'parent_offset': 0x18, 'scale_offset': 0x70, 'position_offset': 0xb0, 'range_offset': 0x158, 'max_hops': 8,
                      'detour_length': DETOUR_LENGTH, 'detour_counted_offset': DETOUR_COUNTED_OFFSET, 'window': WINDOW, 'site': SITE, 'reject_prefix': REJECT_PREFIX[:4]}


def decode(exe):
    return common.parse_objdump(common.objdump_window(exe, *FUNCTION, timeout=60), *FUNCTION)


def inspect(data, instructions, core_text):
    image = common.Image(data)
    by_va = {i.va: i for i in instructions}
    site, admit, reject = by_va.get(SITE_VA), by_va.get(ADMIT_VA), by_va.get(REJECT_VA)
    window_end = WINDOW_VA + len(WINDOW)
    incoming = sorted((i.va, t) for i in instructions for t in [common._is_direct_control(i)] if t is not None and WINDOW_VA < t < window_end)
    reject_sources = sorted(i.va for i in instructions if common._is_direct_control(i) == REJECT_VA)
    constants = source_constants(core_text)
    checks = {
        'exe_identity': exe_identity.identity_ok(data),
        'preferred_base': image.image_base == common.IMAGE_BASE,
        'window_bytes': image.read(WINDOW_VA, len(WINDOW)) == WINDOW,
        'window_whole_instructions': [i.va for i in instructions if WINDOW_VA <= i.va < window_end] == [0x4c27a1, 0x4c27a7, 0x4c27aa, 0x4c27ad, 0x4c27af, 0x4c27b5, 0x4c27b7],
        'site_whole_instruction': site is not None and site.raw == SITE and site.mnemonic == 'jg' and common._is_direct_control(site) == REJECT_VA,
        'admit_target_boundary': admit is not None and admit.va == SITE_VA + 6 and admit.raw == bytes.fromhex('8bc6'),
        'reject_target_boundary': reject is not None and reject.raw == REJECT_PREFIX[:4] and reject.mnemonic == 'mov'
                                  and image.read(REJECT_VA, len(REJECT_PREFIX)) == REJECT_PREFIX,
        'no_interior_branch': incoming == [(KNOWN_LABEL_SOURCE, KNOWN_LABEL)],
        'reject_target_sources': SITE_VA in reject_sources and len(reject_sources) == 4,
        'source_constants': constants == EXPECTED_CONSTANTS,
        'encoders': len(encode_site_patch(SITE_VA, 0x10000000)) == 6 and len(encode_detour(0x10000000, 0x10001000, ADMIT_VA, REJECT_VA, 0x10002000)) == DETOUR_LENGTH,
    }
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'exe_info': exe_identity.info(data), 'site': hex(SITE_VA),
            'incoming_window_branches': [(hex(a), hex(t)) for a, t in incoming], 'reject_target_sources': [hex(a) for a in reject_sources],
            'function_instructions': len(instructions), 'exe_sha256': hashlib.sha256(data).hexdigest()}


def verify(exe=DEFAULT_EXE, core=CORE):
    data = common.image_bytes(exe)
    try:
        return inspect(data, decode(exe), Path(core).read_text())
    except (ValueError, OSError, subprocess.SubprocessError, RuntimeError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': str(error)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--core', type=Path, default=CORE)
    args = parser.parse_args()
    report = verify(args.exe, args.core)
    print(json.dumps(report, indent=2, sort_keys=True))
    raise SystemExit(report['result'] != 'PASS')
