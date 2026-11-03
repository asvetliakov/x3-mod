#!/usr/bin/env python3
"""Read-only qualification of the FL UI scale sites against X3FL.exe.

Checks the eleven claims of src/engine/fl/ui_scale_fl.cpp and the identity windows of src/engine/fl/ui_scale_sites_fl.h
on the installed executable (Capstone over the file; nothing executed): the bytes and FNV-1a of every window with its
HIGHLOW dwords zeroed and the .reloc census of every window (exactly the declared fixups and targets); every span's bytes,
whole-instruction decode from a known boundary and expected instruction text; the declared fixups inside the spans and no
other; the five-byte jump inside one aligned qword; no direct branch (linear sweep of .text), no raw rel8/rel32 encoding
in the containing function and no relocated image dword (jump tables, pointer tables) into any span interior; the
dispatch facts (the B3D, INS and X2 jump-table entries and name-table strings that make the case bodies the named
natives); the register facts the stubs rely on (anchor instructions) and a liveness walk proving every register or flag a
stub clobbers is written before it is read on the continuation path (into the callee where the path calls one); the
projection routine's two direct callers and its suffix (immediate stores only after the site). With --source the
header's window bytes, table and site table are compared with the executable too. docs/reverse-engineering/fl/ui-scale.md,
"Verified 2026-11-03".

usage: verify_ui_scale_sites_fl.py [--exe PATH] [--source] [--json] [--out FILE]   (exit 0 = PASS, 2 = executable not
found)
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

from verify_chase_camera_site import FlImage, fnv1a32, FL_DEFAULT_EXE, FL_SHA256, FL_TIMESTAMP, FL_IMAGE_SIZE

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'src/engine/fl/ui_scale_sites_fl.h'
# (name, start RVA, end RVA, {fixup RVA: target RVA}) - ui_scale_sites_fl.h `windows`.
WINDOWS = (
    ('proj_entry', 0x0de570, 0x0de5bf, {}), ('proj_matrix', 0x0de760, 0x0de76b, {0x0de761: 0x1aae64, 0x0de767: 0x1aae4c}),
    ('proj_tail', 0x0de885, 0x0de96d, {}), ('proj_caller1', 0x096fb1, 0x096fd6, {}), ('proj_caller2', 0x09769b, 0x0976c0, {}),
    ('size_cases', 0x0b23c6, 0x0b2410, {0x0b23c7: 0x1a82f8, 0x0b23d7: 0x1aa174, 0x0b23ec: 0x1a82f8, 0x0b23fc: 0x1aa174}),
    ('size_callee', 0x0c1940, 0x0c1958, {}), ('delta_direct', 0x077fa9, 0x077ff5, {0x077fdb: 0x18ba3c}),
    ('delta_mapped', 0x0d22d6, 0x0d232d, {0x0d22d8: 0x21c880, 0x0d22e1: 0x21c884, 0x0d22ea: 0x21c884, 0x0d22f0: 0x21c880,
                                          0x0d22f9: 0x18ba3c, 0x0d2311: 0x21c880, 0x0d2320: 0x21c884}),
    ('callback_direct', 0x0bb5b0, 0x0bb5c3, {}), ('callback_mapped', 0x0bb500, 0x0bb513, {}),
    ('cursor', 0x07bc3a, 0x07bc62, {0x07bc41: 0x1aaa10, 0x07bc49: 0x1aa184, 0x07bc51: 0x1aa180, 0x07bc59: 0x1aa188}),
    ('cursor_tail', 0x07b71e, 0x07b72e, {0x07b720: 0x1aa174}),
    ('overlay_case', 0x0320aa, 0x0320f2, {0x0320af: 0x1a82e4, 0x0320e1: 0x1aa174}), ('overlay_callee', 0x02bae0, 0x02baf5, {}),
    ('aim_case', 0x031093, 0x031103, {0x031098: 0x1a82e4, 0x0310f2: 0x1aa174}),
    ('map_label', 0x020138, 0x020186, {0x02016b: 0x185150, 0x020175: 0x1a82e8}), ('pick_case', 0x031cd8, 0x031d15, {}),
    ('pick_callee', 0x01f6b0, 0x01f6e3, {0x01f6be: 0x1a82e0, 0x01f6df: 0x1a82f8}),
    ('cockpit_walk', 0x0ce57e, 0x0ce5a2, {0x0ce580: 0x1a82e4}), ('cockpit_init', 0x021b60, 0x021bc6, {0x021b7b: 0x185194}),
    ('camera_attach', 0x0a5860, 0x0a589a, {}), ('b3d_entries', 0x0b2b48, 0x0b2b50, {0x0b2b48: 0x0b23c6, 0x0b2b4c: 0x0b23eb}),
    ('ins_aim_entry', 0x032528, 0x03252c, {0x032528: 0x031093}), ('ins_pick_entry', 0x032580, 0x032584, {0x032580: 0x031cd8}),
    ('ins_overlay_entry', 0x032618, 0x03261c, {0x032618: 0x0320aa}),
    ('x2_cursor_entry', 0x07beac, 0x07beb0, {0x07beac: 0x07bc3a}), ('x2_cursor_index', 0x07bef7, 0x07bef8, {}),
)
# (name, RVA, length, bytes with the relocated dword zeroed, fixup offset or 0, its target RVA, bypass, decode seed RVA,
#  expected instruction texts at the preferred base, containing function [start, end) for the raw scan).
SITES = (
    ('ui_scale_width_fl', 0x0b23db, 5, '0fbf400450', 0, 0, True, 0x0b23c6,
     ('movsx eax, word ptr [eax + 4]', 'push eax'), (0x0afbf0, 0x0b297c)),
    ('ui_scale_height_fl', 0x0b2400, 5, '0fbf400650', 0, 0, True, 0x0b23c6,
     ('movsx eax, word ptr [eax + 6]', 'push eax'), (0x0afbf0, 0x0b297c)),
    ('ui_scale_delta_direct_fl', 0x077fda, 5, '6800000000', 1, 0x18ba3c, False, 0x077fa9, ('push 0x58ba3c',),
     (0x077ba0, 0x078688)),
    ('ui_scale_delta_mapped_fl', 0x0d22f8, 5, '6800000000', 1, 0x18ba3c, False, 0x0d22d6, ('push 0x58ba3c',),
     (0x0cfd00, 0x0d2478)),
    ('ui_scale_cursor_x_fl', 0x07bc50, 5, 'a300000000', 1, 0x1aa180, False, 0x07bc3a,
     ('mov dword ptr [0x5aa180], eax',), (0x07b560, 0x07be4c)),
    ('ui_scale_cursor_y_fl', 0x07bc58, 5, 'a300000000', 1, 0x1aa188, False, 0x07bc3a,
     ('mov dword ptr [0x5aa188], eax',), (0x07b560, 0x07be4c)),
    ('ui_scale_overlay_icon_fl', 0x0320c3, 12, 'ff760b8d88bc030000ff7606', 0, 0, True, 0x0320aa,
     ('push dword ptr [esi + 0xb]', 'lea ecx, [eax + 0x3bc]', 'push dword ptr [esi + 6]'), (0x030540, 0x032488)),
    ('ui_scale_cursor_aim_fl', 0x0310c0, 6, '8b56068b7e0b', 0, 0, True, 0x031093,
     ('mov edx, dword ptr [esi + 6]', 'mov edi, dword ptr [esi + 0xb]'), (0x030540, 0x032488)),
    ('ui_scale_map_label_fl', 0x020158, 10, 'f20f2cc150f20f2cc450', 0, 0, True, 0x020138,
     ('cvttsd2si eax, xmm1', 'push eax', 'cvttsd2si eax, xmm4', 'push eax'), (0x01fa30, 0x0201c7)),
    ('ui_scale_map_pick_fl', 0x031d01, 7, 'ff760bff760650', 0, 0, True, 0x031cd8,
     ('push dword ptr [esi + 0xb]', 'push dword ptr [esi + 6]', 'push eax'), (0x030540, 0x032488)),
    ('ui_scale_projection_fl', 0x0de91b, 5, 'b801000000', 0, 0, False, 0x0de885, ('mov eax, 1',), (0x0de570, 0x0dec68)),
)
# Register facts: (RVA, Capstone text at the preferred base, what it establishes).
ANCHORS = (
    # A: the projection routine 0x0de570 and its 2D branch
    (0x0de577, 'cmp dword ptr [ecx + 0x3c], 0', 'ECX = the object at entry'),
    (0x0de57c, 'mov ebx, dword ptr [ebp + 8]', 'EBX = the camera (first stack argument)'),
    (0x0de581, 'mov edi, edx', 'EDI = the instance (EDX at entry)'),
    (0x0de583, 'mov eax, dword ptr [ebx + 0x1c]', 'camera+0x1c = its attached scene'),
    (0x0de5af, 'test dword ptr [edi + 0x130], 0x200', 'instance+0x130 bit 0x200 selects the 2D branch'),
    (0x0de5b9, 'je 0x4de96d', 'the perspective branch leaves at 0x0de96d, past the site'),
    (0x0de5cc, 'movss xmm5, dword ptr [0x591aa0]', 'XMM5 = the anchor unit (1.0f, checked below)'),
    (0x0de765, 'mov esi, dword ptr [0x5aae4c]', 'ESI = P, the 2D projection matrix'),
    (0x0de76b, 'movss xmm2, dword ptr [0x5919e0]', 'XMM2 = the 0.25 pixel bias'),
    (0x0de7fd, 'movss dword ptr [esi], xmm1', 'P[0] = 2/Wvp'),
    (0x0de83b, 'addss xmm1, xmm1', 'P[5] = 2/Hvp (stored at +0x14 next)'),
    (0x0de880, 'movss dword ptr [esi + 0x30], xmm1', 'P[12] before the anchor'),
    (0x0de885, 'mov eax, dword ptr [edi + 0x130]', 'the flag word'),
    (0x0de88b, 'test eax, 0x1000', 'left anchor tested first'),
    (0x0de892, 'subss xmm1, xmm5', 'left: P[12] - 1'),
    (0x0de898, 'test eax, 0x2000', 'right anchor second'),
    (0x0de89f, 'addss xmm1, xmm5', 'right: P[12] + 1'),
    (0x0de8ec, 'movss dword ptr [esi + 0x34], xmm1', 'P[13] before the anchor'),
    (0x0de8f7, 'test eax, 0x4000', 'top anchor tested first'),
    (0x0de8fe, 'addss xmm1, xmm5', 'top: P[13] + 1'),
    (0x0de904, 'test eax, 0x8000', 'bottom anchor second'),
    (0x0de90b, 'subss xmm1, xmm5', 'bottom: P[13] - 1'),
    (0x0de914, 'mov dword ptr [esi + 0x38], 0x3f800000', 'P[14] = 1 before the site'),
    (0x0de91b, 'mov eax, 1', 'A: the 2D branch exit (EAX dead, the tail loads 1)'),
    (0x0de966, 'pop edi', 'the epilogue'),
    (0x0de96c, 'ret', 'plain ret: the callers clean the stack'),
    (0x096fb9, 'call 0x4de570', 'caller 1'), (0x096fc4, 'add esp, 8', 'caller 1 cleans and writes EFLAGS'),
    (0x0976a0, 'call 0x4de570', 'caller 2'), (0x0976a8, 'add esp, 8', 'caller 2 cleans and writes EFLAGS'),
    # B, C: the B3D dispatcher
    (0x0afc23, 'mov esi, dword ptr [ebx + 0x10]', 'B3D dispatcher: ESI = the opcode'),
    (0x0afc35, 'cmp esi, 0xa9', '0xaa cases'),
    (0x0afc41, 'jmp dword ptr [esi*4 + 0x4b297c]', 'jump table base+0x0b297c'),
    (0x082897, 'mov dword ptr [edx + eax*8], 0x4afbf0', 'registration: the dispatcher'),
    (0x0828ce, 'mov dword ptr [edx + eax*8 + 0x40], 0x5a5e78', 'registration: its names table'),
    (0x0b23c6, 'mov eax, dword ptr [0x5a82f8]', 'case 0x73: the display slot'),
    (0x0b23d3, 'mov eax, dword ptr [eax]', 'EAX = the display record'),
    (0x0b23d5, 'mov ecx, dword ptr [0x5aa174]', 'ECX = the VM, loaded before the site'),
    (0x0b23e0, 'push edi', 'EDI = the return record'),
    (0x0b23e1, 'call 0x4c1940', 'the return helper'),
    (0x0b23fa, 'mov ecx, dword ptr [0x5aa174]', 'case 0x74: ECX = the VM'),
    (0x0b2406, 'call 0x4c1940', 'the return helper'),
    (0x0c194c, 'mov ebx, ecx', 'the helper takes ECX = VM'),
    # D1, D2: the InputMouseDelta callback sends
    (0x077fa9, 'cmp word ptr [edx + 0x414], 0', 'EDX = the input context, +0x414 dx'),
    (0x077fc6, 'movsx eax, word ptr [edx + 0x416]', 'dy pushed first (signed dword)'),
    (0x077fce, 'movsx eax, word ptr [edx + 0x414]', 'dx pushed second'),
    (0x077fd6, 'push 2', '[esp+4] at the site'), (0x077fd8, 'push 1', '[esp] at the site'),
    (0x077fed, 'call 0x4bb5b0', 'the direct callback wrapper'),
    (0x077ff2, 'add esp, 0x20', 'eight caller-cleaned words'),
    (0x0d22e8, 'push dword ptr [0x61c884]', 'the mapped y sum pushed first'),
    (0x0d22ee, 'push dword ptr [0x61c880]', 'the mapped x sum'),
    (0x0d2306, 'call 0x4bb500', 'the callback wrapper'),
    (0x0d230f, 'mov dword ptr [0x61c880], 0', 'the native sums cleared after the send'),
    # E: X2_UpdateCursorSteering
    (0x07b580, 'add eax, -3', 'X2 dispatcher: opcode - 3'),
    (0x07b598, 'movzx eax, byte ptr [eax + 0x47bedc]', 'the index byte table'),
    (0x07b59f, 'jmp dword ptr [eax*4 + 0x47be4c]', 'the jump table'),
    (0x0778d8, 'push 0x5a5d60', 'registration: the X2 names table'),
    (0x07bc3a, 'mov ecx, dword ptr [ebp + 0x18]', 'ECX = the argument records'),
    (0x07bc4d, 'mov eax, dword ptr [ecx + 0xb]', 'x'), (0x07bc55, 'mov eax, dword ptr [ecx + 0x10]', 'y'),
    (0x07bc5d, 'jmp 0x47b71e', 'the common return'),
    (0x0ce5b0, 'push dword ptr [0x5aa188]', 'the unprojection reads the cursor cells (real pixels)'),
    (0x0ce5b6, 'push dword ptr [0x5aa180]', 'the unprojection reads the cursor cells (real pixels)'),
    # INS dispatcher, G, H, J
    (0x030564, 'mov eax, dword ptr [ebx + 0x10]', 'INS dispatcher: EAX = the opcode'),
    (0x030579, 'cmp eax, 0x70', '0x71 cases'),
    (0x030582, 'jmp dword ptr [eax*4 + 0x432488]', 'the jump table'),
    (0x01df2f, 'mov dword ptr [ecx + eax*8], 0x430540', 'registration: the INS dispatcher'),
    (0x01df66, 'mov dword ptr [ecx + eax*8 + 0x40], 0x5a3d98', 'registration: its names table'),
    (0x0320aa, 'mov esi, dword ptr [ebx + 0x18]', 'G: ESI = the argument records'),
    (0x0320b6, 'call 0x41e420', 'G: EAX = the cockpit (registry resolver)'),
    (0x0320cf, 'call 0x42bae0', 'G: the overlay icon hit test'),
    (0x02bae0, 'sub esp, 0x14', 'the hit test writes EFLAGS first'),
    (0x02bafe, 'mov eax, dword ptr [0x5a82f8]', 'the hit test reads the real display size'),
    (0x0310af, 'mov ecx, dword ptr [ebx + 0x14]', 'H: ECX = the argument count'),
    (0x0310bb, 'cmp ecx, 3', 'H: short argument lists'),
    (0x0310be, 'jl 0x4310d9', 'H: the -1/-1 path jumps past the span'),
    (0x0310c6, 'cmp ecx, 4', 'H: EFLAGS regenerated at the continuation'),
    (0x0310de, 'push edi', 'H: y'), (0x0310df, 'push edx', 'H: x'),
    (0x0310e0, 'call 0x426d70', 'H: the native cursor aim'),
    (0x031cd8, 'mov esi, dword ptr [ebx + 0x18]', 'J: ESI = the argument records'),
    (0x031cde, 'call 0x4a49f0', 'J: EAX = the scene'),
    (0x031d08, 'call 0x41f6b0', 'J: the map pick'),
    (0x01f6b3, 'and esp, 0xfffffff8', 'the pick writes EFLAGS first'),
    (0x01f6bc, 'cmp dword ptr [0x5a82e0], 0', 'the pick tests the map state itself'),
    (0x01f6f8, 'movsx ecx, word ptr [esi + 4]', 'the pick reads the real display width'),
    # I: the galaxy-map worker's label callback
    (0x02016a, 'push 0x585150', 'WriteGalaxyMapSectorName (string checked below)'),
    (0x020179, 'call 0x4bb500', 'the callback wrapper'),
    (0x02017e, 'add esp, 0x28', 'ten caller-cleaned words'),
    # The excluded camera: registry, cockpit, attach
    (0x0ce57e, 'mov ecx, dword ptr [0x5a82e4]', 'the cockpit registry slot'),
    (0x0ce58c, 'mov eax, dword ptr [ecx + 0x10]', 'registry+0x10 = the active handle'),
    (0x0ce594, 'call 0x41e420', 'the resolver'),
    (0x01e420, 'mov eax, dword ptr [ecx]', 'resolver: registry+0 = the map'),
    (0x01e428, 'mov eax, dword ptr [eax + 4]', 'map+4 = the bucket count'),
    (0x01e430, 'and eax, ecx', 'bucket = handle & (count - 1)'),
    (0x01e440, 'cmp dword ptr [eax + 4], ecx', 'row+4 = the handle'),
    (0x01e445, 'mov eax, dword ptr [eax]', 'row+0 = next'),
    (0x01e450, 'mov eax, dword ptr [eax + 8]', 'row+8 = the cockpit'),
    (0x021b6c, 'mov dword ptr [esi + 4], edi', 'cockpit+4 = its HUD scene'),
    (0x021b96, 'mov dword ptr [esi + 8], eax', 'cockpit+8 = its HUD camera'),
    (0x021bbb, 'push dword ptr [esi + 4]', 'attach the camera to the HUD scene'),
    (0x021bc1, 'call 0x4a5860', 'the attach'),
    (0x0a5897, 'mov dword ptr [esi + 0x1c], ecx', 'attach: camera+0x1c = the scene'),
)
# Liveness: (name, RVA where the walk starts, registers/flags that must be written before any read on the path).
LIVENESS = (
    ('width', 0x0b23db, ('eflags',)), ('height', 0x0b2400, ('eflags',)),
    ('width_eax_after_push', 0x0b23e0, ('eax',)), ('height_eax_after_push', 0x0b2405, ('eax',)),
    ('delta_direct', 0x077fda, ('eax', 'edx', 'eflags')), ('delta_mapped', 0x0d22f8, ('eax', 'edx', 'eflags')),
    ('cursor_x', 0x07bc50, ('eflags',)), ('cursor_x_after_store', 0x07bc55, ('eax',)),
    ('cursor_y', 0x07bc58, ('eflags',)), ('cursor_y_after_store', 0x07bc5d, ('eax',)),
    ('overlay_icon', 0x0320c3, ('ecx', 'eflags')), ('cursor_aim', 0x0310c0, ('eflags',)),
    ('map_label', 0x020158, ('eax', 'edx', 'eflags')), ('map_pick', 0x031d01, ('eflags',)),
    ('projection', 0x0de91b, ('eax',)),
)
NAMES = ((0x1a5e78, 0x73, 'B3D_ScreenGetWidth'), (0x1a5e78, 0x74, 'B3D_ScreenGetHeight'),
         (0x1a3d98, 0x28, 'INS_CockpitGetCursorAim'), (0x1a3d98, 0x3e, 'INS_GalaxyMapFindSector'),
         (0x1a3d98, 0x64, 'INS_CockpitGetObjectByTargetOverlayIconPos'), (0x1a5d60, 0x1e, 'X2_UpdateCursorSteering'))
STRINGS = ((0x18ba3c, 'InputMouseDelta'), (0x185150, 'WriteGalaxyMapSectorName'))
FLOATS = ((0x191aa0, 1.0), (0x1919e0, 0.25))
FULL = {'al': 'eax', 'ah': 'eax', 'ax': 'eax', 'cl': 'ecx', 'ch': 'ecx', 'cx': 'ecx', 'dl': 'edx', 'dh': 'edx', 'dx': 'edx',
        'bl': 'ebx', 'bh': 'ebx', 'bx': 'ebx', 'si': 'esi', 'di': 'edi', 'bp': 'ebp', 'sp': 'esp'}


def parse_header(text):
    """The header's window arrays, window table and site table."""
    arrays = {m.group(1): bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', m.group(3)))
              for m in re.finditer(r'inline constexpr unsigned char (\w+)_window\[(\d+)\] = \{([^}]*)\};', text)}
    table = {}
    for m in re.finditer(r'\{"(\w+)", 0x([0-9a-f]+), (\d+), \w+_window, 0x([0-9a-f]+)u, (\d+), \{([^}]*)\}, \{([^}]*)\}\}',
                         text):
        offsets = [int(x, 16) for x in re.findall(r'0x([0-9a-f]+)', m.group(6))]
        targets = [int(x, 16) for x in re.findall(r'0x([0-9a-f]+)', m.group(7))]
        table[m.group(1)] = (int(m.group(2), 16), int(m.group(3)), int(m.group(4), 16), int(m.group(5)), offsets, targets)
    sites = {}
    for m in re.finditer(r'\{"(ui_scale_\w+_fl)",\s*0x([0-9a-f]+),\s*(\d+),\s*\{([^}]*)\},\s*(\d+),\s*(0x[0-9a-f]+|0),\s*'
                         r'(true|false),\s*w_(\w+)\}', text):
        sites[m.group(1)] = (int(m.group(2), 16), int(m.group(3)), bytes(int(x, 0) for x in re.findall(r'0x[0-9a-f]+|\d+', m.group(4))),
                             int(m.group(5)), int(m.group(6), 16), m.group(7) == 'true', m.group(8))
    return arrays, table, sites


def verify(data, source=False):
    report = {'result': 'FAIL', 'edition': 'fl', 'checks': {}}
    checks = report['checks']
    sha = hashlib.sha256(data).hexdigest()
    report['exe_info'] = {'sha256': sha, 'known': sha == FL_SHA256}

    def check(name, ok, **detail):
        checks[name] = {'ok': bool(ok), **detail}

    img = FlImage(data)
    base = img.image_base
    check('identity', img.timestamp == FL_TIMESTAMP and img.image_size == FL_IMAGE_SIZE and (img.dll_characteristics & 0x40),
          timestamp=hex(img.timestamp), image_size=hex(img.image_size), dynamic_base=bool(img.dll_characteristics & 0x40))
    decoded = 0
    # Windows: bytes with the fixups zeroed, FNV, the exact fixup census.
    window_bytes = {}
    for name, lo, hi, fixups in WINDOWS:
        raw = bytearray(img.rva(lo, hi - lo) or b'')
        census = {a: struct.unpack_from('<I', raw, a - lo)[0] - base
                  for a in range(lo, hi - 3) if a in img.fixups}
        ok = len(raw) == hi - lo and census == fixups
        for a in census:
            raw[a - lo:a - lo + 4] = b'\0\0\0\0'
        window_bytes[name] = bytes(raw)
        check(f'window_{name}', ok, rva=hex(lo), length=hi - lo, fnv=hex(fnv1a32(raw)),
              fixups={hex(a): hex(t) for a, t in census.items()})
    # Sites: bytes, boundaries, instruction text, fixups inside, qword, edges and raw encodings into the interior.
    edges = img.direct_edges()
    spans = [(s[1], s[1] + s[2]) for s in SITES]
    for name, rva, length, hexbytes, reloc_off, reloc_target, bypass, seed, texts, function in SITES:
        raw = bytearray(img.rva(rva, length) or b'')
        fix_inside = {a - rva: struct.unpack_from('<I', raw, a - rva)[0] - base for a in range(rva - 3, rva + length)
                      if a in img.fixups and a >= rva and a + 4 <= rva + length}
        straddle = [a for a in range(rva - 3, rva + length) if a in img.fixups and (a < rva or a + 4 > rva + length)]
        for off in fix_inside:
            raw[off:off + 4] = b'\0\0\0\0'
        insns = img.insns(seed, rva + length)
        boundaries = [i.address - base for i in insns] if insns else []
        decoded += len(insns or [])
        inside = [i for i in (insns or []) if rva <= i.address - base < rva + length]
        text = tuple(f'{i.mnemonic} {i.op_str}'.strip() for i in inside)
        into = [(hex(s + base), m) for s, m, t in edges if rva < t < rva + length]
        code = img.rva(function[0], function[1] - function[0]) or b''
        # Interiors of the function's linearly decoded instructions: a raw encoding there is an operand byte (an alias).
        interior = set()
        for i in img.md.disasm(code, base + function[0]):
            interior.update(range(i.address - base + 1, i.address - base + i.size))
        raw_hits, aliases = [], 0
        for k in range(len(code)):
            b = code[k]
            at = function[0] + k
            if b in (0xe8, 0xe9) and k + 5 <= len(code):
                t = at + 5 + struct.unpack_from('<i', code, k + 1)[0]
            elif b == 0x0f and k + 6 <= len(code) and 0x80 <= code[k + 1] <= 0x8f:
                t = at + 6 + struct.unpack_from('<i', code, k + 2)[0]
            elif (0x70 <= b <= 0x7f or b == 0xeb) and k + 2 <= len(code):
                t = at + 2 + struct.unpack_from('<b', code, k + 1)[0]
            else:
                continue
            if rva < t < rva + length and not (rva <= at < rva + length):
                if at in interior:
                    aliases += 1
                else:
                    raw_hits.append(hex(at + base))
        ok = (bytes(raw) == bytes.fromhex(hexbytes) and fix_inside == ({reloc_off: reloc_target} if reloc_off else {}) and
              not straddle and rva in boundaries and insns is not None and text == texts and not into and not raw_hits and
              (rva % 8) + 5 <= 8)
        check(f'site_{name}', ok, rva=hex(rva), length=length, bypass=bypass, text=list(text), qword_offset=rva % 8,
              fixups={hex(k): hex(v) for k, v in fix_inside.items()}, edges_into=into, raw_encodings_into=raw_hits,
              operand_aliases=aliases, function=[hex(function[0]), hex(function[1])])
    report['direct_edges'] = len(edges)
    # No relocated image dword (jump tables, pointer tables, data) points into a span interior.
    dword_hits = []
    for a in sorted(img.fixups):
        v = img.rva(a, 4)
        if not v:
            continue
        t = struct.unpack('<I', v)[0] - base
        for lo, hi in spans:
            if lo < t < hi:
                dword_hits.append((hex(a), hex(t)))
    check('image_dwords', not dword_hits, scanned=len(img.fixups), hits=dword_hits)
    # Dispatch: names behind the table indexes, the strings, the float constants.
    for table, index, want in NAMES:
        ptr = struct.unpack('<I', img.rva(table + 4 * index, 4))[0]
        s = img.read(ptr, 64) or b''
        got = s.split(b'\0')[0].decode('ascii', 'replace')
        check(f'name_{want}', got == want, table=hex(table), index=hex(index), got=got)
    for rva, want in STRINGS:
        got = (img.rva(rva, 64) or b'').split(b'\0')[0].decode('ascii', 'replace')
        check(f'string_{want}', got == want, rva=hex(rva), got=got)
    for rva, want in FLOATS:
        got = struct.unpack('<f', img.rva(rva, 4))[0]
        check(f'float_{rva:06x}', got == want, got=got)
    callers = sorted(hex(s) for s, m, t in edges if t == 0x0de570 and m == 'call')
    check('projection_callers', callers == ['0x96fb9', '0x976a0'], callers=callers)
    # The projection suffix: only immediate stores to P (not P[0/5/12/13]), then the epilogue; no flag or XMM reader.
    suffix = img.insns(0x0de920, 0x0de96d) or []
    decoded += len(suffix)
    bad = []
    for i in suffix:
        t = f'{i.mnemonic} {i.op_str}'.strip()
        m = re.fullmatch(r'mov dword ptr \[esi \+ (0x[0-9a-f]+|\d+)\], (0x[0-9a-f]+|\d+)', t)
        if m:
            if int(m.group(1), 0) in (0, 0x14, 0x30, 0x34):
                bad.append(t)
        elif t not in ('pop edi', 'pop esi', 'pop ebx', 'mov esp, ebp', 'pop ebp', 'ret'):
            bad.append(t)
    check('projection_suffix', suffix and not bad, instructions=len(suffix), unexpected=bad)
    # EBX (camera), EDI (instance) and ESI (P) at the site: their only writers between the camera load and the site are
    # the entry copies, the early-return pops (before 0x0de595, a separate ret), the 16.16 scale into ESI before the 2D
    # test and the P load at 0x0de765; no jump before the P load lands between it and the site (so every path to the site
    # passes 0x0de765).
    body = img.insns(0x0de57c, 0x0de91b) or []
    decoded += len(body)
    writers = []
    for i in body:
        _, w = i.regs_access()
        if {FULL.get(i.reg_name(r), i.reg_name(r)) for r in w} & {'ebx', 'edi', 'esi'}:
            writers.append(i.address - base)
    branch_back = [i.address - base for i in body if i.mnemonic.startswith('j') and i.op_str.startswith('0x') and
                   i.address - base < 0x0de765 and 0x0de765 < int(i.op_str, 16) - base <= 0x0de91b]
    check('projection_register_writers', body and writers == [0x0de57c, 0x0de581, 0x0de58e, 0x0de58f, 0x0de590, 0x0de59d,
                                                               0x0de59f, 0x0de5a8, 0x0de765] and not branch_back,
          writers=[hex(w) for w in writers], branches_past_p_load=[hex(b) for b in branch_back])
    # Anchors.
    for rva, want, meaning in ANCHORS:
        got, size = img.text_at(rva)
        decoded += 1
        check(f'anchor_{rva:06x}', got == want, text=got, expected=want, meaning=meaning)
    # Liveness walks.
    import capstone

    def walk(start, regs, limit=60):
        pending = set(regs)
        at = start
        path = []
        for _ in range(limit):
            code = img.rva(at, 16)
            i = next(img.md.disasm(code, base + at), None)
            if i is None:
                return False, path + ['undecodable']
            path.append(f'{at:06x} {i.mnemonic} {i.op_str}')
            read, written = i.regs_access()
            read = {FULL.get(i.reg_name(r), i.reg_name(r)) for r in read}
            written = {FULL.get(i.reg_name(r), i.reg_name(r)) for r in written}
            if pending & read:
                return False, path + [f'read {sorted(pending & read)}']
            pending -= written
            if not pending:
                return True, path
            if i.mnemonic in ('call', 'jmp') and i.op_str.startswith('0x'):
                at = int(i.op_str, 16) - base
            elif i.mnemonic.startswith('j') or i.mnemonic.startswith('ret') or i.mnemonic.startswith('loop'):
                return False, path + ['unresolved branch']
            else:
                at += i.size
        return False, path + ['limit']

    for name, start, regs in LIVENESS:
        ok, path = walk(start, regs)
        decoded += len(path)
        check(f'dead_{name}', ok, registers=list(regs), path=path[-4:])
    report['instructions_decoded'] = decoded
    # --source: the header against the executable.
    if source:
        arrays, table, sites = parse_header(HEADER.read_text())
        problems = []
        if set(table) != {w[0] for w in WINDOWS}:
            problems.append('window set')
        for name, lo, hi, fixups in WINDOWS:
            row = table.get(name)
            if not row or row[0] != lo or row[1] != hi - lo or arrays.get(name) != window_bytes[name] or \
                    row[2] != fnv1a32(window_bytes[name]) or row[3] != len(fixups) or \
                    dict(zip([lo + o for o in row[4][:row[3]]], row[5][:row[3]])) != fixups:
                problems.append(name)
        for name, rva, length, hexbytes, reloc_off, reloc_target, bypass, seed, texts, function in SITES:
            row = sites.get(name)
            if not row or row[0] != rva or row[1] != length or row[2][:length] != bytes.fromhex(hexbytes) or \
                    row[3] != reloc_off or row[4] != reloc_target or row[5] != bypass:
                problems.append(name)
        if len(sites) != len(SITES) or list(sites) != [s[0] for s in SITES]:
            problems.append('site order')
        check('source', not problems, problems=problems)
    report['check_count'] = len(checks)
    report['result'] = 'PASS' if all(c['ok'] for c in checks.values()) else 'FAIL'
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, default=FL_DEFAULT_EXE)
    parser.add_argument('--source', action='store_true')
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    if not args.exe.is_file():
        print(f'executable not found: {args.exe}', file=sys.stderr)
        return 2
    report = verify(args.exe.read_bytes(), args.source)
    text = json.dumps(report, indent=1)
    if args.out:
        args.out.write_text(text + '\n')
    if args.json:
        print(text)
    else:
        failed = [k for k, v in report['checks'].items() if not v['ok']]
        print(f"{report['result']} checks={report['check_count']} failed={len(failed)} "
              f"instructions_decoded={report['instructions_decoded']} direct_edges={report['direct_edges']} "
              f"sha256_known={report['exe_info']['known']}")
        for k in failed:
            print('FAIL', k, json.dumps(report['checks'][k])[:400])
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
