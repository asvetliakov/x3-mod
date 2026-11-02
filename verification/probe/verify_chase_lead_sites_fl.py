#!/usr/bin/env python3
"""Read-only qualification of the FL chase lead marker and central HUD seams against X3FL.exe.

Checks the four seams of src/engine/fl/chase_lead_fl.cpp (gate 0x02c7b9, publish 0x02cbe7, final FOV 0x022cf5, HUD gate
0x02cc1e) and the twelve identity windows of src/engine/fl/chase_lead_sites_fl.h on the installed executable (Capstone
over the file; nothing executed): exact bytes and the FNV-1a of every window with its HIGHLOW dwords zeroed, the .reloc
census of every window and seam (the relocated dwords and their targets), whole-instruction decodes, inbound direct
edges (none into a seam's interior), qword crossings, the cockpit updater's CFG (the pose seam 0x0227ad dominates the
overlay call and the final-FOV seam; EBX = cockpit), the overlay's stack depth (ESP = the post-prologue base P at every
seam, so the solver's absolute point P+0xe0 is saved ESP + 0xe4 in the stub), the solver's fifth argument and RET 0x14,
the hide helper's ABI, the sole overlay caller, and the field anchors the handler relies on. With --source the header's
window bytes and table are compared with the executable too. docs/reverse-engineering/fl/chase-lead.md, "Verified
2026-11-02".

usage: verify_chase_lead_sites_fl.py [--exe PATH] [--json] [--out FILE]   (exit 0 = PASS, 2 = executable not found)
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
HEADER = ROOT / 'src/engine/fl/chase_lead_sites_fl.h'
# (name, start RVA, end RVA, {fixup RVA: target RVA}) - chase_lead_sites_fl.h `windows`.
WINDOWS = (
    ('call', 0x0229bc, 0x0229c7, {}), ('fov', 0x022ce0, 0x022d0a, {}), ('prologue', 0x02c33c, 0x02c358, {}),
    ('gate', 0x02c799, 0x02c7d1, {}), ('solver', 0x02c842, 0x02c888, {}), ('publish', 0x02cbb2, 0x02cc2b, {}),
    ('cleanup', 0x02d019, 0x02d073, {}), ('hide', 0x027c20, 0x027c3f, {}),
    ('distance', 0x024860, 0x0248b2, {0x02487e: 0x1a82e4}), ('producer', 0x0266fa, 0x026720, {}),
    ('texture', 0x1123a0, 0x112406, {0x1123ac: 0x21b410, 0x1123bd: 0x21b418, 0x1123cc: 0x1fb404, 0x1123d3: 0x21b40c,
                                     0x1123e1: 0x1cb3f4, 0x1123fc: 0x1cb3f4}),
    ('projection', 0x02cabe, 0x02cb56, {0x02cac6: 0x1a82f8, 0x02cb48: 0x1a82f8}),
)
WINDOW_FNV = {'call': 0xa26fea9a, 'fov': 0x247c90cb, 'prologue': 0xc6e1e559, 'gate': 0xf2edff5d, 'solver': 0x7ece295a,
              'publish': 0x457c74a0, 'cleanup': 0x08143880, 'hide': 0x8da11c57, 'distance': 0x61976064,
              'producer': 0x728ce072, 'texture': 0x56445803, 'projection': 0xbdd984e9}
# (name, RVA, bytes, Capstone text at the preferred base, rel32 target RVA or None).
SEAMS = (
    ('gate', 0x02c7b9, '0f8436040000', 'je 0x42cbf5', 0x02cbf5),
    ('publish', 0x02cbe7, '898be0030000', 'mov dword ptr [ebx + 0x3e0], ecx', None),
    ('final_fov', 0x022cf5, '39b340020000', 'cmp dword ptr [ebx + 0x240], esi', None),
    ('hud_gate', 0x02cc1e, '0f84f5030000', 'je 0x42d019', 0x02d019),
)
UPDATER = (0x021f50, 0x022fb0)
OVERLAY = (0x02c330, 0x02f0a8)        # the inline-overlay updater, up to its first jump table
POSE_SEAM, OVERLAY_CALL = 0x0227ad, 0x0229c2
# EBX writes inside the updater: the entry copy, the loop [0x02268d, 0x0226d8) and its reload, the epilogue pops.
UPDATER_EBX_WRITES = (0x021f5e, 0x02268d, 0x0226bd, 0x0226d8, 0x022f9d, 0x022fab)
# Overlay ESP relative to the post-prologue base P (0 after `push edi` at 0x02c340): P at every seam and around the
# solver; -0x14 at the solver call itself (its five callee-cleaned arguments).
OVERLAY_ESP_POINTS = {0x02c7b9: 0, 0x02c842: 0, 0x02c87b: -0x14, 0x02c880: 0, 0x02cbe7: 0, 0x02cbf5: 0, 0x02cc11: 0,
                      0x02cc1e: 0, 0x02d019: 0, 0x02d06d: 0}
# Field and ABI anchors (RVA, Capstone text, what it establishes).
ANCHORS = (
    (0x0229bc, 'lea ecx, [ebx + 0x3bc]', 'O = cockpit + 0x3bc, passed to the overlay'),
    (0x02c33d, 'mov ebx, ecx', 'overlay: EBX = O'),
    (0x02c345, 'cmp dword ptr [ebx + 0x3c0], 0', 'O+0x3c0 target-body enable'),
    (0x02c352, 'mov eax, dword ptr [ebx + 0x3c4]', 'O+0x3c4 cockpit backpointer'),
    (0x02c799, 'mov esi, dword ptr [ebx + 0x3c4]', 'gate: ESI = cockpit'),
    (0x02c79f, 'movsx eax, word ptr [esi + 0x1e4]', 'cockpit+0x1e4 tracking short (2..3 native)'),
    (0x02c7b2, 'test byte ptr [esi + 0x150], 1', 'gate ZF: view-mode bit 0'),
    (0x02c7bf, 'mov edx, dword ptr [esi + 0x1e0]', 'cockpit+0x1e0 target'),
    (0x02c850, 'lea eax, [esp + 0xe0]', 'P+0xe0 absolute point output'),
    (0x02c858, 'lea eax, [esp + 0xc4]', 'P+0xc0 direction output (after one push)'),
    (0x02c87b, 'call 0x4505a0', 'native solver, ECX = ship'),
    (0x02c88e, 'mov eax, dword ptr [eax + 0x1d8]', 'cockpit+0x1d8 main gun'),
    (0x02c89c, 'mov eax, dword ptr [esp + 0xe0]', 'main-gun projection reads P+0xe0'),
    (0x02cbb2, 'lea ecx, [ebx + 0x3c]', 'O+0x3c lead entry'),
    (0x02cbba, 'mov eax, dword ptr [ebx + 0x40]', 'publish: EAX = marker node (entry+4)'),
    (0x02cbbd, 'mov ecx, dword ptr [esp + 0x14]', 'publish: ECX = x'),
    (0x02cbc1, 'mov edx, dword ptr [esp + 0x40]', 'publish: EDX = y'),
    (0x02cbc9, 'mov dword ptr [eax + 0x30], ecx', 'node+0x30 x'),
    (0x02cbcc, 'mov dword ptr [eax + 0x34], edx', 'node+0x34 y'),
    (0x02cbdd, 'or dword ptr [eax + 0x130], 0x200', 'node+0x130 Flags2 screen bit'),
    (0x02cbed, 'mov dword ptr [ebx + 0x3e4], edx', 'O+0x3e4 cached y'),
    (0x02cbf8, 'call 0x427c20', 'native hide of the lead entry (ECX = O+0x3c)'),
    (0x02cbfd, 'mov dword ptr [ebx + 0x3e4], 0xffffffff', 'hide stores -1'),
    (0x02cc11, 'mov eax, dword ptr [ebx + 0x3c4]', 'HUD gate: EAX = cockpit'),
    (0x02cc17, 'test byte ptr [eax + 0x150], 1', 'HUD gate ZF: view-mode bit 0'),
    (0x02ccc7, 'mov dword ptr [eax + 0x30], 0', 'crosshair node (0,9)'),
    (0x02ccce, 'mov dword ptr [eax + 0x34], 9', 'crosshair node (0,9)'),
    (0x02cd98, 'mov dword ptr [eax + 0x30], 1', 'status icon node (1,-25)'),
    (0x02cd9f, 'mov dword ptr [eax + 0x34], 0xffffffe7', 'status icon node (1,-25)'),
    (0x02cdee, 'mov dword ptr [eax + 0x30], 0xffffffba', 'target-speed node (-70,-10)'),
    (0x02cdf5, 'mov dword ptr [eax + 0x34], 0xfffffff6', 'target-speed node (-70,-10)'),
    (0x02ceb1, 'mov dword ptr [eax + 0x30], 0x46', 'own-speed node (70,-10)'),
    (0x02ceb8, 'mov dword ptr [eax + 0x34], 0xfffffff6', 'own-speed node (70,-10)'),
    (0x02cf74, 'mov dword ptr [eax + 0x30], 0', 'distance node (0,40)'),
    (0x02cf7b, 'mov dword ptr [eax + 0x34], 0x28', 'distance node (0,40)'),
    (0x02d06d, 'mov eax, dword ptr [ebx + 0x3c4]', 'central end: EBX = O again'),
    (0x02c2b5, 'test byte ptr [eax + 0x150], 3', 'target brackets run for (mode & 3) != 0: mode 258 passes natively'),
    (0x027c21, 'mov esi, ecx', 'hide helper: ECX = entry'),
    (0x027c2f, 'push dword ptr [ecx + 0x1c]', 'hide helper: node+0x1c scene argument'),
    (0x027c32, 'call 0x4a5740', 'hide helper: detacher'),
    (0x027c3e, 'ret', 'hide helper: plain RET'),
    (0x0a574a, 'cmp dword ptr [edi + 0x1c], ebx', 'detacher: node+0x1c scene'),
    (0x0266fa, 'mov esi, ecx', 'distance producer: ECX = cockpit'),
    (0x026701, 'mov eax, dword ptr [esi + 4]', 'cockpit+4 HUD scene'),
    (0x02670c, 'test byte ptr [eax + 0x18], 1', 'scene+0x18 bit 0 active'),
    (0x02671b, 'call 0x5123a0', 'texture 15 query'),
    (0x026729, 'ret 8', 'distance producer RET 8'),
    (0x024870, 'cmp dword ptr [esi + 0x244], 0', 'cockpit+0x244 draw-2D enable'),
    (0x02489a, 'cmp dword ptr [esi + 0x2b0], 0', 'cockpit+0x2b0 display-body enable'),
    (0x0248ad, 'call 0x4266f0', 'distance producer, sole parent call'),
    (0x02487d, 'mov eax, dword ptr [0x5a82e4]', 'registry slot RVA 0x1a82e4'),
    (0x031fae, 'mov esi, dword ptr [0x5a82e4]', 'active setter loads the registry'),
    (0x031fd0, 'mov dword ptr [esi + 0x10], eax', 'registry+0x10 active handle'),
    (0x01e420, 'mov eax, dword ptr [ecx]', 'resolver: registry+0 map'),
    (0x01e422, 'mov edx, dword ptr [eax]', 'map+0 bucket array'),
    (0x01e428, 'mov eax, dword ptr [eax + 4]', 'map+4 bucket count'),
    (0x01e430, 'and eax, ecx', 'index (count-1) & handle'),
    (0x01e440, 'cmp dword ptr [eax + 4], ecx', 'link+4 key'),
    (0x01e445, 'mov eax, dword ptr [eax]', 'link+0 next'),
    (0x01e450, 'mov eax, dword ptr [eax + 8]', 'link+8 value = cockpit'),
    (0x02caca, 'mov ecx, dword ptr [eax + 8]', 'cockpit+8 HUD camera'),
    (0x02cacd, 'mov esi, dword ptr [esi]', 'defaults+0 screen record'),
    (0x02cadf, 'movsx ecx, word ptr [esi + 4]', 'screen width short +4'),
    (0x02cb0d, 'movsx ecx, word ptr [esi + 6]', 'screen height short +6'),
    (0x0defa4, 'push dword ptr [ebx + 0x278]', 'camera+0x278 D3DVIEWPORT9 pointer (SetViewport)'),
    (0x022cc5, 'mov dword ptr [eax + 0x298], esi', 'sector camera FOV written before the final-FOV seam'),
    (0x022cfd, 'mov dword ptr [ebx + 0x240], esi', 'cached FOV after the seam'),
    (0x022d05, 'call 0x424860', 'distance/HUD parent after the seam'),
    (0x027173, 'mov eax, dword ptr [ecx + 0x79c]', 'aim consumer reads cockpit+0x79c = O+0x3e0'),
    (0x027195, 'mov eax, dword ptr [ecx + 0x7a0]', 'aim consumer reads cockpit+0x7a0 = O+0x3e4'),
    (0x0224c5, 'mov eax, dword ptr [ebx + 0x1c0]', 'cockpit+0x1c0 connect'),
    (0x0224e1, 'test byte ptr [ebx + 0x1a0], 4', 'cockpit+0x1a0 flags'),
    (0x03107d, 'push dword ptr [eax + 8]', 'object+8 native id'),
    (0x059780, 'mov eax, dword ptr [ecx + 0x54]', 'object+0x54 owner'),
    (0x059787, 'cmp word ptr [eax + 0x48], 1', 'owner+0x48 type 1 (sector)'),
    (0x059fc0, 'cmp word ptr [ecx + 0x48], 7', 'object+0x48 type 7 (ship)'),
    # The solver writes its fifth argument (P+0xe0) from target root+0x30 on the no-prediction branch and returns RET 0x14.
    (0x0506ce, 'mov ecx, dword ptr [ebp + 0x18]', 'solver: fifth argument = absolute point output'),
    (0x0506d8, 'mov eax, dword ptr [edx + 0x70]', 'target+0x70 root'),
    (0x0506db, 'movups xmm0, xmmword ptr [eax + 0x30]', 'root+0x30 position'),
    (0x0506df, 'movups xmmword ptr [ecx], xmm0', 'written to the absolute output'),
    (0x050723, 'ret 0x14', 'solver RET 0x14'),
    (0x050741, 'ret 0x14', 'solver RET 0x14 (predictive branch)'),
)


def decode_from(img, start, end):
    code = img.rva(start, end - start) or b''
    return {i.address - IMAGE_BASE: i for i in img.md.disasm(code, IMAGE_BASE + start)}


def successors(img, ins, lo, hi):
    """Direct successors of one instruction inside [lo, hi), jump tables followed; None for an unresolved transfer."""
    m, o, nxt = ins.mnemonic, ins.op_str, ins.address - IMAGE_BASE + ins.size
    if m.startswith('ret'):
        return []
    if m.startswith('j'):
        if o.startswith('0x'):
            t = int(o, 16) - IMAGE_BASE
            return [t] if m == 'jmp' else [t, nxt]
        table = re.match(r'dword ptr \[(\w+)\*4 \+ (0x[0-9a-f]+)\]', o)
        if not table:
            return None
        out, k = [], 0
        while True:
            t = struct.unpack('<I', img.read(int(table.group(2), 16) + 4 * k, 4))[0] - IMAGE_BASE
            if not lo <= t < hi:
                return out
            out.append(t)
            k += 1
    return [nxt]


def updater_cfg(img):
    lo, hi = UPDATER
    ins = decode_from(img, lo, hi)
    succ, unresolved = {}, []
    for va, i in ins.items():
        s = successors(img, i, lo, hi)
        if s is None or any(not lo <= t < hi for t in s):
            unresolved.append(f'{va:#08x}')
            s = [t for t in (s or []) if lo <= t < hi]
        succ[va] = s

    def reach(removed):
        seen, work = set(), [lo]
        while work:
            v = work.pop()
            if v in seen or v == removed or v not in succ:
                continue
            seen.add(v)
            work.extend(succ[v])
        return seen
    full, cut = reach(None), reach(POSE_SEAM)
    targets = (OVERLAY_CALL, SEAMS[2][1])
    writes = []
    for va in sorted(full):
        _, w = ins[va].regs_access()
        if any(ins[va].reg_name(r) in ('ebx', 'bx', 'bl', 'bh') for r in w):
            writes.append(va)
    return {'ok': not unresolved and all(t in full and t not in cut for t in targets) and
            tuple(writes) == UPDATER_EBX_WRITES,
            'unresolved': unresolved, 'reachable': {f'{t:#08x}': t in full for t in targets},
            'reachable_without_pose_seam': {f'{t:#08x}': t in cut for t in targets},
            'ebx_writes': [f'{w:#08x}' for w in writes], 'instructions': len(ins)}


def callee_pops(img, rva, cache={}):
    if rva in cache:
        return cache[rva]
    code = img.rva(rva, 0x1000) or img.rva(rva, 0x100) or b''
    pops = set()
    for i in img.md.disasm(code, IMAGE_BASE + rva):
        if i.mnemonic == 'ret':
            pops.add(int(i.op_str, 16) if i.op_str else 0)
        if i.mnemonic == 'int3' and pops:
            break
    cache[rva] = pops
    return pops


def overlay_stack(img):
    """ESP relative to the post-prologue base along every path of the overlay (jump tables followed)."""
    lo, hi = OVERLAY
    ins = decode_from(img, lo, hi)
    depth, problems, work = {}, [], [(0x02c341, 0)]
    while work:
        va, d = work.pop()
        while True:
            if va in depth:
                if depth[va] != d:
                    problems.append(f'{va:#08x} depth {depth[va]} vs {d}')
                break
            depth[va] = d
            i = ins.get(va)
            if i is None:
                break
            m, o = i.mnemonic, i.op_str
            if m == 'push':
                d -= 4
            elif m == 'pop':
                d += 4
            elif m in ('sub', 'add') and o.startswith('esp, '):
                d += (-1 if m == 'sub' else 1) * int(o[5:], 16)
            elif m == 'call':
                if not o.startswith('0x'):
                    problems.append(f'{va:#08x} indirect call')
                    break
                pops = callee_pops(img, int(o, 16) - IMAGE_BASE)
                if len(pops) != 1:
                    problems.append(f'{va:#08x} callee pops {sorted(pops)}')
                    break
                d += next(iter(pops))
            elif o.startswith('esp,') and m not in ('cmp', 'test'):
                break  # the epilogue (mov esp, ebp): the end of this path's depth tracking
            s = successors(img, i, lo, hi)
            if s is None:
                problems.append(f'{va:#08x} unresolved {m} {o}')
                break
            if m.startswith('j') or m.startswith('ret'):
                work.extend((t, d) for t in s if lo <= t < hi)
                break
            va = s[0]
    points = {f'{p:#08x}': depth.get(p) for p in OVERLAY_ESP_POINTS}
    expected = {f'{p:#08x}': d for p, d in OVERLAY_ESP_POINTS.items()}
    # Paths that leave the decoded range (the epilogue at 0x02f09e) or reach the CRT rand() helper are not seams.
    seam_problems = [p for p in problems if 'depth' in p]
    return {'ok': points == expected and not seam_problems, 'depth_at': points,
            'conflicts': seam_problems, 'other_stops': [p for p in problems if p not in seam_problems][:8],
            'reached': len(depth)}


def header_windows():
    """The bytes and table of chase_lead_sites_fl.h (relocated dwords zeroed), parsed from the source."""
    text = HEADER.read_text()
    arrays = {m.group(1): bytes(int(x, 16) for x in re.findall(r'0x[0-9a-f]{2}\b', m.group(3)))
              for m in re.finditer(r'constexpr unsigned char (\w+)_window\[(\d+)\] = \{(.*?)\};', text, re.S)}
    table = {}
    for m in re.finditer(r'\{"(\w+)", 0x([0-9a-f]+), (\d+), \w+_window, 0x([0-9a-f]+)u, (\d+), \{([^}]*)\}, '
                         r'\{([^}]*)\}\}', text):
        offs = [int(x, 16) for x in m.group(6).split(',') if x.strip() and int(m.group(5))]
        tgts = [int(x, 16) for x in m.group(7).split(',') if x.strip() and int(m.group(5))]
        table[m.group(1)] = (int(m.group(2), 16), int(m.group(3)), int(m.group(4), 16), offs, tgts)
    return arrays, table


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
    seams = {}
    for name, rva, hexbytes, text, target in SEAMS:
        raw = img.rva(rva, 6)
        got, size = img.text_at(rva)
        interior = sorted(a for a, _, t in edges if rva < t < rva + 6)
        to_start = sorted(a for a, _, t in edges if t == rva)
        fixups = sorted(f for f in img.fixups if rva - 3 <= f < rva + 6)
        rel_ok = target is None or rva + 6 + struct.unpack_from('<i', raw, 2)[0] == target
        seams[name] = {'ok': raw == bytes.fromhex(hexbytes) and got == text and size == 6 and rel_ok and not interior
                       and not fixups,
                       'bytes': raw.hex(), 'decode': got, 'interior_edges': [f'{a:#08x}' for a in interior],
                       'edges_to_start': [f'{a:#08x}' for a in to_start], 'fixups': fixups,
                       'qword': 'fits' if (rva & 7) + 5 <= 8 else 'crosses', 'rva_mod_8': rva & 7}
    checks['seams'] = {'ok': all(s['ok'] for s in seams.values()), 'direct_edges': len(edges), **seams}
    callers = sorted(a for a, m, t in edges if t == OVERLAY[0])
    hide_callers = len([1 for a, m, t in edges if t == 0x027c20])
    checks['overlay_caller'] = {'ok': callers == [OVERLAY_CALL], 'callers': [f'{a:#08x}' for a in callers],
                                'hide_helper_call_sites': hide_callers}
    checks['updater_cfg'] = updater_cfg(img)
    checks['overlay_stack'] = overlay_stack(img)
    anchors = []
    for rva, text, what in ANCHORS:
        got, _ = img.text_at(rva)
        anchors.append({'rva': f'{rva:#08x}', 'ok': got == text, 'got': got, 'what': what})
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
