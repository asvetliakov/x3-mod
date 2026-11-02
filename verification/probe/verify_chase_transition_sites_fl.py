#!/usr/bin/env python3
"""Read-only check of the FL chase view restore's native seams against the installed X3FL.exe.

The FL edition of verify_chase_transition_sites.py + verify_chase_restore_sites.py (docs/reverse-engineering/fl/
chase-transition.md, "Verified 2026-11-02"; constants src/engine/fl/chase_transition_sites_fl.h). For each of the
twelve spans the FL build claims (six lifetime/update witnesses, the script and load mode stores, the input-worker
injection entry and three VM cancel boundaries): the exact bytes (the EH adapter's relocated immediate compared as
preferred base + its target RVA), Capstone whole-instruction lengths, no relative control flow inside the span, the
.reloc census per span, the image-wide direct-edge census (none into an interior), no relocated pointer into an
interior, the start's own inbound edges and the qword geometry of the five-byte jump. Then the identity windows the
module checks at the loaded base before any write (FNV-1a with relocated dwords zeroed, each fixup's target RVA), and
the register/frame/layout anchors the handlers and the restore call rely on (Capstone text at each RVA), the caller
sets that make the constructor/destructor return addresses meaningful, and the case-0x30 jump-table entry.

usage: verify_chase_transition_sites_fl.py [--exe PATH] [--json] [--emit-windows]   (exit 0 = PASS)
Nothing is written; the game directory is only read.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_chase_camera_site import (FL_DEFAULT_EXE, FL_IMAGE_SIZE, FL_SHA256, FL_TIMESTAMP, FlImage,  # noqa: E402
                                      fnv1a32)

BASE = 0x00400000
# (group, kind, name, rva, bytes, instruction lengths, relocated offsets in the span -> target RVA)
SITES = (
    ('mandatory', 0, 'chase_cockpit_construct_fl', 0x020ec0, '538bdc83ec08', (1, 2, 3), {}),
    ('mandatory', 1, 'chase_cockpit_complete_fl', 0x0215c6, '5e8be55d8be3', (1, 2, 1, 2), {}),
    ('mandatory', 2, 'chase_cockpit_destroy_fl', 0x01e3f0, '558bec83e4f8', (1, 2, 3), {}),
    ('mandatory', 3, 'chase_update_begin_fl', 0x021f50, '558bec83e4f0', (1, 2, 3), {}),
    ('mandatory', 4, 'chase_update_end_internal_fl', 0x022f9b, '5f5e5b8be5', (1, 1, 1, 2), {}),
    ('mandatory', 5, 'chase_update_end_fl', 0x022fa9, '5f5e5b8be5', (1, 1, 1, 2), {}),
    ('observer', 6, 'chase_mode_script_fl', 0x031abc, '898850010000', (6,), {}),
    ('observer', 7, 'chase_mode_load_fl', 0x01bf6e, '899750010000', (6,), {}),
    ('restore', 0, 'chase_restore_inject_fl', 0x0cfd00, '558bec83e4f0', (1, 2, 3), {}),
    ('restore', 1, 'chase_restore_vm_clear_fl', 0x0ba6b0, '83ec0c5355', (3, 1, 1), {}),
    ('restore', 2, 'chase_restore_vm_load_fl', 0x0bcb10, '558bec6aff', (1, 2, 2), {}),
    ('restore', 3, 'chase_restore_eh_adapter_fl', 0x14e80a, 'b820e55900', (5,), {1: 0x19e520}),
)
# Identity windows (name -> rva, length): the module compares these at the loaded base before any write.
WINDOW_SPANS = (
    ('construct', 0x020ec0, 0x33),      # prologue .. mov edi,ecx; mov [ebp-0x14],edi: ECX = cockpit
    ('complete', 0x021534, 0x9a),       # mov eax,edi .. pop ebx; ret: EAX = cockpit through the epilogue
    ('destroy', 0x01e3f0, 0x26),        # the deleting wrapper whole: ECX = cockpit, call 0x421820, free 0x8c0, ret 4
    ('update_begin', 0x021f50, 0x10),   # push ebp; mov ebp,esp; ...; mov ebx,ecx
    ('update_end', 0x022f9b, 0x15),     # both exits (pop edi/esi/ebx; mov esp,ebp; pop ebp; ret)
    ('mode_script', 0x031a9b, 0x32),    # case 0x30: resolver by script id, store [eax+0x150]=ecx, task [ebx+0xc]
    ('mode_load', 0x01bf68, 0x14),      # the load chain's store [edi+0x150]=edx
    ('dispatcher', 0x030540, 0x27),     # EBX = entry ESP-4 frame, command [ebx+0x10]
    ('native_call', 0x0c0751, 0x39),    # task index/PC publication, call eax, return 0x4c0785
    ('script_call', 0x0bb5b0, 0x44),    # the named dispatcher (callback, token, key, name, kind, argc, args...)
    ('callback_bound_ok', 0x0be9de, 0x15),    # cmp edx,0x20; jge before the callback table
    ('callback_bound_fail', 0x0bb22d, 0x24),  # cmp eax,0x20; jge on the refusal path
    ('inject', 0x0cfd00, 0x13),         # prologue .. mov [esp+0x20],ecx: ECX = controlled object
    ('inject_callers', 0x0787a8, 0x39),  # ECX = esi = cockpit +0xc/+0x10, push flag, call 0x4cfd00 (both callers)
    ('worker_call', 0x0cfd48, 0x52),    # game root, push [root+0x418], call 0x4bb5b0
    ('pause', 0x0786ad, 0x1f),          # game root +0x4a0 bit 0
    ('vm_clear', 0x0ba6b0, 0x09),       # ECX = VM -> EDI
    ('vm_load', 0x0bcb10, 0x0a),        # frame, SEH handler push
    ('eh_adapter', 0x14e80a, 0x0a),     # mov eax,metadata; jmp handler
    ('active_control', 0x0cf46f, 0x37),  # cockpit registry +0x10 handle, map +0, {next,key,value}
    ('sim_registry', 0x0495f1, 0x11),   # simulation manager map +0x14 keyed by object +8
    ('ship_script_id', 0x06a81d, 0x1f),  # resolver 0x443d30 then [eax+0x94]
    ('ship_flag', 0x065313, 0x0d),      # test [esi+0x40],0x08000000; jne (skip)
    ('alloc_call', 0x0305a0, 0x0b),     # mov ecx,eax; call 0x420ec0 -> return 0x4305ab
    ('load_call', 0x020e07, 0x0b),      # mov ecx,eax; call 0x420ec0 -> return 0x420e12
    ('free_call', 0x0305e8, 0x2a),      # resolve, registry removal, call 0x41e3f0 -> return 0x430612
)
# Capstone text at RVA (register ABI, frame and layout anchors).
ANCHORS = (
    (0x020ef1, 'mov edi, ecx', 'constructor: ECX = cockpit'),
    (0x021534, 'mov eax, edi', 'constructor: EAX = cockpit before the epilogue'),
    (0x0215cd, 'ret', 'constructor: plain RET after the complete span'),
    (0x01e3f8, 'mov esi, ecx', 'destroy wrapper: ECX = cockpit'),
    (0x01e3fa, 'call 0x421820', 'destroy wrapper: the actual destructor'),
    (0x01e3ff, 'push 0x8c0', 'destroy wrapper: frees the cockpit block'),
    (0x01e413, 'ret 4', 'destroy wrapper: RET 4'),
    (0x021f5e, 'mov ebx, ecx', 'updater: ECX = cockpit'),
    (0x022fa0, 'pop ebp', 'updater internal exit: EBP frame'),
    (0x022fa1, 'ret', 'updater internal exit: plain RET'),
    (0x022fae, 'pop ebp', 'updater exit: EBP frame'),
    (0x022faf, 'ret', 'updater exit: plain RET'),
    (0x031a9b, 'mov esi, dword ptr [ebx + 0x18]', 'mode store: EBX = dispatcher argument frame'),
    (0x031aa7, 'call 0x41e420', 'mode store: cockpit resolver'),
    (0x031ab9, 'push dword ptr [ebx + 0xc]', 'mode store: task = [EBX+0xc]'),
    (0x031abc, 'mov dword ptr [eax + 0x150], ecx', 'mode store: EAX = cockpit, ECX = mode'),
    (0x01bf6e, 'mov dword ptr [edi + 0x150], edx', 'load store: EDI = cockpit, EDX = mode'),
    (0x030540, 'push ebx', 'dispatcher frame'),
    (0x030541, 'mov ebx, esp', 'dispatcher frame: EBX = entry ESP - 4'),
    (0x030564, 'mov eax, dword ptr [ebx + 0x10]', 'dispatcher: command = [EBX+0x10]'),
    (0x0c0783, 'call eax', 'interpreter native call'),
    (0x0c0785, 'add esp, 0x14', 'interpreter native call return (five caller-cleaned words)'),
    (0x0bedbc, 'mov eax, dword ptr [ecx + 0x3c]', 'task +0x3c current context'),
    (0x0bb620, 'mov eax, dword ptr [esi + 0x18]', 'task +0x18 signed cell index'),
    (0x0bb62e, 'add ecx, dword ptr [esi + 0x14]', 'task +0x14 stack base (5-byte cells)'),
    (0x0c0824, 'cmp dword ptr [eax + 8], 0', 'return scan: context class pointer +8'),
    (0x0c0836, 'cmp al, 0xa', 'return scan: tag 10 saved context'),
    (0x0c083a, 'cmp byte ptr [edi + 5], 3', 'return scan: tag 3 return offset above it'),
    (0x0be9ee, 'cmp edx, 0x20', 'callback selector bound (success)'),
    (0x0be9f1, 'jge 0x4bea07', 'callback selector >= 32 skips the callback (success)'),
    (0x0bb248, 'cmp eax, 0x20', 'callback selector bound (failure)'),
    (0x0bb24b, 'jge 0x4bb2f3', 'callback selector >= 32 skips the callback (failure)'),
    (0x0bb5ea, 'call 0x4bb310', 'named dispatcher: receiver/method lookup'),
    (0x0bb65b, 'call 0x4bb470', 'named dispatcher: task run'),
    (0x0cfd0f, 'mov dword ptr [esp + 0x20], ecx', 'inject: ECX = controlled object spilled'),
    (0x0787b9, 'mov ecx, esi', 'inject callers: ECX = controlled object'),
    (0x0cfd48, 'mov ecx, dword ptr [0x6243cc]', 'game root'),
    (0x0cfd88, 'push dword ptr [ecx + 0x418]', 'game root +0x418 controller'),
    (0x0cfd95, 'call 0x4bb5b0', 'the worker calls the named dispatcher'),
    (0x0786ad, 'mov edx, dword ptr [0x6243cc]', 'pause: game root'),
    (0x0786bd, 'mov ecx, dword ptr [edx + 0x4a0]', 'pause bits +0x4a0'),
    (0x0786c3, 'test cl, 1', 'pause bit 0'),
    (0x0ba6b7, 'mov edi, ecx', 'VM clear: ECX = VM'),
    (0x0cf46f, 'mov eax, dword ptr [0x5a82e4]', 'cockpit registry root'),
    (0x0cf474, 'mov ecx, dword ptr [eax + 0x10]', 'cockpit registry +0x10 active handle'),
    (0x0cf477, 'mov eax, dword ptr [eax]', 'cockpit registry +0 map'),
    (0x0cf494, 'cmp dword ptr [eax + 4], ecx', 'row +4 key'),
    (0x0cf4a3, 'mov ecx, dword ptr [eax + 8]', 'row +8 value'),
    (0x0495f1, 'mov ecx, dword ptr [0x5a82ec]', 'simulation manager root'),
    (0x0495f7, 'mov edx, dword ptr [esi + 8]', 'object +8 native id (map key)'),
    (0x0495fa, 'mov ecx, dword ptr [ecx + 0x14]', 'simulation manager +0x14 object map'),
    (0x043d30, 'mov eax, dword ptr [ecx + 0x14]', 'simulation resolver: map +0x14'),
    (0x043d60, 'mov eax, dword ptr [eax + 8]', 'simulation resolver: row value +8'),
    (0x06a836, 'mov eax, dword ptr [eax + 0x94]', 'ship script id +0x94'),
    (0x065313, 'test dword ptr [esi + 0x40], 0x8000000', 'excluded-object flag +0x40'),
    (0x0305a4, 'mov ecx, eax', 'alloc command: ECX = new cockpit'),
    (0x0305a6, 'call 0x420ec0', 'alloc command constructor call (return 0x4305ab)'),
    (0x020e0b, 'mov ecx, eax', 'load chain: ECX = new cockpit'),
    (0x020e0d, 'call 0x420ec0', 'load chain constructor call (return 0x420e12)'),
    (0x030605, 'call 0x50d040', 'free command: registry removal before the wrapper'),
    (0x03060b, 'mov ecx, esi', 'free command: ECX = cockpit'),
    (0x03060d, 'call 0x41e3f0', 'free command wrapper call (return 0x430612)'),
    (0x14e80f, 'jmp 0x521368', 'EH adapter tail jump'),
)
CALLERS = {  # callee RVA -> exact direct callers
    0x020ec0: (0x020e0d, 0x0305a6),
    0x021820: (0x01e3fa,),
    0x021f50: (0x01e5b0,),
    0x0cfd00: (0x0787c8, 0x0787dc),
    0x0bcb10: (0x07983c,),
    0x0ba6b0: (0x07861b, 0x0b30cb, 0x0b9aef),
}
WRAPPER_CALLERS = 13
FREE_CALLER, ALLOC_RETURN, LOAD_RETURN, FREE_RETURN = 0x03060d, 0x0305ab, 0x020e12, 0x030612
JUMP_TABLE, CASE_MODE, CASE_TARGET = 0x032488, 0x30, 0x031a9b
EH_REGISTRATION = 0x0bed96  # relocated pointer to the EH adapter (expected publication)
NATIVE_RETURN = 0x0c0785


def window_record(img, rva, length):
    raw = bytearray(img.rva(rva, length) or b'')
    fix = []
    for f in sorted(img.fixups):
        if rva - 3 <= f < rva + length:
            if not (rva <= f and f + 4 <= rva + length):
                return None  # a fixup straddling the edge: choose another window
            value = struct.unpack_from('<I', raw, f - rva)[0]
            fix.append((f - rva, value - BASE))
            raw[f - rva:f - rva + 4] = b'\0\0\0\0'
    return bytes(raw), tuple(fix)


def verify(data):
    report = {'result': 'FAIL', 'edition': 'fl', 'checks': {}}
    checks = report['checks']
    sha = hashlib.sha256(data).hexdigest()
    report['exe_info'] = {'sha256': sha, 'known': sha == FL_SHA256}
    img = FlImage(data)
    checks['fl_identity'] = {'ok': img.timestamp == FL_TIMESTAMP and img.image_size == FL_IMAGE_SIZE,
                             'timestamp': f'{img.timestamp:#010x}', 'image_size': f'{img.image_size:#010x}'}
    edges = img.direct_edges()
    # Relocated pointers: every HIGHLOW value, for the "no pointer into an interior" census.
    pointers = {}
    for f in img.fixups:
        raw = img.rva(f, 4)
        if raw:
            pointers.setdefault(struct.unpack('<I', raw)[0] - BASE, []).append(f)
    sites = []
    for group, kind, name, rva, hexbytes, lengths, relocs in SITES:
        want = bytes.fromhex(hexbytes)
        got = img.rva(rva, len(want))
        masked = bytearray(got or b'')
        reloc_ok = True
        for off, target in relocs.items():
            reloc_ok &= rva + off in img.fixups and struct.unpack_from('<I', masked, off)[0] == BASE + target
        span_fix = sorted(f - rva for f in img.fixups if rva - 3 <= f < rva + len(want))
        insns = img.insns(rva, rva + len(want))
        rel = [i for i in insns or [] if i.mnemonic.startswith(('j', 'call', 'loop', 'ret'))]
        interior = sorted(a for a, _, t in edges if rva < t < rva + len(want))
        to_start = sorted(a for a, _, t in edges if t == rva)
        ptr_interior = sorted(p for p in pointers if rva < p < rva + len(want))
        ptr_start = sorted(pointers.get(rva, []))
        crossing = (rva & 7) + 5 > 8
        ok = (got == want and reloc_ok and span_fix == sorted(relocs) and insns is not None and
              tuple(i.size for i in insns) == lengths and not rel and not interior and not ptr_interior and
              ptr_start == ([EH_REGISTRATION] if name.startswith('chase_restore_eh') else []))
        sites.append({'name': name, 'group': group, 'kind': kind, 'rva': f'{rva:#08x}', 'ok': ok,
                      'bytes': got.hex() if got else None, 'lengths': [i.size for i in insns] if insns else None,
                      'span_fixups': span_fix, 'interior_edges': [f'{a:#08x}' for a in interior],
                      'edges_to_start': len(to_start), 'pointers_to_start': [f'{p:#08x}' for p in ptr_start],
                      'pointers_into_interior': [f'{p:#08x}' for p in ptr_interior],
                      'qword_crossing': crossing, 'write': 'plain' if crossing else 'cmpxchg8b',
                      'text': [f'{i.mnemonic} {i.op_str}'.strip() for i in insns or []]})
    checks['sites'] = {'ok': all(s['ok'] for s in sites) and len(sites) == 12, 'count': len(sites),
                       'direct_edges': len(edges), 'highlow': len(img.fixups),
                       'failed': [s for s in sites if not s['ok']], 'all': sites}
    windows = {}
    for name, rva, length in WINDOW_SPANS:
        rec = window_record(img, rva, length)
        whole = img.insns(rva, rva + length)
        windows[name] = {'ok': rec is not None and whole is not None, 'rva': f'{rva:#08x}', 'length': length,
                         'fnv1a32': f'{fnv1a32(rec[0]):08x}' if rec else None,
                         'fixups': [[o, f'{t:#08x}'] for o, t in rec[1]] if rec else None,
                         'whole_instructions': whole is not None}
    checks['windows'] = {'ok': all(w['ok'] for w in windows.values()), 'count': len(windows), **windows}
    anchors = []
    for rva, text, what in ANCHORS:
        got, _ = img.text_at(rva)
        anchors.append({'rva': f'{rva:#08x}', 'ok': got == text, 'got': got, 'what': what})
    checks['anchors'] = {'ok': all(a['ok'] for a in anchors), 'count': len(anchors),
                         'failed': [a for a in anchors if not a['ok']]}
    callers = {}
    for callee, want in CALLERS.items():
        got = tuple(sorted(a for a, m, t in edges if t == callee and m == 'call'))
        callers[f'{callee:#08x}'] = {'ok': got == want, 'callers': [f'{a:#08x}' for a in got],
                                     'pointers_to_entry': [f'{p:#08x}' for p in pointers.get(callee, [])]}
    wrapper = sorted(a for a, m, t in edges if t == 0x01e3f0 and m == 'call')
    callers['0x01e3f0'] = {'ok': len(wrapper) == WRAPPER_CALLERS and FREE_CALLER in wrapper and
                           not pointers.get(0x01e3f0), 'count': len(wrapper),
                           'free_caller': f'{FREE_CALLER:#08x}', 'free_return': f'{FREE_RETURN:#08x}'}
    returns_ok = (ALLOC_RETURN == 0x0305a6 + 5 and LOAD_RETURN == 0x020e0d + 5 and FREE_RETURN == FREE_CALLER + 5)
    callers['returns'] = {'ok': returns_ok, 'alloc': f'{ALLOC_RETURN:#08x}', 'load': f'{LOAD_RETURN:#08x}',
                          'free': f'{FREE_RETURN:#08x}'}
    checks['callers'] = {'ok': all(c['ok'] for c in callers.values()), **callers}
    # Complete span: EAX holds the cockpit from 0x421534 to the span; nothing else enters that range.
    body = img.insns(0x021536, 0x0215c6)
    eax_writes = []
    for i in body or []:
        _, written = i.regs_access()
        if any(i.reg_name(r) in ('eax', 'ax', 'al', 'ah') for r in written):
            eax_writes.append(f'{i.address - BASE:#08x}')
    foreign = sorted(a for a, _, t in edges if 0x021536 <= t <= 0x0215c6 and not 0x021536 <= a < 0x0215c6)
    checks['complete_eax'] = {'ok': body is not None and not eax_writes and not foreign,
                              'instructions': len(body or []), 'eax_writes': eax_writes,
                              'foreign_edges': [f'{a:#08x}' for a in foreign]}
    # Case 0x30 of the dispatcher's jump table reaches the mode store; the native return follows `call eax`.
    entry = img.rva(JUMP_TABLE + 4 * CASE_MODE, 4)
    entry_va = struct.unpack('<I', entry)[0] if entry else None
    checks['mode_case'] = {'ok': entry_va == BASE + CASE_TARGET and JUMP_TABLE + 4 * CASE_MODE in img.fixups,
                           'entry': f'{entry_va:#010x}' if entry_va else None}
    eh = img.rva(EH_REGISTRATION, 4)
    checks['eh_registration'] = {'ok': EH_REGISTRATION in img.fixups and eh and
                                 struct.unpack('<I', eh)[0] == BASE + 0x14e80a}
    dispatcher_pub = sorted(pointers.get(0x030540, []))
    checks['dispatcher_publication'] = {'ok': bool(dispatcher_pub) and
                                        not [a for a, m, t in edges if t == 0x030540],
                                        'pointers': [f'{p:#08x}' for p in dispatcher_pub]}
    report['result'] = 'PASS' if all(c.get('ok') for c in checks.values()) else 'FAIL'
    return report


def emit_windows(data):
    img = FlImage(data)
    out = []
    for name, rva, length in WINDOW_SPANS:
        raw, fix = window_record(img, rva, length)
        body = ', '.join(f'0x{b:02x}' for b in raw)
        fixups = ', '.join(f'{{{o}, 0x{t:06x}}}' for o, t in fix)
        out.append(f'// {name}\n{{0x{rva:06x}, {length}, 0x{fnv1a32(raw):08x}u, {len(fix)}, {{{fixups}}}, {{{body}}}}},')
    return '\n'.join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--exe', type=Path, default=FL_DEFAULT_EXE)
    parser.add_argument('--json', action='store_true', help='print the full report')
    parser.add_argument('--emit-windows', action='store_true', help='print the C++ window rows of the sites header')
    args = parser.parse_args()
    if not args.exe.is_file():
        print(json.dumps({'result': 'SKIP', 'reason': f'{args.exe} not found'}))
        return 2
    data = args.exe.read_bytes()
    if args.emit_windows:
        print(emit_windows(data))
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
