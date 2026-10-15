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
the core's mask. Step 3 (the gate approach): the three gate sites P2 0x00454b91 / P4 0x00454700 (`mov [ebx+0x14],eax`
| `mov [ebx+0x10],eax`, 6 B) and P5 0x0045526f (`mov eax,0x1967e8`, 5 B) decoded in their handlers' neighbourhoods,
the register facts before and after each window (EAX = 0 from `xor eax,eax`, EBX = obj, ECX = flight; the t / t^2
sequence before P5 and the `imul; idiv` the stub skips), the resume addresses as instruction starts, the capital
predicate 0x0043fd50's byte table against the core's mask, and the order-4 / order-5 gate facts of ship-navigation-ai.md
section 10.3 and 10.5 the lead-in relies on.

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
    # the engine's obstacle list (ship_steering_sites.h kFlightObstacle*; amendment 13's ship_avoid_engine_warn row): L0 clears
    # f+0x1e0, the warn caller appends B at f+0x1e4[n] and d at f+0x1f8[n] while n < 5 (capstone listing 2026-10-12)
    ('obstacle_count_clear_L0', 0x0045d298, bytes.fromhex('89bbe0010000')),                                      # mov [ebx+0x1e0],edi
    ('obstacle_list_append', 0x0044e54c, bytes.fromhex('8b81e0010000' '83f805' '7d26' '89ac81e4010000')),         # mov eax,[ecx+0x1e0]; cmp eax,5; jge; mov [ecx+eax*4+0x1e4],ebp
    ('obstacle_dist_append', 0x0044e565, bytes.fromhex('898c82f8010000')),                                       # mov [edx+eax*4+0x1f8],ecx
    ('bucket_table_collide_pairs', 0x0045cbf4, bytes.fromhex('8b4b54' '8b5150' '8b442410' '8b3c10' '833f00')),  # mov ecx,[ebx+0x54]; mov edx,[ecx+0x50]; mov eax,[esp+0x10]; mov edi,[eax+edx]; cmp [edi],0
    ('child_list_head_sentinel', 0x0048aa46, bytes.fromhex('8b7b0c' '833f00')),                                # mov edi,[ebx+0xc]; cmp [edi],0
    ('child_list_next_sentinel', 0x00488248, bytes.fromhex('8b6d00' '837d0000' '0f85')),                       # mov ebp,[ebp]; cmp [ebp],0; jne
    ('child_offset_scale_flag', 0x004881a9, bytes.fromhex('f6852c01000040')),                                   # test byte [ebp+0x12c],0x40
    ('child_offset_scale_mul', 0x004881c4, bytes.fromhex('8b5070')),                                            # mov edx,[eax+0x70] (the parent's scale)
)
# Step 3: the gate sites (ship_steering_sites.h (h)-(k)), their decode windows and the facts around them.
STOP_BYTES = bytes.fromhex('894314894310')
P2_VA, P4_VA, P5_VA = 0x00454b91, 0x00454700, 0x0045526f
P2_WINDOW, P4_WINDOW, P5_WINDOW = (0x00454b74, 0x00454ba1), (0x004546f1, 0x00454728), (0x0045523c, 0x004552a0)
P2_RESUME, P4_RESUME, P5_RESUME = 0x00454b97, 0x00454706, 0x00455281
# Stage B (gate-traffic-controller.md B.1, D.12; ship-navigation-ai.md 10.11 Q4): the P2 handler zeroes the three turn remainders
# obj+0x30/+0x34/+0x38 on a kept stage-B hand-over (EBX = obj at P2: `mov esi,ebx` in p2_registers). Pinned: they are the remainders
# the autopilot integrator consumes, order 0 under autopilot writes none of them, 0x1f's aligned first frame zeroes the roll only,
# and P2's window writes the speeds only (nothing between P2 and the integrator rewrites them).
P2_REMAINDER_FACTS = (
    ('integrator_roll_read', 0x00459980, bytes.fromhex('8b4338' '33ff' '3bc7' '7e7a')),
    ('integrator_roll_write', 0x004599f4, bytes.fromhex('894338')),
    ('integrator_pitch_write', 0x00459b0e, bytes.fromhex('894334')),
    ('integrator_yaw_write', 0x00459c2c, bytes.fromhex('894330')),
    ('order0_autopilot_no_remainder', 0x00458f8e, bytes.fromhex('f7434000000800' '7416' '33ff' 'f6434404' '7503' '897b14' '89be24020000' 'e933010000')),
    ('order0_tail_no_remainder', 0x004590e0, bytes.fromhex('8b5350' '39ba18020000')),  # the jmp +0x133 target: f+0x218 test
    ('rotate_aligned_test', 0x004592d9, bytes.fromhex('3d70fd0000' '0f8c7c000000')),       # 0x1f: nose within 8.11 deg ...
    ('rotate_no_roll_target', 0x004592e4, bytes.fromhex('8b8748020000' '3d00000080' '751d')),  # ... no roll target ...
    ('rotate_first_frame_roll_only', 0x004592f4, bytes.fromhex('33c0' '894338' '6a01')),
    ('p2_speeds_then_wake', 0x00454b91, bytes.fromhex('894314' '894310' 'e8c4b90000')),
)
# Step 4: the capital alignment facts (SA_RotateToAngles and the 0x1f handler). Site W (0x00465bac, the SA_WarpObjectIn exit spread)
# was deleted with stage B on 2026-10-15 (inert under the traffic controller since D.6) and is no longer claimed or checked.
GATE_FACTS = (
    # P2: ecx = flight; eax = 0; the result word; push 1; esi = obj; then the window; then the wake call and the jump to the loop continue
    ('p2_registers', 0x00454b81, bytes.fromhex('8b4b50' '33c0' '66898158020000' '6a01' '8bf3')),
    ('p2_window', P2_VA, STOP_BYTES),
    ('p2_resume_call_wake', P2_RESUME, bytes.fromhex('e8c4b90000' 'e93c480000')),  # call 0x00460560; jmp 0x004593dd
    # P4: ecx = flight; eax = 0; the window; then flight+0x250 = -1 (the resume)
    ('p4_registers', 0x004546fb, bytes.fromhex('8b4b50' '33c0')),
    ('p4_window', P4_VA, STOP_BYTES),
    ('p4_resume', P4_RESUME, bytes.fromhex('c78150020000ffffffff')),
    # P5: ship |= 0x400 (ebx = obj); eax = t = ecx + 900; ecx = t^2; [esp+0x10] = t^2; the window; the engine's
    # imul/idiv the stub skips; the resume `mov edx,[ebx+0x50]`
    ('p5_registers', 0x00455259, bytes.fromhex('814b4000040000' '8d8184030000' '8bc8' '0fafc8' '894c2410')),
    ('p5_window', P5_VA, bytes.fromhex('b8e8671900')),
    ('p5_skipped_imul_idiv', 0x00455274, bytes.fromhex('8b542410' 'b940420f00' 'f7ea' 'f7f9')),
    ('p5_resume', P5_RESUME, bytes.fromhex('8b5350')),
    # the order-4 gate facts the lead-in relies on (section 10.3): target class 0x12 test, the arrival radius 3 r / 4,
    # the approach vector (0, -0.8, -1.0) written for every class-0x12 object, the point read from flight+0x240
    ('order4_target_class_0x12', 0x00454b09, bytes.fromhex('0fb74748' '663d1200')),
    ('order4_arrival_radius_3r_4', 0x00454b1a, bytes.fromhex('8b87a4000000' '8d0440' '99' '83e203' '03c2' 'c1f802')),
    ('gate_approach_vector', 0x00440e75, bytes.fromhex('8998a0010000' '8b4d50' 'c781a40100003433ffff' '8b5550')),  # [gate+0x50]+0x1a0 = 0, +0x1a4 = -0.8 (0xffff3334)
    ('order4_point_from_flight_0x240', 0x00454a20, bytes.fromhex('83b85002000000')),
    # the order-5 facts the crossing heading targets (section 10.5): the 0.95 alignment test, the 500 units/s creep
    ('order5_aligned_0x95', 0x00454897, bytes.fromhex('3d33f30000' '7d1a')),
    ('order5_creep_500', 0x004548a5, bytes.fromhex('c74314f4010000')),
    # the capital alignment (ship_steering_sites.h (m)): the angles written by SA_RotateToAngles, read by the handler, the
    # 0xfd70 tolerance (untouched)
    ('rotate_angles_written', 0x00465404, bytes.fromhex('8b5306' '899140020000' '8b530b' '899144020000')),
    ('rotate_roll_read', 0x00459211, bytes.fromhex('8b8f48020000')),
    ('rotate_tolerance_0xfd70', 0x004592d9, bytes.fromhex('3d70fd0000')),
)
# The gate traffic controller, stage A (ship_steering_sites.h (n); ship-navigation-ai.md section 10.10): the launch 0x00444170
# (stdcall ship, gate; `ret 8` at 0x00444a4f, a jump table from 0x00444a54) and its window 0x00444606 before the copy P -> f+0x240.
T_VA, T_WINDOW, T_RESUME = 0x00444606, (0x00444170, 0x00444a52), 0x0044460c
T_ESP_RESTORE, T_ESI_REWRITE, T_EDI_READ = 0x00444637, 0x0044466f, 0x00444651
T_EDI_SETS = (0x004444ce, 0x00444575)
ARRIVAL_FACTS = (
    ('t_entry_args', 0x0044417a, bytes.fromhex('8b5d08' '56' '8b750c' '66837e4812')),  # mov ebx,[ebp+8] (ship); push esi; mov esi,[ebp+0xc] (gate); cmp [esi+0x48],0x12
    ('t_edi_gate_noncapital', 0x004444ce, bytes.fromhex('8b7d0c')),                     # mov edi,[ebp+0xc]
    ('t_edi_gate_capital', 0x00444575, bytes.fromhex('8b7d0c')),
    ('t_p_written_noncapital', 0x004444ec, bytes.fromhex('8b4370' '83c030' '8908')),    # mov eax,[ebx+0x70]; add eax,0x30; mov [eax],ecx (P)
    ('t_p_written_capital', 0x0044457b, bytes.fromhex('8b4370' '83c030' '8908')),
    ('t_deadline_before_window', 0x00444600, bytes.fromhex('899014020000')),            # mov [eax+0x214],edx (now + 900)
    ('t_window', T_VA, bytes.fromhex('8b43708b4b50')),                                  # mov eax,[ebx+0x70]; mov ecx,[ebx+0x50]
    ('t_resume_copy', T_RESUME, bytes.fromhex('8b5030' '83c030' '899140020000')),       # mov edx,[eax+0x30]; add eax,0x30; mov [ecx+0x240],edx
    ('t_or_0x400_add_esp_8', 0x00444630, bytes.fromhex('814b4000040000' '83c408')),     # or [ebx+0x40],0x400; add esp,8
    ('t_edi_read_gate_phys', T_EDI_READ, bytes.fromhex('8b4770')),                     # mov eax,[edi+0x70]
    ('t_esi_rewrite', T_ESI_REWRITE, bytes.fromhex('8d742418')),                       # lea esi,[esp+0x18]
    ('t_wake', 0x00444a32, bytes.fromhex('6a0151ffd0')),                               # push 1; push ecx; call eax (the KC wake, after the site)
)
FLAG_READERS = re.compile(r'^(j(?!mp)\w*|set\w+|cmov\w+|adc|sbb|pushf\w*|lahf|rcl|rcr|into|loop\w*)$')
FLAG_WRITERS = re.compile(r'^(add|sub|and|or|xor|cmp|test|inc|dec|neg|shl|shr|sar|sal|imul|mul|bt\w*|popf\w*|sahf)$')
CAPITAL_PREDICATE, CAPITAL_JUMP_TABLE, CAPITAL_BYTE_TABLE, CAPITAL_TABLE_LEN = 0x0043fd50, 0x0043fd84, 0x0043fd8c, 14
CAPITAL_PROLOGUE = bytes.fromhex('6683784807' '752a' '0fbf404a' '8b0dd46f6000' '69c0b80d0000' '8b440814' '83f80d' '7711')
SITES = (
    H('ship_steer_leaf_yaw_pitch', 0x0044ccc0, LEAF_BYTES, 0x0044ccc0, 0x0044d014),
    H('ship_steer_leaf_roll_yaw', 0x0044c620, LEAF_BYTES, 0x0044c620, 0x0044c963),
    H('ship_steer_leaf_roll_pitch', 0x0044c970, LEAF_BYTES, 0x0044c970, 0x0044ccb5),
    H('ship_steer_loop_continue', B_VA, bytes.fromhex('8b1b833b00'), *B_WINDOW),
    H('ship_avoid_warn_test', E_VA, bytes.fromhex('558bec83e4f0'), E_VA, E_END),
    H('ship_gate_stop_a', P2_VA, STOP_BYTES, *P2_WINDOW),
    H('ship_gate_stop_b', P4_VA, STOP_BYTES, *P4_WINDOW),
    H('ship_gate_launch', P5_VA, bytes.fromhex('b8e8671900'), *P5_WINDOW),
    H('ship_gate_arrival_launch', T_VA, bytes.fromhex('8b43708b4b50'), *T_WINDOW),
)
RET_POP = {0x0044ccc0: 4, 0x0044c620: 4, 0x0044c970: 4, B_VA: 0, E_VA: 0x10, P2_VA: 0, P4_VA: 0, P5_VA: 0, T_VA: 0}
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


_STARTS = re.compile(r'^\s*([0-9a-f]+):\s+[0-9a-f]{2}', re.M)
_listing_cache = {}


def text_listing(exe):
    if exe not in _listing_cache:
        run = subprocess.run([common.OBJDUMP, '-d', '-Mintel', '-j', '.text', str(exe)], check=True, capture_output=True, text=True, timeout=300)
        _listing_cache[exe] = run.stdout
    return _listing_cache[exe]


def text_branches(exe):
    return [(int(m.group(1), 16), m.group(2), int(m.group(3), 16)) for m in _BRANCH.finditer(text_listing(exe))]


def text_instruction_starts(exe):
    return {int(m.group(1), 16) for m in _STARTS.finditer(text_listing(exe))}


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
    g_start = text.index('constexpr engine_patch::SiteSpec kGateSites[GateCount]')
    table += text[g_start:text.index('};', g_start)]
    t_start = text.index('constexpr engine_patch::SiteSpec kArrivalSite')
    table += text[t_start:text.index(';', t_start)]
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
    # the decoder-free sweep over-approximates: a hit whose source byte is not an instruction start of the linear
    # .text decode is an operand byte (0x00454c08: the 0x70 displacement of `mov ecx,[ebx+0x70]` read as `jo` into P2's
    # window) and is reported, not counted
    raw_all = [h for s in SITES for h in raw_branch_targets(image, s.va + 1, s.end)]
    starts = text_instruction_starts(exe) if whole_text else None

    def is_instruction_start(va):
        if starts is not None:
            return va in starts
        # the fast mode: decode a window around the source (the decoder resynchronises well within 48 bytes)
        listing = common.parse_objdump(common.objdump_window(exe, va - 48, va + 16, timeout=60), va - 48, va + 16)
        return any(i.va == va for i in listing)

    raw_interior = [h for h in raw_all if is_instruction_start(h[0])]
    checks['no_text_branch_into_span_interior'] = {'ok': not interior and not raw_interior, 'interior_branches': interior,
                                                   'raw_interior': [hex(h[0]) for h in raw_interior],
                                                   'raw_operand_bytes_ignored': [hex(h[0]) for h in raw_all if h not in raw_interior]}
    # no dword anywhere (every section, every alignment) points into a span interior; dwords equal to a span
    # start are incoming edges and are reported (the leaves: none expected; B: the two jump-table entries)
    interior_dwords = {s.name: [hex(at) for at, v in dwords_into(image, data, s.va + 1, s.end)] for s in SITES}
    start_dwords = {s.name: [hex(at) for at, v in dwords_into(image, data, s.va, s.va + 1)] for s in SITES}
    checks['no_data_dword_into_window'] = {'ok': not any(interior_dwords.values()) and all(not start_dwords[s.name] for s in SITES[:3] + SITES[4:]),
                                           'interior': interior_dwords, 'start_edges': start_dwords}
    # 4b. step 3: the gate windows are whole instructions in their handlers (check 1 above covers the span and the interior
    #     branches), the register facts before and after each window hold, every resume address is an instruction start,
    #     the window of P4 lies before the join 0x00454710, and no branch anywhere targets a window interior or a resume
    #     other than as an instruction start (the join into 0x00454710 is after P4's resume)
    gate_facts = {name: image.read(at, len(b)) == b for name, at, b in GATE_FACTS}
    p2_rem = {name: image.read(at, len(b)) == b for name, at, b in P2_REMAINDER_FACTS}
    # review F10: order 0's `jmp rel32` (e9 33 01 00 00 at 0x00458fa8) lands on the tail pinned above (0x004590e0), and the aligned
    # first frame of 0x1f falls through to the roll-only zeroing (the jl over it is the not-aligned path)
    jmp_at = 0x00458fa8
    jmp_target = jmp_at + 5 + int.from_bytes(image.read(jmp_at + 1, 4), 'little', signed=True) if image.read(jmp_at, 1) == b'\xe9' else None
    p2_rem['order0_jmp_targets_the_tail'] = jmp_target == 0x004590e0
    jne_at = 0x004592ef  # the `jne +0x1d` after the roll-target test: falls through to 0x004592f1 (the roll-only path's first bytes)
    p2_rem['rotate_no_roll_target_falls_into_roll_only'] = image.read(jne_at, 2) == bytes.fromhex('751d')
    checks['p2_remainder_contract'] = {'ok': all(p2_rem.values()) and gate_facts.get('p2_registers', False), 'facts': p2_rem,
                                       'pinned': 'the P2 handler may write [obj+0x30/+0x34/+0x38] (EBX = ESI = obj): the integrator\'s three '
                                                 'remainders; order 0 and a first-frame 0x1f leave yaw and pitch, so a kept stage-B hand-over '
                                                 'zeroes all three (ship_steering_sites.h (h))'}
    resumes = {}
    for name, (lo, hi), resume, va in (('p2', P2_WINDOW, P2_RESUME, P2_VA), ('p4', P4_WINDOW, P4_RESUME, P4_VA), ('p5', P5_WINDOW, P5_RESUME, P5_VA)):
        listing = decoded[(lo, hi)]
        starts = {i.va for i in listing}
        span = [i for i in listing if va <= i.va < resume]
        resumes[name] = {'resume_is_instruction_start': resume in starts, 'window_instructions': [common._instruction_dict(i) for i in span],
                         'window_ends_at_resume': bool(span) and span[-1].end == resume}
    p4_join_after = all(not (P4_VA < tgt < P4_RESUME) for _, _, tgt in (text_branches(exe) if whole_text else []))
    p4_join = [hex(at) for at, _, tgt in (text_branches(exe) if whole_text else []) if tgt == 0x00454710]
    checks['gate_sites'] = {'ok': all(gate_facts.values()) and all(r['resume_is_instruction_start'] and r['window_ends_at_resume'] for r in resumes.values())
                            and all([i['mnemonic'] for i in r['window_instructions']] == (['mov', 'mov'] if n != 'p5' else ['mov', 'mov', 'mov', 'imul', 'idiv'])
                                    for n, r in resumes.items())
                            and p4_join_after and (not whole_text or p4_join == ['0x459191']),
                            'facts': gate_facts, 'resumes': resumes, 'join_0x00454710_from': p4_join,
                            'pinned': 'P2/P4: EAX = 0, EBX = obj, ECX = flight, flags dead; P5: EAX = t ms, ECX = [esp+0x10] = t^2, EBX = obj; resume 0x00454b97 / 0x00454706 / 0x00455281'}
    # 5a. the gate traffic controller's launch window (ship_steering_sites.h (n)): the two whole instructions at instruction starts of
    #      the launch's gap-free decode (check 1); the stub contract: EDI = the gate on both branches and not written again before the
    #      window or before its read at 0x00444651; P written to phys+0x30 on both branches before the window; EAX/ECX rewritten by the
    #      replayed window, EDX by the resume; ESI rewritten (lea) at 0x0044466f with no read of it after the window before that; the
    #      flags dead (the first flag access after the window is a write, `add eax,0x30` at 0x0044460f); ESP untouched between the window
    #      and `add esp,8` at 0x00444637 (no push, pop or call: the two cdecl arguments stay where they are); no x87 instruction from the
    #      window to 0x00444637; the function returns with `ret 8`
    t_listing = decoded[T_WINDOW]
    t_starts = {i.va for i in t_listing}
    t_facts = {name: image.read(at, len(b)) == b for name, at, b in ARRIVAL_FACTS}
    t_span = [i for i in t_listing if T_VA <= i.va < T_RESUME]
    t_after = [i for i in t_listing if T_RESUME <= i.va < T_ESP_RESTORE]
    first_flag = next((k for k, i in enumerate(t_after) if FLAG_READERS.match(i.mnemonic) or FLAG_WRITERS.match(i.mnemonic)), None)
    t_flags_dead = first_flag is not None and bool(FLAG_WRITERS.match(t_after[first_flag].mnemonic)) and t_after[first_flag].va == 0x0044460f

    def writes(i, reg):
        ops = i.operands.replace(' ', '')
        return (ops.startswith(reg + ',') and i.mnemonic not in ('cmp', 'test', 'push')) or (i.mnemonic == 'pop' and ops == reg) or \
            (i.mnemonic == 'xchg' and reg in ops.split(','))

    # the two paths: the non-capital branch 0x004444ce .. `jmp 0x00444590` at 0x00444501; the capital branch from 0x00444506 sets EDI at
    # 0x00444575 and falls through to 0x00444590; both run straight to the window and on to the read at 0x00444651
    t_jmp = next((i for i in t_listing if i.va == 0x00444501), None)
    t_paths_ok = t_jmp is not None and t_jmp.mnemonic == 'jmp' and _target(t_jmp) == 0x00444590 and 0x00444506 in t_starts
    t_edi_writes = [common._instruction_dict(i) for i in t_listing
                    if (T_EDI_SETS[0] < i.va < 0x00444506 or T_EDI_SETS[1] < i.va < T_EDI_READ) and writes(i, 'edi')]
    t_esi_reads = [common._instruction_dict(i) for i in t_listing if T_VA <= i.va < T_ESI_REWRITE and 'esi' in i.operands and not writes(i, 'esi')]
    t_stack = [common._instruction_dict(i) for i in t_after if i.mnemonic in ('push', 'pop', 'call', 'pushf', 'popf', 'pushfd', 'popfd') or
               'esp' in i.operands.split(',')[0]]
    t_x87 = [common._instruction_dict(i) for i in t_listing if T_VA <= i.va < T_ESP_RESTORE and i.mnemonic.startswith('f')]
    t_restore = next((i for i in t_listing if i.va == T_ESP_RESTORE), None)
    t_ret = image.read(0x00444a4f, 3) == bytes.fromhex('c20800')
    checks['arrival_launch_site'] = {
        'ok': all(t_facts.values()) and [i.mnemonic for i in t_span] == ['mov', 'mov'] and bool(t_span) and t_span[-1].end == T_RESUME
              and T_VA in t_starts and T_RESUME in t_starts and all(va in t_starts for va in T_EDI_SETS) and t_paths_ok and not t_edi_writes
              and not t_esi_reads
              and t_flags_dead and not t_stack and not t_x87 and t_restore is not None and t_restore.mnemonic == 'add'
              and t_restore.operands.replace(' ', '') == 'esp,0x8' and t_ret,
        'facts': t_facts, 'window_instructions': [common._instruction_dict(i) for i in t_span], 'edi_writes_between': t_edi_writes,
        'esi_reads_before_rewrite': t_esi_reads, 'flags_dead_first_access': common._instruction_dict(t_after[first_flag]) if first_flag is not None else None,
        'stack_ops_before_add_esp_8': t_stack, 'x87_between': t_x87, 'ret_8': t_ret,
        'pinned': 'EBX = ship, EDI = gate, EBP = frame; P at phys+0x30 already written; EAX/ECX/EDX/ESI dead; flags dead; '
                  'ESP = frame - 8 with two cdecl arguments live until 0x00444637; the tail replays the window and jumps to 0x0044460c'}
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
    # 9. the capital predicate 0x0043fd50 (step 3: the gate handlers keep capitals on the vanilla path) yields the core's mask
    cjt = [struct.unpack_from('<I', image.read(CAPITAL_JUMP_TABLE + 4 * i, 4))[0] for i in range(2)]
    ctable = list(image.read(CAPITAL_BYTE_TABLE, CAPITAL_TABLE_LEN))
    ctrue = next((i for i, v in enumerate(cjt) if image.read(v, 2) == bytes.fromhex('b001')), None)
    cmask = sum(1 << code for code, idx in enumerate(ctable) if idx == ctrue) if ctrue is not None else None
    capital_mask = int(re.search(r'kCapitalMask\s*=\s*(0x[0-9a-fA-F]+)', CORE_HEADER.read_text()).group(1), 16)
    checks['capital_predicate_mask'] = {'ok': image.read(CAPITAL_PREDICATE, len(CAPITAL_PROLOGUE)) == CAPITAL_PROLOGUE and cmask == capital_mask,
                                        'derived_mask': hex(cmask) if cmask is not None else None, 'core_mask': hex(capital_mask), 'byte_table': ctable}
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
                   'capital_mask': r['checks']['capital_predicate_mask']['derived_mask'],
                   'gate_join_0x00454710_from': r['checks']['gate_sites']['join_0x00454710_from'],
                   'sha256_info': r['exe_info'].get('sha256') if isinstance(r['exe_info'], dict) else None}
        print(json.dumps(summary, indent=2))
    return 0 if r['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
