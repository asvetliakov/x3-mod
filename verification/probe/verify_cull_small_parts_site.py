#!/usr/bin/env python3
"""Read-only qualification of the small-parts cull trampoline site in 0x0047cfe0.

src/proxy/cull_small_parts.cpp claims the five bytes `mov ecx,[edi+0x18];
test ecx,ecx` at 0x0047d2a2 (the start of the effective-limit computation of
the per-node cull and LOD pass) and, when a node's `s` is below the frame's
pixel threshold, jumps to the engine's own size-cull instruction
`and dword [edi+0x12c],0xfffffffd` at 0x0047d2c3 (docs/reverse-engineering/
lod-selection.md, "Cull small parts site"). This checks the installed X3AP.exe
on the host: exact identity, the 56-byte window 0x0047d294..0x0047d2cc as
whole decoded instructions, that the site is exactly two whole instructions
of five bytes whose displaced `test` is the flag writer for the `je` after
the next instruction `mov eax,[edi+0x1d8]`, that the cull target is the
expected `and` followed by `jmp 0x0047d2d1`, that no direct branch anywhere
in the function lands inside the displaced span, that the site's incoming
branches are exactly the documented ones, that every branch inside the window
stays inside it or lands on its end, that the frame prologue and the three `[esp+0x2c]` stores that make that
slot `s` are the expected bytes, that the claim is disjoint from the
cull-census sites (0x0047d258, 0x0047d528), that the function ends in `ret 8`, that the engine's class-0
projectile marker is established by the pinned instructions (0x004401ae
stores 0x20800000, 0x0044123b..0x00441248 ORs it into the root node's +0x130)
and consumed as 0x20000000 by the occluder filter at 0x00488b00, that the
JET flag pair the far-jet block tests is written by the pinned
`or dword [esi+0x130],0x4000001` at 0x00434708, and that
src/proxy/cull_small_parts_core.h carries the same constants and window. Also
the stub encoder (with the carrier dock-port id ranges, 2026-09-29) and the
`cull_small_parts`, `cull_small_parts_value` and `cull_small_parts_frame` line
parsers the host test exercises. No Wine, no game launch.
"""
import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

import verify_chase_aim_sites as common

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/cull_small_parts_core.h'
DEFAULT_EXE = common.DEFAULT_EXE
FUNCTION = (0x47cfe0, 0x47d552)
WINDOW_VA, SITE_VA, NEXT_VA, JE_VA, CULL_VA, AFTER_CULL_VA = 0x47d294, 0x47d2a2, 0x47d2a7, 0x47d2ad, 0x47d2c3, 0x47d2d1
WINDOW = bytes.fromhex('83fe14 7d09 33f6 83a72c010000fd 8b4f18 85c9 8b87d8010000 740c 8b89d8010000 3bc8 7e02 8bc1 85c0 7e0d 3bf0 7d09 83a72c010000fd eb05'.replace(' ', ''))
SITE = WINDOW[SITE_VA - WINDOW_VA:SITE_VA - WINDOW_VA + 5]
CULL = bytes.fromhex('83a72c010000fd')
WINDOW_INSTRUCTIONS = [0x47d294, 0x47d297, 0x47d299, 0x47d29b, 0x47d2a2, 0x47d2a5, 0x47d2a7, 0x47d2ad, 0x47d2af, 0x47d2b5, 0x47d2b7,
                       0x47d2b9, 0x47d2bb, 0x47d2bd, 0x47d2bf, 0x47d2c1, 0x47d2c3, 0x47d2ca]
SITE_SOURCES = [0x47d28c, 0x47d297]
# Claims this stub must not overlap: the census's two six-byte sites (the lod_scale site was removed on 2026-09-25).
OTHER_CLAIMS = {'cull_census_measure': (0x47d258, 6), 'cull_census_exit': (0x47d528, 6)}
RET_VA = 0x47d54f
# The stub reads `s` at [ESP+0x2c] with the site's ESP: the frame is `sub esp,0x14` plus four pushes (cdecl calls
# before the site rebalance with `add esp`), and these are the three final writers of that slot before the site.
PROLOGUE = bytes.fromhex('83ec14 8a44241c 53 55 56 57'.replace(' ', ''))
S_STORES = {0x47d229: bytes.fromhex('c744242c00000007'), 0x47d24a: bytes.fromhex('8944242c'), 0x47d250: bytes.fromhex('c744242c01000000')}
STUB_LENGTH, STUB_PROJECTILE, STUB_REPLAY, STUB_CULL, STUB_EXEMPT, STUB_CONTINUE = 188, 91, 150, 175, 180, 63
STUB_POP_CONTINUE, STUB_DOCK, STUB_DOCK_PROJECTILE, STUB_DOCK_COUNT, STUB_SMALL, STUB_COUNT = 62, 69, 70, 82, 90, 144
STUB_FAR, STUB_FAR_LENGTH, STUB_FAR_CALL = 103, 41, 133
# Carrier dock-port parts (docs/reverse-engineering/ship-scene-parts.md): model ids (node+0x140) of the inline bodies of the
# stock dock cut scenes 9013/9014 and 9098/9099, id = local + (cut - 1) * 100000.
MODEL_OFFSET, DOCK_FIRST_BASE, DOCK_SECOND_BASE, DOCK_SPAN = 0x140, 901300000, 909800000, 200000
# The engine's class-0 (TBullets) root-node marker the stub's exemption tests (docs/reverse-engineering/lod-selection.md, "Projectile nodes").
FLAGS130_OFFSET, PROJECTILE_FLAG = 0x130, 0x20000000
MARKER_STORE_VA, MARKER_STORE = 0x4401ae, bytes.fromhex('c744242000008020')              # mov dword [esp+0x20],0x20800000 (class-0 case)
MARKER_OR_VA, MARKER_OR = 0x44123b, bytes.fromhex('8b4570 8b542420 099030010000'.replace(' ', ''))  # mov eax,[ebp+0x70]; mov edx,[esp+0x20]; or [eax+0x130],edx
MARKER_USE_VA, MARKER_USE = 0x488b00, bytes.fromhex('f7873001000000000020')             # test dword [edi+0x130],0x20000000 (occluder filter)
# The JET flag pair (docs/reverse-engineering/engine-effects.md, 0x00434620): every SBTYPE_JET body's node takes
# +0x130 |= 0x4000001; the far-jet block (X3M_ENGINE_EFFECTS=plumes) tests both bits.
JET_FLAGS, JET_FLAG_HIGH, JET_FLAG_LOW = 0x4000001, 0x4000000, 0x1
JET_WRITER_VA, JET_WRITER = 0x434708, bytes.fromhex('818e3001000001000004')               # or dword [esi+0x130],0x4000001
LOG_RE = re.compile(r'\bcull_small_parts requested=(?P<requested>\S+) px=(?P<px>[0-9.e+-]+) patched=(?P<patched>[01]) reason=(?P<reason>\S+) '
                    r'site=0x(?P<site>[0-9a-f]{8}) cull=0x(?P<cull>[0-9a-f]{8}) write=(?P<write>none|atomic|plain) stub=0x(?P<stub>[0-9a-f]{8}) camera=(?P<camera>\S+)(?: scope=(?P<scope>bodies|all|invalid))?'
                    r'(?: projectiles=(?P<projectiles>on|off|marker_mismatch|invalid))?(?: dock_px=(?P<dock_px>[0-9.e+-]+) dock_requested=(?P<dock_requested>\S+))?'
                    r'(?: far_jets=(?P<far_jets>on|off|writer_mismatch))?')
VALUE_RE = re.compile(r'\bcull_small_parts_value px=(?P<px>[0-9.e+-]+) m00=(?P<m00>[0-9.e+-]+) width=(?P<width>\d+) threshold=(?P<threshold>-?\d+)'
                      r'(?: focus=0x(?P<focus>[0-9a-f]+))?(?: source=(?P<source>scene|registry))?(?: fallback=(?P<fallback>none|no_scene|reset|aged))?'
                      r'(?: dock_px=(?P<dock_px>[0-9.e+-]+) dock_threshold=(?P<dock_threshold>-?\d+))?')
FRAME_RE = re.compile(r'\bcull_small_parts_frame device=(?P<device>\d+) frame=(?P<frame>\d+) px=(?P<px>[0-9.e+-]+) threshold=(?P<threshold>-?\d+) '
                      r'culled=(?P<culled>\d+) m00=(?P<m00>[0-9.e+-]+) width=(?P<width>\d+)(?: scope=(?P<scope>bodies|all))?'
                      r'(?: projectiles=(?P<projectiles>on|off) exempt_bullet=(?P<exempt>\d+))?'
                      r'(?: focus=0x(?P<focus>[0-9a-f]+) source=(?P<source>scene|registry))?(?: fallback=(?P<fallback>none|no_scene|reset|aged))?'
                      r'(?: dock_px=(?P<dock_px>[0-9.e+-]+) dock_threshold=(?P<dock_threshold>-?\d+) dock_culled=(?P<dock_culled>\d+))?'
                      r'(?: far_jets=(?P<far_jets>on|off))?')


def encode_stub(at, threshold, upper, culled, exempt, dock_culled, cull_target, next_slot, projectiles=True, far_handler=0, far_jets=False):
    """cmp dword [upper],0; jle continue; push eax; mov eax,[upper]; cmp [esp+0x30],eax; jge pop_continue;
    mov eax,[threshold]; cmp [esp+0x30],eax; jl pop_small; mov eax,[edi+0x140]; sub eax,901300000; cmp eax,200000; jb pop_dock;
    sub eax,8500000; cmp eax,200000; jb pop_dock; pop_continue: pop eax; continue: jmp [next];
    pop_dock: pop eax; test dword [edi+0x130],0x20000000; jne exempt; inc dword [dock_culled]; jmp replay;
    pop_small: pop eax; test dword [edi+0x130],0x20000000; jne exempt; test dword [edi+0x130],0x4000000; je count;
    test byte [edi+0x130],1; je count; push eax; push ecx; push edx; push dword [esp+0x34]; push esi; push edi;
    call far_handler; add esp,12; pop edx; pop ecx; pop eax; count: inc dword [culled];
    replay: mov ecx,[edi+0x18]; test ecx,ecx; mov eax,[edi+0x1d8]; je cull; mov ecx,[ecx+0x1d8]; cmp ecx,eax; jle cull; mov eax,ecx;
    cull: jmp cull_target; exempt: inc dword [exempt]; jmp continue. Projectiles off replaces each 12-byte marker test
    (bytes 70..81 and 91..102) with jmp +10 and int3 padding; far jets off (the default) the 41-byte far block (bytes
    103..143) with jmp +39 and int3 padding. The C++ encoder's contract (cull_small_parts_core.h)."""
    for value in (at, threshold, upper, culled, exempt, dock_culled, cull_target, next_slot, far_handler):
        if not 0 <= value <= 0xffffffff:
            raise ValueError('addresses must be 32-bit VAs')
    code = bytearray()

    def rel8(target):
        code.append((target - (len(code) + 1)) & 0xff)

    def marker():
        code.extend(b'\xf7\x87' + struct.pack('<II', FLAGS130_OFFSET, PROJECTILE_FLAG) + b'\x75')
        rel8(STUB_EXEMPT)
    code += b'\x83\x3d' + struct.pack('<I', upper) + b'\x00' + b'\x7e'
    rel8(STUB_CONTINUE)
    code += b'\x50' + b'\xa1' + struct.pack('<I', upper) + b'\x39\x44\x24\x30' + b'\x7d'
    rel8(STUB_POP_CONTINUE)
    code += b'\xa1' + struct.pack('<I', threshold) + b'\x39\x44\x24\x30' + b'\x7c'
    rel8(STUB_SMALL)
    code += b'\x8b\x87' + struct.pack('<I', MODEL_OFFSET) + b'\x2d' + struct.pack('<I', DOCK_FIRST_BASE) + b'\x3d' + struct.pack('<I', DOCK_SPAN) + b'\x72'
    rel8(STUB_DOCK)
    code += b'\x2d' + struct.pack('<I', DOCK_SECOND_BASE - DOCK_FIRST_BASE) + b'\x3d' + struct.pack('<I', DOCK_SPAN) + b'\x72'
    rel8(STUB_DOCK)
    code += b'\x58' + b'\xff\x25' + struct.pack('<I', next_slot) + b'\x58'
    marker()
    code += b'\xff\x05' + struct.pack('<I', dock_culled) + b'\xeb'
    rel8(STUB_REPLAY)
    code += b'\x58'
    marker()
    code += b'\xf7\x87' + struct.pack('<II', FLAGS130_OFFSET, JET_FLAG_HIGH) + b'\x74'
    rel8(STUB_COUNT)
    code += b'\xf6\x87' + struct.pack('<I', FLAGS130_OFFSET) + bytes([JET_FLAG_LOW]) + b'\x74'
    rel8(STUB_COUNT)
    code += b'\x50\x51\x52' + b'\xff\x74\x24\x34' + b'\x56\x57'
    code += b'\xe8' + struct.pack('<I', (far_handler - (at + STUB_FAR_CALL + 5)) & 0xffffffff)
    code += b'\x83\xc4\x0c' + b'\x5a\x59\x58'
    code += b'\xff\x05' + struct.pack('<I', culled)
    code += b'\x8b\x4f\x18' + b'\x85\xc9' + b'\x8b\x87\xd8\x01\x00\x00' + b'\x74'
    rel8(STUB_CULL)
    code += b'\x8b\x89\xd8\x01\x00\x00' + b'\x3b\xc8' + b'\x7e'
    rel8(STUB_CULL)
    code += b'\x8b\xc1' + b'\xe9' + struct.pack('<I', (cull_target - (at + STUB_EXEMPT)) & 0xffffffff)
    code += b'\xff\x05' + struct.pack('<I', exempt) + b'\xeb'
    rel8(STUB_CONTINUE)
    if not projectiles:
        for start in (STUB_DOCK_PROJECTILE, STUB_PROJECTILE):
            code[start:start + 12] = b'\xeb\x0a' + b'\xcc' * 10
    if not far_jets:
        code[STUB_FAR:STUB_FAR + STUB_FAR_LENGTH] = bytes([0xeb, STUB_FAR_LENGTH - 2]) + b'\xcc' * (STUB_FAR_LENGTH - 2)
    assert len(code) == STUB_LENGTH
    return bytes(code)


def focus_from_projection(m00, m11):
    """The view's binary-angle FOV from P[0]/P[5]: cot(F/2) = max(0.75*m11, m00); 0 when unusable or outside 0x106..0x8000 (the core's twin)."""
    import math
    if not (math.isfinite(m00) and math.isfinite(m11) and m00 > 0 and m11 > 0):
        return 0
    exact = 65536 / math.pi * math.atan(1 / max(0.75 * m11, m00))
    if abs(exact - 0x4000) <= 2.0:   # focus_snap: the engine's projection is about one unit of F off at the default
        return 0x4000
    focus = math.floor(exact + 0.5)
    return focus if 0x106 <= focus <= 0x8000 else 0


def threshold_for(px, m00, width, focus=0x4000):
    """The smallest integer t with t * px_per_s >= px (px_per_s = m00 * width / 1280 * focus / 0x4000, focus the engine's
    binary-angle FOV base), the summariser's bucket rule; 0 when unusable."""
    import math
    if not (0 < px <= 64) or not (0.05 < m00 < 20) or not (64 <= width <= 16384) or not (0x106 <= focus <= 0x8000):
        return 0
    px_per_s = m00 * width / 1280.0 * (focus / 0x4000)
    t = math.ceil(px / px_per_s)
    while t > 1 and (t - 1) * px_per_s >= px:
        t -= 1
    while t * px_per_s < px:
        t += 1
    return min(t, 0x1000000)


def parse_log_line(line):
    match = LOG_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'requested': row['requested'], 'px': float(row['px']), 'patched': row['patched'] == '1', 'reason': row['reason'],
            'site': int(row['site'], 16), 'cull': int(row['cull'], 16), 'write': row['write'], 'stub': int(row['stub'], 16), 'camera': row['camera'], 'scope': row['scope'],
            'projectiles': row['projectiles'], 'dock_px': float(row['dock_px']) if row['dock_px'] is not None else None,
            'dock_requested': row['dock_requested'], 'far_jets': row['far_jets']}


def parse_value_line(line):
    match = VALUE_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'px': float(row['px']), 'm00': float(row['m00']), 'width': int(row['width']), 'threshold': int(row['threshold']),
            'focus': int(row['focus'], 16) if row['focus'] else None, 'source': row['source'], 'fallback': row['fallback'],
            'dock_px': float(row['dock_px']) if row['dock_px'] is not None else None,
            'dock_threshold': int(row['dock_threshold']) if row['dock_threshold'] is not None else None}


def parse_frame_line(line):
    match = FRAME_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'device': int(row['device']), 'frame': int(row['frame']), 'px': float(row['px']), 'threshold': int(row['threshold']),
            'culled': int(row['culled']), 'm00': float(row['m00']), 'width': int(row['width']), 'scope': row['scope'],
            'projectiles': row['projectiles'], 'exempt_bullet': int(row['exempt']) if row['exempt'] is not None else None,
            'focus': int(row['focus'], 16) if row['focus'] else None, 'source': row['source'], 'fallback': row['fallback'],
            'dock_px': float(row['dock_px']) if row['dock_px'] is not None else None,
            'dock_threshold': int(row['dock_threshold']) if row['dock_threshold'] is not None else None,
            'dock_culled': int(row['dock_culled']) if row['dock_culled'] is not None else None,
            'far_jets': row['far_jets']}


def dock_model(model_id):
    """The stub's dock-port id test: two unsigned range compares (cull_small_parts_core.h dock_model)."""
    return (model_id - DOCK_FIRST_BASE) % (1 << 32) < DOCK_SPAN or (model_id - DOCK_SECOND_BASE) % (1 << 32) < DOCK_SPAN


def projectile_stub_ok():
    """Both marker tests sit below `upper` only (after the jge), read node+0x130 against 0x20000000 and branch to the exempt count,
    which jumps back to the continue jump; the dock-port id compares run only between the two thresholds (after the jl);
    `off` jumps over both marker tests to the counts."""
    args = (0x10000000, 0x10002000, 0x1000200c, 0x10002004, 0x10002008, 0x10002010, CULL_VA, 0x10000094)
    on, off = encode_stub(*args), encode_stub(*args, projectiles=False)
    marker = b'\xf7\x87' + struct.pack('<II', FLAGS130_OFFSET, PROJECTILE_FLAG) + b'\x75'
    return (on[STUB_DOCK_PROJECTILE:STUB_DOCK_PROJECTILE + 11] == marker and STUB_DOCK_PROJECTILE + 12 + on[STUB_DOCK_PROJECTILE + 11] == STUB_EXEMPT
            and on[STUB_PROJECTILE:STUB_PROJECTILE + 11] == marker and STUB_PROJECTILE + 12 + on[STUB_PROJECTILE + 11] == STUB_EXEMPT
            and on[STUB_EXEMPT:STUB_EXEMPT + 2] == b'\xff\x05' and struct.unpack_from('<I', on, STUB_EXEMPT + 2)[0] == 0x10002008
            and on[STUB_EXEMPT + 6] == 0xeb and (STUB_EXEMPT + 8 + struct.unpack_from('<b', on, STUB_EXEMPT + 7)[0]) == STUB_CONTINUE
            and on[STUB_CONTINUE:STUB_CONTINUE + 2] == b'\xff\x25' and 9 + on[8] == STUB_CONTINUE and 21 + on[20] == STUB_POP_CONTINUE
            and 32 + on[31] == STUB_SMALL and 50 + on[49] == STUB_DOCK and 62 + on[61] == STUB_DOCK
            and on[STUB_DOCK] == 0x58 and on[STUB_SMALL] == 0x58 and on[STUB_POP_CONTINUE] == 0x58
            and on[STUB_REPLAY:STUB_REPLAY + 5] == SITE
            and struct.unpack_from('<I', on, 34)[0] == MODEL_OFFSET and struct.unpack_from('<I', on, 39)[0] == DOCK_FIRST_BASE
            and struct.unpack_from('<I', on, 51)[0] == DOCK_SECOND_BASE - DOCK_FIRST_BASE
            and off[:STUB_DOCK_PROJECTILE] == on[:STUB_DOCK_PROJECTILE] and off[STUB_DOCK_COUNT:STUB_PROJECTILE] == on[STUB_DOCK_COUNT:STUB_PROJECTILE]
            and off[STUB_COUNT:] == on[STUB_COUNT:]
            and off[STUB_DOCK_PROJECTILE:STUB_DOCK_COUNT] == off[STUB_PROJECTILE:STUB_FAR] == b'\xeb\x0a' + b'\xcc' * 10)


def far_stub_ok():
    """The far block sits after the small path's marker test only (below the small threshold, never on the dock path): a
    node without both bits of node+0x130 (0x4000000, then bit 0) skips to the culled count; one with both saves EAX/ECX/
    EDX, pushes the site's view ([ESP+0x28] = [ESP+0x34] after the three pushes), ESI (measure) and EDI (node), calls the
    handler, pops its three arguments and the registers, and falls into the count and the replay: the node is culled
    either way. Off (the default) jumps over the 41 bytes to the count; nothing else differs between the two."""
    args = (0x10000000, 0x10002000, 0x1000200c, 0x10002004, 0x10002008, 0x10002010, CULL_VA, 0x10000094)
    on = encode_stub(*args, far_handler=0x10400000, far_jets=True)
    off = encode_stub(*args, far_handler=0x10400000)
    call = STUB_FAR + 30
    return (on[STUB_FAR:STUB_FAR + 10] == b'\xf7\x87' + struct.pack('<II', FLAGS130_OFFSET, JET_FLAG_HIGH)
            and on[STUB_FAR + 10] == 0x74 and STUB_FAR + 12 + on[STUB_FAR + 11] == STUB_COUNT
            and on[STUB_FAR + 12:STUB_FAR + 19] == b'\xf6\x87' + struct.pack('<I', FLAGS130_OFFSET) + bytes([JET_FLAG_LOW])
            and on[STUB_FAR + 19] == 0x74 and STUB_FAR + 21 + on[STUB_FAR + 20] == STUB_COUNT
            and on[STUB_FAR + 21:call] == b'\x50\x51\x52\xff\x74\x24\x34\x56\x57' and call == STUB_FAR_CALL
            and on[call] == 0xe8 and 0x10000000 + call + 5 + struct.unpack_from('<i', on, call + 1)[0] == 0x10400000
            and on[call + 5:STUB_COUNT] == b'\x83\xc4\x0c\x5a\x59\x58'
            and JET_FLAG_HIGH | JET_FLAG_LOW == JET_FLAGS and STUB_PROJECTILE + 12 == STUB_FAR and STUB_FAR + STUB_FAR_LENGTH == STUB_COUNT
            and on[STUB_COUNT:STUB_COUNT + 2] == b'\xff\x05' and struct.unpack_from('<I', on, STUB_COUNT + 2)[0] == 0x10002004
            and on[STUB_REPLAY:STUB_REPLAY + 5] == SITE
            and off[STUB_FAR:STUB_COUNT] == bytes([0xeb, STUB_FAR_LENGTH - 2]) + b'\xcc' * (STUB_FAR_LENGTH - 2)
            and off[:STUB_FAR] == on[:STUB_FAR] and off[STUB_COUNT:] == on[STUB_COUNT:])


def source_constants(text):
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*[;,]', text)
        return int(match.group(1), 0) if match else None

    def array(name):
        match = re.search(rf'\b{name}\[\w+\]\s*=\s*\{{([^}}]*)\}}', text)
        return bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))) if match else b''
    names = ('function_va', 'function_end_va', 'window_va', 'site_va', 'next_va', 'je_va', 'cull_va', 'after_cull_va', 'window_length', 'site_offset',
             'site_length', 'cull_offset', 'ret_pop', 'parent_offset', 'threshold_1d8_offset', 'stub_length', 'stub_cull', 'stub_continue',
             'stub_projectile', 'stub_replay', 'stub_exempt', 'flags130_offset', 'projectile_flag', 'marker_store_va', 'marker_or_va',
             'marker_store_length', 'marker_or_length', 'stub_pop_continue', 'stub_dock', 'stub_dock_projectile', 'stub_dock_count',
             'stub_small', 'stub_count', 'model_offset', 'dock_first_base', 'dock_second_base', 'dock_span', 'stub_far',
             'stub_far_length', 'stub_far_call', 'jet_flags', 'jet_flag_high', 'jet_flag_low', 'jet_writer_va', 'jet_writer_length')
    return {name: value(name) for name in names} | {'window': array('window'), 'site': array('site'),
                                                     'marker_store': array('marker_store'), 'marker_or': array('marker_or'),
                                                     'jet_writer': array('jet_writer')}


EXPECTED_CONSTANTS = {'function_va': FUNCTION[0], 'function_end_va': FUNCTION[1], 'window_va': WINDOW_VA, 'site_va': SITE_VA, 'next_va': NEXT_VA, 'je_va': JE_VA,
                      'cull_va': CULL_VA, 'after_cull_va': AFTER_CULL_VA, 'window_length': len(WINDOW), 'site_offset': SITE_VA - WINDOW_VA, 'site_length': 5,
                      'cull_offset': CULL_VA - WINDOW_VA, 'ret_pop': 8, 'parent_offset': 0x18, 'threshold_1d8_offset': 0x1d8,
                      'stub_length': STUB_LENGTH, 'stub_cull': STUB_CULL, 'stub_continue': STUB_CONTINUE, 'window': WINDOW, 'site': SITE,
                      'stub_projectile': STUB_PROJECTILE, 'stub_replay': STUB_REPLAY, 'stub_exempt': STUB_EXEMPT, 'flags130_offset': FLAGS130_OFFSET,
                      'projectile_flag': PROJECTILE_FLAG, 'marker_store_va': MARKER_STORE_VA, 'marker_or_va': MARKER_OR_VA,
                      'marker_store_length': len(MARKER_STORE), 'marker_or_length': len(MARKER_OR), 'marker_store': MARKER_STORE, 'marker_or': MARKER_OR,
                      'stub_pop_continue': STUB_POP_CONTINUE, 'stub_dock': STUB_DOCK, 'stub_dock_projectile': STUB_DOCK_PROJECTILE,
                      'stub_dock_count': STUB_DOCK_COUNT, 'stub_small': STUB_SMALL, 'stub_count': STUB_COUNT, 'model_offset': MODEL_OFFSET,
                      'dock_first_base': DOCK_FIRST_BASE, 'dock_second_base': DOCK_SECOND_BASE, 'dock_span': DOCK_SPAN,
                      'stub_far': STUB_FAR, 'stub_far_length': STUB_FAR_LENGTH, 'stub_far_call': STUB_FAR_CALL, 'jet_flags': JET_FLAGS,
                      'jet_flag_high': JET_FLAG_HIGH, 'jet_flag_low': JET_FLAG_LOW, 'jet_writer_va': JET_WRITER_VA,
                      'jet_writer_length': len(JET_WRITER), 'jet_writer': JET_WRITER}


def decode(exe):
    return common.parse_objdump(common.objdump_window(exe, *FUNCTION, timeout=60), *FUNCTION)


def inspect(data, instructions, core_text):
    image = common.Image(data)
    by_va = {i.va: i for i in instructions}
    branches = [(i.va, t) for i in instructions for t in [common._is_direct_control(i)] if t is not None]
    interior = sorted((a, t) for a, t in branches if SITE_VA < t < SITE_VA + 5)
    sources = sorted(a for a, t in branches if t == SITE_VA)
    window_end = WINDOW_VA + len(WINDOW)
    inside = [(a, t) for a, t in branches if WINDOW_VA <= a < window_end]
    site, test, nxt, je = by_va.get(SITE_VA), by_va.get(SITE_VA + 3), by_va.get(NEXT_VA), by_va.get(JE_VA)
    cull, after = by_va.get(CULL_VA), by_va.get(CULL_VA + 7)
    ret = by_va.get(RET_VA)
    claim = (SITE_VA, SITE_VA + 5)
    checks = {
        'exe_identity': exe_identity.identity_ok(data),
        'preferred_base': image.image_base == common.IMAGE_BASE,
        'window_bytes': image.read(WINDOW_VA, len(WINDOW)) == WINDOW,
        'window_whole_instructions': [i.va for i in instructions if WINDOW_VA <= i.va < window_end] == WINDOW_INSTRUCTIONS and by_va.get(window_end) is not None,
        # Two whole instructions of five bytes; the displaced TEST is the flag writer the JE after the next instruction consumes.
        'site_whole_instructions': site is not None and test is not None and site.raw + test.raw == SITE and site.mnemonic == 'mov' and test.mnemonic == 'test' and test.end == NEXT_VA,
        'next_instruction': nxt is not None and nxt.mnemonic == 'mov' and nxt.raw == bytes.fromhex('8b87d8010000') and nxt.end == JE_VA,
        'je_consumes_flags': je is not None and je.mnemonic == 'je' and common._is_direct_control(je) == 0x47d2bb,
        'cull_instruction': cull is not None and cull.raw == CULL and cull.mnemonic == 'and' and after is not None and after.mnemonic == 'jmp' and common._is_direct_control(after) == AFTER_CULL_VA,
        'prologue_frame': image.read(FUNCTION[0], len(PROLOGUE)) == PROLOGUE
                          and [by_va[a].mnemonic if a in by_va else None for a in (0x47cfe0, 0x47cfe7, 0x47cfe8, 0x47cfe9, 0x47cfea)] == ['sub', 'push', 'push', 'push', 'push'],
        's_slot_stores': all(a in by_va and by_va[a].raw == raw and by_va[a].mnemonic == 'mov' for a, raw in S_STORES.items()),
        'no_interior_branch': interior == [],
        'site_sources': sources == SITE_SOURCES,
        # Every branch inside the window lands inside it or on its end (0x0047d2cc) or on the after-cull target.
        'window_branches_contained': all(WINDOW_VA <= t <= window_end or t == AFTER_CULL_VA for _, t in inside) and len(inside) == 6,
        'claim_disjoint': all(claim[1] <= a or a + n <= claim[0] for a, n in OTHER_CLAIMS.values()),
        'function_ret': ret is not None and ret.mnemonic == 'ret' and ret.raw == bytes.fromhex('c20800') and ret.end == FUNCTION[1],
        'source_constants': source_constants(core_text) == EXPECTED_CONSTANTS,
        'encoder': len(encode_stub(0x10000000, 0x10002000, 0x1000200c, 0x10002004, 0x10002008, 0x10002010, CULL_VA, 0x10000094)) == STUB_LENGTH,
        # The exemption's premise: the class-0 creation path stores 0x20800000 and ORs it into the root node's +0x130, and the engine
        # itself reads bit 0x20000000 of +0x130 (occluder filter). Bytes only: these lie outside the decoded pass.
        'projectile_marker': image.read(MARKER_STORE_VA, len(MARKER_STORE)) == MARKER_STORE and image.read(MARKER_OR_VA, len(MARKER_OR)) == MARKER_OR
                             and image.read(MARKER_USE_VA, len(MARKER_USE)) == MARKER_USE,
        'encoder_projectiles': projectile_stub_ok(),
        # The far-jet block's premise: the JET list's match ORs 0x4000001 into the node's +0x130 (bytes only).
        'jet_writer': image.read(JET_WRITER_VA, len(JET_WRITER)) == JET_WRITER,
        'encoder_far_jets': far_stub_ok(),
        'threshold_rule': (threshold_for(2, struct.unpack('<f', struct.pack('<I', 0x3f4ccccc))[0], 1280), threshold_for(4, struct.unpack('<f', struct.pack('<I', 0x3f4ccccc))[0], 1280),
                           threshold_for(8, struct.unpack('<f', struct.pack('<I', 0x3f4ccccc))[0], 1280)) == (3, 6, 11),
    }
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'exe_info': exe_identity.info(data), 'site': hex(SITE_VA), 'cull': hex(CULL_VA),
            'site_sources': [hex(a) for a in sources], 'interior_branches': [(hex(a), hex(t)) for a, t in interior],
            'other_claims': {k: hex(a) for k, (a, _) in OTHER_CLAIMS.items()},
            'function_instructions': len(instructions), 'exe_sha256': hashlib.sha256(data).hexdigest()}


def verify(exe=DEFAULT_EXE, core=CORE):
    data = common.image_bytes(exe)
    try:
        return inspect(data, decode(exe), Path(core).read_text())
    except (ValueError, OSError, subprocess.SubprocessError, RuntimeError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': str(error)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--core', type=Path, default=CORE)
    args = parser.parse_args()
    report = verify(args.exe, args.core)
    print(json.dumps(report, indent=2, sort_keys=True))
    raise SystemExit(report['result'] != 'PASS')
