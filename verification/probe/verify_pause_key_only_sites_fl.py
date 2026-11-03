#!/usr/bin/env python3
"""Read-only qualification of the FL pause-key-only site against X3FL.exe.

Checks the site of src/engine/fl/pause_key_only_fl.cpp (13 bytes at RVA 0x07880c inside the in-flight input function
RVA 0x078690) and the two identity windows of src/engine/fl/pause_key_only_sites_fl.h on the installed executable
(Capstone over the file; nothing executed): exact bytes and the FNV-1a of every window with its HIGHLOW dwords zeroed,
the .reloc census of the windows and the span, whole-instruction decodes of the original and of the replacement for the
default, a Shift and a range-end key (same boundaries at both ends, the same two branch targets, only EFLAGS written),
inbound direct edges (none into the span or its first byte; the exit's three, the mouse test's one, the loop head's
one, the function's one caller, none from outside), the [esp+0x14] writers (both zero-extended keys), the ESI writers,
EAX dead on both successors, the context +0x4a0 writer census (the exit is the sole bit-0 clearer after construction),
DIK_PAUSE -> 0x1b5 through the converter's tables, the Shift bit, the operand anchors, and the AP addresses refused (the
AP window is absent from X3FL.exe, the bytes at the AP VA differ, the header names no AP address). With the header
present its window bytes, table and site are compared with the executable too.
docs/reverse-engineering/fl/pause-background.md, "Port (2026-11-03)".

usage: verify_pause_key_only_sites_fl.py [--exe PATH] [--json] [--out FILE]   (exit 0 = PASS, 2 = executable not found)
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

from verify_chase_camera_site import (FlImage, fnv1a32, IMAGE_BASE, FL_DEFAULT_EXE, FL_SHA256, FL_TIMESTAMP,
                                      FL_IMAGE_SIZE, FL_HIGHLOW_COUNT)

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'src/engine/fl/pause_key_only_sites_fl.h'
AP_CORE = ROOT / 'src/proxy/pause_key_only_core.h'
# (name, start RVA, end RVA, {fixup RVA: target RVA}) - pause_key_only_sites_fl.h `windows`.
WINDOWS = (
    ('entry', 0x078690, 0x0786cc, {0x07869d: 0x22ddf0, 0x0786af: 0x2243cc, 0x0786b9: 0x22ddf0}),
    ('loop', 0x0787ee, 0x078876, {0x0787ef: 0x1a8478, 0x07882f: 0x1a8478, 0x078860: 0x2243cc}),
)
WINDOW_FNV = {'entry': 0x87e58492, 'loop': 0x8180c9b7}
SITE, SITE_LEN = 0x07880c, 13
ORIGINAL = bytes.fromhex('8bc633442414a9ff0f00007545')
ORIGINAL_TEXT = (('mov eax, esi', 0x07880c), ('xor eax, dword ptr [esp + 0x14]', 0x07880e),
                 ('test eax, 0xfff', 0x078812), ('jne 0x47885e', 0x078817))
MOUSE_TEST, EXIT, LOOP_HEAD, LOOP_ENTRY = 0x078819, 0x07885e, 0x078807, 0x0787ee
FUNCTION = (0x078690, 0x0788ab)
CALLERS = (0x078150,)
EXIT_EDGES = (0x078817, 0x07881f, 0x078827)
PREV_KEY_WRITES = {0x0786a4: 'ebx', 0x07884c: 'eax'}
PREV_KEY_SOURCES = ((0x07869a, 'movzx ebx, word ptr [0x62ddf0]'), (0x078845, 'movzx eax, si'))
# ESI writers on the paths that reach the site: entry to the paused branch (straight line 0x0786b3..0x0786c6, the loop's
# only way in) and the loop window; the other writers lie on the unpaused paths and the epilogue.
ESI_WRITES = (0x0786b3, 0x078859)
CTX_WRITERS = (0x075ec9, 0x07819c, 0x0786e8, 0x078870, 0x07b5b5, 0x07b7c4)  # [reg + 0x4a0] stores in .text
KEYS = (0x1b5, 0x11b5, 0x1fff, 0x2c)
# Operand anchors (RVA, Capstone text at the preferred base, what it establishes).
ANCHORS = (
    (0x078699, 'push ebx', 'entry saves EBX'),
    (0x07869a, 'movzx ebx, word ptr [0x62ddf0]', 'previous key, zero-extended'),
    (0x0786a4, 'mov dword ptr [esp + 0x14], ebx', 'previous key at [esp+0x14]'),
    (0x0786a8, 'call 0x4eccd0', 'key reader'),
    (0x0786b3, 'movzx esi, ax', 'ESI = the key, zero-extended'),
    (0x0786bd, 'mov ecx, dword ptr [edx + 0x4a0]', 'context flags'),
    (0x0786c3, 'test cl, 1', 'paused bit'),
    (0x0786c6, 'jne 0x4787ee', 'paused: the wait loop'),
    (0x0787ee, 'mov eax, dword ptr [0x5a8478]', 'input block'),
    (0x0787f3, 'mov ecx, dword ptr [eax + 0x454]', 'mouse buttons'),
    (0x0787f9, 'mov ebx, ecx', 'EBX = mouse snapshot (AP cmp si,bx is wrong here)'),
    (0x0787fb, 'mov ebp, dword ptr [eax + 0x450]', 'joystick buttons'),
    (0x078801, 'mov edi, ebp', 'EDI = joystick snapshot'),
    (0x078807, 'test si, si', 'loop head: any key'),
    (0x07880a, 'je 0x478819', 'no key: the mouse test'),
    (0x078819, 'mov eax, ebx', 'mouse test writes EAX first (EAX dead after the site)'),
    (0x07881b, 'or eax, ecx', 'mouse rising bit'),
    (0x07881d, 'cmp eax, ebx', 'mouse rising bit'),
    (0x07881f, 'jne 0x47885e', 'exit: new mouse button'),
    (0x078821, 'mov eax, edi', 'joystick test'),
    (0x078827, 'jne 0x47885e', 'exit: new joystick button'),
    (0x078829, 'call 0x4ec620', 'message pump + input poll'),
    (0x078845, 'movzx eax, si', 'next previous key, zero-extended'),
    (0x07884c, 'mov dword ptr [esp + 0x14], eax', 'previous key stored'),
    (0x078850, 'call 0x4eccd0', 'next key'),
    (0x078855, 'mov ecx, dword ptr [esp + 0x10]', 'mouse reloaded after the reader'),
    (0x078859, 'movzx esi, ax', 'ESI = next key, zero-extended'),
    (0x07885c, 'jmp 0x478807', 'loop'),
    (0x07885e, 'mov ecx, dword ptr [0x6243cc]', 'exit: context'),
    (0x078864, 'mov eax, dword ptr [ecx + 0x4a0]', 'exit writes EAX before reading it'),
    (0x07886a, 'and eax, 0xfffffffe', 'clear paused'),
    (0x07886d, 'or eax, 2', 'swallow the ending key'),
    (0x078870, 'mov dword ptr [ecx + 0x4a0], eax', 'flags stored'),
    (0x0788aa, 'ret', 'plain RET'),
    (0x075ec9, 'mov dword ptr [esi + 0x4a0], 0', 'constructor zeroes the flags'),
    (0x07b7c4, 'or dword ptr [esi + 0x4a0], edx', 'X2_SetPause sets bit 0'),
    (0x0eccd0, 'mov eax, dword ptr [0x5a8478]', 'key reader entry'),
    (0x0ecd7e, 'call 0x4efb70', 'key reader: DIK -> engine code'),
    (0x0ecd83, 'mov ecx, dword ptr [0x5ab17c]', 'Shift state'),
    (0x0ecd8d, 'and ecx, 0x1000', 'Shift bit 0x1000'),
    (0x0ecd93, 'or eax, ecx', 'key | Shift'),
    (0x0efb70, 'dec ecx', 'converter: DIK - 1'),
    (0x0efb7d, 'movzx eax, byte ptr [ecx + 0x4f0134]', 'converter byte table'),
    (0x0efb84, 'jmp dword ptr [eax*4 + 0x4efef0]', 'converter pointer table'),
    (0x0efe4f, 'mov eax, 0x1b5', 'DIK_PAUSE -> 0x1b5'),
)
DIK_PAUSE, BYTE_TABLE, PTR_TABLE, PAUSE_CASE = 0xc5, 0x0f0134, 0x0efef0, 0x0efe4f
AP_VAS = ('0x004043a0', '0x004043a5', '0x004043b1', '0x004043d8', '0x0057fc60', '0x00606f3c', '0x004d3b60',
          '0x004d34b0')


def encode(key):
    return bytes([0x66, 0x81, 0xfe, key & 0xff, (key >> 8) & 0xff, 0x75, 0x06, 0x3b, 0x74, 0x24, 0x14, 0x75, 0x45])


def decode_span(img, code, rva):
    insns = list(img.md.disasm(code, IMAGE_BASE + rva))
    return insns if insns and sum(i.size for i in insns) == len(code) else None


def written(i):
    _, w = i.regs_access()
    return {i.reg_name(r) for r in w}


def header_tables():
    text = HEADER.read_text()
    arrays = {m.group(1): bytes(int(x, 16) for x in re.findall(r'0x[0-9a-f]{2}\b', m.group(3)))
              for m in re.finditer(r'constexpr unsigned char (\w+)_window\[(\d+)\] = \{(.*?)\};', text, re.S)}
    table = {}
    for m in re.finditer(r'\{"(\w+)", 0x([0-9a-f]+), (\d+), \w+_window, 0x([0-9a-f]+)u, (\d+), \{([^}]*)\}, '
                         r'\{([^}]*)\}\}', text):
        offs = [int(x, 16) for x in m.group(6).split(',') if x.strip()]
        tgts = [int(x, 16) for x in m.group(7).split(',') if x.strip()]
        table[m.group(1)] = (int(m.group(2), 16), int(m.group(3)), int(m.group(4), 16), offs, tgts)
    original = re.search(r'original\[site_length\] = \{(.*?)\};', text, re.S)
    return arrays, table, bytes(int(x, 16) for x in re.findall(r'0x[0-9a-f]{2}\b', original.group(1))), text


def ap_window():
    m = re.search(r'constexpr unsigned char window\[window_length\] = \{(.*?)\};', AP_CORE.read_text(), re.S)
    return bytes(int(x, 16) for x in re.findall(r'0x[0-9a-f]{2}\b', m.group(1)))


def ctx_writers(img):
    """[reg + 0x4a0] memory stores in .text (linear sweep; ESP-relative locals excluded)."""
    import capstone
    _, start, _, rp, rsize = next(s for s in img.sections if s[0] == '.text')
    code = img.data[rp:rp + rsize]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    out, pos = [], 0
    while pos < len(code):
        last = pos
        for i in md.disasm(code[pos:pos + 0x10000], start + pos):
            last = i.address + i.size - start
            dest = i.op_str.split(',')[0]
            if i.mnemonic not in ('cmp', 'test', 'push') and ',' in i.op_str and \
                    re.fullmatch(r'\w+ ptr \[(eax|ebx|ecx|edx|esi|edi|ebp) \+ 0x4a0\]', dest):
                out.append(i.address - IMAGE_BASE)
        pos = last if last > pos else pos + 1
    return sorted(set(out))


def verify(data, source=True):
    report = {'result': 'FAIL', 'edition': 'fl', 'checks': {}}
    checks = report['checks']
    report['exe_info'] = {'sha256': hashlib.sha256(data).hexdigest(), 'known': hashlib.sha256(data).hexdigest() == FL_SHA256}
    img = FlImage(data)
    checks['fl_identity'] = {'ok': img.timestamp == FL_TIMESTAMP and img.image_size == FL_IMAGE_SIZE and
                             bool(img.dll_characteristics & 0x40) and len(img.fixups) == FL_HIGHLOW_COUNT and
                             not img.other_fixups,
                             'timestamp': f'{img.timestamp:#010x}', 'highlow': len(img.fixups)}
    windows, measured = {}, {}
    for name, start, end, relocs in WINDOWS:
        raw = bytearray(img.rva(start, end - start) or b'')
        fixups = {f for f in img.fixups if start - 3 <= f < end}
        targets = {}
        for f in sorted(fixups):
            if start <= f and f + 4 <= end:
                targets[f] = struct.unpack_from('<I', raw, f - start)[0] - IMAGE_BASE
                raw[f - start:f - start + 4] = b'\0\0\0\0'
        whole = img.insns(start, end) is not None
        measured[name] = bytes(raw)
        windows[name] = {'ok': len(raw) == end - start and whole and fnv1a32(raw) == WINDOW_FNV[name] and
                         targets == relocs and fixups == set(relocs),
                         'fnv1a32': f'{fnv1a32(raw):08x}', 'whole_instructions': whole,
                         'relocs': {f'{f:#08x}': f'{t:#08x}' for f, t in sorted(targets.items())}}
    checks['windows'] = {'ok': all(w['ok'] for w in windows.values()), **windows}
    edges = img.direct_edges()
    raw = img.rva(SITE, SITE_LEN)
    insns = decode_span(img, raw, SITE) or []
    texts = tuple((f'{i.mnemonic} {i.op_str}'.strip(), i.address - IMAGE_BASE) for i in insns)
    original_writes = set().union(*(written(i) for i in insns)) if insns else set()
    interior = sorted(a for a, _, t in edges if SITE < t < SITE + SITE_LEN)
    to_start = sorted(a for a, _, t in edges if t == SITE)
    fixups = sorted(f for f in img.fixups if SITE - 3 <= f < SITE + SITE_LEN)
    previous, previous_size = img.text_at(SITE - 2)
    checks['site'] = {'ok': raw == ORIGINAL and texts == ORIGINAL_TEXT and not interior and not to_start and
                      not fixups and previous == 'je 0x478819' and previous_size == 2 and
                      original_writes == {'eax', 'eflags'} and (SITE & 7) + SITE_LEN > 8,
                      'bytes': raw.hex(), 'decode': [t for t, _ in texts], 'writes': sorted(original_writes),
                      'previous': previous, 'interior_edges': interior, 'edges_to_start': to_start, 'fixups': fixups,
                      'direct_edges': len(edges), 'qword': 'crosses' if (SITE & 7) + SITE_LEN > 8 else 'fits'}
    candidates = {}
    for key in KEYS:
        code = encode(key)
        c = decode_span(img, code, SITE) or []
        ctexts = tuple(f'{i.mnemonic} {i.op_str}'.strip() for i in c)
        want = (f'cmp si, {key:#x}', 'jne 0x478819', 'cmp esi, dword ptr [esp + 0x14]', 'jne 0x47885e')
        writes = set().union(*(written(i) for i in c)) if c else set()
        candidates[f'{key:#x}'] = {'ok': len(code) == SITE_LEN and ctexts == want and writes == {'eflags'} and
                                   code[-2:] == ORIGINAL[-2:],
                                   'bytes': code.hex(), 'decode': list(ctexts), 'writes': sorted(writes)}
    checks['replacement'] = {'ok': all(c['ok'] for c in candidates.values()), **candidates}
    ins = img.insns(*FUNCTION) or []
    prev_writes, esi_writes = {}, []
    for i in ins:
        rva = i.address - IMAGE_BASE
        if i.op_str.startswith('dword ptr [esp + 0x14],') and i.mnemonic == 'mov':
            prev_writes[rva] = i.op_str.split(', ')[1]
        if written(i) & {'esi', 'si'} and (0x0786a4 <= rva < 0x0786cc or LOOP_ENTRY <= rva < 0x078876):
            esi_writes.append(rva)
    sources_ok = all(img.text_at(a)[0] == t for a, t in PREV_KEY_SOURCES)
    callers = sorted(a for a, m, t in edges if t == FUNCTION[0])
    outside = sorted(a for a, m, t in edges if FUNCTION[0] < t < FUNCTION[1] and not FUNCTION[0] <= a < FUNCTION[1])
    def into(target):
        return sorted(a for a, _, t in edges if t == target)
    checks['loop_edges'] = {
        'ok': bool(ins) and tuple(callers) == CALLERS and not outside and tuple(into(EXIT)) == EXIT_EDGES and
        into(MOUSE_TEST) == [0x07880a] and into(LOOP_HEAD) == [0x07885c] and into(LOOP_ENTRY) == [0x0786c6] and
        prev_writes == PREV_KEY_WRITES and sources_ok and tuple(esi_writes) == ESI_WRITES,
        'instructions': len(ins), 'callers': [f'{a:#08x}' for a in callers],
        'edges_from_outside': [f'{a:#08x}' for a in outside], 'exit_edges': [f'{a:#08x}' for a in into(EXIT)],
        'mouse_test_edges': [f'{a:#08x}' for a in into(MOUSE_TEST)],
        'loop_head_edges': [f'{a:#08x}' for a in into(LOOP_HEAD)],
        'loop_entry_edges': [f'{a:#08x}' for a in into(LOOP_ENTRY)],
        'previous_key_writes': {f'{a:#08x}': s for a, s in prev_writes.items()}, 'zero_extended_sources': sources_ok,
        'esi_writes': [f'{a:#08x}' for a in esi_writes]}
    writers = ctx_writers(img)
    checks['context_flag_writers'] = {'ok': tuple(writers) == CTX_WRITERS, 'writers': [f'{a:#08x}' for a in writers]}
    # DIK_PAUSE through the converter: dec ecx; byte table; pointer table (a relocated dword) -> mov eax,0x1b5; ret.
    index = img.rva(BYTE_TABLE + DIK_PAUSE - 1, 1)[0]
    slot = PTR_TABLE + 4 * index
    target = struct.unpack('<I', img.rva(slot, 4))[0] - IMAGE_BASE
    ret, _ = img.text_at(PAUSE_CASE + 5)
    checks['pause_key_code'] = {'ok': target == PAUSE_CASE and slot in img.fixups and ret == 'ret',
                                'index': index, 'slot': f'{slot:#08x}', 'target': f'{target:#08x}'}
    anchors = []
    for at, want, what in ANCHORS:
        got, _ = img.text_at(at)
        anchors.append({'rva': f'{at:#08x}', 'ok': got == want, 'got': got, 'what': what})
    checks['anchors'] = {'ok': all(a['ok'] for a in anchors), 'count': len(anchors),
                         'failed': [a for a in anchors if not a['ok']]}
    ap = ap_window()
    at_ap_va = img.read(0x004043a0, len(ap))
    checks['ap_refused'] = {'ok': len(ap) == 79 and data.find(ap) < 0 and at_ap_va != ap and
                            data.find(ap[5:17]) < 0,
                            'ap_window_hits': int(data.find(ap) >= 0), 'ap_site_hits': int(data.find(ap[5:17]) >= 0),
                            'bytes_at_ap_va_equal': at_ap_va == ap}
    if source:
        arrays, table, original, text = header_tables()
        rows = {}
        for name, start, end, relocs in WINDOWS:
            row = table.get(name)
            rows[name] = bool(row) and arrays.get(name) == measured[name] and row[0] == start and \
                row[1] == end - start and row[2] == WINDOW_FNV[name] and \
                [start + o for o in row[3]] == sorted(relocs) and row[4] == [relocs[k] for k in sorted(relocs)]
        rows['site'] = original == ORIGINAL and f'site_rva = 0x{SITE:06x}' in text and \
            f'exit_rva = 0x{EXIT:06x}' in text and f'mouse_test_rva = 0x{MOUSE_TEST:06x}' in text
        lowered = re.sub(r'//.*', '', text).lower()
        rows['no_ap_address'] = not any(v in lowered or v.replace('0x00', '0x', 1) in lowered for v in AP_VAS)
        checks['source_header'] = {'ok': all(rows.values()) and len(table) == len(WINDOWS), **rows}
    report['result'] = 'PASS' if all(c.get('ok') for c in checks.values()) else 'FAIL'
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--exe', type=Path, default=FL_DEFAULT_EXE)
    parser.add_argument('--json', action='store_true', help='print the full report')
    parser.add_argument('--out', type=Path, help='also write the full report here')
    args = parser.parse_args()
    if not args.exe.is_file():
        print(json.dumps({'result': 'SKIP', 'reason': f'{args.exe} not found'}))
        return 2
    report = verify(args.exe.read_bytes())
    report['exe'] = str(args.exe)
    if args.out:
        args.out.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(report, indent=2) if args.json else
          json.dumps({'result': report['result'], **{k: v['ok'] for k, v in report['checks'].items()}}))
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
