#!/usr/bin/env python3
"""Read-only qualification of the FL chase cursor-fire admission seam against X3FL.exe.

Checks the seam of src/engine/fl/chase_fire_fl.cpp (je 0x04ecad inside the native fire function RVA 0x04e310) and the
five identity windows of src/engine/fl/chase_fire_sites_fl.h on the installed executable (Capstone over the file;
nothing executed): exact bytes and the FNV-1a of every window with its HIGHLOW dwords zeroed, the .reloc census of every
window and of the seam, whole-instruction decodes, inbound direct edges (none into the seam's interior or start, none
into the fire function from outside, none into the prologue before the ship store), the qword crossing, the fire
function's complete linear decode (its EBX/EBP writers, the sole [EBP-0x3c] writer, the stack-argument writers, RET
0x10, the four native callers, no HIGHLOW dword pointing into it, its C++ EH FuncInfo with no try block), the direct
references of the cursor-active cell, and the operand anchors the handler relies on. With the header present its window bytes and table are compared with the executable too.
docs/reverse-engineering/fl/chase-fire-aim.md, "Verified 2026-11-03".

usage: verify_chase_fire_sites_fl.py [--exe PATH] [--json] [--out FILE]   (exit 0 = PASS, 2 = executable not found)
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
HEADER = ROOT / 'src/engine/fl/chase_fire_sites_fl.h'
# (name, start RVA, end RVA, {fixup RVA: target RVA}) - chase_fire_sites_fl.h `windows`.
WINDOWS = (
    ('prologue', 0x04e310, 0x04e35b, {0x04e329: 0x14d3b5}),
    ('admission', 0x04ec82, 0x04ed10, {0x04eca8: 0x1aa184, 0x04ecb5: 0x1a82e4}),
    ('unproject_call', 0x04edd2, 0x04edfc, {0x04eded: 0x1aa188, 0x04edf3: 0x1aa180}),
    ('cursor_writer', 0x07bc3a, 0x07bc62, {0x07bc41: 0x1aaa10, 0x07bc49: 0x1aa184, 0x07bc51: 0x1aa180,
                                           0x07bc59: 0x1aa188}),
    ('screen', 0x0a51ef, 0x0a52a5, {0x0a51f1: 0x1a82f8, 0x0a5224: 0x1a82f8}),
)
WINDOW_FNV = {'prologue': 0x5347c1f5, 'admission': 0x56a65ba8, 'unproject_call': 0xf6c4c372,
              'cursor_writer': 0xe849e23a, 'screen': 0xc013bc5c}
SEAM = ('admission', 0x04ecad, '0f84ec020000', 'je 0x44ef9f', 0x04ef9f)
FIRE = (0x04e310, 0x04ff46)
CALLERS = (0x06bb81, 0x06bb9f, 0x06bbe4, 0x06bc03)
EBX_EBP_WRITES = (0x04e311, 0x04e31d, 0x04e324, 0x04ff3f, 0x04ff42)
SHIP_STORE = 0x04e350
ARG_WRITES = {0x04e3fd: 8, 0x04ed24: 8, 0x04ed40: 8}  # only the target word; group/flags/mask are never written
CURSOR_ACTIVE_REFS = (0x04eca8, 0x07bc49)            # HIGHLOW fixups whose value is the cursor-active cell
EH_HANDLER, FUNC_INFO = 0x14d3b5, 0x19c8b8           # pushed at 0x04e328: mov eax,FuncInfo; jmp __CxxFrameHandler
# Operand anchors (RVA, Capstone text at the preferred base, what it establishes).
ANCHORS = (
    (0x04e310, 'push ebx', 'fire entry'),
    (0x04e311, 'mov ebx, esp', 'EBX = entry ESP - 4: stack words at EBX+8'),
    (0x04e324, 'mov ebp, esp', 'EBP = the aligned frame'),
    (0x04e343, 'mov esi, dword ptr [ebx + 0xc]', 'second stack word = gun group'),
    (0x04e34e, 'mov edi, ecx', 'ECX = ship at entry'),
    (0x04e350, 'mov dword ptr [ebp - 0x3c], edi', 'ship saved at [EBP-0x3c]'),
    (0x04e35b, 'cmp word ptr [edi + 0x48], 7', 'ship type 7'),
    (0x04ec82, 'mov ecx, dword ptr [ebx + 0x10]', 'third stack word = fire flags'),
    (0x04ec94, 'and eax, 0x22', 'cursor flag bits'),
    (0x04ec9e, 'cmp al, 0x22', 'both bits required'),
    (0x04eca0, 'jne 0x44ef9f', 'no cursor ray otherwise'),
    (0x04eca6, 'cmp dword ptr [0x5aa184], 0', 'cursor active compare (the seam consumes its ZF)'),
    (0x04ecb3, 'mov ecx, dword ptr [0x5a82e4]', 'registry slot'),
    (0x04ecb9, 'test ecx, ecx', 'flags overwritten after the seam'),
    (0x04ecc1, 'push dword ptr [ecx + 0x10]', 'registry+0x10 active handle'),
    (0x04ecc4, 'call 0x41e420', 'native resolver'),
    (0x04ecc9, 'mov edi, dword ptr [ebp - 0x3c]', 'ship reloaded from [EBP-0x3c]'),
    (0x04ecd9, 'cmp dword ptr [esi + 0x10], edi', 'cockpit+0x10 view object == ship (native)'),
    (0x04ece2, 'mov edx, dword ptr [esi + 0x1d8]', 'cockpit+0x1d8 aim gun index (native)'),
    (0x04ecf2, 'call 0x45a930', 'gun-view count (native)'),
    (0x04ed02, 'call 0x45a9e0', 'gun-view mapping (native)'),
    (0x04ed07, 'cmp eax, dword ptr [ebx + 0xc]', 'mapping == gun group (native)'),
    (0x04ed43, 'mov ecx, dword ptr [ebp - 0x8c]', 'bullet index for the range helper (native)'),
    (0x04ed4c, 'call 0x43d6a0', 'native range helper'),
    (0x04edd5, 'cmp esi, 0x1388', 'depth floor 5000 (native)'),
    (0x04ede7, 'mov ecx, dword ptr [ecx + 0x58]', 'cockpit+0x58 camera for the unprojection'),
    (0x04edeb, 'push dword ptr [0x5aa188]', 'cursor y'),
    (0x04edf1, 'push dword ptr [0x5aa180]', 'cursor x'),
    (0x04edf7, 'call 0x4a50f0', 'native unprojection'),
    (0x04edfc, 'cmp dword ptr [ebx + 0xc], 0', 'main group 0: relative transform'),
    (0x04ee56, 'imul dword ptr [0x6243c8]', 'native cone cosine'),
    (0x04ef9f, 'mov edi, dword ptr [ebp - 0x3c]', 'branch target: no cursor ray'),
    (0x04ff43, 'ret 0x10', 'four callee-cleaned stack words'),
    (0x07bc40, 'mov dword ptr [0x5aaa10], eax', 'writer: steering from record +1'),
    (0x07bc48, 'mov dword ptr [0x5aa184], eax', 'writer: active from record +6'),
    (0x07bc50, 'mov dword ptr [0x5aa180], eax', 'writer: x from record +0xb'),
    (0x07bc58, 'mov dword ptr [0x5aa188], eax', 'writer: y from record +0x10'),
    (0x0a50f4, 'mov ebx, dword ptr [0x5a82f8]', 'unprojection: defaults slot'),
    (0x0a50fd, 'mov edi, ecx', 'unprojection: ECX = camera'),
    (0x0a51f9, 'mov ecx, dword ptr [ebx]', 'defaults+0 screen record'),
    (0x0a51fb, 'movzx edx, word ptr [ecx + 4]', 'screen width +4'),
    (0x0a51ff, 'movsx ecx, dx', 'signed width'),
    (0x0a522a, 'mov eax, dword ptr [edi + 0x290]', 'viewport x0'),
    (0x0a5230, 'add eax, dword ptr [edi + 0x294]', 'viewport x1'),
    (0x0a5270, 'movzx ecx, word ptr [eax + 6]', 'screen height +6'),
    (0x0a5299, 'mov eax, dword ptr [edi + 0x288]', 'viewport y0'),
    (0x0a529f, 'add eax, dword ptr [edi + 0x28c]', 'viewport y1'),
    # The four native callers (command dispatcher): mask, flags, group, target pushed, ECX = ship; the first pair is
    # reached with ECX tested nonzero, the second pair moves the resolved ship into ECX.
    (0x06bb79, 'push dword ptr [esi + 0x15]', 'caller 1: mask'), (0x06bb7c, 'push dword ptr [esi + 0x10]', 'caller 1: flags'),
    (0x06bb7f, 'push edx', 'caller 1: group'), (0x06bb80, 'push eax', 'caller 1: target'),
    (0x06bb98, 'push -1', 'caller 2: mask -1'), (0x06bb9e, 'push eax', 'caller 2: target'),
    (0x06bbe0, 'push 0', 'caller 3: zero target'), (0x06bbe2, 'mov ecx, eax', 'caller 3: ECX = ship'),
    (0x06bbff, 'push 0', 'caller 4: zero target'), (0x06bc01, 'mov ecx, eax', 'caller 4: ECX = ship'),
    (0x01e420, 'mov eax, dword ptr [ecx]', 'resolver: registry+0 map'),
    (0x01e428, 'mov eax, dword ptr [eax + 4]', 'map+4 bucket count'),
    (0x01e440, 'cmp dword ptr [eax + 4], ecx', 'link+4 key'),
    (0x01e445, 'mov eax, dword ptr [eax]', 'link+0 next'),
    (0x01e450, 'mov eax, dword ptr [eax + 8]', 'link+8 value = cockpit'),
)


def header_windows():
    """The bytes and table of chase_fire_sites_fl.h (relocated dwords zeroed), parsed from the source."""
    text = HEADER.read_text()
    arrays = {m.group(1): bytes(int(x, 16) for x in re.findall(r'0x[0-9a-f]{2}\b', m.group(3)))
              for m in re.finditer(r'constexpr unsigned char (\w+)_window\[(\d+)\] = \{(.*?)\};', text, re.S)}
    table = {}
    for m in re.finditer(r'\{"(\w+)", 0x([0-9a-f]+), (\d+), \w+_window, 0x([0-9a-f]+)u, (\d+), \{([^}]*)\}, '
                         r'\{([^}]*)\}\}', text):
        offs = [int(x, 16) for x in m.group(6).split(',') if x.strip()]
        tgts = [int(x, 16) for x in m.group(7).split(',') if x.strip()]
        table[m.group(1)] = (int(m.group(2), 16), int(m.group(3)), int(m.group(4), 16), offs, tgts)
    return arrays, table


def fire_census(img, edges):
    lo, hi = FIRE
    ins = img.insns(lo, hi) or []
    regs, ship, args = [], [], {}
    for i in ins:
        rva = i.address - IMAGE_BASE
        _, w = i.regs_access()
        if {i.reg_name(r) for r in w} & {'ebx', 'bx', 'bl', 'bh', 'ebp', 'bp'}:
            regs.append(rva)
        if i.mnemonic in ('cmp', 'test', 'push'):
            continue
        if re.match(r'\w+ ptr \[ebp - 0x3c\],', i.op_str) or 'ebp - 0x3c]' in i.op_str and i.mnemonic == 'lea':
            ship.append(rva)
        m = re.match(r'\w+ ptr \[ebx \+ (8|0xc|0x10|0x14)\],', i.op_str)
        if m:
            args[rva] = int(m.group(1), 0)
        if re.match(r'lea \w+, \[ebx', f'{i.mnemonic} {i.op_str}'):
            args[rva] = -1
    callers = sorted(a for a, m, t in edges if t == lo)
    outside = sorted(a for a, m, t in edges if lo < t < hi and not lo <= a < hi)
    prologue = sorted(a for a, m, t in edges if lo < t <= SHIP_STORE + 3)
    return {'ok': bool(ins) and tuple(regs) == EBX_EBP_WRITES and ship == [SHIP_STORE] and args == ARG_WRITES and
            tuple(callers) == CALLERS and not outside and not prologue,
            'instructions': len(ins), 'ebx_ebp_writes': [f'{r:#08x}' for r in regs],
            'ship_slot_writers': [f'{r:#08x}' for r in ship],
            'arg_writes': {f'{r:#08x}': f'ebx+{o:#x}' for r, o in args.items()},
            'callers': [f'{a:#08x}' for a in callers], 'edges_from_outside': [f'{a:#08x}' for a in outside],
            'edges_into_prologue': [f'{a:#08x}' for a in prologue]}


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
    window, rva, hexbytes, text, target = SEAM
    raw = img.rva(rva, 6)
    got, size = img.text_at(rva)
    interior = sorted(a for a, _, t in edges if rva < t < rva + 6)
    to_start = sorted(a for a, _, t in edges if t == rva)
    fixups = sorted(f for f in img.fixups if rva - 3 <= f < rva + 6)
    rel_ok = rva + 6 + struct.unpack_from('<i', raw, 2)[0] == target
    previous, previous_size = img.text_at(rva - 7)
    checks['seam'] = {'ok': raw == bytes.fromhex(hexbytes) and got == text and size == 6 and rel_ok and not interior
                      and not to_start and not fixups and previous == 'cmp dword ptr [0x5aa184], 0' and
                      previous_size == 7 and (rva & 7) + 5 > 8,
                      'bytes': raw.hex(), 'decode': got, 'previous': previous, 'interior_edges': interior,
                      'edges_to_start': to_start, 'fixups': fixups, 'direct_edges': len(edges),
                      'qword': 'fits' if (rva & 7) + 5 <= 8 else 'crosses', 'rva_mod_8': rva & 7}
    checks['fire_function'] = fire_census(img, edges)
    # No absolute pointer into the function (no computed entry through a relocated table or immediate), and its EH
    # FuncInfo (the handler the prologue pushes loads it) declares no try block: no catch funclet resumes inside it.
    into = sorted(f for f in img.fixups
                  if FIRE[0] <= struct.unpack('<I', img.rva(f, 4))[0] - IMAGE_BASE < FIRE[1])
    handler, _ = img.text_at(EH_HANDLER)
    info = struct.unpack('<5I', img.rva(FUNC_INFO, 20))
    checks['fire_eh_and_pointers'] = {
        'ok': not into and handler == f'mov eax, {IMAGE_BASE + FUNC_INFO:#x}' and info[0] in (0x19930520, 0x19930521,
                                                                                              0x19930522)
        and info[3] == 0 and info[4] == 0,
        'highlow_into_function': [f'{f:#08x}' for f in into], 'handler': handler, 'magic': f'{info[0]:#x}',
        'max_state': info[1], 'n_try': info[3], 'try_map': f'{info[4]:#x}'}
    active_refs = sorted(f for f in img.fixups
                         if struct.unpack('<I', img.rva(f, 4))[0] == IMAGE_BASE + 0x1aa184)
    checks['cursor_active_refs'] = {'ok': tuple(active_refs) == CURSOR_ACTIVE_REFS,
                                    'fixups': [f'{f:#08x}' for f in active_refs]}
    anchors = []
    for at, want, what in ANCHORS:
        got, _ = img.text_at(at)
        anchors.append({'rva': f'{at:#08x}', 'ok': got == want, 'got': got, 'what': what})
    checks['anchors'] = {'ok': all(a['ok'] for a in anchors), 'count': len(anchors),
                         'failed': [a for a in anchors if not a['ok']]}
    if source:
        arrays, table = header_windows()
        rows = {}
        for name, start, end, relocs in WINDOWS:
            row = table.get(name)
            rows[name] = bool(row) and arrays.get(name) == measured[name] and row[0] == start and \
                row[1] == end - start and row[2] == WINDOW_FNV[name] and \
                [start + o for o in row[3]] == sorted(relocs) and row[4] == [relocs[k] for k in sorted(relocs)]
        text = HEADER.read_text()
        spaced = ', '.join(f'0x{b:02x}' for b in bytes.fromhex(SEAM[2]))
        rows['seam'] = f'0x{SEAM[1]:06x}, {{{spaced}}}, 2, w_admission' in text and \
            f'admission_target_rva = 0x{SEAM[4]:06x}' in text
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
