#!/usr/bin/env python3
"""Read-only instruction/ABI qualification of the three native lead-marker seams."""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
from pathlib import Path

from verify_chase_aim_sites import (
    DEFAULT_EXE, EXPECTED_SHA256, EXPECTED_SIZE, IMAGE_BASE, Image, HookSpec,
    OBJDUMP, parse_objdump, inspect_site, parse_source_specs, _is_direct_control,
)

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = ROOT / 'src/proxy/chase_lead.cpp'
OVERLAY = (0x42a2d0, 0x42c1de)
COCKPIT = (0x4205e0, 0x4216dc)
HIDE = (0x426280, 0x42629e)
SITES = (
    HookSpec('chase_lead_gate', 0x42a6fe, bytes.fromhex('0f84b8030000'), *OVERLAY),
    HookSpec('chase_lead_publish', 0x42aaae, bytes.fromhex('89b340030000'), *OVERLAY),
    HookSpec('chase_lead_final_fov', 0x4213dd, bytes.fromhex('39b330020000'), *COCKPIT),
)
HIDE_BYTES = bytes.fromhex('51833e0074108b4e0485c974098b411c50e8fa3b0600c7060000000059c3')
# Independent contextual witnesses: sole view rejection, successful native
# publication, and all-camera-FOV convergence before notification callbacks.
WITNESSES = {
    'view_bit_test': (0x42a6f7, 'f6865001000001'),
    'tracked_target_gate': (0x42a704, '83bee0010000000f84ab030000'),
    'publish_y_store': (0x42aab4, '89bb44030000'),
    'publish_skip_hide': (0x42aaba, 'eb17'),
    'hide_path': (0x42aabc, '8d733ce8bc b7ffff83c8ff'.replace(' ', '')),
    'sector_fov_write': (0x4213ad, '89b098020000'),
    'dust_fov_write': (0x4213d7, '89b098020000'),
    'cached_fov_write': (0x4213e5, '89b330020000'),
    'notification_call': (0x4213ed, 'e8ce1b0000'),
}


def disassemble_functions(exe, objdump=OBJDUMP):
    """Use the shared strict parser with this probe's own complete function set."""
    tool = shutil.which(objdump)
    if not tool:
        raise RuntimeError(f'{objdump} not found')
    decoded = {}
    for start, end in (COCKPIT, OVERLAY, HIDE):
        try:
            run = subprocess.run([tool, '-d', '-Mintel', '--insn-width=16',
                                  f'--start-address={start:#x}', f'--stop-address={end:#x}',
                                  str(exe)], check=True, capture_output=True, text=True, timeout=30)
        except (subprocess.SubprocessError, OSError) as error:
            raise RuntimeError(f'objdump failed: {error}') from error
        decoded[start, end] = parse_objdump(run.stdout, start, end)
    return decoded


def source_checks(source):
    expected = [dict(name=s.name, va=s.va, bytes=s.expected, length=6,
                     rel32_offset=0, rel32_target=2 if i == 0 else 0)
                for i, s in enumerate(SITES)]
    # Historical shared-parser key names actually mean ret_pop, rel32_offset.
    match = re.search(r'\bhide_bytes\s*\[\s*\]\s*=\s*\{([^}]*)\}', source)
    hide = None
    if match:
        try:
            hide = bytes(int(x.strip(), 0) for x in match.group(1).split(',') if x.strip())
        except ValueError:
            pass
    return {'source_specs': parse_source_specs(source) == expected,
            'source_hide_bytes': hide == HIDE_BYTES}


def inspect(image, decoded, source):
    checks = {'preferred_base': image.image_base == IMAGE_BASE, **source_checks(source)}
    rows = []
    for i, spec in enumerate(SITES):
        instructions = decoded.get((spec.function_start, spec.function_end), [])
        row = inspect_site(image, spec, instructions)
        if i == 0:
            span = [x for x in instructions if spec.va <= x.va < spec.end]
            row['relocated_jz'] = (len(span) == 1 and span[0].mnemonic in ('je', 'jz')
                                   and _is_direct_control(span[0]) == 0x42aabc)
            row['ok'] = all(row[k] for k in
                            ('bytes_ok', 'whole_instructions', 'no_interior_branch', 'relocated_jz'))
        rows.append(row)
    checks['sites'] = all(x['ok'] for x in rows)
    checks['context'] = all(image.read(va, len(bytes.fromhex(raw))) == bytes.fromhex(raw)
                            for va, raw in WITNESSES.values())
    hide = decoded.get(HIDE, [])
    by_va = {x.va: x for x in hide}
    checks['hide_full_bytes'] = image.read(HIDE[0], len(HIDE_BYTES)) == HIDE_BYTES
    checks['hide_whole_function'] = bool(hide) and hide[0].va == HIDE[0] and hide[-1].end == HIDE[1]
    checks['hide_call_target'] = (0x426291 in by_va and by_va[0x426291].mnemonic == 'call'
                                  and _is_direct_control(by_va[0x426291]) == 0x489e90)
    checks['hide_abi'] = (len(hide) == 12 and hide[0].raw == b'\x51'
                           and hide[-2].raw == b'\x59' and hide[-1].raw == b'\xc3'
                           and by_va.get(0x426296) is not None
                           and by_va[0x426296].raw == bytes.fromhex('c70600000000'))
    checks['hide_branches'] = all(_is_direct_control(x) == 0x426296
                                   for x in hide if x.mnemonic.startswith('j')) and sum(
                                       x.mnemonic.startswith('j') for x in hide) == 2
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'sites': rows}


def verify_path(exe=DEFAULT_EXE, source=DEFAULT_SOURCE, objdump=OBJDUMP):
    try:
        data = Path(exe).read_bytes()
        image = Image(data)
        report = inspect(image, disassemble_functions(exe, objdump), Path(source).read_text())
        report['checks']['image_size'] = len(data) == EXPECTED_SIZE
        report['checks']['image_hash'] = hashlib.sha256(data).hexdigest() == EXPECTED_SHA256
        report['result'] = 'PASS' if all(report['checks'].values()) else 'FAIL'
        return report
    except (OSError, RuntimeError, ValueError, IndexError, struct.error) as error:
        return {'result': 'FAIL', 'error': str(error)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--source', type=Path, default=DEFAULT_SOURCE)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    report = verify_path(args.exe, args.source)
    print(json.dumps(report if args.json else {k: v for k, v in report.items() if k != 'sites'}, indent=2))
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
