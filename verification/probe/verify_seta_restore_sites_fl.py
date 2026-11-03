#!/usr/bin/env python3
"""Read-only qualification of the FL SETA restore / key latch sites in X3FL.exe (src/engine/fl/seta_restore_sites_fl.h,
seta_restore_fl.cpp; docs/reverse-engineering/fl/seta.md sections 1, 2, 3 and "Port (2026-11-03)").

Checks on the installed exe (default /mnt/c/X3FL/X3FL.exe; every value an RVA, preferred base 0x400000):
- the factor hook 0x0ce09e (8 bytes, `mov ecx,[eax+1]` 3 + `mov eax,[cfg]` 5, the only HIGHLOW at +4 -> 0x1a83b0, so the
  claim's expected bytes and its tail are the live relocated instructions), no relative flow, no direct edge or
  relocated pointer into its interior, its qword geometry (offset 6: the jump straddles a qword, plain write with the
  emitted read-back), the continuation 0x0ce0a6 storing ECX into cfg+0xbc; the register contract EAX = argument cells
  (`mov eax,[esp+0x24]` at the case entry 0x0ce097), ESI = task (the dispatcher's `mov esi,[esp+0x14]`), two pending
  native words (push 1; push esi); TI_SetTimeWarpFactor = name slot 9, jump-table entry 9 -> the case;
- the SETA wrapper 0x0bb500 ABI: selector -> ECX, token -> EDX for the preparation 0x0bb310 (task +0x30/+0x34), argc
  read at entry ESP+0x18, AL0 on refusal, tail-jump 0x0bb470 (bottom cells, interpreter 0x0bed90, AL1); the
  callback-selector bounds `cmp ...,0x20; jge` on the success and failure paths (selector 32 = callback-free); the
  native stop call (six cdecl words, "StopFastForward");
- the story reader's layout and roots: VM slot 0x1aa174 (CODE +8, size +0x10, build +0x143c at the loader), the return
  scanner (tag10 above tag3, context+1, return+6, class +8), the interpreter's task IP +0x1c, the manager 0x1a82ec
  +0x38, game root 0x2243cc +0x4a0 bit 0, ship script +0x94, autopilot 0x80000 at +0x40;
- the key latch: action counters 0x22d0d8 zeroed (256 dwords) at the injection entry and incremented (< 0x400) by the
  held-key native; SETA action 675; the ring scalars (last read 0x22ddf0, indices 0x1a65e8/0x1a65a4, slots 0x1ab14c);
  the injection callers;
- every identity window of the header (bytes with relocated dwords zeroed, FNV-1a, fixup targets) and the AP addresses
  (the AP hook, wrapper and shared sites' bytes do not appear at their AP VAs: an AP binding is refused).

usage: verify_seta_restore_sites_fl.py [--exe PATH] [--json] [--emit-windows]   (exit 0 = PASS)
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import seta_fl_probe as P  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SITES = ROOT / 'src/engine/fl/seta_restore_sites_fl.h'
BASE = P.BASE
HOOK, HOOK_LEN, HOOK_NEXT, CFG_SLOT = 0x0ce09e, 8, 0x0ce0a6, 0x1a83b0
CASE, TABLE, CASE_INDEX, NAMES = 0x0ce097, 0x0ce1e4, 9, 0x1a6510
WRAPPER, WRAPPER_RUN, PREPARE, INTERPRETER = 0x0bb500, 0x0bb470, 0x0bb310, 0x0bed90
INJECT, COUNTERS, SETA_ACTION = 0x0cfd00, 0x22d0d8, 675
# (name, rva, length) -- the header's identity windows, in its order.
WINDOW_SPANS = (
    ('ti_case', 0x0ce097, 41),              # the TI_SetTimeWarpFactor case: args, pushes, the hook, store, ready list
    ('ti_table_entry', 0x0ce208, 4),        # jump-table entry 9 -> the case
    ('ti_dispatch', 0x0cdf30, 70),          # the published thunk (time owner) and the dispatcher's head (ESI = task)
    ('ready_callee', 0x0c1940, 24),         # the ready-list insert the case calls (RET 8)
    ('wrapper', 0x0bb500, 172),             # the SETA named-call wrapper
    ('wrapper_run', 0x0bb470, 139),         # bottom cells, interpreter, AL 1
    ('callback_bound_ok', 0x0be9de, 21),    # cmp edx,0x20; jge (success completion)
    ('callback_bound_fail', 0x0bb22d, 36),  # cmp eax,0x20; jge (missing method)
    ('stop_call', 0x0d21e1, 26),            # the native StopFastForward call (selector 0, six words)
    ('stop_name', 0x18f048, 16),            # "StopFastForward"
    ('return_scan', 0x0c0836, 38),          # tag10 above tag3, context+1, return+6, class +8
    ('native_ip', 0x0c0751, 11),            # task +0x18 index, +0x1c next CODE offset
    ('manager', 0x059d58, 15),              # *0x1a82ec +0x38 player, its parent +0x54 (outside the alert routine)
    ('pause', 0x0786ad, 31),                # game root +0x4a0 bit 0, last ring read 0x22ddf0
    ('inject_head', 0x0cfd06, 48),          # after chase's claimed prologue: ECX spilled, input context, counters zeroed
    ('counter_increment', 0x0d05f9, 13),    # cmp eax,0x400; inc byte [eax+counters]
    ('ring_reader', 0x0ecd1f, 36),          # ring read/write indices
    ('ring_slots', 0x0ecd76, 8),            # the four 12-byte slots
    ('ship_script', 0x06a81d, 31),          # resolver then [eax+0x94]
    ('autopilot', 0x0d21fb, 7),             # test [edi+0x40],0x80000
    ('vm_build', 0x0b90af, 6),              # mov [edi+0x143c],eax
    ('vm_code', 0x0b9122, 30),              # size +0x10 .. CODE +8
)
DATA = {'ti_table_entry', 'stop_name'}
ANCHORS = (
    (0x0ce097, 'mov eax, dword ptr [esp + 0x24]', 'case: EAX = argument cells'),
    (0x0ce09b, 'push 1', 'case: first pending native word'),
    (0x0ce09d, 'push esi', 'case: second pending word = task'),
    (0x0ce09e, 'mov ecx, dword ptr [eax + 1]', 'hook: the new factor'),
    (0x0ce0a1, 'mov eax, dword ptr [0x5a83b0]', 'hook: cfg (HIGHLOW)'),
    (0x0ce0a6, 'mov dword ptr [eax + 0xbc], ecx', 'continuation: cfg+0xbc store'),
    (0x0ce0ac, 'mov ecx, dword ptr [0x5aa174]', 'case: ECX = VM'),
    (0x0ce0b2, 'call 0x4c1940', 'case: ready-list insert'),
    (0x0ce0bd, 'ret 0x14', 'dispatcher returns RET 0x14'),
    (0x0cdf34, 'mov ecx, dword ptr [0x5aa17c]', 'thunk: time owner'),
    (0x0cdf4a, 'call 0x4cdf50', 'thunk -> dispatcher'),
    (0x0cdf5c, 'mov esi, dword ptr [esp + 0x14]', 'dispatcher: ESI = task'),
    (0x0cdf63, 'mov ebx, dword ptr [esi + 8]', 'dispatcher: EBX = [task+8]'),
    (0x0cdf66, 'cmp eax, 0xf', 'dispatcher: 16 cases'),
    (0x0cdf6f, 'jmp dword ptr [eax*4 + 0x4ce1e4]', 'dispatcher: case table'),
    (0x0bb503, 'mov edx, dword ptr [esp + 0x14]', 'wrapper: token -> EDX'),
    (0x0bb50b, 'mov ecx, dword ptr [esp + 0x10]', 'wrapper: selector -> ECX'),
    (0x0bb53a, 'call 0x4bb310', 'wrapper: preparation'),
    (0x0bb542, 'test al, al', 'wrapper: AL0 unresolved receiver/method'),
    (0x0bb556, 'mov ebx, dword ptr [esp + 0x34]', 'wrapper: argc at entry ESP+0x18'),
    (0x0bb59d, 'jmp 0x4bb470', 'wrapper: tail-jump to the run'),
    (0x0bb5a5, 'xor al, al', 'wrapper: AL 0'),
    (0x0bb4f0, 'call 0x4bed90', 'run: the interpreter'),
    (0x0bb4f7, 'mov al, 1', 'run: AL 1 after a normal return'),
    (0x0bb3fd, 'mov dword ptr [edx + 0x30], eax', 'preparation: selector -> task +0x30'),
    (0x0bb407, 'mov dword ptr [edx + 0x34], eax', 'preparation: token -> task +0x34'),
    (0x0be9ee, 'cmp edx, 0x20', 'selector bound (success)'),
    (0x0be9f1, 'jge 0x4bea07', 'selector >= 32 skips the callback (success)'),
    (0x0bb248, 'cmp eax, 0x20', 'selector bound (failure)'),
    (0x0bb24b, 'jge 0x4bb2f3', 'selector >= 32 skips the callback (failure)'),
    (0x0d21e5, 'push 0x58f048', 'native stop: "StopFastForward"'),
    (0x0d21ea, 'push 0x96', 'native stop: key 150'),
    (0x0d21f3, 'call 0x4bb500', 'native stop: the SETA wrapper'),
    (0x0d21f8, 'add esp, 0x18', 'native stop: six cdecl words'),
    (0x0c0836, 'cmp al, 0xa', 'return scan: tag 10'),
    (0x0c083a, 'cmp byte ptr [edi + 5], 3', 'return scan: tag 3 above'),
    (0x0c0840, 'mov ecx, dword ptr [edi + 6]', 'return scan: return offset'),
    (0x0c0843, 'mov edx, dword ptr [edi + 1]', 'return scan: context'),
    (0x0c0858, 'cmp dword ptr [edx + 8], 0', 'return scan: context class +8'),
    (0x0c0751, 'mov dword ptr [edx + 0x18], eax', 'interpreter: task +0x18 index'),
    (0x0c0759, 'mov dword ptr [edx + 0x1c], eax', 'interpreter: task +0x1c next IP'),
    (0x059d58, 'mov eax, dword ptr [0x5a82ec]', 'manager slot'),
    (0x059d5d, 'mov eax, dword ptr [eax + 0x38]', 'manager +0x38 player'),
    (0x059d64, 'cmp dword ptr [eax + 0x54], edx', 'player +0x54 parent'),
    (0x0786ad, 'mov edx, dword ptr [0x6243cc]', 'game root'),
    (0x0786b6, 'mov word ptr [0x62ddf0], si', 'last ring read'),
    (0x0786bd, 'mov ecx, dword ptr [edx + 0x4a0]', 'pause bits +0x4a0'),
    (0x0786c3, 'test cl, 1', 'pause bit 0'),
    (0x0cfd0f, 'mov dword ptr [esp + 0x20], ecx', 'inject: ECX = controlled object'),
    (0x0cfd20, 'mov edi, 0x62d0d8', 'inject: action counters'),
    (0x0cfd13, 'mov ecx, 0x100', 'inject: 256 dwords'),
    (0x0cfd34, 'rep stosd dword ptr es:[edi], eax', 'inject: counters zeroed'),
    (0x0d05f9, 'cmp eax, 0x400', 'held key: action bound'),
    (0x0d0600, 'inc byte ptr [eax + 0x62d0d8]', 'held key: counter += 1'),
    (0x0ecd1f, 'mov cx, word ptr [0x5a65e8]', 'ring read index'),
    (0x0ecd26, 'mov dx, word ptr [0x5a65a4]', 'ring write index'),
    (0x0ecd76, 'movzx ecx, byte ptr [eax*4 + 0x5ab14c]', 'ring slots'),
    (0x06a836, 'mov eax, dword ptr [eax + 0x94]', 'ship script +0x94'),
    (0x0d21fb, 'test dword ptr [edi + 0x40], 0x80000', 'autopilot bit'),
    (0x0ce7d1, 'test dword ptr [esi + 0x40], 0x80000', 'autopilot bit (controller)'),
    (0x0d227b, 'and dword ptr [edi + 0x40], 0xfff7ffff', 'autopilot cleared'),
    (0x0b90af, 'mov dword ptr [edi + 0x143c], eax', 'VM build +0x143c'),
    (0x0b9122, 'mov dword ptr [edi + 0x10], ecx', 'VM CODE size +0x10'),
    (0x0b913b, 'mov dword ptr [edi + 8], eax', 'VM CODE +8'),
)
CALLERS = {INJECT: [0x0787c8, 0x0787dc], 0x0cdf50: [0x0cdf4a]}
# AP preferred VAs (seta_restore_core.h, chase_transition.cpp): their AP bytes must not be found there in X3FL.exe.
AP_SPANS = ((0x4b148c, 'a1346f60008b5301'), (0x49f4c0, '83ec0c8b542414'), (0x410100, '558bec83e4f0'),
            (0x49ea80, '83ec08558b6c2410'), (0x4a0880, '6aff68c8005300'), (0x52f298, 'b8f4e55600'))


def verify(data):
    report = {'result': 'FAIL', 'edition': 'fl', 'checks': {}}
    checks = report['checks']
    sha = hashlib.sha256(data).hexdigest()
    report['exe_info'] = {'sha256': sha, 'known': sha == P.FL_SHA256}
    img = P.FlImage(data)
    checks['fl_identity'] = {'ok': img.timestamp == P.FL_TIMESTAMP and img.image_size == P.FL_IMAGE_SIZE}
    ptrs = P.pointers(img)
    # ---- the factor hook ----
    got = img.rva(HOOK, HOOK_LEN)
    insns = img.insns(HOOK, HOOK + HOOK_LEN) or []
    fix = sorted(f - HOOK for f in img.fixups if HOOK - 3 <= f < HOOK + HOOK_LEN)
    cfg = struct.unpack_from('<I', got, 4)[0] - BASE if got else None
    hits = P.raw_branch_hits(img, HOOK, HOOK + HOOK_LEN)
    case_starts = P.starts(img, CASE, 0x0ce0c0)
    foreign = [h for h in hits if h[0] in case_starts or not (CASE <= h[0] < 0x0ce0c0)]
    ok = (got is not None and got[:4] == bytes.fromhex('8b4801a1') and cfg == CFG_SLOT and fix == [4] and
          tuple(i.size for i in insns) == (3, 5) and not foreign and
          not [p for p in ptrs if HOOK < p < HOOK + HOOK_LEN] and not ptrs.get(HOOK) and HOOK in case_starts and
          HOOK_NEXT in case_starts and (HOOK & 7) + 5 > 8)
    checks['factor_hook'] = {'ok': ok, 'rva': f'{HOOK:#08x}', 'continuation': f'{HOOK_NEXT:#08x}',
                             'text': [f'{i.mnemonic} {i.op_str}' for i in insns], 'fixups': fix,
                             'fixup_target': f'{cfg:#08x}' if cfg is not None else None,
                             'raw_hits': [[f'{a:#08x}', f'{t:#08x}'] for a, t in hits],
                             'write': 'plain' if (HOOK & 7) + 5 > 8 else 'cmpxchg8b'}
    entry = struct.unpack('<I', img.rva(TABLE + 4 * CASE_INDEX, 4))[0] - BASE
    name_ptr = struct.unpack('<I', img.rva(NAMES + 4 * CASE_INDEX, 4))[0] - BASE
    name = (img.rva(name_ptr, 32) or b'').split(b'\0')[0].decode('ascii', 'replace')
    publish = sorted(ptrs.get(0x0cdf30, []))
    checks['ti_case'] = {'ok': entry == CASE and TABLE + 4 * CASE_INDEX in img.fixups and
                         name == 'TI_SetTimeWarpFactor' and publish == [0x0cde1f] and
                         not P.raw_callers(img, 0x0cdf30),
                         'table_entry': f'{entry:#08x}', 'name_slot_9': name,
                         'thunk_published_at': [f'{p:#08x}' for p in publish]}
    callers = {}
    for callee, want in CALLERS.items():
        c = sorted(P.raw_callers(img, callee))
        callers[f'{callee:#08x}'] = {'ok': c == want, 'callers': [f'{x:#08x}' for x in c]}
    checks['callers'] = {'ok': all(c['ok'] for c in callers.values()), **callers}
    checks['anchors'] = P.anchor_rows(img, ANCHORS)
    stop_name = (img.rva(0x18f048, 16) or b'')
    counter_ok = COUNTERS + SETA_ACTION == 0x22d37b and COUNTERS + 524 == 0x22d2e4
    checks['constants'] = {'ok': stop_name == b'StopFastForward\0' and counter_ok,
                           'stop_name': stop_name.rstrip(b'\0').decode('ascii', 'replace'),
                           'seta_counter': f'{COUNTERS + SETA_ACTION:#08x}', 'steer_counters': f'{COUNTERS + 524:#08x}'}
    # ---- identity windows (and the header's rows) ----
    header = P.header_window_rows(SITES.read_text()) if SITES.is_file() else {}
    windows = {}
    for wname, rva, length in WINDOW_SPANS:
        rec = P.window_record(img, rva, length)
        whole = wname in DATA or img.insns(rva, rva + length) is not None
        h = P.fnv1a32(rec[0]) if rec else None
        in_header = header.get(wname) == (rva, length, h, rec[1] if rec else None)
        windows[wname] = {'ok': rec is not None and whole and in_header, 'rva': f'{rva:#08x}', 'length': length,
                          'fnv1a32': f'{h:08x}' if h is not None else None, 'header': in_header,
                          'fixups': [[o, f'{t:#08x}'] for o, t in rec[1]] if rec else None}
    checks['windows'] = {'ok': all(w['ok'] for w in windows.values()) and len(header) == len(WINDOW_SPANS),
                         'count': len(windows), **windows}
    ap = []
    for va, hexbytes in AP_SPANS:
        got = img.read(va, len(hexbytes) // 2)
        ap.append({'va': f'{va:#010x}', 'matches': got == bytes.fromhex(hexbytes)})
    text = SITES.read_text() if SITES.is_file() else ''
    ap_in_header = [f'{va:#010x}' for va, _ in AP_SPANS if f'{va:#010x}' in text or f'{va:#x}' in text]
    checks['ap_refused'] = {'ok': not any(a['matches'] for a in ap) and not ap_in_header, 'ap_spans': ap,
                            'ap_in_header': ap_in_header}
    report['result'] = 'PASS' if all(c.get('ok') for c in checks.values()) else 'FAIL'
    return report


def emit_windows(data):
    img = P.FlImage(data)
    out = []
    for name, rva, length in WINDOW_SPANS:
        raw, fix = P.window_record(img, rva, length)
        out.append(P.window_row(name, rva, length, raw, fix, ' 0,'))
    return '\n'.join(out)


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
