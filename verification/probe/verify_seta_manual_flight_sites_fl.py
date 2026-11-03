#!/usr/bin/env python3
"""Read-only qualification of the FL SETA manual-flight sites in X3FL.exe (src/engine/fl/seta_manual_flight_sites_fl.h,
seta_manual_flight_fl.cpp; docs/reverse-engineering/fl/seta.md sections 3, 4 and "Port (2026-11-03)").

Checks on the installed exe (default /mnt/c/X3FL/X3FL.exe; every value an RVA, preferred base 0x400000):
- part A: the nine stop rows from 0x0d217f (9 bytes each: `cmp dword ptr [flag], 0` + short Jcc), each flag a
  relocated record+0x10, the eight rewritten Jcc (seven JNE to 0x0d21d0, the look JE to the join 0x0d21fb), the speed
  row's JNE kept; the stop block (cfg+0xbc == 0x10000 guard, the six-word cdecl StopFastForward call of 0x0bb500, the
  join's autopilot test); the qword geometry (only 0x0d218f straddles an aligned qword: plain write, emitted check);
  one page; no direct edge into a row's interior;
- part B: the five SSE spans (bytes, Capstone lengths 4+5+3+4 / 4+3+4+4, no relative flow, no relocation, no direct
  edge and no relocated pointer into an interior: the two raw false encodings 0x0d1f1d->0x0d1f12 and
  0x0d1f80->0x0d1f91 are pinned and proven not to be instruction starts), the continuation, the register contract
  (EDI = record from `mov edi, ecx`, ESI = ship, XMM4 = 2^-16, XMM0 = the double step, the COMISS after the seam;
  strafe ESI = cfg, EDI = ship, XMM6 = 20000.0, XMM7 = 0.001), the exact direct callers of the three controllers, the
  mouse dispatcher's four type words and its jump-table entries 1..3;
- every identity window of the header (bytes with relocated dwords zeroed, FNV-1a, fixup targets) and the AP addresses
  (the AP spans' bytes at their AP VAs do not match: an AP binding is refused).

usage: verify_seta_manual_flight_sites_fl.py [--exe PATH] [--json] [--emit-windows]   (exit 0 = PASS)
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
SITES = ROOT / 'src/engine/fl/seta_manual_flight_sites_fl.h'
BASE = P.BASE
ROT = '660f5ad8660f6e47240f5bc0f30f59c4'
# (axis, rva, bytes, instruction lengths, controller entry or 0, continuation)
SSE = (
    ('yaw', 0x0ce878, ROT, (4, 5, 3, 4), 0x0ce7c0),
    ('pitch', 0x0ceb88, ROT, (4, 5, 3, 4), 0x0cead0),
    ('roll', 0x0ced98, ROT, (4, 5, 3, 4), 0x0cece0),
    ('strafe_y', 0x0d1f04, 'f30f59c60f5ac0f20f59c7660f5ae0', (4, 3, 4, 4), 0),
    ('strafe_x', 0x0d1f8b, 'f30f59c60f5ac0f20f59c7660f5ac8', (4, 3, 4, 4), 0),
)
# Raw byte pairs that encode a branch into a span or a stop row from inside another instruction's operand (LOOPE/LOOP
# inside the strafe MULSS/MOVD, JS inside the row block's MOV); each source is proven not to be an instruction start of
# the input routine's linear decode from its entry 0x0cfd00.
FALSE_ENCODINGS = {(0x0d1f1d, 0x0d1f12), (0x0d1f80, 0x0d1f91)}
ROW_FALSE_ENCODINGS = {(0x0d215b, 0x0d21a5)}
INPUT_ROUTINE = (0x0cfd00, 0x0d2300)
RECORDS = {'yaw': 0x21c6c0, 'pitch': 0x21c6e8, 'roll': 0x21c710}
CALLERS = {0x0ce7c0: [0x0d1e90], 0x0cead0: [0x0d1e9b], 0x0cece0: [0x0d1ea6]}
STOP_ROWS, ROW_LEN = 0x0d217f, 9
FLAGS = (0x21c6d0, 0x21c6f8, 0x21c720, 0x21c748, 0x21c77c, 0x21c7b0, 0x21c7dc, 0x21c808, 0x21c840)
ROW_NAMES = ('yaw', 'pitch', 'roll', 'turret_yaw', 'turret_pitch', 'strafe_x', 'strafe_y', 'speed', 'look')
STOP, JOIN, SPEED_ROW = 0x0d21d0, 0x0d21fb, 7
CFG_SLOT, MOUSE = 0x1a83b0, (0x1a6c60, 0x1a6c64, 0x1a6c68, 0x1a6c6c)
# (name, rva, length, part) -- the header's identity windows, in its order.
WINDOW_SPANS = tuple((f'stop_{n}', STOP_ROWS + ROW_LEN * i, ROW_LEN, 1) for i, n in enumerate(ROW_NAMES)) + (
    ('stop_block', 0x0d21d0, 50, 1),          # cfg+0xbc guard, StopFastForward call, the join's autopilot test
    ('yaw_controller', 0x0ce7c0, 0xcb, 2),    # entry (EDI = record, ESI = ship) .. the COMISS after the seam
    ('pitch_controller', 0x0cead0, 0xcb, 2),
    ('roll_controller', 0x0cece0, 0xcb, 2),
    ('axis_calls', 0x0d1e8a, 0x21, 2),        # push edi (ship); mov ecx, record; call controller (x3)
    ('strafe_block', 0x0d1eab, 0x132, 2),     # ESI = cfg, XMM6/XMM7 constants, both strafe axes through ftol
    ('mouse_dispatch', 0x0cfa10, 0x75, 2),    # the four type words and the type 1..3 cases
    ('mouse_table', 0x0cfbe4, 12, 2),         # jump-table entries for types 1..3
    ('angle_scale', 0x191994, 4, 2),          # float 2^-16 (XMM4)
    ('milli', 0x191a68, 8, 2),                # double 0.001
    ('strafe_limit', 0x191ce4, 4, 2),         # float 20000.0 (XMM6)
)
DATA_WINDOWS = {'angle_scale': 0x37800000, 'milli': 0x3f50624dd2f1a9fc, 'strafe_limit': 0x469c4000}
ANCHORS = (
    (0x0ce7cb, 'mov edi, ecx', 'yaw controller: EDI = record'), (0x0ce7c7, 'mov esi, dword ptr [ebp + 8]', 'ESI = ship'),
    (0x0ceadb, 'mov edi, ecx', 'pitch controller: EDI = record'),
    (0x0ceceb, 'mov edi, ecx', 'roll controller: EDI = record'),
    (0x0ce800, 'movss xmm4, dword ptr [0x591994]', 'XMM4 = 2^-16'),
    (0x0ce85a, 'movss xmm2, dword ptr [edi]', 'XMM2 = the current rate'),
    (0x0ce85e, 'movd xmm0, dword ptr [eax + 0x704]', 'cfg+0x704 game ms'),
    (0x0ce870, 'mulsd xmm0, qword ptr [0x591a68]', 'XMM0 = double step'),
    (0x0ce888, 'comiss xmm0, xmm2', 'flags written after the yaw seam'),
    (0x0ceb98, 'comiss xmm0, xmm2', 'flags written after the pitch seam'),
    (0x0ceda8, 'comiss xmm0, xmm2', 'flags written after the roll seam'),
    (0x0d1eb1, 'mov esi, dword ptr [0x5a83b0]', 'strafe: ESI = cfg'),
    (0x0d1ec7, 'movss xmm6, dword ptr [0x591ce4]', 'XMM6 = 20000.0'),
    (0x0d1ecf, 'movsd xmm7, qword ptr [0x591a68]', 'XMM7 = 0.001'),
    (0x0d1ee2, 'mov eax, dword ptr [0x61c7f0]', 'strafe Y command'),
    (0x0d1f25, 'comiss xmm0, xmm1', 'flags written after the Y seam'),
    (0x0d1f6b, 'mov ecx, dword ptr [0x61c7c4]', 'strafe X command'),
    (0x0d1fac, 'comiss xmm0, xmm2', 'flags written after the X seam'),
    (0x0d1e8a, 'push edi', 'axis calls: ship argument'),
    (0x0d21d0, 'mov eax, dword ptr [0x5a83b0]', 'stop: cfg'),
    (0x0d21d5, 'cmp dword ptr [eax + 0xbc], 0x10000', 'stop: native factor guard'),
    (0x0d21df, 'je 0x4d21fb', 'stop: SETA off -> join'),
    (0x0d21f3, 'call 0x4bb500', 'stop: the SETA wrapper'),
    (0x0d21f8, 'add esp, 0x18', 'stop: six cdecl words'),
    (0x0d21fb, 'test dword ptr [edi + 0x40], 0x80000', 'join: autopilot test'),
    (0x0cfa1f, 'mov ebx, dword ptr [eax*4 + 0x5a6c68]', 'mouse: view-flag pair'),
    (0x0cfa28, 'mov ebx, dword ptr [eax*4 + 0x5a6c60]', 'mouse: ship-view pair'),
    (0x0cfa4b, 'jmp dword ptr [eax*4 + 0x4cfbe4]', 'mouse: type table'),
)
# AP preferred VAs of the same spans (seta_manual_flight_core.h): their AP bytes must not be found there in X3FL.exe.
AP_SPANS = ((0x40ebde, 'a1346f6000da8814070000'), (0x40ee3e, 'a1346f6000da8814070000'),
            (0x40f04d, 'a1346f6000da8814070000'), (0x411e0c, 'db8614070000'), (0x411eb2, 'db8614070000'),
            (0x4120cb, '7540'), (0x41210b, '7429'), (0x4b148c, 'a1346f60008b5301'))


def verify(data):
    report = {'result': 'FAIL', 'edition': 'fl', 'checks': {}}
    checks = report['checks']
    sha = hashlib.sha256(data).hexdigest()
    report['exe_info'] = {'sha256': sha, 'known': sha == P.FL_SHA256}
    img = P.FlImage(data)
    checks['fl_identity'] = {'ok': img.timestamp == P.FL_TIMESTAMP and img.image_size == P.FL_IMAGE_SIZE}
    ptrs = P.pointers(img)
    # ---- part B: the five SSE spans ----
    input_starts = P.starts(img, *INPUT_ROUTINE)
    spans = []
    for axis, rva, hexbytes, lengths, entry in SSE:
        want = bytes.fromhex(hexbytes)
        got = img.rva(rva, len(want))
        insns = img.insns(rva, rva + len(want)) or []
        hits = P.raw_branch_hits(img, rva, rva + len(want))
        foreign = [h for h in hits if h not in FALSE_ENCODINGS]
        pinned = [h for h in hits if h in FALSE_ENCODINGS]
        pinned_ok = all(src not in input_starts for src, _ in pinned) and 0x0d1f04 in input_starts
        rel = [i for i in insns if i.mnemonic.startswith(('j', 'call', 'loop', 'ret'))]
        fix = [f for f in img.fixups if rva - 3 <= f < rva + len(want)]
        ptr_in = sorted(p for p in ptrs if rva < p < rva + len(want))
        crossing = (rva & 7) + 5 > 8
        ok = (got == want and tuple(i.size for i in insns) == lengths and not rel and not fix and not foreign and
              pinned_ok and not ptr_in)
        row = {'axis': axis, 'rva': f'{rva:#08x}', 'continuation': f'{rva + len(want):#08x}', 'ok': ok,
               'lengths': [i.size for i in insns], 'text': [f'{i.mnemonic} {i.op_str}' for i in insns],
               'pinned_false_encodings': [[f'{a:#08x}', f'{t:#08x}'] for a, t in pinned],
               'foreign_edges': [[f'{a:#08x}', f'{t:#08x}'] for a, t in foreign],
               'write': 'plain' if crossing else 'cmpxchg8b'}
        if entry:
            callers = sorted(P.raw_callers(img, entry))
            row['callers'] = [f'{c:#08x}' for c in callers]
            row['ok'] = ok and callers == CALLERS[entry] and not ptrs.get(entry)
            # the record each caller passes in ECX (mov ecx, record; call entry)
            mov = img.text_at(CALLERS[entry][0] - 5)[0]
            row['record'] = mov
            row['ok'] = row['ok'] and mov == f'mov ecx, {BASE + RECORDS[axis]:#x}'
        spans.append(row)
    pinned_seen = {tuple(int(x, 16) for x in p) for r in spans for p in r['pinned_false_encodings']}
    checks['sse_spans'] = {'ok': all(r['ok'] for r in spans) and pinned_seen == FALSE_ENCODINGS,
                           'atomic': sum(r['write'] == 'cmpxchg8b' for r in spans),
                           'plain': [r['axis'] for r in spans if r['write'] == 'plain'], 'spans': spans}
    # ---- part A: the stop rows ----
    rows, row_pinned = [], set()
    for i, name in enumerate(ROW_NAMES):
        rva = STOP_ROWS + ROW_LEN * i
        insns = img.insns(rva, rva + ROW_LEN) or []
        texts = [f'{x.mnemonic} {x.op_str}' for x in insns]
        jcc_rva = rva + 7
        want_jcc = (f'je {BASE + JOIN:#x}' if name == 'look' else f'jne {BASE + STOP:#x}')
        hits = P.raw_branch_hits(img, rva, rva + ROW_LEN) + P.raw_branch_hits(img, jcc_rva, jcc_rva + 2)
        foreign = [h for h in hits if h not in ROW_FALSE_ENCODINGS or h[0] in input_starts]
        row_pinned |= {h for h in hits if h in ROW_FALSE_ENCODINGS}
        ok = (len(insns) == 2 and texts[0] == f'cmp dword ptr [{BASE + FLAGS[i]:#x}], 0' and texts[1] == want_jcc and
              rva + 2 in img.fixups and not foreign and rva in input_starts)
        rows.append({'row': name, 'rva': f'{rva:#08x}', 'jcc': f'{jcc_rva:#08x}', 'ok': ok, 'text': texts,
                     'patched': i != SPEED_ROW, 'qword_straddle': (jcc_rva & 7) + 2 > 8})
    straddle = [r['jcc'] for r in rows if r['patched'] and r['qword_straddle']]
    first, last = STOP_ROWS + 7, STOP_ROWS + ROW_LEN * 8 + 9
    checks['stop_rows'] = {'ok': all(r['ok'] for r in rows) and straddle == ['0x0d218f'] and
                           row_pinned == ROW_FALSE_ENCODINGS and
                           first >> 12 == (last - 1) >> 12, 'straddling': straddle,
                           'span': [f'{first:#08x}', f'{last:#08x}'], 'rows': rows,
                           'pinned_false_encodings': [[f'{a:#08x}', f'{t:#08x}'] for a, t in sorted(row_pinned)]}
    # ---- anchors, data constants, mouse table ----
    checks['anchors'] = P.anchor_rows(img, ANCHORS)
    data_ok = {}
    for name, value in DATA_WINDOWS.items():
        rva = next(w[1] for w in WINDOW_SPANS if w[0] == name)
        raw = img.rva(rva, 8 if name == 'milli' else 4)
        data_ok[name] = raw is not None and int.from_bytes(raw, 'little') == value
    table = [struct.unpack('<I', img.rva(0x0cfbe4 + 4 * k, 4))[0] - BASE for k in range(3)]
    checks['constants'] = {'ok': all(data_ok.values()) and table == [0x0cfa52, 0x0cfa63, 0x0cfa74],
                           **data_ok, 'mouse_table': [f'{t:#08x}' for t in table]}
    # ---- identity windows (and the header's rows) ----
    header = P.header_window_rows(SITES.read_text()) if SITES.is_file() else {}
    windows = {}
    for name, rva, length, part in WINDOW_SPANS:
        rec = P.window_record(img, rva, length)
        whole = name in DATA_WINDOWS or name == 'mouse_table' or img.insns(rva, rva + length) is not None
        h = P.fnv1a32(rec[0]) if rec else None
        in_header = header.get(name) == (rva, length, h, rec[1] if rec else None)
        windows[name] = {'ok': rec is not None and whole and in_header, 'rva': f'{rva:#08x}', 'length': length,
                         'part': part, 'fnv1a32': f'{h:08x}' if h is not None else None, 'header': in_header,
                         'fixups': [[o, f'{t:#08x}'] for o, t in rec[1]] if rec else None}
    checks['windows'] = {'ok': all(w['ok'] for w in windows.values()) and len(header) == len(WINDOW_SPANS),
                         'count': len(windows), **windows}
    # ---- the AP addresses are refused ----
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
    for name, rva, length, part in WINDOW_SPANS:
        raw, fix = P.window_record(img, rva, length)
        out.append(P.window_row(name, rva, length, raw, fix, f' {part},'))
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
