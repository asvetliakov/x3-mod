#!/usr/bin/env python3
"""Read-only qualification of the LOD threshold read site 0x0047d44b.

src/proxy/lod_scale.cpp replaces `fmul dword [ecx+0x760]` (six bytes) with
`fmul dword [abs32]` of the same length (docs/reverse-engineering/lod-selection.md,
option 1). This checks the installed X3AP.exe on the host: exact identity, the
17-byte window 0047d440..0047d450, the instruction boundaries (the site is one
whole 6-byte instruction, the next instruction starts at 0047d451 and is the
ftol call), and that src/proxy/lod_scale_core.h carries the same constants.
Also the replacement encoder and the `lod_scale` log-line parser the host test
exercises. No Wine, no game launch.
"""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
from pathlib import Path

import verify_chase_aim_sites as common

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/lod_scale_core.h'
DEFAULT_EXE = common.DEFAULT_EXE
WINDOW_VA, SITE_VA, NEXT_VA = 0x47d440, 0x47d44b, 0x47d451
FTOL_VA = 0x52b5d0
WINDOW = bytes.fromhex('8b03 db4034 8b0d346f6000 d88960070000'.replace(' ', ''))
SITE = WINDOW[SITE_VA - WINDOW_VA:]
DECODE = (WINDOW_VA, NEXT_VA + 5)  # through the call, so the boundary after the site is decoded too
LOG_RE = re.compile(r'\blod_scale requested=(?P<requested>\S+) applied=(?P<applied>\S+) game_value=(?P<game_value>\S+) '
                    r'proxy_value=(?P<proxy_value>\S+) patched=(?P<patched>[01]) reason=(?P<reason>\S+) write=(?P<write>none|atomic|plain)')


def encode_replacement(mirror_address):
    """FMUL m32fp, ModRM /1 mod=00 r/m=101 (disp32), little-endian: the C++ encoder's contract."""
    if not 0 <= mirror_address <= 0xffffffff:
        raise ValueError('mirror address must be a 32-bit VA')
    return b'\xd8\x0d' + struct.pack('<I', mirror_address)


def parse_log_line(line):
    """The one `lod_scale` install line -> dict, or None when the line is not one."""
    match = LOG_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    try:
        requested = float(row['requested'])  # the raw setting; unparseable text stays a string
    except ValueError:
        requested = row['requested']
    return {'requested': requested, 'applied': float(row['applied']),
            'game_value': float(row['game_value']), 'proxy_value': float(row['proxy_value']),
            'patched': row['patched'] == '1', 'reason': row['reason'], 'write': row['write']}


def source_constants(text):
    """window/site VAs and the expected window bytes from lod_scale_core.h."""
    def value(name):
        match = re.search(rf'constexpr\s+[\w:]+\s+{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*;', text)
        return int(match.group(1), 0) if match else None
    array = re.search(r'expected_window\[window_length\]\s*=\s*\{([^}]*)\}', text)
    window = bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', array.group(1))) if array else b''
    return {'window_va': value('window_va'), 'site_va': value('site_va'), 'next_va': value('next_va'),
            'config_pointer_va': value('config_pointer_va'), 'config_scale_offset': value('config_scale_offset'),
            'window': window}


def decode(exe):
    tool = shutil.which(common.OBJDUMP)
    if not tool:
        raise RuntimeError(f'{common.OBJDUMP} not found')
    run = subprocess.run([tool, '-d', '-Mintel', '--insn-width=16', f'--start-address={DECODE[0]:#x}',
                          f'--stop-address={DECODE[1]:#x}', str(exe)], check=True, capture_output=True, text=True, timeout=30)
    return common.parse_objdump(run.stdout, *DECODE)


def inspect(data, instructions, core_text):
    image = common.Image(data)
    by_va = {i.va: i for i in instructions}
    site = by_va.get(SITE_VA)
    following = by_va.get(NEXT_VA)
    constants = source_constants(core_text)
    checks = {
        'exe_identity': hashlib.sha256(data).hexdigest() == common.EXPECTED_SHA256 and len(data) == common.EXPECTED_SIZE,
        'preferred_base': image.image_base == common.IMAGE_BASE,
        'window_bytes': image.read(WINDOW_VA, len(WINDOW)) == WINDOW,
        'site_whole_instruction': site is not None and site.raw == SITE and site.mnemonic == 'fmul' and '0x760' in site.operands,
        'next_instruction_boundary': following is not None and following.va == SITE_VA + 6 and following.mnemonic == 'call'
                                     and common._is_direct_control(following) == FTOL_VA,
        'window_whole_instructions': [i.va for i in instructions if i.va < NEXT_VA] == [0x47d440, 0x47d442, 0x47d445, 0x47d44b],
        'no_branch_in_window': all(common._is_direct_control(i) is None for i in instructions if i.va < NEXT_VA),
        'source_constants': constants == {'window_va': WINDOW_VA, 'site_va': SITE_VA, 'next_va': NEXT_VA,
                                          'config_pointer_va': 0x606f34, 'config_scale_offset': 0x760, 'window': WINDOW},
        'replacement_length': len(encode_replacement(0)) == len(SITE),
    }
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'site': hex(SITE_VA),
            'exe_sha256': hashlib.sha256(data).hexdigest()}


def verify(exe=DEFAULT_EXE, core=CORE):
    data = Path(exe).read_bytes()
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
