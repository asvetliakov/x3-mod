#!/usr/bin/env python3
"""Read-only qualification of lead-marker and optional central-HUD seams."""
import argparse
import json
import re
import shutil
import struct
import subprocess
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

from verify_chase_aim_sites import (
    DEFAULT_EXE, EXPECTED_SHA256, EXPECTED_SIZE, IMAGE_BASE, Image, HookSpec,
    OBJDUMP, parse_objdump, inspect_site, parse_source_specs, _is_direct_control,
)

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = ROOT / 'src/proxy/chase_lead.cpp'
OVERLAY = (0x42a2d0, 0x42c1de)
COCKPIT = (0x4205e0, 0x4216dc)
HIDE = (0x426280, 0x42629e)
DISTANCE_CALLER = (0x422fc0, 0x4230ef)
SITES = (
    HookSpec('chase_lead_gate', 0x42a6fe, bytes.fromhex('0f84b8030000'), *OVERLAY),
    HookSpec('chase_lead_publish', 0x42aaae, bytes.fromhex('89b340030000'), *OVERLAY),
    HookSpec('chase_lead_final_fov', 0x4213dd, bytes.fromhex('39b330020000'), *COCKPIT),
)
HUD_SITE = HookSpec('chase_central_hud_gate', 0x42aae0,
                    bytes.fromhex('0f849c030000'), *OVERLAY)
TIMING_SITES = (
    HookSpec('chase_native_solver_begin', 0x42a792, bytes.fromhex('e819ca0100'), *OVERLAY),
    HookSpec('chase_native_solver_end', 0x42a797, bytes.fromhex('85c00f841d030000'), *OVERLAY),
    HookSpec('chase_native_distance_begin', 0x423007, bytes.fromhex('e8f41d0000'), *DISTANCE_CALLER),
    HookSpec('chase_native_distance_end', 0x42300c, bytes.fromhex('8b460485c0'), *DISTANCE_CALLER),
    HookSpec('chase_native_central_end', 0x42aed6, bytes.fromhex('8b8324030000'), *OVERLAY),
)
TIMING_RELOC_OFFSETS = (1, 4, 1, 0, 0)
ALL_SITES = (*SITES, HUD_SITE, *TIMING_SITES)
# Target, relative-control instruction address, and decoded operation. The
# solver end includes TEST before its JZ; the other relative sites are single
# instructions. Exact bytes and source displacement fields are checked too.
RELATIVE_SITES = {
    0x42a6fe: (0x42aabc, 0x42a6fe, ('je', 'jz')),
    0x42aae0: (0x42ae82, 0x42aae0, ('je', 'jz')),
    0x42a792: (0x4471b0, 0x42a792, ('call',)),
    0x42a797: (0x42aabc, 0x42a799, ('je', 'jz')),
    0x423007: (0x424e00, 0x423007, ('call',)),
}
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
HUD_WITNESSES = {
    # EAX (not the lead gate's ESI) receives the overlay's cockpit backpointer.
    'owner_view': (0x42aad3, '8b8324030000f6805001000001'),
    # Original cleanup order is +118, +140, +12c, each through native hide.
    'extra_cleanup': (0x42aeb5, '8db318010000e8c0b3ffff8db340010000e8b5b3ffff8db32c010000e8aab3ffff'),
    # Signed metadata count, 0x3c stride, metadata short +0xc.
    'texture_metadata': (0x4f5116, '663b0dac8d60007d170fbfc18bd0c1e2042bd0a1b08d600066837c900c00743e'),
    'texture_counts': (0x4f5136, '0fbf15b06960000fbfc10fbf0db469600003ca3bc17d27'),
    # 0x10 stride, pointer +8, native lazy-load path and table reload.
    'texture_cache': (0x4f514d, '8b0dac6960008bf0c1e604837c0e08007509e8fcefffff85c0740c8b15ac6960008b4416085ec333c05ec3'),
    'view_and_2d_guards': (0x422fc1, '8b461085c00f842101000083be34020000000f8414010000'),
    'display_body_call': (0x422ff5, '83bea0020000000f84eb0000008b56105256e8f41d0000'),
    'scene_texture_guard': (0x424e11, '8b460485c0570f84d9050000f64018010f84cf050000b90f000000e8df020d0085c07509'),
}
HUD_SOURCE_RE = re.compile(r'\bhud_spec\s*=\s*(\{[^;]+\})\s*;')
TIMING_SOURCE_RE = re.compile(r'\btiming_specs\s*\[\s*(?:5)?\s*\]\s*=\s*(\{[^;]+\})\s*;')


def disassemble_functions(exe, objdump=OBJDUMP):
    """Use the shared strict parser with this probe's own complete function set."""
    tool = shutil.which(objdump)
    if not tool:
        raise RuntimeError(f'{objdump} not found')
    decoded = {}
    for start, end in (COCKPIT, OVERLAY, HIDE, DISTANCE_CALLER):
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
    hud = HUD_SOURCE_RE.search(source)
    hud_expected = [dict(name=HUD_SITE.name, va=HUD_SITE.va, bytes=HUD_SITE.expected,
                         length=6, rel32_offset=0, rel32_target=2)]
    timing = TIMING_SOURCE_RE.search(source)
    timing_expected = [dict(name=s.name, va=s.va, bytes=s.expected, length=len(s.expected),
                            rel32_offset=0, rel32_target=offset)
                       for s, offset in zip(TIMING_SITES, TIMING_RELOC_OFFSETS)]
    original = TIMING_SOURCE_RE.sub('', HUD_SOURCE_RE.sub('', source))
    return {'source_specs': parse_source_specs(original) == expected,
            'source_hud_spec': bool(hud) and parse_source_specs(hud.group(1)) == hud_expected,
            'source_timing_specs': bool(timing) and parse_source_specs(timing.group(1)) == timing_expected,
            'source_hide_bytes': hide == HIDE_BYTES}


def all_paths_reach(instructions, start, endpoint):
    """Conservative normal-return CFG proof, treating native calls as returning.

    Every direct conditional edge is considered feasible. Reject early return,
    indirect transfer, missing decode, escape or cycle before the endpoint.
    Exceptions/nonreturning callbacks are explicitly outside this static proof.
    """
    by_va = {x.va: x for x in instructions}
    visiting, proved = set(), set()

    def visit(va):
        if va == endpoint:
            return va in by_va
        if va in proved:
            return True
        if va in visiting or va not in by_va:
            return False
        ins = by_va[va]
        if ins.mnemonic.startswith('ret'):
            return False
        target = _is_direct_control(ins)
        if ins.mnemonic.startswith(('j', 'loop')):
            if target is None:
                return False
            successors = (target,) if ins.mnemonic == 'jmp' else (target, ins.end)
        else:
            successors = (ins.end,)
        visiting.add(va)
        ok = all(visit(at) for at in successors)
        visiting.remove(va)
        if ok:
            proved.add(va)
        return ok

    return visit(start)


def inspect(image, decoded, source):
    checks = {'preferred_base': image.image_base == IMAGE_BASE, **source_checks(source)}
    rows = []
    for spec in ALL_SITES:
        instructions = decoded.get((spec.function_start, spec.function_end), [])
        row = inspect_site(image, spec, instructions)
        if spec.va in RELATIVE_SITES:
            target, control_va, mnemonics = RELATIVE_SITES[spec.va]
            span = [x for x in instructions if spec.va <= x.va < spec.end]
            controls = [x for x in span if _is_direct_control(x) is not None]
            row['relocated_control'] = (len(controls) == 1 and controls[0].va == control_va
                                        and controls[0].mnemonic in mnemonics
                                        and _is_direct_control(controls[0]) == target)
            if mnemonics == ('je', 'jz'):
                row['relocated_jz'] = row['relocated_control']
            row['ok'] = all(row[k] for k in
                            ('bytes_ok', 'whole_instructions', 'no_interior_branch', 'relocated_control'))
        rows.append(row)
    checks['sites'] = all(x['ok'] for x in rows)
    checks['context'] = all(image.read(va, len(bytes.fromhex(raw))) == bytes.fromhex(raw)
                            for va, raw in WITNESSES.values())
    checks['hud_context'] = all(image.read(va, len(bytes.fromhex(raw))) == bytes.fromhex(raw)
                               for va, raw in HUD_WITNESSES.values())
    overlay = decoded.get(OVERLAY, [])
    checks['lead_common_endpoint'] = all_paths_reach(overlay, 0x42a6fe, 0x42aae0)
    checks['solver_success_failure_endpoint'] = all_paths_reach(overlay, 0x42a797, 0x42aae0)
    checks['central_draw_hide_endpoint'] = all_paths_reach(overlay, 0x42aae0, 0x42aed6)
    checks['distance_normal_return_endpoint'] = all_paths_reach(
        decoded.get(DISTANCE_CALLER, []), 0x423007, 0x42300c)
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
        report['checks']['exe_identity'] = exe_identity.identity_ok(data)
        report['exe_info'] = exe_identity.info(data)
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
