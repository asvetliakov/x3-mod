#!/usr/bin/env python3
"""Static verifier of the --collide-memo hook site (docs/reverse-engineering/sector-collide.md, section 14).

Reads the installed X3AP.exe only; never runs Wine or the game. Proves, from the bytes: the site 0x0047f329 is one whole
`call 0x004e29f0`, the only reference to that function in the image; nothing lands inside the rel32; the six bodies the
memo's input/output enumeration rests on are the pinned ones; the caller reads of its two nodes exactly the 13 words
per node that end up in the floats the key holds, and nothing after the site but the contact counter; 0x004e29f0 reads
its tenth stack word (the caller's saved EDI) and stores exactly four globals; the whole collider range references no
global outside the two blocks the memo replays or leaves alone; the x87 stack is empty at the site; and no other claim
of the proxy touches the compared windows.
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
CORE = ROOT / 'src/proxy/collide_memo_core.h'
SITE, TARGET, RETURN = 0x47f329, 0x4e29f0, 0x47f32e
CALLER = (0x47f1b0, 0x47f341)
TARGET_FN = (0x4e29f0, 0x4e2a50)
QUERY = (0x4e2780, 0x4e2969)
DESCENT = (0x4e2530, 0x4e2777)
LEAF = (0x4e2190, 0x4e252e)
TRIANGLE = (0x4e2a50, 0x4e3280)
COLLIDER = (0x4e2190, 0x4e38ae)   # leaf, descent, query, 0x004e29f0, triangle test, SAT
ALLOCATOR = (0x4e2970, 0x4e29eb)   # the model constructor in the middle of that range: not on the query path
HELPERS = ((0x4e1ff0, 0x4e2187), (0x4dfd80, 0x4dfee3))   # matrix and vector products: no global, no call
FTOL_VA = 0x52b5d0
HELPER_ENTRIES = [0x4dfd80, 0x4dfe60, 0x4dfeb0, 0x4e1ff0, 0x4e20d0, 0x4e2130]
PRE_WINDOW = bytes.fromhex('8b842494000000518d4c2434d91c24518b8c24980000008d54246852')
POST_WINDOW = bytes.fromhex('33c083c42839054c8560005e0f95c083c464c3')
HASHES = {'caller': 0xfdd929ef070d4324, 'target': 0x9d655aae0a820ae8, 'query': 0x5a4d7c6efe584a18, 'descent': 0xcef4870863cdd1de, 'leaf': 0xa9766de75c6ecfca,
          'triangle': 0x90c2eb0126ac4f2a, 'sat': 0xad8a2cb66c0bf30c, 'matrix_helpers': 0xd21cd0c0e39e9854, 'vector_helpers': 0xc5107d96ea42adcd, 'ftol': 0xc648662b5a549dd9}
FTOL = (0x52b5d0, 0x52b67b)
SSE2_FLAG_VA = 0x6619ec
CALLER_SITES = [0x48a69e, 0x48a9a5]
NODE_WORDS = [0x70, 0xb0, 0xb4, 0xb8, 0xc0, 0xc4, 0xc8, 0xd0, 0xd4, 0xd8, 0xe0, 0xe4, 0xe8]
TARGET_STORES = [0x608534, 0x608538, 0x60853c, 0x608540]
# Every global the collider range names: constants, the root transform block, the contact record, mode and counters, the one-time init pair.
CONSTANTS = {0x565600, 0x565604, 0x565608}
ROOT_BLOCK = (0x596928, 0x596960)
STATE_BLOCK = (0x60851c, 0x608550)
INIT_PAIR = {0x608d98, 0x608d9c}
INSTALL_RE = re.compile(r'\bcollide_memo requested=(?P<requested>[01]) patched=(?P<patched>[01]) verify=(?P<verify>[01]) reason=(?P<reason>\S+) site=0x(?P<site>[0-9a-f]{8}) '
                        r'target=0x(?P<target>[0-9a-f]{8}) write=(?P<write>none|atomic|plain) handler=0x(?P<handler>[0-9a-f]{8}) entries=(?P<entries>\d+)')
WINDOW_KEYS = ('device', 'frame', 'frames', 'verify', 'queries', 'hits', 'misses', 'stored', 'contacts', 'ineligible', 'evictions', 'skipped_visits', 'skipped_triangles',
               'verified', 'verify_mismatches', 'foreign_thread', 'reentered', 'clears', 'stuck_busy', 'min_relaxed_hits', 'miss_none_found', 'miss_none_found_visits', 'miss_xform_a', 'miss_xform_a_visits', 'miss_xform_b', 'miss_xform_b_visits', 'miss_scale', 'miss_scale_visits', 'miss_mode', 'miss_mode_visits', 'miss_models', 'miss_models_visits', 'miss_min_value', 'miss_min_value_visits', 'miss_expired', 'miss_expired_visits')
WINDOW_RE = re.compile(r'\bcollide_memo ' + ' '.join(rf'{k}=(?P<{k}>\d+)' for k in WINDOW_KEYS) + r'\s*$')
EXPECTED_CONSTANTS = {
    'memo_site_va': SITE, 'memo_target_va': TARGET, 'memo_return_va': RETURN, 'caller_va': CALLER[0], 'query_va': QUERY[0], 'descent_va': DESCENT[0], 'leaf_va': LEAF[0],
    'triangle_va': TRIANGLE[0], 'call_length': 5, 'memo_pre_length': len(PRE_WINDOW), 'memo_post_length': len(POST_WINDOW), 'caller_length': SITE - CALLER[0],
    'target_length': TARGET_FN[1] - TARGET_FN[0], 'query_length': QUERY[1] - QUERY[0], 'descent_length': DESCENT[1] - DESCENT[0], 'leaf_length': LEAF[1] - LEAF[0],
    'triangle_length': TRIANGLE[1] - TRIANGLE[0], 'memo_pre_window': PRE_WINDOW, 'memo_post_window': POST_WINDOW, 'entry_hole': 5, 'sat_rel32_offset': 0x74, 'sat_rel32_length': 4,
    'caller_fnv1a': HASHES['caller'], 'target_fnv1a': HASHES['target'], 'query_fnv1a': HASHES['query'], 'descent_fnv1a': HASHES['descent'], 'leaf_fnv1a': HASHES['leaf'],
    'triangle_fnv1a': HASHES['triangle'], 'sat_va': sites.SAT_CALLEE[0], 'matrix_helpers_va': HELPERS[0][0], 'vector_helpers_va': HELPERS[1][0], 'ftol_va': FTOL_VA,
    'sat_length': sites.SAT_CALLEE[1] - sites.SAT_CALLEE[0], 'matrix_helpers_length': HELPERS[0][1] - HELPERS[0][0], 'vector_helpers_length': HELPERS[1][1] - HELPERS[1][0],
    'ftol_length': FTOL[1] - FTOL[0], 'sat_fnv1a': HASHES['sat'], 'matrix_helpers_fnv1a': HASHES['matrix_helpers'], 'vector_helpers_fnv1a': HASHES['vector_helpers'],
    'ftol_fnv1a': HASHES['ftol'], 'queries_without_tick_limit': 100000, 'minimum_value_word': 30, 'model_words_begin': 31, 'flags_va': 0x608534, 'cap_va': 0x608538, 'tolerance_va': 0x60853c, 'minimum_va': 0x608540, 'visits_va': 0x608544,
    'triangles_va': 0x608548, 'contacts_va': 0x60854c, 'root_block_va': ROOT_BLOCK[0], 'root_block_words': (ROOT_BLOCK[1] - ROOT_BLOCK[0]) // 4, 'header_words': 6, 'box_words': 18,
    'model_built': 3, 'model_state_word': 5, 'ways': 4, 'sets': 256}


def parse_install_line(line):
    match = INSTALL_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {k: (v == '1' if k in ('requested', 'patched', 'verify') else int(v, 16) if k in ('site', 'target', 'handler') else int(v) if k == 'entries' else v) for k, v in row.items()}


def parse_window_line(line):
    match = WINDOW_RE.search(line)
    return {k: int(v) for k, v in match.groupdict().items()} if match else None


def source_constants(text):
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)(?:ull)?\s*[;,]', text)
        return int(match.group(1), 0) if match else None

    def array(name):
        match = re.search(rf'\b{name}\[\w+\]\s*=\s*\{{([^}}]*)\}}', text)
        return bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))) if match else b''
    return {n: array(n) if isinstance(expected, bytes) else value(n) for n, expected in EXPECTED_CONSTANTS.items()}


def body_hashes(image):
    def masked(bounds, holes=()):
        body = bytearray(image.read(bounds[0], bounds[1] - bounds[0]) or b'')
        for at, length in holes:
            body[at:at + length] = bytes(length)
        return sites.fnv1a(bytes(body))
    return {'caller': masked((CALLER[0], SITE)), 'target': masked(TARGET_FN), 'query': masked(QUERY), 'descent': masked(DESCENT, ((0, 5), (0x74, 4))), 'leaf': masked(LEAF, ((0, 5),)),
            'triangle': masked(TRIANGLE), 'sat': masked(sites.SAT_CALLEE), 'matrix_helpers': masked(HELPERS[0]), 'vector_helpers': masked(HELPERS[1]), 'ftol': masked(FTOL)}


def own_windows():
    return [(SITE - len(PRE_WINDOW), RETURN + len(POST_WINDOW))]


def other_claims(root=ROOT):
    return sites.other_claims(root, skip_prefix='collide_memo', own_names=())


def overlaps(claims):
    return [(name, hex(address)) for name, address, length in claims for lo, hi in own_windows() if address < hi and lo < address + length]


_X87_DEPTH = {'fld': 1, 'fild': 1, 'fld1': 1, 'fldz': 1, 'fstp': -1, 'faddp': -1, 'fmulp': -1, 'fsubp': -1, 'fmul': 0, 'fimul': 0, 'fadd': 0, 'fsub': 0, 'fdiv': 0, 'fxch': 0, 'fst': 0}


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
    for bounds in (CALLER, TARGET_FN, COLLIDER, FTOL, *HELPERS):
        run = subprocess.run([tool, '-d', '-Mintel', '--insn-width=16', f'--start-address={bounds[0]:#x}', f'--stop-address={bounds[1]:#x}', str(exe)],
                             check=True, capture_output=True, text=True, timeout=60)
        decoded[bounds] = [i for i in common.parse_objdump(run.stdout, *bounds)]
    return decoded


def _node_reads(caller, register):
    """Displacements the caller reads through `register` (the node in ECX or EAX) before the site, memory operands only."""
    found = set()
    for i in caller:
        if i.va >= SITE:
            break
        for m in re.finditer(rf'\[{register}\+0x([0-9a-f]+)\]', i.operands.lower()):
            if i.mnemonic in ('fild', 'fimul'):
                found.add(int(m.group(1), 16))
    return sorted(found)


def _globals_named(instructions):
    return {int(m.group(1), 16) for i in instructions for m in re.finditer(r'ds:0x([0-9a-f]+)', i.operands.lower())}


def _minimum_reads(collider):
    """Instructions that load the running-minimum pointer [0x00608540] (its stores are in 0x004e29f0)."""
    return [i.va for i in collider if '0x608540' in i.operands.lower() and not re.match(r'(?:dword ptr )?ds:0x608540,', i.operands.lower())]


def _leaf_counts_at_entry(collider):
    """From the leaf's entry to `add [0x00608548],1` there is no branch, call or return: every entry is counted before anything else can happen."""
    head = [i for i in collider if LEAF[0] <= i.va <= 0x4e22a5]
    return bool(head) and head[-1].raw == bytes.fromhex('83054885600001') and not any(i.mnemonic.startswith(('j', 'call', 'ret', 'loop')) for i in head)


def inspect(data, decoded, core_text, claims):
    image = common.Image(data)
    caller, target_fn, collider = decoded[CALLER], decoded[TARGET_FN], decoded[COLLIDER]
    by_caller = {i.va: i for i in caller}
    target = lambda i: common._is_direct_control(i) if i is not None else None
    after = [i for i in caller if i.va >= RETURN]
    # ECX is reloaded with the flags at 0x0047f31d, EAX with the cap at 0x0047f30d: the node registers are read before that only.
    node_a = _node_reads([i for i in caller if i.va < 0x47f2f7], 'ecx') + [d for d in _node_reads([i for i in caller if 0x47f2f7 <= i.va < 0x47f31d], 'ecx')]
    node_b = _node_reads([i for i in caller if i.va < 0x47f2fc], 'eax')
    stores = sorted(int(m.group(1), 16) for i in target_fn if i.mnemonic == 'mov' for m in [re.match(r'(?:dword ptr )?ds:0x([0-9a-f]+),', i.operands.lower())] if m)
    named = _globals_named(collider)
    outside = sorted(g for g in named if g not in CONSTANTS and g not in INIT_PAIR and not ROOT_BLOCK[0] <= g < ROOT_BLOCK[1] and not STATE_BLOCK[0] <= g < STATE_BLOCK[1])
    calls_out = sorted({t for i in collider if i.mnemonic == 'call' and not ALLOCATOR[0] <= i.va < ALLOCATOR[1] for t in [target(i)] if t is not None and not COLLIDER[0] <= t < COLLIDER[1]})
    helpers = [i for bounds in HELPERS for i in decoded[bounds]]
    checks = {
        'exe_identity': hashlib.sha256(data).hexdigest() == common.EXPECTED_SHA256 and len(data) == common.EXPECTED_SIZE,
        'preferred_base': image.image_base == common.IMAGE_BASE,
        'windows': image.read(SITE - len(PRE_WINDOW), len(PRE_WINDOW)) == PRE_WINDOW and image.read(RETURN, len(POST_WINDOW)) == POST_WINDOW,
        'site_whole_call': SITE in by_caller and by_caller[SITE].mnemonic == 'call' and len(by_caller[SITE].raw) == 5 and target(by_caller[SITE]) == TARGET and RETURN in by_caller,
        'windows_whole_instructions': SITE - len(PRE_WINDOW) in by_caller and caller[-1].mnemonic == 'ret' and caller[-1].end == RETURN + len(POST_WINDOW),
        'sole_caller': sites.rel32_references(image, TARGET, TARGET + 1) == [(SITE, 0xe8)] and sites.abs32_references(image, TARGET, TARGET + 1) == [],
        'no_rel32_into_span': sites.rel32_references(image, SITE + 1, SITE + 5) == [],
        'no_abs32_into_span': sites.abs32_references(image, SITE + 1, SITE + 5) == [],
        'no_short_jump_into_span': [t for i in caller for t in [target(i)] if t is not None and SITE < t < SITE + 5] == [],
        'body_hashes': body_hashes(image) == HASHES,
        # The caller: two call sites in the image; of each node it reads the scale, the saved position and the nine matrix words, nothing else.
        'caller_sites': sorted(va for va, _ in sites.rel32_references(image, CALLER[0], CALLER[0] + 1)) == CALLER_SITES and sites.abs32_references(image, CALLER[0], CALLER[0] + 1) == [],
        'node_a_words': sorted(set(node_a)) == NODE_WORDS,
        'node_b_words': node_b == NODE_WORDS,
        'caller_calls_only_the_target': [target(i) for i in caller if i.mnemonic == 'call'] == [TARGET],
        # After the site the caller reads the contact counter and nothing else of the query's state; EFLAGS and EAX are rewritten first.
        'after_site_reads_contacts_only': after[0].raw == bytes.fromhex('33c0') and _globals_named(after) == {0x60854c} and [i.mnemonic for i in after] == ['xor', 'add', 'cmp', 'pop', 'setne', 'add', 'ret'],
        # 0x004e29f0: flags from ECX, cap from EAX, the tenth stack word ([esp+0x2c] after its one push) into 0x00608540, one call each of ftol and the query.
        'target_stores': stores == TARGET_STORES,
        'target_reads_tenth_word': any(i.raw == bytes.fromhex('8b4c242c') for i in target_fn) and any(i.raw == bytes.fromhex('890d40856000') for i in target_fn),
        'target_calls': [target(i) for i in target_fn if i.mnemonic == 'call'] == [0x52b5d0, QUERY[0]],
        'target_mode_mask': any(i.raw == bytes.fromhex('83e6f3') for i in target_fn),
        # The whole collider names no global outside the blocks the memo replays (root block, mode and counters) or leaves alone (contact record), and calls out only to fabs.
        'collider_globals_enumerated': outside == [],
        'collider_calls_out': calls_out == sorted([sites.FABS_HELPER_VA, FTOL_VA, *HELPER_ENTRIES]),
        'helpers_pure': _globals_named(helpers) == set() and not any(i.mnemonic == 'call' for i in helpers),
        # ftol reads one global, the process-constant SSE2 flag (cvttsd2si when set, else an x87 path under the control word), writes none and calls nothing.
        # The running minimum is read in one place in the whole collider, inside the leaf and after its triangle-test counter: a run
        # that counted no triangle test never read it (the memo's relaxation of key word 30).
        'minimum_read_in_the_leaf_only': _minimum_reads(collider) == [0x4e246e] and _leaf_counts_at_entry(collider),
        'ftol_reads_the_sse2_flag_only': _globals_named(decoded[FTOL]) == {SSE2_FLAG_VA} and not any(i.mnemonic == 'call' for i in decoded[FTOL]),
        # The tenth word is an argument: `push edi` right after the two null tests, before the nine others.
        'tenth_word_is_pushed_edi': by_caller.get(0x47f1d7) is not None and by_caller[0x47f1d7].raw == b'\x57',
        'x87_empty_at_site': x87_depth([i for i in caller if 0x47f1d1 <= i.va < SITE]) == 0,
        'no_xmm_in_caller': not any(sites._mentions_simd(i) for i in caller + target_fn),
        'claims_disjoint': overlaps(claims) == [] and len(claims) >= 40 and any(address == sites.N6_SITE for _, address, _ in claims) and any(address == sites.SAT_SITE for _, address, _ in claims),
        'census_windows_disjoint': not any(lo < RETURN + len(POST_WINDOW) and SITE - len(PRE_WINDOW) < hi for lo, hi in sites.narrow_own_windows() + sites.sat_own_windows()),
        'source_constants': source_constants(core_text) == EXPECTED_CONSTANTS,
    }
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'site': hex(SITE), 'target': hex(TARGET), 'other_claims_checked': len(claims),
            'overlaps': overlaps(claims), 'globals_named': len(named), 'exe_sha256': hashlib.sha256(data).hexdigest()}


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
