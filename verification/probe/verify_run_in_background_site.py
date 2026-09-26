#!/usr/bin/env python3
"""Read-only qualification of the run-in-background site 0x004033c9 (docs/reverse-engineering/run-in-background.md).

src/proxy/run_in_background.cpp redirects the init routine's `call 0x004d2580` at 0x004033c9 to a thunk that sets the
RunInBackground bit 0x4000 of [*0x00606f3c] once, as `-runinbg` does. This checks X3AP.exe on the host: the structural
identity; the 60-byte window 0x00403392..0x004033cd with its whole-instruction boundaries; the argument parser (the four
strings /runinbg -runinbg /noruninbg -noruninbg, each compare's je target, the stores 1 / 0 into [esp+0x14] and the -1
initialiser 0x004027ba) and that the window's `mov eax,[esp+0x14]` is the only other instruction of the init routine
0x00402780..0x0040383c naming [esp+0x14]; that the redirected call is the only direct call or jump to 0x004d2580 in .text
(raw rel32 scan) and that no raw rel8/rel32 branch lands inside its five bytes; the pump's test of the same bit; the
script setter/reader of the bit (X2_SetRunInBackground / X2_IsRunInBackground); the call's atomic-word rule; no other DLL
claim on the window; and that src/proxy/run_in_background_sites.h carries the same constants. Also the
`run_in_background` log-row parser the runner and the host test use. No Wine, no game launch.

    python3 verification/probe/verify_run_in_background_site.py [--exe PATH] [--json OUT]
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)
import verify_collide_sites as claims_source

common = claims_source.common
ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/run_in_background_sites.h'
DEFAULT_EXE = common.DEFAULT_EXE
INIT_FUNCTION = (0x00402780, 0x0040383d)  # int3 padding from 0x0040383d
WINDOW_VA, SITE_VA, TARGET_VA, RETURN_VA, SLOT_VA, BIT = 0x00403392, 0x004033c9, 0x004d2580, 0x004033ce, 0x00606f3c, 0x4000
WINDOW = bytes.fromhex('8b0d3c6f6000 8b442414 810900100000 83f8ff 7510 bea8565500 e85f510b00 8b0d3c6f6000 85c0 7408 '
                       '810900400000 eb06 8121ffbfffff e8b2f10c00'.replace(' ', ''))
WINDOW_STARTS = [0x403392, 0x403398, 0x40339c, 0x4033a2, 0x4033a5, 0x4033a7, 0x4033ac, 0x4033b1, 0x4033b7, 0x4033b9, 0x4033bb,
                 0x4033c1, 0x4033c3, 0x4033c9]
# The parser: (string VA, text, the compare's je VA, the store it reaches, the value stored).
ARGUMENTS = ((0x555514, b'/runinbg', 0x402bf7, 0x402d16, 1), (0x555520, b'-runinbg', 0x402c10, 0x402d16, 1),
             (0x55552c, b'/noruninbg', 0x402c29, 0x402d09, 0), (0x555538, b'-noruninbg', 0x402c42, 0x402d09, 0))
STORES = {0x402d09: bytes.fromhex('c744241400000000'), 0x402d16: bytes.fromhex('c744241401000000')}
# 0x004027a7 push 0x510 is outstanding at 0x004027ba, so [esp+0x18] there is the local [esp+0x14] = ebx = -1 (0x004027a4).
LOCAL_INIT = {0x4027a4: bytes.fromhex('83cbff'), 0x4027a7: bytes.fromhex('6810050000'), 0x4027ba: bytes.fromhex('895c2418')}
LOCAL_OPERAND_USES = [0x402d09, 0x402d16, 0x403398]  # every instruction of the init routine naming [esp+0x14]
PUMP = (0x4d34bc, bytes.fromhex('a1dc8a6000 85c0 0f85b3000000 8b0d3c6f6000 f70100400000 0f85a1000000'.replace(' ', '')))
SCRIPT = {0x40764d: bytes.fromhex('810800400000'), 0x407657: bytes.fromhex('8120ffbfffff'),
          0x40767e: bytes.fromhex('8b153c6f6000 8b02 8b0de4856000 c1e80e'.replace(' ', ''))}
REGISTRY_NAME = (0x5556a8, b'RunInBackground\0')
CALLEE = bytes.fromhex('51e8dafeffff')  # push ecx (a slot, no argument); call 0x004d2460
LOG_RE = re.compile(r'\brun_in_background site=0x(?P<site>[0-9a-f]{8}) status=(?P<status>armed|armed_unverified|off|refused|patched|already) '
                    r'reason=(?P<reason>\S+) setting=(?P<setting>[01?-]) value_before=(?P<before>[01-]) value_after=(?P<after>[01-])'
                    r'(?: write=(?P<write>none|atomic|plain) handler=0x(?P<handler>[0-9a-f]{8})'
                    r'| flags_before=(?P<flags_before>0x[0-9a-f]{8}|-) flags_after=(?P<flags_after>0x[0-9a-f]{8}|-))')


def parse_log_line(line):
    """One `run_in_background` row (the install row or the site row) -> dict, or None."""
    match = LOG_RE.search(line)
    if not match:
        return None
    row = {k: v for k, v in match.groupdict().items() if v is not None}
    row['site'] = int(row['site'], 16)
    row['kind'] = 'install' if 'write' in row else 'site'
    for key in ('before', 'after'):
        row[key] = None if row[key] == '-' else int(row[key])
    for key in ('flags_before', 'flags_after', 'handler'):
        if key in row:
            row[key] = None if row[key] == '-' else int(row[key], 16)
    return row


def source_constants(text):
    """The header's addresses and window as Python values."""
    value = lambda name: int(re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+)', text).group(1), 16)
    body = re.search(r'expected_window\[window_length\]\s*=\s*\{([^}]*)\}', text).group(1)
    return {'window_va': value('window_va'), 'site_va': value('site_va'), 'target_va': value('target_va'),
            'return_va': value('return_va'), 'input_block_slot_va': value('input_block_slot_va'),
            'run_in_background_bit': value('run_in_background_bit'),
            'window': bytes(int(b, 16) for b in re.findall(r'0x([0-9a-fA-F]{2})', body))}


EXPECTED_CONSTANTS = {'window_va': WINDOW_VA, 'site_va': SITE_VA, 'target_va': TARGET_VA, 'return_va': RETURN_VA,
                      'input_block_slot_va': SLOT_VA, 'run_in_background_bit': BIT, 'window': WINDOW}


def raw_branch_hits(image, lo, hi):
    """(VA, kind) of every raw direct-branch encoding in .text whose target lies in [lo, hi)."""
    text_lo, text_hi = image.text_range()
    code = image.read(text_lo, text_hi - text_lo)
    hits = []
    for at in range(len(code) - 1):
        op = code[at]
        va = text_lo + at
        if op in (0xe8, 0xe9) and at + 5 <= len(code):
            target = (va + 5 + struct.unpack_from('<i', code, at + 1)[0]) & 0xffffffff
            if lo <= target < hi:
                hits.append((va, 'call' if op == 0xe8 else 'jmp'))
        if op == 0x0f and 0x80 <= code[at + 1] <= 0x8f and at + 6 <= len(code):
            target = (va + 6 + struct.unpack_from('<i', code, at + 2)[0]) & 0xffffffff
            if lo <= target < hi:
                hits.append((va, 'jcc32'))
        if (0x70 <= op <= 0x7f or op == 0xeb) and lo <= va + 2 + struct.unpack_from('<b', code, at + 1)[0] < hi:
            hits.append((va, 'rel8'))
    return hits


def verify(exe):
    data = Path(exe).read_bytes()
    image = common.Image(data)
    read = lambda va, n: image.read(va, n) or b''
    checks = {'exe_identity': bool(exe_identity.identity_ok(data))}
    checks['window_bytes'] = read(WINDOW_VA, len(WINDOW)) == WINDOW
    decoded = common.parse_objdump(common.objdump_window(exe, *INIT_FUNCTION), *INIT_FUNCTION)
    starts = {i.va for i in decoded}
    checks['window_boundaries'] = all(va in starts for va in WINDOW_STARTS) and not any(
        WINDOW_VA < va < WINDOW_VA + len(WINDOW) and va not in WINDOW_STARTS for va in starts)
    by_va = {i.va: i for i in decoded}
    call = by_va.get(SITE_VA)
    checks['site_is_call_target'] = bool(call) and call.mnemonic == 'call' and common._is_direct_control(call) == TARGET_VA
    arguments = []
    for string_va, text, je_va, store_va, value in ARGUMENTS:
        push = by_va.get(je_va - 0x10)
        je = by_va.get(je_va)
        ok = (read(string_va, len(text) + 1) == text + b'\0' and bool(push) and push.raw == b'\x68' + struct.pack('<I', string_va)
              and bool(je) and je.mnemonic == 'je' and common._is_direct_control(je) == store_va)
        arguments.append({'string': text.decode(), 'string_va': f'{string_va:#010x}', 'je': f'{je_va:#010x}', 'store': f'{store_va:#010x}',
                          'value': value, 'ok': ok})
    checks['argument_parser'] = all(a['ok'] for a in arguments)
    checks['argument_stores'] = all(read(va, len(raw)) == raw for va, raw in STORES.items())
    checks['local_initialised_minus_one'] = all(read(va, len(raw)) == raw for va, raw in LOCAL_INIT.items())
    local_uses = [i.va for i in decoded if re.search(r'\[esp\+0x14\]', i.operands)]
    checks['local_only_read_in_window'] = local_uses == LOCAL_OPERAND_USES
    checks['pump_tests_bit'] = read(PUMP[0], len(PUMP[1])) == PUMP[1]
    checks['script_setter_reader'] = all(read(va, len(raw)) == raw for va, raw in SCRIPT.items())
    checks['registry_value_name'] = read(REGISTRY_NAME[0], len(REGISTRY_NAME[1])) == REGISTRY_NAME[1]
    checks['callee_prologue'] = read(TARGET_VA, len(CALLEE)) == CALLEE
    target_hits = raw_branch_hits(image, TARGET_VA, TARGET_VA + 1)
    checks['single_caller'] = target_hits == [(SITE_VA, 'call')]
    interior_hits = raw_branch_hits(image, SITE_VA + 1, RETURN_VA)
    # A raw encoding that starts inside a decoded instruction of the init routine (not at a boundary) is operand bytes.
    interior_real = [(va, kind) for va, kind in interior_hits
                     if not (INIT_FUNCTION[0] <= va < INIT_FUNCTION[1] and va not in starts)]
    checks['no_branch_into_call'] = interior_real == []
    checks['atomic_word'] = (SITE_VA + 1) // 8 == (SITE_VA + 4) // 8 and SITE_VA // 8 == (RETURN_VA - 1) // 8
    overlapping = [(name, f'{address:#010x}', length) for name, address, length in
                   claims_source.other_claims(skip_prefix='run_in_background', own_names=())
                   if address < WINDOW_VA + len(WINDOW) and WINDOW_VA < address + length]
    checks['no_overlapping_claim'] = overlapping == []
    checks['header_constants'] = source_constants(CORE.read_text()) == EXPECTED_CONSTANTS
    report = {'exe': str(exe), 'checks': checks, 'arguments': arguments, 'local_uses': [f'{va:#010x}' for va in local_uses],
              'target_callers': [f'{va:#010x}' for va, _ in target_hits], 'raw_interior_hits': [f'{va:#010x}' for va, _ in interior_hits],
              'interior_branches': [f'{va:#010x}' for va, _ in interior_real], 'overlapping_claims': overlapping,
              'window': WINDOW.hex(), 'site_bytes': read(SITE_VA, 5).hex(),
              'result': 'PASS' if all(checks.values()) else 'FAIL'}
    return report


def patched_image(data):
    """The executable with the call's rel32 changed (a redirected or foreign window), for the negative test."""
    out = bytearray(data)
    offset = SITE_VA - 0x401000 + 0x400  # .text: VA 0x00401000 at file offset 0x400 (exe_identity.SECTIONS)
    out[offset + 1] ^= 0x01
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--exe', default=str(DEFAULT_EXE))
    parser.add_argument('--json', default=None, help='write the report here as well')
    args = parser.parse_args()
    report = verify(args.exe)
    text = json.dumps(report, indent=1)
    if args.json:
        Path(args.json).write_text(text + '\n')
    print(text)
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
