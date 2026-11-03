#!/usr/bin/env python3
"""Read-only qualification of the FL run-in-background site against X3FL.exe.

Checks the site of src/engine/fl/run_in_background_fl.cpp (the startup decision's JE at RVA 0x0776ec) and the two
identity windows of src/engine/fl/run_in_background_sites_fl.h on the installed executable (Capstone over the file;
nothing executed): exact bytes and the FNV-1a of every window with its HIGHLOW dwords zeroed, the .reloc census of the
windows and the span, the JE and its two-NOP replacement as whole instructions in one aligned qword, inbound direct
edges (none into the span, the clear-bit AND reached only from the JE, the decision's other joins), the choice local's
six uses in the complete startup decode and the four argument aliases, the registry helper (HKCU read only: RegOpenKeyExA,
RegQueryValueExA, RegCloseKey; its two callers), the input block's lifetime edges (the initializer's one caller, its
allocation and publication, the null check, the absolute store census of the slot: publication and two zero stores),
the flag's readers and writers (the message pump's gate with GetMessageA / PeekMessageA, the media query, the two
script natives; the census of 0x4000 tests and writes through the input word), the flags the OR leaves dead before the
input setup's first SUB, the install point (the adapter probe's Direct3DCreate9 precedes the decision on the startup
path), the operand anchors, and the AP addresses refused (the AP window is absent from X3FL.exe, the bytes at the AP VA
differ, the header names no AP address). With the header present its window bytes, table and site are compared with the
executable too.
docs/reverse-engineering/fl/pause-background.md, "Port (2026-11-03)".

usage: verify_run_in_background_sites_fl.py [--exe PATH] [--json] [--out FILE]   (exit 0 = PASS, 2 = not found)
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
HEADER = ROOT / 'src/engine/fl/run_in_background_sites_fl.h'
AP_SITES = ROOT / 'src/proxy/run_in_background_sites.h'
# (name, start RVA, end RVA, {fixup RVA: target RVA}) - run_in_background_sites_fl.h `windows`.
WINDOWS = (
    ('decision', 0x0776c2, 0x077701, {0x0776c3: 0x1a8478, 0x0776d6: 0x18b9e8, 0x0776e3: 0x1a8478}),
    ('pump_gate', 0x0ec632, 0x0ec651, {0x0ec634: 0x1aaf34, 0x0ec641: 0x1a8478}),
)
WINDOW_FNV = {'decision': 0xd9802844, 'pump_gate': 0xf620f8e2}
SITE, ORIGINAL, PATCHED = 0x0776ec, bytes.fromhex('7408'), bytes.fromhex('9090')
SET_BIT, CLEAR_BIT, SETUP_CALL, AFTER = 0x0776ee, 0x0776f6, 0x0776fc, 0x077701
STARTUP = (0x076bc0, 0x077b78)  # startup body up to its inline dispatch table 0x077b78
LOCAL_USES = {0x076bfd: 'w', 0x077118: 'w', 0x077124: 'w', 0x0776c7: 'r', 0x0776df: 'w', 0x0776e7: 'r'}
ALIASES = {0x18b854: ('/runinbg', 0x077124), 0x18b860: ('-runinbg', 0x077124), 0x18b86c: ('/noruninbg', 0x077118),
           0x18b878: ('-noruninbg', 0x077118)}
STORE_EDGES = {0x077124: (0x077032, 0x07704a), 0x077118: (0x077062, 0x07707a)}
VALUE_NAME = 0x18b9e8
REGISTRY = (0x0d8850, 0x0d88fb)
REGISTRY_CALLERS = (0x0776da, 0x07bd65)
REGISTRY_IMPORTS = ('RegOpenKeyExA', 'RegQueryValueExA', 'RegCloseKey')
INPUT_INIT, INPUT_INIT_CALLERS = 0x0ebef0, (0x0776b1,)
SLOT_STORES = (0x076a07, 0x0ebf0a, 0x0ec004)  # fatal-cleanup zero, publication, destruction zero
SETUP, SETUP_INNER = 0x100590, 0x100480
# Every `test/or/and dword ptr [reg], 0x4000 | 0xffffbfff` in .text: the decision's OR/AND, the pump gate, the media
# query, the two script natives and X2_IsRunInBackground's neighbours are pinned by address.
BIT_OPS = {0x0776ee: 'or', 0x0776f6: 'and', 0x07bd94: 'or', 0x07bda4: 'and', 0x0ec645: 'test', 0x0ff598: 'test'}
IMPORTS = {0x5502b4: 'GetMessageA', 0x5502d4: 'PeekMessageA', 0x5502cc: 'TranslateMessage',
           0x5502b8: 'DispatchMessageA', 0x550320: 'Direct3DCreate9'}
ANCHORS = (
    (0x076bfd, 'mov dword ptr [ebp + 0x60], 0xffffffff', 'choice = -1'),
    (0x077118, 'mov dword ptr [ebp + 0x60], 0', '-noruninbg: choice = 0'),
    (0x077124, 'mov dword ptr [ebp + 0x60], 1', '-runinbg: choice = 1'),
    (0x0772bc, 'call 0x4f13b0', 'adapter probe (the proxy loads here), before the decision'),
    (0x0f13cd, 'call dword ptr [0x550320]', 'probe: Direct3DCreate9'),
    (0x0776b1, 'call 0x4ebef0', 'input block created'),
    (0x0776b6, 'test eax, eax', 'null check'),
    (0x0776b8, 'jne 0x4776c2', 'created: the decision'),
    (0x0776bd, 'call 0x4762d0', 'null: fatal cleanup'),
    (0x0776c2, 'mov eax, dword ptr [0x5a8478]', 'input block'),
    (0x0776c7, 'mov ecx, dword ptr [ebp + 0x60]', 'choice'),
    (0x0776ca, 'or dword ptr [eax], 0x1000', 'bit 0x1000'),
    (0x0776d0, 'cmp ecx, -1', 'no argument'),
    (0x0776d5, 'mov ecx, 0x58b9e8', 'value name in ECX'),
    (0x0776da, 'call 0x4d8850', 'registry read'),
    (0x0776ea, 'test ecx, ecx', 'choice'),
    (0x0776ec, 'je 0x4776f6', 'the site'),
    (0x0776ee, 'or dword ptr [eax], 0x4000', 'RunInBackground on'),
    (0x0776f4, 'jmp 0x4776fc', 'join'),
    (0x0776f6, 'and dword ptr [eax], 0xffffbfff', 'RunInBackground off'),
    (0x0776fc, 'call 0x500590', 'input setup'),
    (0x100590, 'call 0x500480', 'setup: first instruction a call (no flag read)'),
    (0x100480, 'sub esp, 0x410', 'inner: arithmetic flags overwritten first'),
    (0x0d8850, 'sub esp, 0xc', 'registry helper entry'),
    (0x0d8854, 'mov esi, ecx', 'name in ECX'),
    (0x0ebeff, 'mov ecx, 0x4fc', 'input block size'),
    (0x0ebf05, 'call 0x4d8fc0', 'allocation'),
    (0x0ebf0a, 'mov dword ptr [0x5a8478], eax', 'publication'),
    (0x0ebf1c, 'mov dword ptr [eax], ebx', 'seed flags'),
    (0x0ec004, 'mov dword ptr [0x5a8478], 0', 'destruction zeroes the slot'),
    (0x076a07, 'mov dword ptr [0x5a8478], 0', 'fatal cleanup zeroes the slot'),
    (0x0ec632, 'mov ecx, dword ptr [0x5aaf34]', 'pump: active'),
    (0x0ec63a, 'jne 0x4ec6f8', 'active: non-blocking'),
    (0x0ec645, 'test dword ptr [eax], 0x4000', 'pump: the bit'),
    (0x0ec64b, 'jne 0x4ec6f8', 'bit set: non-blocking'),
    (0x0ec657, 'mov esi, dword ptr [0x5502b4]', 'GetMessageA'),
    (0x0ec669, 'test ecx, ecx', 'inactive'),
    (0x0ec67b, 'call esi', 'blocking GetMessageA'),
    (0x0ec6f8, 'mov esi, dword ptr [0x5502d4]', 'PeekMessageA'),
    (0x0ec70b, 'call esi', 'non-blocking PeekMessageA'),
    (0x0ff583, 'cmp dword ptr [0x5aaf34], 0', 'media query: active'),
    (0x0ff598, 'test dword ptr [eax], 0x4000', 'media query: the bit'),
    (0x07bd94, 'or dword ptr [eax], 0x4000', 'X2_SetRunInBackground on'),
    (0x07bd9a, 'call 0x4d8640', 'X2_SetRunInBackground: registry write'),
    (0x07bda4, 'and dword ptr [eax], 0xffffbfff', 'X2_SetRunInBackground off'),
    (0x07bdb6, 'mov eax, dword ptr [0x5a8478]', 'X2_IsRunInBackground'),
)
AP_VAS = ('0x00403392', '0x004033c9', '0x004033ce', '0x004d2580', '0x00606f3c', '0x004b8510', '0x004d34b0')


def imports(img):
    """IAT slot VA -> imported name."""
    d = img.data
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    rva = struct.unpack_from('<I', d, pe + 24 + 96 + 8)[0]
    out, o = {}, img.file_offset(IMAGE_BASE + rva)
    while o is not None:
        oft, _, _, name, ft = struct.unpack_from('<5I', d, o)
        if not name:
            break
        t, k = oft or ft, 0
        while True:
            e = struct.unpack_from('<I', d, img.file_offset(IMAGE_BASE + t) + 4 * k)[0]
            if not e:
                break
            out[IMAGE_BASE + ft + 4 * k] = f'#{e & 0xffff}' if e & 0x80000000 else \
                d[img.file_offset(IMAGE_BASE + e) + 2:].split(b'\0')[0].decode()
            k += 1
        o += 20
    return out


def sweep(img):
    """Linear sweep of .text (resuming after undecodable bytes): every instruction."""
    import capstone
    _, start, _, rp, rsize = next(s for s in img.sections if s[0] == '.text')
    code = img.data[rp:rp + rsize]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    pos = 0
    while pos < len(code):
        last = pos
        for i in md.disasm(code[pos:pos + 0x10000], start + pos):
            last = i.address + i.size - start
            yield i
        pos = last if last > pos else pos + 1


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
    return arrays, table, text


def ap_window():
    m = re.search(r'expected_window\[window_length\] = \{(.*?)\};', AP_SITES.read_text(), re.S)
    return bytes(int(x, 16) for x in re.findall(r'0x[0-9a-f]{2}\b', m.group(1)))


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

    def into(target):
        return sorted(a for a, _, t in edges if t == target)
    raw = img.rva(SITE, 2)
    got, size = img.text_at(SITE)
    nops = [f'{i.mnemonic} {i.op_str}'.strip() for i in img.md.disasm(PATCHED, IMAGE_BASE + SITE)]
    interior = sorted(a for a, _, t in edges if SITE < t < SITE + 2)
    fixups = sorted(f for f in img.fixups if SITE - 3 <= f < SITE + 2)
    checks['site'] = {'ok': raw == ORIGINAL and got == 'je 0x4776f6' and size == 2 and nops == ['nop', 'nop'] and
                      not interior and not into(SITE) and not into(SET_BIT) and not fixups and
                      into(CLEAR_BIT) == [SITE] and into(SETUP_CALL) == [0x0776f4] and into(0x0776ea) == [0x0776d3] and
                      (SITE & 7) + 2 <= 8,
                      'bytes': raw.hex(), 'decode': got, 'replacement': nops, 'interior_edges': interior,
                      'edges_to_start': into(SITE), 'edges_to_or': into(SET_BIT), 'edges_to_and': into(CLEAR_BIT),
                      'fixups': fixups, 'direct_edges': len(edges), 'qword': f'{SITE & ~7:#08x}'}
    ins = img.insns(*STARTUP) or []
    uses = {}
    for i in ins:
        if '[ebp + 0x60]' in i.op_str:
            uses[i.address - IMAGE_BASE] = 'w' if i.op_str.startswith('dword ptr [ebp + 0x60],') else 'r'
    aliases = {f'{s:#08x}': img.rva(s, 16).split(b'\0')[0].decode('ascii', 'replace') for s in ALIASES}
    alias_refs = {s: sorted(f for f in img.fixups if struct.unpack('<I', img.rva(f, 4))[0] == IMAGE_BASE + s)
                  for s in ALIASES}
    checks['choice_local'] = {
        'ok': bool(ins) and uses == LOCAL_USES and all(aliases[f'{s:#08x}'] == a for s, (a, _) in ALIASES.items()) and
        all(into(t) == list(e) for t, e in STORE_EDGES.items()) and
        all(refs and all(STARTUP[0] <= f < STARTUP[1] for f in refs) for refs in alias_refs.values()) and
        img.rva(VALUE_NAME, 16).split(b'\0')[0] == b'RunInBackground',
        'startup_instructions': len(ins), 'uses': {f'{a:#08x}': k for a, k in sorted(uses.items())},
        'aliases': aliases, 'alias_refs': {f'{s:#08x}': [f'{f:#08x}' for f in r] for s, r in alias_refs.items()},
        'store_edges': {f'{t:#08x}': [f'{a:#08x}' for a in into(t)] for t in STORE_EDGES}}
    names = imports(img)
    helper = img.insns(*REGISTRY) or []
    iat_calls = [names.get(int(i.op_str[len('dword ptr ['):-1], 16)) for i in helper
                 if i.mnemonic == 'call' and i.op_str.startswith('dword ptr [0x')]
    checks['registry_read'] = {'ok': bool(helper) and tuple(iat_calls) == REGISTRY_IMPORTS and
                               tuple(into(REGISTRY[0])) == REGISTRY_CALLERS,
                               'imports_called': iat_calls, 'callers': [f'{a:#08x}' for a in into(REGISTRY[0])]}
    slot_va = IMAGE_BASE + 0x1a8478
    stores, bit_ops = [], {}
    for i in sweep(img):
        if i.mnemonic in ('mov', 'and', 'or', 'xor', 'add', 'sub', 'inc', 'dec', 'xchg') and \
                i.op_str.startswith(f'dword ptr [{slot_va:#x}]'):
            stores.append(i.address - IMAGE_BASE)
        if i.mnemonic in ('test', 'or', 'and') and re.fullmatch(r'dword ptr \[e[a-d]x\], (0x4000|0xffffbfff)', i.op_str):
            bit_ops[i.address - IMAGE_BASE] = i.mnemonic
    checks['input_lifetime'] = {'ok': tuple(sorted(stores)) == SLOT_STORES and
                                tuple(into(INPUT_INIT)) == INPUT_INIT_CALLERS,
                                'slot_stores': [f'{a:#08x}' for a in sorted(stores)],
                                'initializer_callers': [f'{a:#08x}' for a in into(INPUT_INIT)]}
    checks['flag_readers_writers'] = {'ok': bit_ops == BIT_OPS and into(SETUP) == [SETUP_CALL] and
                                      all(names.get(a) == n for a, n in IMPORTS.items()),
                                      'bit_ops': {f'{a:#08x}': m for a, m in sorted(bit_ops.items())},
                                      'setup_callers': [f'{a:#08x}' for a in into(SETUP)],
                                      'imports': {f'{a:#x}': names.get(a) for a in IMPORTS}}
    anchors = []
    for at, want, what in ANCHORS:
        got, _ = img.text_at(at)
        anchors.append({'rva': f'{at:#08x}', 'ok': got == want, 'got': got, 'what': what})
    checks['anchors'] = {'ok': all(a['ok'] for a in anchors) and 0x0772bc < WINDOWS[0][1], 'count': len(anchors),
                         'failed': [a for a in anchors if not a['ok']]}
    ap = ap_window()
    at_ap_va = img.read(0x00403392, len(ap))
    checks['ap_refused'] = {'ok': len(ap) == 60 and data.find(ap) < 0 and at_ap_va != ap,
                            'ap_window_hits': int(data.find(ap) >= 0), 'bytes_at_ap_va_equal': at_ap_va == ap}
    if source:
        arrays, table, text = header_tables()
        rows = {}
        for name, start, end, relocs in WINDOWS:
            row = table.get(name)
            rows[name] = bool(row) and arrays.get(name) == measured[name] and row[0] == start and \
                row[1] == end - start and row[2] == WINDOW_FNV[name] and \
                [start + o for o in row[3]] == sorted(relocs) and row[4] == [relocs[k] for k in sorted(relocs)]
        rows['site'] = f'site_rva = 0x{SITE:06x}' in text and 'original[site_length] = {0x74, 0x08}' in text and \
            'patched[site_length] = {0x90, 0x90}' in text and f'clear_bit_rva = 0x{CLEAR_BIT:06x}' in text
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
