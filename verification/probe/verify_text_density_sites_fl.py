#!/usr/bin/env python3
"""Read-only check of the FL counterparts of every engine site and data item the AP text_density module uses.

The FL edition of verify_text_density_sites.py (docs/reverse-engineering/fl/text-density.md, "Verified 2026-11-03";
AP constants src/proxy/text_density_sites.h). Nothing is implemented for FL yet: this pins the table an implementer
ports from. Against X3FL.exe (preferred base 0x400000, every address an RVA) it checks:

* the claim spans (font entry, the Materials call and the call after it, the two block blits, the image block, the
  two diagnostic entries) and the in-place edits (the style displacement byte, the four native cockpit literals):
  exact bytes, Capstone whole-instruction lengths, no relative control flow inside a detour span, no HIGHLOW fixup in
  a span, no direct edge and no relocated pointer into a span interior, qword geometry of a five-byte jump;
* the register/frame/layout anchors each adapter relies on (Capstone text at each RVA);
* the data items (config slot and +0x774, Materials row count / rows / texture table / named and fixed counts, the
  row and entry layout, the font cache, the "%s%d" and F\\%s strings and the descriptor slot, the copy class table and
  its relocated slot table, the KC dispatcher's case-111 entry, the Direct3DCreate9 import slot);
* exact direct-caller sets, the config +0x774 writer census, EBX liveness in the font open's style loop, EAX/flags
  deadness at the image and blit entries, and the static order of the init routine 0x76bc0 (config constructor,
  Direct3DCreate9, CreateDevice/display, Materials, first native font open);
* identity windows (FNV-1a with relocated dwords zeroed, each fixup's target RVA) for the module's pre-claim compare.

usage: verify_text_density_sites_fl.py [--exe PATH] [--json] [--emit-windows]   (exit 0 = PASS)
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
# (kind, name, rva, bytes, instruction lengths, AP counterpart VA). kind: 'detour' = five-byte jmp over whole
# instructions; 'call' = rel32 call redirect; 'edit' = in-place operand edit (never an entry point).
SITES = (
    ('detour', 'font_entry', 0x0a8990, '5153568b742410', (1, 1, 1, 4), 0x0048cdc0),
    ('call', 'materials_call', 0x0a69f6, 'e8c5ac0600', (5,), 0x0048af71),
    ('call', 'materials_next_call', 0x0a69fb, 'e8c0bb0600', (5,), 0x0048af76),
    ('detour', 'blt_block', 0x0a7b30, '83ec34535556', (3, 1, 1, 1), 0x0048c090),
    ('detour', 'blt_alpha', 0x0a7f80, '83ec34535556', (3, 1, 1, 1), 0x0048c460),
    ('detour', 'blt_image', 0x0b22f4, '0fbf4e240fbfc2', (4, 3), 0x004960c0),
    ('detour', 'text_line', 0x0a6d90, '83ec14535556', (3, 1, 1, 1), 0x0048b2d0),
    ('detour', 'rect_fill', 0x0a6b60, '83ec2c535556', (3, 1, 1, 1), 0x0048b0b0),
    ('edit', 'style_factor_lea', 0x1151b6, '8d5301', (3,), 0x004f813a),
    ('edit', 'cockpit_name_size', 0x01dfd9, '6a0d', (2,), None),
    ('edit', 'cockpit_yoff', 0x01e000, '6a01', (2,), None),
    ('edit', 'cockpit_cell_width', 0x01e004, 'ba10000000', (5,), None),
    ('edit', 'cockpit_size', 0x01e00b, '6a0d', (2,), None),
)
CALL_TARGETS = {'materials_call': 0x1116c0, 'materials_next_call': 0x1125c0}
# Capstone text at RVA: (rva, text, what).
ANCHORS = (
    # config object: constructor, slot, density field
    (0x076c96, 'call 0x509e00', 'init routine calls the config constructor'),
    (0x076c9e, 'mov dword ptr [0x5a83b0], eax', 'init routine stores the config slot'),
    (0x109e3c, 'mov ecx, 0x7a0', 'config object is 0x7a0 bytes'),
    (0x109e4d, 'mov dword ptr [0x5a83b0], edi', 'constructor publishes the slot before filling it'),
    (0x109e95, 'mov dword ptr [edi + 0xc8], 0x5a5ca0', 'config +0xc8 = path descriptor 0x1a5ca0'),
    (0x109ed5, 'mov dword ptr [edi + 0x774], 1', 'density default 1 (AP 0x004ecae3, +0x784)'),
    (0x10a34b, 'push 0x590f0c', '"-fontscale" compare'),
    (0x10a363, 'call 0x52eafd', 'atol of the -fontscale argument'),
    (0x10a368, 'mov ecx, dword ptr [0x5a83b0]', 'config slot reload'),
    (0x10a371, 'mov dword ptr [ecx + 0x774], eax', 'density = atol (AP 0x004ecff8)'),
    # init order (0x76bc0 = AP 0x00402780)
    (0x076b68, 'call 0x476bc0', 'the init routine is called first by its parent'),
    (0x0772bc, 'call 0x4f13b0', 'Direct3DCreate9 helper'),
    (0x0f13cd, 'call dword ptr [0x550320]', 'Direct3DCreate9 through the IAT'),
    (0x07765a, 'call 0x516890', 'display creation (CreateDevice inside)'),
    (0x07765f, 'add esp, 8', 'display return (inherited initial-commit seam)'),
    (0x077662, 'mov dword ptr [0x5a82f8], eax', 'display container slot'),
    (0x1168c5, 'call 0x4f3a30', 'display -> device setup'),
    (0x0f3e0a, 'call 0x4f1e10', 'device setup -> CreateDevice caller'),
    (0x0f2489, 'call dword ptr [ecx + 0x40]', 'IDirect3D9::CreateDevice (vtable slot 16)'),
    (0x0777db, 'call 0x4a69f0', 'Materials wrapper (AP 0x00403497)'),
    (0x077c52, 'call 0x479430', 'game start: first native font opens below it'),
    (0x077e21, 'call 0x41df90', 'cockpit constructor (inline Tahoma open)'),
    # Materials wrapper and table
    (0x0a69f0, 'push ebp', 'Materials wrapper frame'),
    (0x0a69f3, 'and esp, 0xfffffff8', 'Materials wrapper aligns ESP to 8'),
    (0x0a6a31, 'mov eax, 1', 'Materials wrapper returns 1 on success'),
    (0x1116e0, 'push 0x59140c', 'loader opens "Materials"'),
    (0x111754, 'mov word ptr [0x61b410], ax', 'row count (i16)'),
    (0x111773, 'shl ecx, 4', 'rows x 15 ...'),
    (0x111778, 'shl ecx, 2', '... x 4 = stride 0x3c'),
    (0x111780, 'mov dword ptr [0x61b418], eax', 'rows pointer'),
    (0x1118fb, 'mov word ptr [edi + ebx + 0xc], ax', 'row +0xc texture id'),
    (0x111942, 'mov dword ptr [edi + ebx + 0x10], eax', 'row +0x10 MPF flags'),
    (0x11198f, 'mov word ptr [edi + ebx + 0x14], ax', 'row +0x14 width'),
    (0x1119dd, 'mov word ptr [edi + ebx + 0x16], ax', 'row +0x16 height'),
    (0x111c36, 'mov edx, dword ptr [edi + edx + 0x10]', 'row flags handed to the entry-flag copy'),
    (0x111c3a, 'call 0x512280', 'entry-flag copy'),
    (0x111cca, 'mov word ptr [0x5fb404], cx', 'named count = 0 (AP 0x006069b4)'),
    (0x111cd1, 'mov word ptr [0x61b40c], ax', 'fixed count = rows (AP 0x006069b0)'),
    (0x111ce1, 'add ecx, 0x3e8', 'texture table capacity rows + 1000'),
    (0x111cf5, 'mov dword ptr [0x5cb3f4], ecx', 'texture table pointer'),
    (0x1121ae, 'mov word ptr [0x5fb404], ax', 'named count ++ per registration'),
    (0x1122af, 'add esi, dword ptr [0x5cb3f4]', 'entry = table + id*0x10'),
    (0x1122b5, 'mov edx, dword ptr [esi + 4]', 'entry +4 flags'),
    # lookup 0x1123a0 (AP 0x004f5110)
    (0x1123a0, 'mov dx, cx', 'lookup: CX = id'),
    (0x1123a9, 'cmp dx, word ptr [0x61b410]', 'lookup: row count'),
    (0x1123c1, 'cmp word ptr [eax + ecx*4 + 0xc], 0', 'lookup: row +0xc'),
    (0x1123c9, 'movsx ecx, word ptr [0x5fb404]', 'lookup: named count'),
    (0x1123d0, 'movsx eax, word ptr [0x61b40c]', 'lookup: fixed count'),
    (0x1123f2, 'call 0x5113b0', 'lookup: lazy creator'),
    (0x112400, 'mov eax, dword ptr [eax + esi*8 + 8]', 'lookup: entry +8 object'),
    (0x112405, 'ret', 'lookup: plain RET'),
    # font entry 0xa8990 (AP 0x0048cdc0)
    (0x0a8999, 'push ecx', 'font entry: ECX = name (%s)'),
    (0x0a8998, 'push esi', 'font entry: [esp+4] = size (%d)'),
    (0x0a899a, 'push 0x58cc38', 'font entry: "%s%d"'),
    (0x0a89a1, 'mov edi, edx', 'font entry: EDX = cell width'),
    (0x0a89a3, 'call 0x50b890', 'font entry: formatter (allocates the name)'),
    (0x0a89a8, 'push dword ptr [esp + 0x2c]', 'font entry: [esp+0xc] = y offset'),
    (0x0a89b0, 'push dword ptr [esp + 0x2c]', 'font entry: [esp+8] = flags'),
    (0x0a89b7, 'call 0x515190', 'font entry: font open'),
    (0x020dc4, 'push 0xd', 'native caller: size 13'),
    (0x020dc6, 'mov edx, 0x10', 'native caller: cell width 16'),
    (0x020dcb, 'mov ecx, 0x584fdc', 'native caller: "Tahoma"'),
    (0x0b238f, 'mov edx, dword ptr [esi + 6]', 'KC B3D_OpenFont: cell width from the record'),
    # native cockpit inline opener 0x1df90 (bypasses the font entry)
    (0x01dfdb, 'push 0x584fdc', 'cockpit: "Tahoma"'),
    (0x01dfe0, 'push 0x58cc38', 'cockpit: "%s%d"'),
    (0x01dffb, 'call 0x50b890', 'cockpit: formatter'),
    (0x01e009, 'push 0x21', 'cockpit: flags 0x21'),
    (0x01e00d, 'mov ecx, ebx', 'cockpit: ECX = formatted name'),
    (0x01e00f, 'call 0x515190', 'cockpit: font open'),
    # font open 0x115190 (AP 0x004f8120): style factor and cache
    (0x115190, 'mov eax, dword ptr [0x5a83b0]', 'font open: config'),
    (0x115197, 'xor ebx, ebx', 'font open: EBX = 0'),
    (0x11519b, 'cmp dword ptr [eax + 0x774], 1', 'font open: density > 1 ?'),
    (0x1151a4, 'setg bl', 'font open: EBX = (density > 1)'),
    (0x1151b6, 'lea edx, [ebx + 1]', 'font open: factor = EBX + 1 (the edited disp8 at 0x1151b8)'),
    (0x1151cd, 'cmp ecx, 1', 'font open: style type 1 keeps factor 1'),
    (0x1151d2, 'cmp ecx, 4', 'font open: style type 4 keeps factor 1'),
    (0x1151d7, 'mov edx, 1', 'font open: factor 1 for types 1/4 (as AP 0x004f8167)'),
    (0x115434, 'add esi, 0x44', 'style record stride 0x44'),
    (0x115437, 'cmp esi, 0x2ec', '11 style records'),
    (0x115445, 'mov ecx, dword ptr [esi*4 + 0x5a8c98]', 'font cache slots 0x1a8c98'),
    (0x1154bc, 'call 0x514a40', 'font open -> loader on a cache miss'),
    # font loader path
    (0x114a91, 'mov eax, dword ptr [0x5a83b0]', 'loader: config'),
    (0x114a97, 'mov eax, dword ptr [eax + 0xc8]', 'loader: path descriptor'),
    (0x114a9d, 'push dword ptr [eax + 0x3c]', 'loader: descriptor +0x3c = "F\\%s"'),
    (0x114aa7, 'call 0x419df0', 'loader: format the path'),
    (0x114ad4, 'push 0x5918c0', 'loader: "tga bmp"'),
    (0x1053e6, 'jne 0x505ce0', 'resolver: loose success skips the catalogues (inherited semantics)'),
    # blits (AP 0x0048c090 / 0x0048c460)
    (0x0a7b36, 'mov esi, edx', 'block: EDX = destination id'),
    (0x0a7b38, 'mov ebp, ecx', 'block: ECX = source id'),
    (0x0a7b45, 'test esi, esi', 'block: negative destination = composite'),
    (0x0a7d06, 'call 0x5123a0', 'block: destination lookup'),
    (0x0a7d24, 'mov ax, word ptr [0x61b410]', 'block: row count (AP 0x0048c265)'),
    (0x0a7d53, 'mov eax, dword ptr [0x61b418]', 'block: rows (AP 0x0048c28b)'),
    (0x0a7d5f, 'test dword ptr [eax + ecx*4 + 0x10], 0x10000', 'block: row +0x10 & MPF_FONTSCALE'),
    (0x0a7d69, 'mov eax, dword ptr [0x5a83b0]', 'block: config (AP 0x0048c29a)'),
    (0x0a7d6e, 'mov ecx, dword ptr [eax + 0x774]', 'block: density'),
    (0x0a7f3f, 'call 0x50bd20', 'block: colour copy (colour >= 0)'),
    (0x0a7f50, 'jmp 0x5123a0', 'block: tail lookup -> EAX'),
    (0x0a7f63, 'call 0x4f4590', 'block: plain copy (colour < 0)'),
    (0x0a7f74, 'jmp 0x5123a0', 'block: tail lookup -> EAX'),
    (0x0a7f86, 'mov esi, edx', 'alpha: EDX = destination id'),
    (0x0a7f88, 'mov ebp, ecx', 'alpha: ECX = source id'),
    (0x0a81be, 'mov ecx, dword ptr [eax + 0x774]', 'alpha: density'),
    (0x0a8386, 'call 0x50cde0', 'alpha: alpha copy'),
    (0x0a8397, 'jmp 0x5123a0', 'alpha: tail lookup -> EAX'),
    # diagnostics
    (0x0a6d97, 'mov edi, ecx', 'text line: ECX = destination'),
    (0x0a6d99, 'mov dword ptr [esp + 0x14], edx', 'text line: EDX = text'),
    (0x0a6dd7, 'mov eax, dword ptr [eax*4 + 0x5a8c98]', 'text line: [esp+4] = font slot'),
    (0x0a6b66, 'mov esi, ecx', 'rect fill: ECX = destination'),
    (0x0a6b68, 'mov ebx, edx', 'rect fill: EDX = x'),
    # line (graph stroke): endpoints x N into a flagged row
    (0x0a7795, 'mov ecx, dword ptr [eax + 0x774]', 'line: density'),
    (0x0a779d, 'imul eax, dword ptr [esp + 0x28]', 'line: endpoint x N'),
    # image (AP 0x00495fc1 .. 0x00496101)
    (0x0afc35, 'cmp esi, 0xa9', 'KC dispatcher: 0xaa cases (AP 0xa8)'),
    (0x0afc41, 'jmp dword ptr [esi*4 + 0x4b297c]', 'KC dispatcher jump table 0xb297c'),
    (0x0b21ed, 'mov esi, dword ptr [ebx + 0x18]', 'image case: ESI = record'),
    (0x0b21f8, 'mov ecx, dword ptr [0x5aa0a4]', 'image path slot'),
    (0x0b2217, 'cmp dword ptr [0x5aa070], 0', 'image object slot'),
    (0x0b2225, 'call 0x510c10', 'image replacement frees the old object'),
    (0x0b227a, 'call 0x510890', 'image load'),
    (0x0b228b, 'mov dword ptr [0x5aa070], eax', 'image object store'),
    (0x0b2295, 'mov dword ptr [0x5aa0a4], eax', 'image path store'),
    (0x0b22a3, 'mov dword ptr [ebp - 0x50], eax', 'frame -0x50 = image object'),
    (0x0b22af, 'mov dword ptr [ebp - 0x20], eax', 'frame -0x20 = destination object'),
    (0x0b22c7, 'mov edx, 1', 'factor 1'),
    (0x0b22cc, 'mov eax, dword ptr [esi + 1]', 'record +1 destination id'),
    (0x0b22d1, 'jge 0x4b22f4', 'inbound edge 1 to the image site'),
    (0x0b22df, 'test dword ptr [eax + ecx*4 + 0x10], 0x10000', 'destination row flagged?'),
    (0x0b22e7, 'je 0x4b22f4', 'inbound edge 2 to the image site'),
    (0x0b22ee, 'mov edx, dword ptr [eax + 0x774]', 'factor = density'),
    (0x0b22fb, 'imul ecx, eax', 'h x factor (writes EFLAGS)'),
    (0x0b2339, 'movsx ecx, word ptr [esi + 0xb]', 'record +0xb sx (six words, stride 5)'),
    (0x0b2348, 'push dword ptr [ebp - 0x20]', 'copy dst'),
    (0x0b234b, 'push dword ptr [ebp - 0x50]', 'copy src'),
    (0x0b234e, 'call 0x4f4590', 'plain copy'),
    (0x0b2353, 'mov cx, word ptr [esi + 1]', 'image return: CX = destination id'),
    (0x0b2357, 'add esp, 0x20', 'image return: pop the eight arguments'),
    (0x0b235a, 'call 0x5123a0', 'image: lookup defines EAX'),
    # helpers
    (0x110c94, 'mov word ptr [esp + 0xa], dx', 'alloc: DX = height'),
    (0x110c99, 'mov bx, cx', 'alloc: CX = width'),
    (0x110ca1, 'lea ecx, [edx + 0x64]', 'alloc: object 0x64 bytes'),
    (0x110cb7, 'mov edx, dword ptr [esp + 0x24]', 'alloc: [esp+8] = surface flags'),
    (0x110cc0, 'movzx eax, word ptr [esp + 0x20]', 'alloc: [esp+4] = bpp'),
    (0x110d21, 'mov dword ptr [edi + 0x10], ebp', 'alloc: +0x10 flags'),
    (0x110d24, 'mov word ptr [edi + 4], bx', 'alloc: +4 width'),
    (0x110d28, 'mov word ptr [edi + 6], ax', 'alloc: +6 height'),
    (0x110d38, 'mov word ptr [edi + 0xc], ax', 'alloc: +0xc bytes per pixel'),
    (0x110dd7, 'ret', 'alloc: plain RET (caller pops 8)'),
    (0x07612e, 'add esp, 8', 'alloc caller pops two words'),
    (0x110c11, 'mov edi, ecx', 'free: ECX = object**'),
    (0x110c1e, 'dec dword ptr [eax + 0x60]', 'free: +0x60 reference count'),
    (0x0f4595, 'mov edx, dword ptr [esp + 0x10]', 'plain copy: arg1 = dst'),
    (0x0f459d, 'mov ecx, dword ptr [esp + 0x10]', 'plain copy: arg0 = src'),
    (0x0f45b1, 'call 0x50bc10', 'plain copy -> leaf wrapper'),
    (0x0f45ba, 'ret', 'plain copy: plain RET, cdecl'),
    (0x10bc12, 'mov esi, edx', 'leaf wrapper: EDX = dst'),
    (0x10bc14, 'mov edi, ecx', 'leaf wrapper: ECX = src'),
    (0x10bc16, 'call 0x4f5b70', 'leaf wrapper: pair lock'),
    (0x10bc28, 'call 0x50ba70', 'leaf wrapper: classifier'),
    (0x10bc2e, 'cmp eax, 0x18', 'leaf wrapper: 25 classes'),
    (0x10bc37, 'movzx eax, byte ptr [eax + 0x50bcfc]', 'class table 0x10bcfc'),
    (0x10bc3e, 'jmp dword ptr [eax*4 + 0x50bce4]', 'slot table 0x10bce4'),
    (0x10bd24, 'mov edi, ecx', 'colour wrapper: ECX = src'),
    (0x10bd26, 'call 0x4f5b70', 'colour wrapper: pair lock'),
    (0x10cde4, 'mov edi, ecx', 'alpha wrapper: ECX = src'),
    (0x10cde6, 'call 0x4f5b70', 'alpha wrapper: pair lock'),
    (0x0f59c7, 'mov dword ptr [ebx + 0x34], eax', 'object +0x34 D3D texture'),
    (0x0f5a64, 'mov dword ptr [esi + 0x30], 0', 'object +0x30 surface cleared'),
    (0x0f5a75, 'call dword ptr [eax + 8]', 'object +0x34 released (IUnknown::Release)'),
)
CALLERS = {  # callee RVA -> exact direct callers
    0x0a8990: (0x020dd0, 0x0b2394),             # font entry: native + KC (AP has four)
    0x115190: (0x01e00f, 0x0a89b7),             # font open: cockpit inline + font entry
    0x1116c0: (0x0a69f6,),                      # Materials loader
    0x1125c0: (0x0a69fb,),                      # the load after it (AP 0x004f5460)
    0x0a69f0: (0x0777db,),                      # Materials wrapper
    0x109e00: (0x076c96,),                      # config constructor
    0x076bc0: (0x076b68,),                      # init routine
    0x116890: (0x07765a,),                      # display creation
    0x01df90: (0x077e21, 0x0798a5),             # cockpit constructor
    0x020d30: (0x0798bf,),                      # native ordinary-entry caller's function
    0x079430: (0x077c52,),                      # game start
    0x0f13b0: (0x0772bc, 0x0f3c38),             # Direct3DCreate9 helper
    0x1123a0: None,                             # lookup: many (count only)
}
INIT_ROUTINE, INIT_END = 0x076bc0, 0x078688
INIT_ORDER = ((0x076c96, 0x109e00, 'config constructor'), (0x0772bc, 0x0f13b0, 'Direct3DCreate9'),
              (0x07765a, 0x116890, 'display + CreateDevice'), (0x0777db, 0x0a69f0, 'Materials'),
              (0x077c52, 0x079430, 'game start (native fonts)'), (0x077e21, 0x01df90, 'cockpit constructor'))
STRINGS = ((0x18cc38, b'%s%d\0'), (0x18b104, b'F\\%s\0'), (0x19140c, b'Materials\0'), (0x190f0c, b'-fontscale\0'),
           (0x184fdc, b'Tahoma\0'), (0x1918c0, b'tga bmp\0'), (0x1918c8, b'abc siz\0'))
RELOCATED = ((0x1a5cdc, 0x18b104, 'descriptor +0x3c -> "F\\%s"'), (0x109e9b, 0x1a5ca0, 'config +0xc8 descriptor'),
             (0x0a899b, 0x18cc38, 'font entry "%s%d"'), (0x01dfe1, 0x18cc38, 'cockpit "%s%d"'),
             (0x0b2b38, 0x0b21ed, 'KC jump table entry 111 -> image case'))
IAT_D3DCREATE = 0x150320
CLASS_TABLE, CLASS_BYTES = 0x10bcfc, bytes.fromhex('00050505050105050505020505050503050505050505050504')
SLOT_TABLE, SLOTS = 0x10bce4, (0x10bc45, 0x10bc6c, 0x10bc93, 0x10bcde, 0x10bcba, 0x10bcde)
JUMP_TABLE, IMAGE_CASE_INDEX, IMAGE_CASE = 0x0b297c, 111, 0x0b21ed
CONFIG_SLOT, DENSITY = 0x1a83b0, 0x774
CONFIG_WRITERS = (0x109ed5, 0x10a371)
STYLE_FN, STYLE_EBX_SET, STYLE_FN_END = 0x115190, 0x1151a4, 0x1154ac
STYLE_EBX_READS, STYLE_EBX_WRITES = (0x1151b6,), (0x1154aa,)  # lea edx,[ebx+1]; pop ebx
# Identity windows (name -> rva, length) the module compares before any write.
WINDOW_SPANS = (
    ('font_entry', 0x0a8990, 0x2c),        # through call 0x515190; add esp,0x1c
    ('materials', 0x0a69f0, 0x10),         # frame, both calls, push ecx
    ('style', 0x115190, 0x4c),             # config read .. lea edx,[ebx+1] .. the type 1/4 exception
    ('blt_block', 0x0a7b30, 0x1d),         # prologue .. jns
    ('blt_block_reads', 0x0a7d24, 0x50),   # row count, row flag test, density read and divide
    ('blt_alpha', 0x0a7f80, 0x1d),
    ('image_scale', 0x0b22c0, 0x34),       # factor block (both inbound edges) up to the site
    ('image_block', 0x0b22f4, 0x6b),       # site .. call copy .. return sequence .. call lookup .. jmp
    ('text_line', 0x0a6d90, 0x15),
    ('rect_fill', 0x0a6b60, 0x0a),
    ('lookup', 0x1123a0, 0x6a),            # the whole lookup
    ('alloc', 0x110c90, 0x27),
    ('alloc_fields', 0x110cb7, 0x85),      # depth selection and the field stores
    ('free', 0x110c10, 0x11),
    ('plain_copy', 0x0f4590, 0x2b),
    ('copy_dispatch', 0x10bc10, 0x35),
    ('colour', 0x10bd20, 0x0e),
    ('alpha', 0x10cde0, 0x0e),
    ('config_default', 0x109ed5, 0x0a),
    ('config_atol', 0x10a35f, 0x18),
    ('cockpit_literals', 0x01dfd8, 0x3c),
    ('font_loader_path', 0x114a91, 0x1b),
)


def window_record(img, rva, length):
    raw = bytearray(img.rva(rva, length) or b'')
    fix = []
    for f in sorted(img.fixups):
        if rva - 3 <= f < rva + length:
            if not (rva <= f and f + 4 <= rva + length):
                return None
            value = struct.unpack_from('<I', raw, f - rva)[0]
            fix.append((f - rva, value - BASE))
            raw[f - rva:f - rva + 4] = b'\0\0\0\0'
    return bytes(raw), tuple(fix)


def reg_sets(img, insn):
    read, written = insn.regs_access()
    return {insn.reg_name(r) for r in read}, {insn.reg_name(r) for r in written}


EAX_NAMES, EBX_NAMES = {'eax', 'ax', 'al', 'ah'}, {'ebx', 'bx', 'bl', 'bh'}


def verify(data):
    report = {'result': 'FAIL', 'edition': 'fl', 'checks': {}}
    checks = report['checks']
    sha = hashlib.sha256(data).hexdigest()
    report['exe_info'] = {'sha256': sha, 'known': sha == FL_SHA256}
    img = FlImage(data)
    checks['fl_identity'] = {'ok': img.timestamp == FL_TIMESTAMP and img.image_size == FL_IMAGE_SIZE,
                             'timestamp': f'{img.timestamp:#010x}', 'image_size': f'{img.image_size:#010x}'}
    edges = img.direct_edges()
    pointers = {}
    for f in img.fixups:
        raw = img.rva(f, 4)
        if raw:
            pointers.setdefault(struct.unpack('<I', raw)[0] - BASE, []).append(f)
    sites = []
    for kind, name, rva, hexbytes, lengths, ap in SITES:
        want = bytes.fromhex(hexbytes)
        got = img.rva(rva, len(want))
        insns = img.insns(rva, rva + len(want))
        span_fix = sorted(f - rva for f in img.fixups if rva - 3 <= f < rva + len(want))
        flow = [i for i in insns or [] if i.mnemonic.startswith(('j', 'call', 'loop', 'ret'))]
        interior = sorted(a for a, _, t in edges if rva < t < rva + len(want))
        to_start = sorted(a for a, _, t in edges if t == rva)
        ptr_interior = sorted(p for p in pointers if rva < p < rva + len(want))
        ptr_start = sorted(pointers.get(rva, []))
        flow_ok = not flow
        if kind == 'call':
            flow_ok = (len(flow) == 1 and flow[0].mnemonic == 'call' and
                       int(flow[0].op_str, 16) - BASE == CALL_TARGETS[name])
        # an edit is never an entry: no edge/pointer may reach any byte of it except an instruction start that
        # the native code already branches to (none of the listed edits has one)
        start_ok = kind != 'edit' or (not to_start and not ptr_start)
        ok = (got == want and insns is not None and tuple(i.size for i in insns) == lengths and not span_fix and
              flow_ok and not interior and not ptr_interior and start_ok and
              (kind == 'edit' or not ptr_start))
        sites.append({'name': name, 'kind': kind, 'rva': f'{rva:#08x}', 'ap_va': f'{ap:#010x}' if ap else None,
                      'ok': ok, 'bytes': got.hex() if got else None,
                      'lengths': [i.size for i in insns] if insns else None, 'span_fixups': span_fix,
                      'interior_edges': [f'{a:#08x}' for a in interior], 'edges_to_start': [f'{a:#08x}' for a in to_start],
                      'pointers_to_start': [f'{p:#08x}' for p in ptr_start],
                      'pointers_into_interior': [f'{p:#08x}' for p in ptr_interior],
                      'qword_crossing_jmp5': (rva & 7) + 5 > 8 if kind != 'edit' else None,
                      'text': [f'{i.mnemonic} {i.op_str}'.strip() for i in insns or []]})
    checks['sites'] = {'ok': all(s['ok'] for s in sites) and len(sites) == len(SITES), 'count': len(sites),
                       'direct_edges': len(edges), 'highlow': len(img.fixups),
                       'failed': [s for s in sites if not s['ok']], 'all': sites}
    anchors = []
    for rva, text, what in ANCHORS:
        got, _ = img.text_at(rva)
        anchors.append({'rva': f'{rva:#08x}', 'ok': got == text, 'got': got, 'what': what})
    checks['anchors'] = {'ok': all(a['ok'] for a in anchors), 'count': len(anchors),
                         'failed': [a for a in anchors if not a['ok']]}
    strings = [{'rva': f'{r:#08x}', 'ok': img.rva(r, len(s)) == s, 'text': s[:-1].decode()} for r, s in STRINGS]
    relocs = []
    for at, target, what in RELOCATED:
        raw = img.rva(at, 4)
        relocs.append({'at': f'{at:#08x}', 'what': what, 'ok': at in img.fixups and raw is not None and
                       struct.unpack('<I', raw)[0] == BASE + target})
    iat = img.rva(IAT_D3DCREATE, 4)
    name_rva = struct.unpack('<I', iat)[0] if iat else 0
    iat_ok = name_rva and img.rva(name_rva + 2, 16) == b'Direct3DCreate9\0'
    slot_vals = [struct.unpack('<I', img.rva(SLOT_TABLE + 4 * k, 4))[0] - BASE for k in range(6)]
    jt = struct.unpack('<I', img.rva(JUMP_TABLE + 4 * IMAGE_CASE_INDEX, 4))[0] - BASE
    checks['data'] = {'ok': all(s['ok'] for s in strings) and all(r['ok'] for r in relocs) and bool(iat_ok) and
                      img.rva(CLASS_TABLE, 25) == CLASS_BYTES and tuple(slot_vals) == SLOTS and
                      all(SLOT_TABLE + 4 * k in img.fixups for k in range(6)) and jt == IMAGE_CASE,
                      'strings': strings, 'relocated': relocs, 'iat_direct3dcreate9': bool(iat_ok),
                      'class_table': img.rva(CLASS_TABLE, 25).hex(), 'slots': [f'{s:#08x}' for s in slot_vals],
                      'image_case_entry': f'{jt:#08x}'}
    callers = {}
    for callee, want in CALLERS.items():
        got = tuple(sorted(a for a, m, t in edges if t == callee and m == 'call'))
        jumps = sorted(a for a, m, t in edges if t == callee and m != 'call')
        ptrs = pointers.get(callee, [])
        ok = (got == want if want is not None else len(got) > 0)
        if callee != 0x1123a0:
            ok = ok and not jumps and not ptrs
        callers[f'{callee:#08x}'] = {'ok': ok, 'callers': [f'{a:#08x}' for a in got] if want else len(got),
                                     'jumps': [f'{a:#08x}' for a in jumps][:8], 'pointers': [f'{p:#08x}' for p in ptrs]}
    checks['callers'] = {'ok': all(c['ok'] for c in callers.values()), **callers}
    # Config +0x774 writer census: every instruction writing [reg+0x774] whose base was loaded from the slot within
    # the previous eight instructions, plus the constructor's own store through EDI (the fresh object).
    writers = []
    _, start, _, rp, rsize = next(s for s in img.sections if s[0] == '.text')
    code, pos = img.data[rp:rp + rsize], 0
    while pos < len(code):  # resumable linear sweep (as direct_edges)
        last = pos
        for i in img.md.disasm(code[pos:pos + 0x10000], start + pos):
            last = i.address + i.size - start
            dest = i.op_str.split(',')[0]
            if dest.endswith(f'+ {DENSITY:#x}]') and i.mnemonic in ('mov', 'add', 'sub', 'inc', 'dec', 'or', 'and',
                                                                    'xchg', 'adc', 'sbb', 'xor'):
                writers.append(i.address - BASE)
        pos = last if last > pos else pos + 1
    writers = sorted(set(writers))
    checks['config_writers'] = {'ok': tuple(writers) == CONFIG_WRITERS, 'writers': [f'{w:#08x}' for w in writers]}
    # Style loop: EBX is written by xor/setg and read only by lea edx,[ebx+1] (and restored by pop ebx).
    body = img.insns(STYLE_FN, STYLE_FN_END)
    reads, writes = [], []
    for i in body or []:
        r, w = reg_sets(img, i)
        a = i.address - BASE
        if a > STYLE_EBX_SET and r & EBX_NAMES and i.mnemonic != 'push':
            reads.append(a)
        if a > STYLE_EBX_SET and w & EBX_NAMES:
            writes.append(a)
    checks['style_ebx'] = {'ok': body is not None and tuple(reads) == STYLE_EBX_READS and tuple(writes) == STYLE_EBX_WRITES,
                           'reads': [f'{a:#08x}' for a in reads], 'writes': [f'{a:#08x}' for a in writes]}
    # Image site: the displaced loads write ECX/EAX, the imul right after them writes EFLAGS; at the return 0xb2353
    # EAX and EFLAGS are dead (CX loaded, add esp writes EFLAGS, lookup defines EAX without reading it).
    lookup = img.insns(0x1123a0, 0x1123b2)
    lookup_eax_read = [i.address - BASE for i in lookup or [] if reg_sets(img, i)[0] & EAX_NAMES]
    site = img.insns(0x0b22f4, 0x0b22fe)
    site_ok = (site is not None and 'ecx' in reg_sets(img, site[0])[1] and 'eax' in reg_sets(img, site[1])[1] and
               site[2].mnemonic == 'imul')
    checks['image_liveness'] = {'ok': site_ok and lookup is not None and not lookup_eax_read,
                                'lookup_eax_reads': lookup_eax_read}
    # Blit entries: EAX is not read before it is written on either path (0xa7b53 mov eax,edx / the lookup call).
    for name, start, neg, jns_target in (('blt_block', 0x0a7b30, 0x0a7b53, 0x0a7d06),
                                         ('blt_alpha', 0x0a7f80, 0x0a7fa3, 0x0a8156)):
        seg = img.insns(start, neg + 2)
        early = [i.address - BASE for i in seg or [] if reg_sets(img, i)[0] & EAX_NAMES]
        tgt, _ = img.text_at(jns_target)
        checks[f'{name}_eax_dead'] = {'ok': seg is not None and not early and tgt == 'call 0x5123a0' and
                                      img.text_at(neg)[0] == 'mov eax, edx', 'early_reads': early}
    # Init order: the call sites appear in this order in the routine's linear decode and target as listed.
    routine = img.insns(INIT_ROUTINE, INIT_END)
    seen = [i.address - BASE for i in routine or [] if i.mnemonic == 'call' and i.op_str.startswith('0x') and
            (i.address - BASE, int(i.op_str, 16) - BASE) in {(a, t) for a, t, _ in INIT_ORDER}]
    checks['init_order'] = {'ok': routine is not None and tuple(seen) == tuple(a for a, _, _ in INIT_ORDER),
                            'order': [w for _, _, w in INIT_ORDER], 'sites': [f'{a:#08x}' for a in seen],
                            'routine_ends_with': routine[-1].mnemonic if routine else None}
    windows = {}
    for name, rva, length in WINDOW_SPANS:
        rec = window_record(img, rva, length)
        whole = img.insns(rva, rva + length)
        windows[name] = {'ok': rec is not None and whole is not None, 'rva': f'{rva:#08x}', 'length': length,
                         'fnv1a32': f'{fnv1a32(rec[0]):08x}' if rec else None,
                         'fixups': [[o, f'{t:#08x}'] for o, t in rec[1]] if rec else None}
    checks['windows'] = {'ok': all(w['ok'] for w in windows.values()), 'count': len(windows), **windows}
    report['result'] = 'PASS' if all(c.get('ok') for c in checks.values()) else 'FAIL'
    return report


def emit_windows(data):
    img = FlImage(data)
    out = []
    for name, rva, length in WINDOW_SPANS:
        raw, fix = window_record(img, rva, length)
        fixups = ', '.join(f'{{{o}, 0x{t:06x}}}' for o, t in fix)
        out.append(f'// {name}\n{{0x{rva:06x}, {length}, 0x{fnv1a32(raw):08x}u, {len(fix)}, {{{fixups}}}}},')
    return '\n'.join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--exe', type=Path, default=FL_DEFAULT_EXE)
    parser.add_argument('--json', action='store_true', help='print the full report')
    parser.add_argument('--emit-windows', action='store_true', help='print the identity-window rows (no bytes)')
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
