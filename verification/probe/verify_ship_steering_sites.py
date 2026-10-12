#!/usr/bin/env python3
"""Instruction-aware verification of the four ship-steering sites (X3M_SHIP_STEERING=1;
docs/architecture/ship-steering-and-avoidance.md section 7.2, src/proxy/ship_steering_sites.h).

A1-A3: the three steering-leaf entries 0x0044ccc0 / 0x0044c620 / 0x0044c970 (`sub esp,0x18` | `mov eax,[edx]`,
5 B) decoded inside their whole functions, each ending in `ret 4`. B: the command pass's object-loop continue
0x004593dd (`mov ebx,[ebx]` | `cmp dword [ebx],0`, 5 B) followed by `jne 0x00452b60`, decoded in its neighbourhood.
Every span is whole instructions with no relative control; no direct branch in .text and no data dword in
.rdata/.data lands in a span interior; every branch and jump-table entry into [0x004593dd, 0x004593e2) lands on
0x004593dd exactly (the census is reported); the SiteSpec table of the sites header equals the derived rows; the
five engine getters' conventions (obj in EAX, or ECX for the acceleration getter; plain `ret`) are read from their
first instruction, their returns and the MovePos call sites; the slew block is the body of a second bucket loop
that runs after the order loop (no branch from outside it; the order loop's bucket advance falls through into it);
the leaves' fpatan operand sequences witness the axis sign conventions; the "large" predicate's byte table yields
the core's mask.

usage: verify_ship_steering_sites.py [--exe PATH] [--json]
"""
import argparse
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

import verify_chase_aim_sites as common
import exe_identity

ROOT = Path(__file__).resolve().parents[2]
SITES_HEADER = ROOT / 'src/proxy/ship_steering_sites.h'
CORE_HEADER = ROOT / 'src/proxy/ship_steering_core.h'
DEFAULT_EXE = Path('/mnt/c/X3/X3AP.exe')
H = common.HookSpec
LEAF_BYTES = bytes.fromhex('83ec188b02')
LEAVES = ((0x0044ccc0, 0x0044d014, 0x0044d011), (0x0044c620, 0x0044c963, 0x0044c960), (0x0044c970, 0x0044ccb5, 0x0044ccb2))
B_VA, B_JNE, B_TARGET = 0x004593dd, 0x004593e2, 0x00452b60
B_WINDOW = (0x004593c2, 0x00459403)          # the loop tail: `jle` ... the bucket advance `jl 0x00452b42`, fall-through
SLEW_REGION = (0x00459403, 0x004595de)       # the second bucket loop (speed slew) up to the pass's `ret 4` at 0x004595db
SLEW_LOOP_HEAD, SLEW_CALL, SLEW_BUCKET_HEAD, PASS_RET = 0x004594b0, 0x004594ef, 0x00459490, 0x004595db
ORDER_JUMP_TABLE, ORDER_BYTE_TABLE, ORDER_BYTE_COUNT = 0x0045963c, 0x00459694, 0x25
# Site E (step 2): the warn test 0x0044dd90, stdcall (A, B, d, &out), `ret 0x10` x4, decoded as a whole function.
E_VA, E_END = 0x0044dd90, 0x0044e445
E_RETS = (0x0044ded2, 0x0044e2c6, 0x0044e3c5, 0x0044e442)
# Its three callers: (call site, the bytes from the call through the `test eax,eax; je` that consume the result).
E_CALLERS = (
    (0x0044e522, bytes.fromhex('e869f8ffff' '85c0' '7452')),  # 0x0044e450 proximity handler, normal path
    (0x0044e540, bytes.fromhex('e84bf8ffff' '85c0' '7434')),  # 0x0044e450, the order-0x12 refresh path
    (0x00456c02, bytes.fromhex('e88971ffff' '85c0' '744c')),  # the avoid order's handler
)
E_OUT_INIT = (0x0044e460, bytes.fromhex('c744241800000000'))  # mov dword [esp+0x18], 0: the caller's &out local
E_CALLER_TRUE_PATH = (0x0044e52b, bytes.fromhex('814b4000100000'))  # or dword [ebx+0x40], 0x1000 (the true branch; no read of [esp+0x18])
# The snapshot walk's engine facts (ship_steering_sites.h (f) and (g), ship_avoidance_walk_core.h): the bucket table is
# `[sector+0x50]` and the heads `[table + 0xc*k]` (two dereferences) at the three readers; the child walk ends on the
# node whose first dword is 0; a child's offset is scaled by the parent's +0x70 under `[child+0x12c] & 0x40`.
WALK_FACTS = (
    ('bucket_table_order_loop', 0x00452b4a, bytes.fromhex('8d1440' '8b4508' '8b4850' '8b1c91' '833b00')),      # lea edx,[eax+eax*2]; mov eax,[ebp+8]; mov ecx,[eax+0x50]; mov ebx,[ecx+edx*4]; cmp [ebx],0
    ('bucket_table_collide_L0', 0x0045d275, bytes.fromhex('8b4250' '8b0401' '3938')),                          # mov eax,[edx+0x50]; mov eax,[ecx+eax]; cmp [eax],edi
    ('bucket_table_collide_pairs', 0x0045cbf4, bytes.fromhex('8b4b54' '8b5150' '8b442410' '8b3c10' '833f00')),  # mov ecx,[ebx+0x54]; mov edx,[ecx+0x50]; mov eax,[esp+0x10]; mov edi,[eax+edx]; cmp [edi],0
    ('child_list_head_sentinel', 0x0048aa46, bytes.fromhex('8b7b0c' '833f00')),                                # mov edi,[ebx+0xc]; cmp [edi],0
    ('child_list_next_sentinel', 0x00488248, bytes.fromhex('8b6d00' '837d0000' '0f85')),                       # mov ebp,[ebp]; cmp [ebp],0; jne
    ('child_offset_scale_flag', 0x004881a9, bytes.fromhex('f6852c01000040')),                                   # test byte [ebp+0x12c],0x40
    ('child_offset_scale_mul', 0x004881c4, bytes.fromhex('8b5070')),                                            # mov edx,[eax+0x70] (the parent's scale)
)
SITES = (
    H('ship_steer_leaf_yaw_pitch', 0x0044ccc0, LEAF_BYTES, 0x0044ccc0, 0x0044d014),
    H('ship_steer_leaf_roll_yaw', 0x0044c620, LEAF_BYTES, 0x0044c620, 0x0044c963),
    H('ship_steer_leaf_roll_pitch', 0x0044c970, LEAF_BYTES, 0x0044c970, 0x0044ccb5),
    H('ship_steer_loop_continue', B_VA, bytes.fromhex('8b1b833b00'), *B_WINDOW),
    H('ship_avoid_warn_test', E_VA, bytes.fromhex('558bec83e4f0'), E_VA, E_END),
)
RET_POP = {0x0044ccc0: 4, 0x0044c620: 4, 0x0044c970: 4, B_VA: 0, E_VA: 0x10}
# Getters: (va, first instruction bytes, the register that carries obj, the plain `ret` addresses).
GETTERS = {
    'max_speed': (0x00450980, bytes.fromhex('83ec0853568bf0'), 'eax', (0x00450b20,)),            # sub esp,8; push ebx; push esi; mov esi,eax
    'accel': (0x00450b80, bytes.fromhex('0fbf4148'), 'ecx', (0x00450ba7, 0x00450bbe, 0x00450c04)),  # movsx eax,word [ecx+0x48]
    'yaw_cap': (0x00450d60, bytes.fromhex('0fb75048'), 'eax', (0x00450db2, 0x00450dce)),         # movzx edx,word [eax+0x48]
    'pitch_cap': (0x00450cf0, bytes.fromhex('0fb75048'), 'eax', (0x00450d42, 0x00450d5e)),
    'roll_cap': (0x00450dd0, bytes.fromhex('0fb75048'), 'eax', (0x00450e22, 0x00450e3e)),
}
# MovePos call sites: (the register move before the call, the call) as bytes at the move's address.
GETTER_CALL_SITES = (
    ('yaw_cap', 0x00456519, bytes.fromhex('8bc3') + b'\xe8' + struct.pack('<i', 0x00450d60 - 0x00456520)),  # mov eax,ebx; call
    ('max_speed', 0x00456544, bytes.fromhex('8bc3') + b'\xe8' + struct.pack('<i', 0x00450980 - 0x0045654b)),
    ('accel', 0x00456550, bytes.fromhex('8bcb8bf8') + b'\xe8' + struct.pack('<i', 0x00450b80 - 0x00456559)),  # mov ecx,ebx; mov edi,eax; call
)
RECORD_MAX_SPEED_CASE = (0x0043a6a6, bytes.fromhex('8b15d46f60000fbfc969c9b80d00008b441144c3'))  # class-7 case: record +0x44, ret
# Sign-convention witnesses (ship_steering_sites.h (c)).
SIGN_WITNESSES = (
    ('yaw = atan2(-R, F)', 0x0044cede, bytes.fromhex('f7de' '89442410' '89742424' 'db442424' 'db442410' 'd9f3')),
    ('pitch = atan2(U, F\')', 0x0044cf95, bytes.fromhex('db442414' 'db442410' 'd9f3')),
    ('roll = atan2(t1, t0)', 0x0044c814, bytes.fromhex('89442414' 'd9c9' 'e8b1ed0d00' '89442410' 'db442410' 'db442414' 'd9f3')),
)
LARGE_PREDICATE, LARGE_JUMP_TABLE, LARGE_BYTE_TABLE = 0x0043fd00, 0x0043fd34, 0x0043fd3c
LARGE_PROLOGUE = bytes.fromhex('6683784807' '752a' '0fbf404a' '8b0dd46f6000' '69c0b80d0000' '8b440814' '83f80f' '7711')

_BRANCH = re.compile(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(j[a-z]+|call|loop[a-z]*)\s+(?:short\s+)?0x([0-9a-f]+)', re.M)


def text_branches(exe):
    run = subprocess.run([common.OBJDUMP, '-d', '-Mintel', '-j', '.text', str(exe)], check=True, capture_output=True, text=True, timeout=300)
    return [(int(m.group(1), 16), m.group(2), int(m.group(3), 16)) for m in _BRANCH.finditer(run.stdout)]


def raw_branch_targets(image, lo, hi):
    """Raw rel8/rel32 branch and call encodings over .text whose target is in [lo, hi) (a second, decoder-free witness)."""
    base, end = image.text_range()
    text = image.read(base, end - base)
    hits = []
    for i in range(len(text) - 6):
        b = text[i]
        src = base + i
        if b in (0xe8, 0xe9):
            tgt = (src + 5 + struct.unpack_from('<i', text, i + 1)[0]) & 0xffffffff
            if lo <= tgt < hi:
                hits.append((src, 'rel32', tgt))
        if b == 0x0f and 0x80 <= text[i + 1] <= 0x8f:
            tgt = (src + 6 + struct.unpack_from('<i', text, i + 2)[0]) & 0xffffffff
            if lo <= tgt < hi:
                hits.append((src, 'jcc32', tgt))
        if 0x70 <= b <= 0x7f or b == 0xeb:
            tgt = (src + 2 + struct.unpack_from('<b', text, i + 1)[0]) & 0xffffffff
            if lo <= tgt < hi:
                hits.append((src, 'rel8', tgt))
    return hits


def dwords_into(image, data, lo, hi):
    """Every dword in [lo, hi) found in any section at any byte alignment (jump tables live in .text, unaligned
    pointers in data): [(va, value)]. A value equal to a site's start is an incoming edge (the claim's jmp
    handles it); a value inside the span is a refusal."""
    hits = []
    for name, base, vsize, rp, rsize in image.sections:
        blob = data[rp:rp + rsize]
        for i in range(0, len(blob) - 3):
            v = struct.unpack_from('<I', blob, i)[0]
            if lo <= v < hi:
                hits.append((base + i, v))
    return hits


def decode(exe):
    out = {}
    for start, end in sorted({(s.function_start, s.function_end) for s in SITES} | {SLEW_REGION}):
        out[(start, end)] = common.parse_objdump(common.objdump_window(exe, start, end, timeout=60), start, end)
    return out


_SPEC_RE = re.compile(r'\{\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*\{([^}]*)\}\s*,\s*(\d+)\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*,\s*(\d+)\s*\}')


def header_specs(text):
    start = text.index('constexpr engine_patch::SiteSpec kSites[Count]')
    table = text[start:text.index('};', start)]
    e_start = text.index('constexpr engine_patch::SiteSpec kWarnSite')
    table += text[e_start:text.index(';', e_start)]
    rows = []
    for m in _SPEC_RE.finditer(table):
        rows.append({'name': m.group(1), 'va': int(m.group(2), 16),
                     'bytes': bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{1,2})', m.group(3))),
                     'length': int(m.group(4)), 'ret_pop': int(m.group(5), 0), 'rel32_offset': int(m.group(6))})
    return rows


def expected_specs():
    return [{'name': s.name, 'va': s.va, 'bytes': s.expected, 'length': len(s.expected), 'ret_pop': RET_POP[s.va], 'rel32_offset': 0}
            for s in SITES]


def _target(instruction):
    return common._is_direct_control(instruction)


def verify(exe=DEFAULT_EXE, whole_text=True):
    data = common.image_bytes(exe)
    report = {'result': 'FAIL', 'checks': {}, 'exe_info': exe_identity.info(data)}
    checks = report['checks']
    checks['exe_identity'] = {'ok': exe_identity.identity_ok(data), 'size': len(data)}
    image = common.Image(data)
    checks['pe'] = {'ok': image.image_base == common.IMAGE_BASE}
    decoded = decode(exe)
    # 1. the spans: bytes, whole instructions, no relative control, no interior branch inside the function/window
    sites = [common.inspect_site(image, s, decoded[(s.function_start, s.function_end)]) for s in SITES]
    checks['sites'] = {'ok': all(s['ok'] for s in sites), 'items': sites}
    # 2. the leaves end in `ret 4` and their functions decode gap-free; the B span is followed by `jne 0x00452b60`
    rets = {hex(ret): image.read(ret, 3) == bytes.fromhex('c20400') for _, _, ret in LEAVES}
    b_listing = decoded[B_WINDOW]
    jne = next((i for i in b_listing if i.va == B_JNE), None)
    checks['leaf_returns'] = {'ok': all(rets.values()), 'ret_4': rets}
    checks['b_jne'] = {'ok': jne is not None and jne.mnemonic == 'jne' and _target(jne) == B_TARGET,
                       'instruction': common._instruction_dict(jne) if jne else None}
    # the flags the stub writes are dead: the span is `mov` + `cmp` (cmp rewrites every arithmetic flag before the jne)
    span = [i for i in b_listing if B_VA <= i.va < B_JNE]
    checks['b_flags_dead'] = {'ok': [i.mnemonic for i in span] == ['mov', 'cmp'], 'span': [common._instruction_dict(i) for i in span]}
    # 3. the branch census into [B_VA, B_JNE): decoded .text branches, raw encodings and the order jump table
    decoded_hits = [(at, mn, tgt) for at, mn, tgt in text_branches(exe) if B_VA <= tgt < B_JNE] if whole_text else []
    raw_hits = raw_branch_targets(image, B_VA, B_JNE)
    jt_count = (ORDER_BYTE_TABLE - ORDER_JUMP_TABLE) // 4
    jump_table = [struct.unpack_from('<I', image.read(ORDER_JUMP_TABLE + 4 * i, 4))[0] for i in range(jt_count)]
    byte_table = list(image.read(ORDER_BYTE_TABLE, ORDER_BYTE_COUNT))
    jt_hits = [i for i, v in enumerate(jump_table) if B_VA <= v < B_JNE]
    # every dword equal to the site start anywhere in the image (every section, every alignment): the order jump
    # table's default entry and a second jump table at 0x004595fc
    dword_edges = [hex(at) for at, v in dwords_into(image, data, B_VA, B_VA + 1)]
    exact = all(t == B_VA for _, _, t in decoded_hits) and all(t == B_VA for _, _, t in raw_hits) and all(jump_table[i] == B_VA for i in jt_hits)
    checks['b_incoming'] = {'ok': exact and (not whole_text or len(decoded_hits) == 100) and len(raw_hits) == 100 and len(jt_hits) == 1
                            and len(dword_edges) == 2 and max(byte_table) == jt_count - 1,
                            'decoded_branches': len(decoded_hits), 'raw_encodings': len(raw_hits), 'order_jump_table_entries': len(jt_hits),
                            'jump_table_size': jt_count, 'dword_edges': dword_edges,
                            'total_incoming': len(decoded_hits) + len(dword_edges), 'all_exact_start': exact,
                            'fall_through_from': hex(B_VA - 3), 'kinds': sorted({mn for _, mn, _ in decoded_hits})}
    # 4. no direct branch anywhere in .text into any span interior; no data dword into any window
    interior = []
    if whole_text:
        for at, mn, tgt in text_branches(exe):
            for s in SITES:
                if s.va < tgt < s.end:
                    interior.append({'at': hex(at), 'mnemonic': mn, 'target': hex(tgt), 'site': s.name})
    raw_interior = [h for s in SITES for h in raw_branch_targets(image, s.va + 1, s.end)]
    checks['no_text_branch_into_span_interior'] = {'ok': not interior and not raw_interior, 'interior_branches': interior,
                                                   'raw_interior': [hex(h[0]) for h in raw_interior]}
    # no dword anywhere (every section, every alignment) points into a span interior; dwords equal to a span
    # start are incoming edges and are reported (the leaves: none expected; B: the two jump-table entries)
    interior_dwords = {s.name: [hex(at) for at, v in dwords_into(image, data, s.va + 1, s.end)] for s in SITES}
    start_dwords = {s.name: [hex(at) for at, v in dwords_into(image, data, s.va, s.va + 1)] for s in SITES}
    checks['no_data_dword_into_window'] = {'ok': not any(interior_dwords.values()) and all(not start_dwords[s.name] for s in SITES[:3] + SITES[4:]),
                                           'interior': interior_dwords, 'start_edges': start_dwords}
    # 5b. site E: the whole function decodes gap-free to its last `ret 0x10`, all four returns are `ret 0x10`, exactly the
    #     three callers reach it (rel32 calls in .text), each consumes EAX with `test eax,eax; je` right after the call
    #     (EFLAGS dead after the call), the proximity handler initialises its &out local to 0 and its true path sets the
    #     warn bit without reading that local: a false return needs no &out write (ship_steering_sites.h (e))
    e_listing = decoded[(E_VA, E_END)]
    e_rets = {hex(r): image.read(r, 3) == bytes.fromhex('c21000') for r in E_RETS}
    e_calls = sorted(at for at, mn, tgt in raw_branch_targets(image, E_VA, E_VA + 1) if mn == 'rel32' and image.read(at, 1) == b'\xe8')
    e_callers = {hex(at): image.read(at, len(b)) == b for at, b in E_CALLERS}
    e_out = image.read(E_OUT_INIT[0], len(E_OUT_INIT[1])) == E_OUT_INIT[1] and image.read(E_CALLER_TRUE_PATH[0], len(E_CALLER_TRUE_PATH[1])) == E_CALLER_TRUE_PATH[1]
    # no instruction of the proximity handler after the call reads [esp+0x18] (the &out local) except the lea that passes its address
    handler = common.parse_objdump(common.objdump_window(exe, 0x0044e450, 0x0044e584, timeout=60), 0x0044e450, 0x0044e584)
    out_reads = [common._instruction_dict(i) for i in handler if 'esp+0x18]' in i.operands.replace(' ', '') and i.mnemonic not in ('lea',)
                 and i.va != E_OUT_INIT[0]]
    facts = {name: image.read(at, len(b)) == b for name, at, b in WALK_FACTS}
    checks['snapshot_walk_facts'] = {'ok': all(facts.values()), 'facts': facts,
                                     'pinned': 'table = [sector+0x50], head = [table + 0xc*k]; child walk ends on the 0-dword sentinel; offsets scaled under [child+0x12c] & 0x40'}
    checks['warn_test_site'] = {'ok': bool(e_listing) and e_listing[-1].va == E_RETS[-1] and all(e_rets.values()) and e_calls == sorted(at for at, _ in E_CALLERS)
                               and all(e_callers.values()) and e_out and not out_reads,
                               'rets': e_rets, 'callers': [hex(a) for a in e_calls], 'callers_consume_eax_with_test_je': e_callers,
                               'out_local_initialised_and_unread': e_out and not out_reads, 'out_reads': out_reads,
                               'pinned': 'false return: no &out write needed; EFLAGS, ECX, EDX dead after the call at all three callers'}
    # 5. the header's SiteSpec rows equal the derived rows (name, va, bytes, length, ret_pop, rel32)
    actual = header_specs(SITES_HEADER.read_text())
    expected = expected_specs()
    ser = lambda r: {**r, 'va': hex(r['va']), 'bytes': r['bytes'].hex()}
    checks['header_specs'] = {'ok': actual == expected, 'expected': [ser(r) for r in expected], 'actual': [ser(r) for r in actual]}
    # 6. getter conventions: first instruction (the obj register), plain `ret`s, the MovePos call sites
    getters = {}
    for name, (va, first, reg, rets_) in GETTERS.items():
        head = image.read(va, len(first)) == first
        plain = {hex(r): image.read(r, 1) == b'\xc3' for r in rets_}
        getters[name] = {'va': hex(va), 'obj_register': reg, 'first_instruction_ok': head, 'plain_ret': plain,
                         'ok': head and all(plain.values())}
    call_sites = {}
    for name, at, expect in GETTER_CALL_SITES:
        call_sites[hex(at)] = {'getter': name, 'ok': image.read(at, len(expect)) == expect}
    record_case = image.read(RECORD_MAX_SPEED_CASE[0], len(RECORD_MAX_SPEED_CASE[1])) == RECORD_MAX_SPEED_CASE[1]
    checks['getter_conventions'] = {'ok': all(g['ok'] for g in getters.values()) and all(c['ok'] for c in call_sites.values()) and record_case,
                                    'getters': getters, 'call_sites': call_sites, 'record_max_speed_case_0x0043a6a6': record_case,
                                    'header_pinned': 'EAX = obj for max speed and the three rate caps, ECX = obj for the acceleration; no stack argument; result EAX'}
    # 7. slew order: the slew region is a second bucket loop entered by fall-through after the order loop's bucket
    #    advance; no branch from outside the region targets it; it ends with the back edge to its loop head
    slew = decoded[SLEW_REGION]
    region_lo, region_hi = SLEW_REGION
    outside = [(at, mn, tgt) for at, mn, tgt in (text_branches(exe) if whole_text else []) if region_lo < tgt < region_hi and not (region_lo <= at < region_hi)]
    advance = next((i for i in b_listing if i.va == 0x004593fd), None)
    back_edge = next((i for i in slew if i.va == 0x004595b9), None)
    outer_edge = next((i for i in slew if i.va == 0x004595cf), None)
    slew_call = next((i for i in slew if i.va == SLEW_CALL), None)
    pass_ret = next((i for i in slew if i.va == PASS_RET), None)
    checks['slew_after_b'] = {'ok': not outside and advance is not None and advance.mnemonic == 'jl' and _target(advance) == 0x00452b42
                              and advance.end == region_lo and back_edge is not None and back_edge.mnemonic == 'jne'
                              and _target(back_edge) == SLEW_LOOP_HEAD and outer_edge is not None and outer_edge.mnemonic == 'jne'
                              and _target(outer_edge) == SLEW_BUCKET_HEAD and slew_call is not None and slew_call.mnemonic == 'call'
                              and _target(slew_call) == GETTERS['max_speed'][0] and pass_ret is not None and pass_ret.raw == bytes.fromhex('c20400'),
                              'branches_from_outside': [hex(a) for a, _, _ in outside], 'bucket_advance': common._instruction_dict(advance) if advance else None,
                              'list_back_edge': common._instruction_dict(back_edge) if back_edge else None,
                              'bucket_back_edge': common._instruction_dict(outer_edge) if outer_edge else None,
                              'pass_ret_4': pass_ret is not None and pass_ret.raw == bytes.fromhex('c20400'),
                              'fact': 'site B runs before the slew for every object in the same frame (zero-frame clamp latency)'}
    # 8. sign-convention witnesses and the large predicate's mask
    witnesses = {label: image.read(at, len(b)) == b for label, at, b in SIGN_WITNESSES}
    checks['sign_witnesses'] = {'ok': all(witnesses.values()), 'witnesses': witnesses}
    jt = [struct.unpack_from('<I', image.read(LARGE_JUMP_TABLE + 4 * i, 4))[0] for i in range(2)]
    table = list(image.read(LARGE_BYTE_TABLE, 16))
    true_index = next((i for i, v in enumerate(jt) if image.read(v, 2) == bytes.fromhex('b001')), None)
    mask = sum(1 << code for code, idx in enumerate(table) if idx == true_index) if true_index is not None else None
    core_mask = int(re.search(r'kLargeMask\s*=\s*(0x[0-9a-fA-F]+)', CORE_HEADER.read_text()).group(1), 16)
    checks['large_predicate_mask'] = {'ok': image.read(LARGE_PREDICATE, len(LARGE_PROLOGUE)) == LARGE_PROLOGUE and mask == core_mask,
                                      'derived_mask': hex(mask) if mask is not None else None, 'core_mask': hex(core_mask), 'byte_table': table}
    report['result'] = 'PASS' if all(c['ok'] for c in checks.values()) else 'FAIL'
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    p.add_argument('--json', action='store_true')
    a = p.parse_args()
    if not a.exe.is_file():
        print(json.dumps({'result': 'SKIP', 'reason': f'{a.exe} not found'}))
        return 2
    r = verify(a.exe)
    if a.json:
        print(json.dumps(r, indent=2))
    else:
        summary = {'result': r['result'], 'exe': str(a.exe), 'checks': len(r['checks']), **{k: v['ok'] for k, v in r['checks'].items()},
                   'b_incoming': {k: v for k, v in r['checks']['b_incoming'].items() if k != 'kinds'},
                   'warn_test': {k: v for k, v in r['checks']['warn_test_site'].items() if k in ('ok', 'callers', 'out_local_initialised_and_unread')},
                   'large_mask': r['checks']['large_predicate_mask']['derived_mask'],
                   'sha256_info': r['exe_info'].get('sha256') if isinstance(r['exe_info'], dict) else None}
        print(json.dumps(summary, indent=2))
    return 0 if r['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
