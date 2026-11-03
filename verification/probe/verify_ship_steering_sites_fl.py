#!/usr/bin/env python3
"""Read-only qualification of the FL base ship-steering sites in X3FL.exe (src/engine/fl/ship_steering_sites_fl.h,
ship_steering_fl.cpp; docs/reverse-engineering/fl/ship-steering.md, "Port (2026-11-03)").

Checks on the installed exe (default /mnt/c/X3FL/X3FL.exe; every value an RVA, preferred base 0x400000):
- the four claimed spans (bytes, Capstone lengths, no relative flow, no HIGHLOW, no direct edge or relocated pointer into an
  interior) and their continuations: A0 yaw/pitch 0x056450 (7 B), A1 roll/yaw 0x055cd0 (7 B), A2 roll/pitch 0x056090 (7 B),
  B the object-loop continue 0x062c72 (9 B, then `cmp dword [ebx],0; jne 0x45c911`); the unsafe 13-byte candidate from
  0x062c6e is refused (0x062c72 is a branch target); B's one jump-table entry lands on its first byte;
- the leaf ABI: A0 reads the target at entry [esp+4] (`mov eax,[esp+0x14]` after `sub esp,0x10`), A1/A2 `mov ebx,ecx` (obj in
  ECX) and `mov eax,[esp+4]`; each leaf writes obj+0x30/+0x34/+0x38 and returns `ret 4`; 16/2/2 direct callers;
- the five getters (max speed 0x05a450, acceleration 0x05a610, pitch/yaw/roll caps 0x05a7d0/0x05a830/0x05a890): obj in ECX,
  integer EAX, plain `ret`; their transitive graph has no x87, no SIMD, no indirect call (switch tables followed), and its only
  global stores are the timed max-speed factor's expiry flight+0x914/+0x918;
- the bindings: TShips slot 0x1a83e4 (record stride 0xdb8, class code +0x14, rotation acceleration +0xd0, the caps +0xc/+8/+0x10,
  acceleration +0x48, max speed +0x44), clock slot 0x1a83b0 (game time +0x708, frame step +0x704), flight tuning +0x270/+0x274,
  the rotation plant flight+0x2b0 x record+0xd0 / 100, the speed slew reading desired +0x14 and writing current +0x10, the order
  switch on flight+0x258 (bound 0x24) with every steered order handled, the large mask 0xe91d and capital mask 0x211d;
- every identity window of the header (bytes with relocated dwords zeroed, FNV-1a, fixup targets) and the AP addresses (the AP
  spans' bytes at their AP VAs do not match: an AP binding is refused).

usage: verify_ship_steering_sites_fl.py [--exe PATH] [--json] [--emit-windows]   (exit 0 = PASS)
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import seta_fl_probe as P  # noqa: E402
import ship_steering_fl_probe as S  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SITES = ROOT / 'src/engine/fl/ship_steering_sites_fl.h'
BASE = P.BASE
# (name, rva, bytes, instruction lengths, continuation)
SPANS = (
    ('ship_steer_leaf_yaw_pitch_fl', 0x056450, '83ec108b442414', (3, 4)),
    ('ship_steer_leaf_roll_yaw_fl', 0x055cd0, '8b44240483ec10', (4, 3)),
    ('ship_steer_leaf_roll_pitch_fl', 0x056090, '8b44240483ec10', (4, 3)),
    ('ship_steer_loop_continue_fl', 0x062c72, '8b1b899c2408010000', (2, 7)),
)
LEAVES = ((0x056450, 0x056803, 0x056800, 16), (0x055cd0, 0x05608e, 0x05608b, 2), (0x056090, 0x05644e, 0x05644b, 2))
B_TABLE_ENTRY = 0x062f1c
B_PREDECESSOR = 0x062c6e
GETTERS = {'max_speed': 0x05a450, 'accel': 0x05a610, 'pitch_cap': 0x05a7d0, 'yaw_cap': 0x05a830, 'roll_cap': 0x05a890}
GETTER_RETS = {'max_speed': 0x05a5b1, 'accel': 0x05a678}
TABLE_SLOT, CLOCK_SLOT = 0x1a83e4, 0x1a83b0
ORDER_SWITCH, ORDER_BYTES, ORDER_JUMPS, ORDER_DEFAULT = 0x05db2f, 0x062fb4, 0x062f5c, 0x062c6e
STEERED_ORDERS = (4, 5, 6, 0x10, 0x11, 0x12, 0x14, 0x15, 0x18, 0x19, 0x1d, 0x22, 0x23, 0x24)
# (name, rva, length, part, code) -- the header's identity windows, in its order (part 1: needed by the steering install).
WINDOW_SPANS = (
    ('leaf_yaw_pitch', 0x056450, 0x20, 1, True),      # A0 entry span + the target read and the obj (ECX) use
    ('leaf_roll_yaw', 0x055cd0, 0x20, 1, True),       # A1 entry span + `mov ebx,ecx`
    ('leaf_roll_pitch', 0x056090, 0x20, 1, True),     # A2 entry span + `mov ebx,ecx`
    ('leaf_yaw_pitch_tail', 0x0567ec, 0x17, 1, True),  # the three remainder stores and `ret 4`
    ('leaf_roll_yaw_tail', 0x056077, 0x17, 1, True),
    ('leaf_roll_pitch_tail', 0x056437, 0x17, 1, True),
    ('loop_continue', 0x062c6e, 0x16, 1, True),       # the sector reload, B's 9 bytes, `cmp [ebx],0; jne`
    ('slew_walk', 0x062d13, 0x13, 1, True),           # the slew's bucket walk after the order loop
    ('slew_read', 0x062de0, 0x0b, 1, True),           # desired speed +0x14
    ('slew_write', 0x062ec7, 0x0e, 1, True),          # current speed +0x10, the next object
    ('max_speed_a', 0x05a450, 0xd1, 1, True),
    ('max_speed_b', 0x05a521, 0x91, 1, True),         # the timed factor (clock +0x708), the sector cap, `ret`
    ('accel', 0x05a610, 0x6c, 1, True),
    ('pitch_cap', 0x05a7d0, 0x60, 1, True),
    ('yaw_cap', 0x05a830, 0x60, 1, True),
    ('roll_cap', 0x05a890, 0x60, 1, True),
    ('large_predicate', 0x049690, 0x34, 1, True),
    ('large_table', 0x0496c4, 0x18, 1, False),
    ('capital_predicate', 0x0496e0, 0x34, 1, True),
    ('capital_table', 0x049714, 0x16, 1, False),
    ('order_switch', 0x05db2f, 0x1e, 1, True),
    ('order_bytes', 0x062fb4, 0x25, 1, False),
    ('clock_command_pass', 0x05c810, 0x20, 1, True),  # the clock slot and the frame step +0x704
    ('integrator_plant', 0x0632e4, 0x45, 1, True),    # record +0xd0 x flight +0x2b0 / 100, the TShips slot
)
ANCHORS = (
    (0x056453, 'mov eax, dword ptr [esp + 0x14]', 'A0: the target at entry [esp+4]'),
    (0x05645a, 'mov esi, dword ptr [eax]', 'A0: target x'),
    (0x056463, 'mov edi, ecx', 'A0: obj in ECX'),
    (0x056465, 'mov ecx, dword ptr [edi + 0x70]', 'A0: phys obj+0x70'),
    (0x056468, 'sub esi, dword ptr [ecx + 0x30]', 'A0: phys position +0x30'),
    (0x0564a0, 'cmp word ptr [edi + 0x48], 7', 'A0: class word +0x48'),
    (0x0564aa, 'add ecx, 0x870', 'A0: the flight basis +0x870'),
    (0x055cd0, 'mov eax, dword ptr [esp + 4]', 'A1: the target at entry [esp+4]'),
    (0x055cd8, 'mov ebx, ecx', 'A1: obj in ECX'),
    (0x056090, 'mov eax, dword ptr [esp + 4]', 'A2: the target at entry [esp+4]'),
    (0x056098, 'mov ebx, ecx', 'A2: obj in ECX'),
    (0x0567ec, 'mov dword ptr [edi + 0x30], eax', 'A0 writes +0x30'),
    (0x0567ef, 'mov dword ptr [edi + 0x34], ecx', 'A0 writes +0x34'),
    (0x0567f2, 'mov dword ptr [edi + 0x38], 0', 'A0 writes +0x38'),
    (0x056800, 'ret 4', 'A0 ret 4'),
    (0x056077, 'mov dword ptr [ebx + 0x30], esi', 'A1 writes +0x30'),
    (0x05607a, 'mov dword ptr [ebx + 0x34], 0', 'A1 writes +0x34'),
    (0x056081, 'mov dword ptr [ebx + 0x38], edi', 'A1 writes +0x38'),
    (0x05608b, 'ret 4', 'A1 ret 4'),
    (0x056437, 'mov dword ptr [ebx + 0x30], 0', 'A2 writes +0x30'),
    (0x05643e, 'mov dword ptr [ebx + 0x34], esi', 'A2 writes +0x34'),
    (0x056441, 'mov dword ptr [ebx + 0x38], edi', 'A2 writes +0x38'),
    (0x05644b, 'ret 4', 'A2 ret 4'),
    (0x062c6e, 'mov esi, dword ptr [esp + 0x24]', 'B predecessor: the sector reload'),
    (0x062c7b, 'cmp dword ptr [ebx], 0', 'B continuation: flags rewritten'),
    (0x062c7e, 'jne 0x45c911', 'B continuation: the loop body'),
    (0x062d17, 'mov eax, dword ptr [eax + 0x50]', 'slew: the sector bucket table'),
    (0x062de0, 'mov edi, dword ptr [esi + 0x14]', 'slew reads desired speed'),
    (0x062ec7, 'mov dword ptr [esi + 0x10], edi', 'slew writes current speed'),
    (0x05a452, 'mov ebx, ecx', 'max speed: obj in ECX'),
    (0x05a477, 'mov eax, dword ptr [ecx + 0x27c]', 'max speed: the flight override'),
    (0x05a485, 'mov eax, dword ptr [ecx + 0x274]', 'max speed: the flight tuning'),
    (0x05a52e, 'mov eax, dword ptr [0x5a83b0]', 'max speed: the clock slot'),
    (0x05a533, 'mov eax, dword ptr [eax + 0x708]', 'max speed: game time'),
    (0x05a543, 'mov dword ptr [ecx + 0x914], 0', 'max speed: the expiry write'),
    (0x05a550, 'mov dword ptr [eax + 0x918], 0', 'max speed: the expiry write'),
    (0x05a5b1, 'ret', 'max speed: plain ret'),
    (0x05a612, 'movsx ecx, word ptr [edx + 0x48]', 'accel: obj in ECX (copied to EDX)'),
    (0x05a64c, 'mov eax, dword ptr [0x5a83e4]', 'accel: the TShips slot'),
    (0x05a651, 'mov ecx, dword ptr [ecx + eax + 0x48]', 'accel: record +0x48'),
    (0x05a658, 'mov eax, dword ptr [eax + 0x274]', 'accel: flight +0x274'),
    (0x05a678, 'ret', 'accel: plain ret'),
    (0x05a7d0, 'movzx edx, word ptr [ecx + 0x48]', 'pitch cap: obj in ECX'),
    (0x05a7dd, 'mov eax, dword ptr [eax + 0x270]', 'pitch cap: flight +0x270'),
    (0x05a7f6, 'mov ecx, dword ptr [0x5a83e4]', 'pitch cap: the TShips slot'),
    (0x05a7fc, 'imul dword ptr [edx + ecx + 0xc]', 'pitch cap: record +0xc'),
    (0x05a85c, 'imul dword ptr [edx + ecx + 8]', 'yaw cap: record +8'),
    (0x05a8bc, 'imul dword ptr [edx + ecx + 0x10]', 'roll cap: record +0x10'),
    (0x043eff, 'mov eax, dword ptr [0x5a83e4]', 'record max speed: the TShips slot'),
    (0x043f04, 'mov eax, dword ptr [ecx + eax + 0x44]', 'record max speed +0x44'),
    (0x0496a1, 'mov eax, dword ptr [0x5a83e4]', 'large predicate: the TShips slot'),
    (0x0496a6, 'mov eax, dword ptr [ecx + eax + 0x14]', 'large predicate: record class code +0x14'),
    (0x0496aa, 'cmp eax, 0xf', 'large predicate: codes 0..15'),
    (0x0496fa, 'cmp eax, 0xd', 'capital predicate: codes 0..13'),
    (0x05db2f, 'movsx eax, word ptr [esi + 0x258]', 'order switch: flight +0x258'),
    (0x05db36, 'cmp eax, 0x24', 'order switch: bound'),
    (0x05db39, 'ja 0x462c6e', 'order switch: default = the loop continue'),
    (0x05db3f, 'movzx eax, byte ptr [eax + 0x462fb4]', 'order switch: byte table'),
    (0x05db46, 'jmp dword ptr [eax*4 + 0x462f5c]', 'order switch: jump table'),
    (0x05db4d, 'mov eax, dword ptr [0x5a83b0]', 'command pass: the clock slot'),
    (0x05db52, 'mov ecx, dword ptr [eax + 0x708]', 'command pass: game time'),
    (0x05c81c, 'mov eax, dword ptr [0x5a83b0]', 'command pass entry: the clock slot'),
    (0x05c82a, 'mov eax, dword ptr [eax + 0x704]', 'command pass entry: the frame step'),
    (0x0632f5, 'mov eax, dword ptr [0x5a83e4]', 'integrator: the TShips slot'),
    (0x0632fa, 'mov eax, dword ptr [ecx + eax + 0xd0]', 'integrator: record rotation acceleration +0xd0'),
    (0x063308, 'mov eax, dword ptr [eax + 0x2b0]', 'integrator: flight +0x2b0'),
    (0x06331a, 'mov ecx, 0x64', 'integrator: / 100'),
    (0x0632d8, 'test ecx, 0x80000', 'integrator: the autopilot bit'),
)
# AP preferred VAs of the same spans (src/proxy/ship_steering_sites.h): their AP bytes must not match in X3FL.exe.
AP_SPANS = ((0x44ccc0, '83ec188b02'), (0x44c620, '83ec188b02'), (0x44c970, '83ec188b02'), (0x4593dd, '8b1b833b00'),
            (0x450980, '83ec085356'), (0x450b80, '0fbf4148'))


def verify(data):
    report = {'result': 'FAIL', 'edition': 'fl', 'checks': {}}
    checks = report['checks']
    report['exe_info'] = {'sha256': hashlib.sha256(data).hexdigest(), 'known': hashlib.sha256(data).hexdigest() == P.FL_SHA256}
    img = P.FlImage(data)
    checks['fl_identity'] = {'ok': img.timestamp == P.FL_TIMESTAMP and img.image_size == P.FL_IMAGE_SIZE}
    ptrs = P.pointers(img)
    # ---- the four spans ----
    spans = [S.span_row(img, *s) for s in SPANS]
    b_entry = struct.unpack('<I', img.rva(B_TABLE_ENTRY, 4))[0] - BASE
    b_sources = [a for a, t in P.raw_branch_hits(img, B_PREDECESSOR, B_PREDECESSOR + 13) if t == 0x062c72]
    b_ptrs = sorted(ptrs.get(0x062c72, []))
    checks['spans'] = {'ok': all(r['ok'] for r in spans) and b_entry == 0x062c72 and B_TABLE_ENTRY in img.fixups and
                       len(b_sources) > 0 and b_ptrs == [B_TABLE_ENTRY],
                       'spans': spans, 'b_table_entry': f'{b_entry:#08x}', 'b_direct_sources': len(b_sources),
                       'b_pointers': [f'{p:#08x}' for p in b_ptrs],
                       'b_short_candidate': 'refused: 0x062c72 is entered from %d direct branches' % len(b_sources)}
    # ---- the leaves ----
    leaves = []
    for entry, end, ret, callers_want in LEAVES:
        insns = img.insns(entry, end) or []
        rets = [i for i in insns if i.mnemonic == 'ret']
        callers = P.raw_callers(img, entry)
        leaves.append({'entry': f'{entry:#08x}', 'end': f'{end:#08x}', 'decoded': bool(insns),
                       'rets': [f'{i.address - BASE:#08x} {i.op_str}' for i in rets], 'callers': len(callers),
                       'x87': sum(i.mnemonic.startswith('f') for i in insns), 'std': sum(i.mnemonic == 'std' for i in insns),
                       'ok': bool(insns) and [(i.address - BASE, i.op_str) for i in rets] == [(ret, '4')] and
                       len(callers) == callers_want and not any(i.mnemonic == 'std' for i in insns)})
    checks['leaves'] = {'ok': all(r['ok'] for r in leaves), 'leaves': leaves}
    # ---- the getters ----
    g = S.graph(img, list(GETTERS.values()))
    global_stores = [t for _, t in g['stores'] if not t.startswith('mov dword ptr [esp')]
    checks['getters'] = {'ok': not g['bad'] and len(g['functions']) == 9 and len(g['tables']) == 4 and
                         sorted(global_stores) == ['mov dword ptr [eax + 0x918], 0', 'mov dword ptr [ecx + 0x914], 0'],
                         'functions': [f'{f:#08x}' for f in g['functions']], 'instructions': g['instructions'],
                         'switch_tables': [[f'{a:#08x}', f'{t:#08x}', k] for a, t, k in g['tables']],
                         'bad': [[f'{a:#08x}', t] for a, t in g['bad']], 'global_stores': global_stores}
    # ---- bindings and masks ----
    large = img.rva(0x0496cc, 16)
    jl = [struct.unpack('<I', img.rva(0x0496c4 + 4 * k, 4))[0] - BASE for k in range(2)]
    large_mask = sum(1 << c for c in range(16) if jl[large[c]] == 0x0496bd)
    cap = img.rva(0x04971c, 14)
    jc = [struct.unpack('<I', img.rva(0x049714 + 4 * k, 4))[0] - BASE for k in range(2)]
    capital_mask = sum(1 << c for c in range(14) if jc[cap[c]] == 0x04970d)
    order_bytes = img.rva(ORDER_BYTES, 0x25)
    order_targets = {o: struct.unpack('<I', img.rva(ORDER_JUMPS + 4 * order_bytes[o], 4))[0] - BASE for o in range(0x25)}
    unhandled = [f'{o:#x}' for o in STEERED_ORDERS if order_targets[o] == ORDER_DEFAULT]
    slot_users = sorted(f for f in img.fixups if struct.unpack('<I', img.rva(f, 4))[0] - BASE == TABLE_SLOT)
    clock_users = sorted(f for f in img.fixups if struct.unpack('<I', img.rva(f, 4))[0] - BASE == CLOCK_SLOT)
    checks['bindings'] = {'ok': large_mask == 0xe91d and capital_mask == 0x211d and not unhandled and
                          {0x05a64d, 0x05a7f8, 0x05a858, 0x05a8b8, 0x0496a2, 0x0496f2, 0x0632f6, 0x043f00} <= set(slot_users) and
                          {0x05a52f, 0x05c81d, 0x05db4e} <= set(clock_users),
                          'large_mask': f'{large_mask:#06x}', 'capital_mask': f'{capital_mask:#06x}',
                          'steered_orders_unhandled': unhandled,
                          'steered_order_targets': {f'{o:#x}': f'{order_targets[o]:#08x}' for o in STEERED_ORDERS},
                          'table_slot_users': len(slot_users), 'clock_slot_users': len(clock_users)}
    checks['anchors'] = S.anchors(img, ANCHORS)
    header_text = SITES.read_text() if SITES.is_file() else ''
    checks['windows'] = S.windows_check(img, WINDOW_SPANS, header_text)
    checks['ap_refused'] = S.ap_refusal(img, AP_SPANS, [header_text])
    report['result'] = 'PASS' if all(c.get('ok') for c in checks.values()) else 'FAIL'
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--exe', type=Path, default=P.FL_DEFAULT_EXE)
    parser.add_argument('--json', action='store_true', help='print the full report')
    parser.add_argument('--emit-windows', action='store_true', help='print the C++ window rows of the sites header')
    args = parser.parse_args()
    if not args.exe.is_file():
        print(json.dumps({'result': 'SKIP', 'reason': f'{args.exe} not found'}))
        return 2
    data = args.exe.read_bytes()
    if args.emit_windows:
        print(S.emit_windows(P.FlImage(data), WINDOW_SPANS))
        return 0
    report = verify(data)
    report['exe'] = str(args.exe)
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(json.dumps({'result': report['result'], 'exe': report['exe'], 'exe_info': report['exe_info'],
                          **{k: v['ok'] for k, v in report['checks'].items()}}))
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
