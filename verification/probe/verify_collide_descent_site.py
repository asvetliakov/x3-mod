#!/usr/bin/env python3
"""Static verifier of the --collide-descent-sse2 hook site (docs/reverse-engineering/sector-collide.md, section 13).

Reads the installed X3AP.exe only; never runs Wine or the game. Proves, from the bytes: the site 0x004e2956 is one whole
`call 0x004e2530`, the only reference to the descent from outside its own body; nothing lands inside the rel32; the
descent, the leaf triangle test and the four transform helpers are the pinned bodies (hashed with the holes other
modules may patch zeroed); the descent is plain cdecl with exactly the calls the replacement reproduces; the leaf reads
only ESI/EAX of its caller's registers and returns 0 on every path; the x87 stack is empty at the site; the engine's
visit counter is written but never read; and no other claim of the proxy touches the compared windows.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

import verify_collide_sites as sites

common = sites.common
ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/collide_descent_sse2_core.h'
SITE, TARGET, RETURN = 0x4e2956, 0x4e2530, 0x4e295b
CALLER = (0x4e2780, 0x4e2969)
DESCENT = (0x4e2530, 0x4e2777)
LEAF = (0x4e2190, 0x4e252e)
HELPERS = ((0x4e1ff0, 223), (0x4e20d0, 95), (0x4dfd80, 223), (0x4dfe60, 75))
ROOT_HELPER = (0x4e2130, 0x4e2187)
PRE_WINDOW = bytes.fromhex('33c0d91c2451525357a344856000a348856000a34c856000')
POST_WINDOW = bytes.fromhex('83c4145f5e5d5b81c498000000c3')
ENTRY_HOLE, SAT_REL32_OFFSET = 5, 0x74
DESCENT_FNV1A, LEAF_FNV1A, HELPERS_FNV1A = 0xcef4870863cdd1de, 0xa9766de75c6ecfca, 0xac374990f77face7
DESCENT_SHA256 = 'df3c73df56f45212db70e6519d36f48c853dcd3f1b8bbe3f1fe84253cf8daf2d'
INBOUND = [0x4e264c, 0x4e269f, 0x4e2705, 0x4e2767, 0x4e2956]
# Every direct call inside the descent, in address order: what the replacement reproduces (SAT, leaf, transforms, recursion).
DESCENT_CALLS = [(0x4e25a3, 0x4e3280), (0x4e25cd, 0x4e2190), (0x4e2612, 0x4e1ff0), (0x4e2630, 0x4e20d0), (0x4e264c, 0x4e2530), (0x4e266c, 0x4e1ff0),
                 (0x4e2686, 0x4e20d0), (0x4e269f, 0x4e2530), (0x4e26bd, 0x4dfd80), (0x4e26ed, 0x4dfe60), (0x4e2705, 0x4e2530), (0x4e271f, 0x4dfd80),
                 (0x4e274f, 0x4dfe60), (0x4e2767, 0x4e2530)]
GLOBALS = {'contacts_va': 0x60854c, 'first_contact_va': 0x596934, 'flags_va': 0x608534, 'cap_va': 0x608538, 'visits_va': 0x608544}
VISIT_COUNTER_REFERENCES = 8   # one `add [m],1` (0x004e257d) and seven `mov [m],x` resets; no reader exists
INSTALL_RE = re.compile(r'\bcollide_descent_sse2 requested=(?P<requested>[01]) patched=(?P<patched>[01]) reason=(?P<reason>\S+) site=0x(?P<site>[0-9a-f]{8}) '
                        r'target=0x(?P<target>[0-9a-f]{8}) write=(?P<write>none|atomic|plain) handler=0x(?P<handler>[0-9a-f]{8})')
EXPECTED_CONSTANTS = {
    'descent_site_va': SITE, 'descent_target_va': TARGET, 'descent_return_va': RETURN, 'descent_target_end_va': DESCENT[1], 'leaf_va': LEAF[0], 'leaf_end_va': LEAF[1],
    **GLOBALS, 'call_length': 5, 'descent_pre_length': len(PRE_WINDOW), 'descent_post_length': len(POST_WINDOW), 'descent_body_length': DESCENT[1] - DESCENT[0],
    'leaf_body_length': LEAF[1] - LEAF[0], 'descent_pre_window': PRE_WINDOW, 'descent_post_window': POST_WINDOW, 'entry_hole': ENTRY_HOLE,
    'sat_rel32_offset': SAT_REL32_OFFSET, 'sat_rel32_length': 4, 'descent_body_fnv1a': DESCENT_FNV1A, 'leaf_body_fnv1a': LEAF_FNV1A, 'helpers_fnv1a': HELPERS_FNV1A,
    'stack_entries': 64}


def parse_install_line(line):
    match = INSTALL_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {k: (v == '1' if k in ('requested', 'patched') else int(v, 16) if k in ('site', 'target', 'handler') else v) for k, v in row.items()}


def source_constants(text):
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)(?:ull)?\s*[;,]', text)
        return int(match.group(1), 0) if match else None

    def array(name):
        match = re.search(rf'\b{name}\[\w+\]\s*=\s*\{{([^}}]*)\}}', text)
        return bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))) if match else b''
    found = {n: array(n) if isinstance(expected, bytes) else value(n) for n, expected in EXPECTED_CONSTANTS.items()}
    ranges = re.search(r'helper_ranges\[4\]\s*=\s*\{(.*?)\};', text, re.S)
    found['helper_ranges'] = tuple((int(a, 16), int(n)) for a, n in re.findall(r'\{(0x[0-9a-fA-F]+),\s*(\d+)\}', ranges.group(1))) if ranges else ()
    return found


def masked_body(image, holes=((0, ENTRY_HOLE), (SAT_REL32_OFFSET, 4))):
    body = bytearray(image.read(DESCENT[0], DESCENT[1] - DESCENT[0]) or b'')
    for at, length in holes:
        body[at:at + length] = bytes(length)
    return bytes(body)


def masked_leaf(image):
    body = bytearray(image.read(LEAF[0], LEAF[1] - LEAF[0]) or b'')
    body[:ENTRY_HOLE] = bytes(ENTRY_HOLE)
    return bytes(body)


def helpers_fnv(image):
    h = 0xcbf29ce484222325
    for va, length in HELPERS:
        for c in image.read(va, length) or b'':
            h = ((h ^ c) * 0x100000001b3) & 0xffffffffffffffff
    return h


def own_windows():
    return [(SITE - len(PRE_WINDOW), RETURN + len(POST_WINDOW))]


def other_claims(root=ROOT):
    return sites.other_claims(root, skip_prefix='collide_descent_sse2', own_names=())


def overlaps(claims):
    return [(name, hex(address)) for name, address, length in claims for lo, hi in own_windows() if address < hi and lo < address + length]


_X87_DEPTH = {'fld': 1, 'fld1': 1, 'fldz': 1, 'fstp': -1, 'faddp': -1, 'fmulp': -1, 'fsubp': -1, 'fmul': 0, 'fadd': 0, 'fsub': 0, 'fdiv': 0, 'fxch': 0, 'fst': 0}


def x87_depth(instructions):
    """x87 stack depth after a straight-line sequence that starts empty; None when an instruction is not modelled or the stack underflows."""
    depth = 0
    for i in instructions:
        if not i.mnemonic.startswith('f'):
            continue
        if i.mnemonic not in _X87_DEPTH:
            return None
        depth += _X87_DEPTH[i.mnemonic]
        if depth < 0:
            return None
    return depth


def decode(exe):
    tool = shutil.which(common.OBJDUMP)
    if not tool:
        raise RuntimeError(f'{common.OBJDUMP} not found')
    decoded = {}
    for bounds in (CALLER, DESCENT, LEAF, ROOT_HELPER, *((va, va + n) for va, n in HELPERS)):
        run = subprocess.run([tool, '-d', '-Mintel', '--insn-width=16', f'--start-address={bounds[0]:#x}', f'--stop-address={bounds[1]:#x}', str(exe)],
                             check=True, capture_output=True, text=True, timeout=60)
        decoded[bounds] = common.parse_objdump(run.stdout, *bounds)
    return decoded


def _first_mention(instructions, names):
    pattern = re.compile(r'\b(' + '|'.join(names) + r')\b')
    return next((i for i in instructions if pattern.search(i.operands.lower())), None)


def _leaf_returns_zero(leaf):
    """Every `ret` of the leaf is reached with EAX = 0: walking back from it, `xor eax,eax` comes before any other EAX write or any join."""
    for index, i in enumerate(leaf):
        if i.mnemonic != 'ret':
            continue
        for back in reversed(leaf[max(0, index - 6):index]):
            if back.mnemonic == 'xor' and back.operands.replace(' ', '').lower() == 'eax,eax':
                break
            if back.mnemonic not in ('pop', 'add', 'fstp') or back.operands.lower().startswith('eax'):
                return False
        else:
            return False
    return True


def _is_store(data, field):
    """The abs32 field at this file offset is the destination of a store: `a3 m`, `89 /r m`, `c7 /0 m imm32` or `83 /0 m imm8` (add)."""
    if data[field - 1] == 0xa3 and data[field - 2] not in (0x89, 0x8b, 0x39, 0x3b, 0x83, 0xc7, 0xff, 0x03, 0x2b):
        return True
    modrm, opcode = data[field - 1], data[field - 2]
    return modrm & 0xc7 == 0x05 and (opcode == 0x89 or (opcode in (0xc7, 0x83) and modrm == 0x05))


def inspect(data, decoded, core_text, claims):
    image = common.Image(data)
    caller, descent, leaf = decoded[CALLER], decoded[DESCENT], decoded[LEAF]
    by_caller = {i.va: i for i in caller}
    target = lambda i: common._is_direct_control(i) if i is not None else None
    calls = [(i.va, target(i)) for i in descent if i.mnemonic == 'call']
    rets = [i for i in descent if i.mnemonic.startswith('ret')]
    leaf_rets = [i for i in leaf if i.mnemonic.startswith('ret')]
    last_call = max(i.va for i in caller if i.mnemonic == 'call' and i.va < SITE)
    visit_refs = sorted(off for off, _ in sites.abs32_references(image, GLOBALS['visits_va'], GLOBALS['visits_va'] + 1))
    simd = [i for bounds in decoded for i in decoded[bounds] if sites._mentions_simd(i)]
    checks = {
        'exe_identity': hashlib.sha256(data).hexdigest() == common.EXPECTED_SHA256 and len(data) == common.EXPECTED_SIZE,
        'preferred_base': image.image_base == common.IMAGE_BASE,
        'windows': image.read(SITE - len(PRE_WINDOW), len(PRE_WINDOW)) == PRE_WINDOW and image.read(RETURN, len(POST_WINDOW)) == POST_WINDOW,
        'site_whole_call': SITE in by_caller and by_caller[SITE].mnemonic == 'call' and len(by_caller[SITE].raw) == 5 and target(by_caller[SITE]) == TARGET and RETURN in by_caller,
        'pre_window_whole_instructions': SITE - len(PRE_WINDOW) in by_caller and caller[-1].mnemonic == 'ret' and caller[-1].end == RETURN + len(POST_WINDOW),
        # The descent is reached by its four recursive calls and this site only: no other rel32, no pointer to it anywhere in the file.
        'inbound_exact': sorted(va for va, _ in sites.rel32_references(image, TARGET, TARGET + 1)) == INBOUND and sites.abs32_references(image, TARGET, TARGET + 1) == [],
        'no_rel32_into_span': sites.rel32_references(image, SITE + 1, SITE + 5) == [],
        'no_abs32_into_span': sites.abs32_references(image, SITE + 1, SITE + 5) == [],
        'no_short_jump_into_span': [t for i in caller for t in [target(i)] if t is not None and SITE < t < SITE + 5] == [],
        'descent_body_hash': sites.fnv1a(masked_body(image)) == DESCENT_FNV1A and hashlib.sha256(masked_body(image)).hexdigest() == DESCENT_SHA256,
        'leaf_body_hash': sites.fnv1a(masked_leaf(image)) == LEAF_FNV1A,
        'helpers_hash': helpers_fnv(image) == HELPERS_FNV1A,
        'descent_calls_exact': calls == DESCENT_CALLS,
        'descent_plain_cdecl': len(rets) == 4 and all(i.raw == b'\xc3' for i in rets) and descent[-1].mnemonic == 'ret'
                               and [target(by_caller[va]) if va in by_caller else None for va in (SITE,)] == [TARGET] and by_caller[RETURN].raw == bytes.fromhex('83c414'),
        'descent_saves_callee_registers': [i.raw for i in descent[1:7] if i.mnemonic == 'push'] == [b'\x53', b'\x55', b'\x56', b'\x57'],
        # The leaf: one caller, plain rets, EAX = 0 on every path, and of its caller's registers it reads ESI and EAX only.
        'leaf_sole_caller': sites.rel32_references(image, LEAF[0], LEAF[0] + 1) == [(0x4e25cd, 0xe8)] and sites.abs32_references(image, LEAF[0], LEAF[0] + 1) == [],
        'leaf_plain_rets': len(leaf_rets) == 2 and all(i.raw == b'\xc3' for i in leaf_rets),
        'leaf_returns_zero': _leaf_returns_zero(leaf),
        'leaf_register_inputs': leaf[3].raw == bytes.fromhex('8bf8') and leaf[4].raw == bytes.fromhex('8b4644') and _first_mention(leaf, ('ebp', 'bp')) is None
                                and getattr(_first_mention(leaf, ('ecx', 'cx', 'cl', 'ch')), 'mnemonic', None) == 'lea'
                                and getattr(_first_mention(leaf, ('edx', 'dx', 'dl', 'dh')), 'mnemonic', None) == 'lea'
                                and getattr(_first_mention(leaf, ('ebx', 'bx', 'bl', 'bh')), 'mnemonic', None) == 'push',
        # x87: the helpers are balanced and the caller's tail from its last call to the site ends empty, so the site is entered with an empty stack.
        'x87_helpers_balanced': all(x87_depth(decoded[(va, va + n)]) == 0 for va, n in HELPERS) and x87_depth(decoded[ROOT_HELPER]) == 0,
        'x87_empty_at_site': x87_depth([i for i in caller if last_call < i.va < SITE]) == 0,
        'flags_dead_at_return': sites._writes_flags(by_caller[RETURN]) if RETURN in by_caller else False,
        'no_xmm_on_path': simd == [],
        # The engine's visit counter 0x00608544 is only ever written (one add, three resets): nothing can observe when it is flushed.
        'visit_counter_never_read': len(visit_refs) == VISIT_COUNTER_REFERENCES and all(_is_store(data, off) for off in visit_refs),
        'claims_disjoint': overlaps(claims) == [] and len(claims) >= 40 and any(address == sites.SAT_SITE for _, address, _ in claims)
                           and any(address == sites.N7_SITE for _, address, _ in claims),
        'source_constants': source_constants(core_text) == {**EXPECTED_CONSTANTS, 'helper_ranges': HELPERS},
    }
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'site': hex(SITE), 'target': hex(TARGET), 'other_claims_checked': len(claims),
            'overlaps': overlaps(claims), 'visit_counter_references': len(visit_refs), 'exe_sha256': hashlib.sha256(data).hexdigest()}


def verify(exe=sites.DEFAULT_EXE, core=CORE):
    data = Path(exe).read_bytes()
    try:
        return inspect(data, decode(exe), Path(core).read_text(), other_claims())
    except (ValueError, OSError, KeyError, IndexError, subprocess.SubprocessError, RuntimeError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': repr(error)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=sites.DEFAULT_EXE)
    parser.add_argument('--core', type=Path, default=CORE)
    args = parser.parse_args()
    report = verify(args.exe, args.core)
    print(json.dumps(report, indent=2, sort_keys=True))
    raise SystemExit(report['result'] != 'PASS')
