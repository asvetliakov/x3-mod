"""Whole-instruction anchors for docs/reverse-engineering/fl/hull-hit-draw.md (FL X3FL.exe, RVAs).

Reuses the hash-checked PE/Capstone reader of verification/results/fl-weapon-effect-spawn/fl_weapon_effect_sites.py.
Every instruction the note quotes is asserted here, plus the strings and the 1/255 constant. Prints one line per group
and the failure list; exit status 1 on any mismatch.

    python3 -I verification/results/fl-hull-hit-draw/fl_hull_hit_draw_anchors.py [/mnt/c/X3FL/X3FL.exe] \
        > verification/results/fl-hull-hit-draw/fl_hull_hit_draw_anchors_out.txt
"""
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/results/fl-weapon-effect-spawn'))
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import fl_weapon_effect_sites as w  # noqa: E402

B = 0x400000
ANCHORS = {
    'impact function 0x014440 (ECX victim, EDX source)': [
        (0x053390, 'call 0x414440'),                            # the impact site (our shield-hit hook)
        (0x01448a, 'mov eax, dword ptr [ebx + 0xa4]'),          # victim radius
        (0x014490, 'cmp eax, dword ptr [edi + 0xa4]'),          # vs source radius: the smaller body gives p
        (0x0144a0, 'lea ecx, [esi + 0x180]'),                   # node +0x180 (victim node)
        (0x0144a6, 'call 0x50e0c0'),                            # R(node +0x40) * node +0x180
        (0x0144b3, 'add ecx, 0x180'),                           # (source node)
        (0x0144cc, 'call 0x412b80'),                            # + node +0x30
        (0x0144d1, 'movzx eax, word ptr [ebx + 0x48]'),         # victim class 0 or 10 -> victim is the projectile
        (0x0144de, 'cmp ax, 0xa'),
        (0x0144f1, 'cmp ax, 0xa'),                              # source class 0 or 10, else no effect
        (0x014502, 'sub eax, dword ptr [esi + 0x30]'),          # p - hit node pos
        (0x014530, 'call 0x50dfa0'),                            # R^T(hit node) * (p - pos): contact in the hit frame
        (0x01454b, 'mov eax, dword ptr [0x5a83c8]'),            # TBullets table
        (0x014550, 'mov esi, dword ptr [ecx + eax + 0x78]'),    # TBullets +0x78 (col 20) impact row
        (0x01455c, 'mov eax, dword ptr [0x5a83f0]'),            # TMissiles table
        (0x014561, 'mov esi, dword ptr [ecx + eax + 0x68]'),    # TMissiles +0x68 impact row
        (0x01456d, 'call 0x414c10'),                            # PROPAGATE children first
        (0x014572, 'mov edx, dword ptr [edi + 8]'),             # k = projectile id
        (0x01457e, 'mov ecx, 3'),                               # kind 3
        (0x014583, 'call 0x415270'),
        (0x014598, 'movups xmm0, xmmword ptr [eax + 0x40]'),    # projectile node rotation ...
        (0x01459c, 'movups xmmword ptr [ecx + 0x70], xmm0'),    # ... -> instance +0x70..+0x9c (BULLETROT)
        (0x0145af, 'mov dword ptr [ecx + 0x14], edi'),          # instance +0x14 = projectile
        (0x01461d, 'mov esi, dword ptr [ecx + eax + 0x78]'),    # mirrored branch (victim is the projectile)
        (0x014650, 'call 0x415270'),
    ],
    'inner 0x015270 and spawner 0x0150e0': [
        (0x01528b, 'mov ebx, dword ptr [eax*4 + 0x61c8a0]'),    # Effects row record, stride 0x14
        (0x0152a6, 'test dword ptr [edi], 0x10000000'),         # EEDF_PUSH -> source resolved
        (0x015323, 'call 0x4150e0'),
        (0x015114, 'cmp dword ptr [esi + 0x10], ecx'),          # find: obj
        (0x015119, 'cmp dword ptr [esi + 0x20], eax'),          # kind
        (0x01511e, 'cmp dword ptr [esi + 0x24], ebx'),          # k (projectile id)
        (0x01518f, 'movups xmmword ptr [esi + 0x30], xmm0'),    # new: instance pos = &pos
        (0x0151d7, 'call 0x50d400'),                            # no orientation: identity
        (0x015205, 'call 0x4153a0'),                            # new instance: nodes cleared, clock, update
        (0x015211, 'movups xmmword ptr [esi + 0x30], xmm0'),    # found: pos overwritten with the new contact
        (0x015253, 'call 0x415400'),                            # re-arm
        (0x0153e0, 'mov dword ptr [ecx + 0x28], eax'),          # clock base = now + t0
        (0x015406, 'test byte ptr [eax + 4], 4'),               # EDF_REINIT tears element nodes down
        (0x015478, 'mov dword ptr [esi + 0x28], eax'),          # re-arm: clock base reset
    ],
    'element start 0x015490 (non-CUT)': [
        (0x015535, 'mov eax, dword ptr [eax + 0x708]'),         # game clock
        (0x01553e, 'cmp eax, dword ptr [edi + ecx + 0x40]'),    # start delay
        (0x01554c, 'test eax, 0x20000'),                        # DISTANT
        (0x0155ae, 'test eax, 0x80000'),                        # HULLDAMAGE gate
        (0x0155b8, 'call 0x4585a0'),
        (0x0155bd, 'cmp eax, 0x64'),                            # shield points < 100
        (0x015632, 'test dword ptr [edx + ecx + 0x3c], 0x8000'),  # RANDOMSIZE
        (0x01564c, 'call 0x50b5d0'),
        (0x015673, 'test al, 0x20'),                            # CUT
        (0x0156a3, 'or dword ptr [edi + 0x12c], ecx'),          # CUT: I3DF on the scene root only
        (0x015abf, 'jns 0x415c03'),                             # LIGHT (0x80)
        (0x015be5, 'push dword ptr [eax + 0x1c]'),
        (0x015be8, 'call 0x4a56b0'),                            # light node attached like the bodies
        (0x015c03, 'test eax, 0x2000'),                         # BREAKUP
        (0x015c34, 'cmp dword ptr [edx + ecx], 0'),             # body id > 0
        (0x015c3e, 'call 0x4a2200'),                            # node
        (0x015c5f, 'or dword ptr [edi + 0x12c], eax'),          # |= element I3DF (I3DF_2D 0x4000)
        (0x015c77, 'call 0x4a35e0'),                            # body -> node +0x144
        (0x015c99, 'test byte ptr [edx + ecx + 0x3c], 2'),      # RELATIVESIZE
        (0x015cb9, 'call 0x4a3920'),                            # start size
        (0x015ccc, 'test eax, 0x200'),                          # RANDOMROT
        (0x015cd7, 'test dword ptr [edi + 0x12c], 0x4000'),     # 2D: random roll only
        (0x015d06, 'call 0x50d400'),
        (0x015d77, 'call 0x4a3cd0'),                            # 3D: three random angles
        (0x015d82, 'test eax, 0x1000'),                         # BULLETROT
        (0x015d89, 'movups xmm0, xmmword ptr [ebx + 0x70]'),    # projectile rotation
        (0x015db9, 'call 0x50ebe0'),                            # else inst orientation x hit-object rotation
        (0x015dfc, 'push dword ptr [eax + 0x1c]'),              # parent = the hit node's +0x1c
        (0x015dff, 'call 0x4a56b0'),
        (0x015e75, 'test dword ptr [eax + edx + 0x3c], 0x10000000'),  # PUSH
        (0x015e9f, 'cmp word ptr [edi + 0x48], 7'),             # only a class-7 ship is pushed
        (0x015579, 'cmp word ptr [eax + 0x48], 0x1b'),          # FADEOUTBODY needs a class-0x1b carrier
    ],
    'per update: placement, orientation': [
        (0x01626b, 'movups xmm0, xmmword ptr [ebx + 0x30]'),    # contact point (hit frame)
        (0x016292, 'test byte ptr [ecx + edi + 0x3c], 2'),
        (0x016339, 'add eax, dword ptr [ecx + edi + 0x20]'),    # + element offset
        (0x016379, 'call 0x50e0c0'),                            # x hit-object rotation
        (0x01638b, 'mov eax, dword ptr [ecx + 0x30]'),          # + hit-object position
        (0x0163c6, 'test dword ptr [eax + edi + 0x3c], 0x1000'),
        (0x0163d0, 'movups xmm0, xmmword ptr [ebx + 0x70]'),    # BULLETROT every update
        (0x0163f0, 'test dword ptr [ecx + 0x12c], 0x4000'),     # 2D keeps its roll
        (0x01640c, 'call 0x50ebe0'),
    ],
    'per update: size and flicker': [
        (0x016600, 'test al, 8'),                               # GROWANDSHRINK
        (0x01662a, 'mov eax, 0x20000'),                         # t = 2 age / timeout - 1
        (0x016811, 'sub edx, eax'),                             # b - (b - a) t^2
        (0x01682d, 'test eax, 0x100'),                          # GROW
        (0x016852, 'cmp eax, ecx'),                             # age > timeout -> f = 1
        (0x016937, 'mov ecx, dword ptr [eax + edi + 8]'),
        (0x01693f, 'sub eax, ecx'),                             # a + (b - a) f
        (0x016973, 'add ecx, dword ptr [edx + edi + 8]'),       # neither: a (+ random)
        (0x01675f, 'test dword ptr [eax + edi + 0x3c], 0x1000000'),  # NOZTEST
        (0x016770, 'or dword ptr [eax + 0x130], 8'),
        (0x01677e, 'or dword ptr [eax + 0x130], 0x10'),
        (0x01678b, 'mov ecx, dword ptr [eax + edi + 0x14]'),    # rndsize
        (0x016797, 'cmp ecx, 0x10000'),
        (0x0167a3, 'call 0x50b5d0'),                            # random in [0, rndsize)
        (0x0167ad, 'sub ecx, eax'),                             # size x (1 - r), every update
        (0x016994, 'imul dword ptr [ecx + 0xa4]'),              # RELATIVESIZE x radius
        (0x0169ae, 'call 0x4a3920'),                            # node scale
    ],
    'per update: FADEOUT alpha ramp': [
        (0x0169bd, 'and eax, 0x4400'),                          # FADEOUT and not PROPAGATE
        (0x0169c2, 'cmp eax, 0x400'),
        (0x0169e4, 'sub eax, dword ptr [ecx + edi + 0x40]'),
        (0x016a00, 'idiv ecx'),                                 # f = age / timeout (16.16)
        (0x016a0f, 'mov ecx, 0xff'),
        (0x016a29, 'mov edx, 0xfe'),
        (0x016a2e, 'sub edx, eax'),                             # 254 - 255 f
        (0x016a38, 'cmovl edx, eax'),                           # floor 1
        (0x016a3e, 'mov dword ptr [ecx + 0x13c], edx'),         # node alpha
        (0x016a50, 'mov dword ptr [eax + 0x13c], edx'),         # and every child
    ],
    'life: manager, timeout, finished bit': [
        (0x0782c5, 'call 0x416f40'),                            # effects manager
        (0x0172a2, 'call 0x415490'),                            # element update per instance
        (0x0172eb, 'test dword ptr [edx + 0x12c], 0x2000'),     # node finished -> element ends
        (0x017302, 'test byte ptr [eax + ecx + 0x3c], 0x10'),   # TIMEOUT
        (0x017315, 'mov ecx, dword ptr [eax + ecx + 0x40]'),
        (0x017319, 'add ecx, dword ptr [eax + ebx + 0x10]'),    # start + timeout
        (0x017335, 'call 0x4a3300'),                            # node destroyed
    ],
    'texture animation (negative ids)': [
        (0x0a36d4, 'call 0x4a2eb0'),                            # body set -> group walk
        (0x0a305f, 'mov dword ptr [ebx + 0x1a8], esi'),         # instance count
        (0x0a308c, 'call 0x512da0'),
        (0x0a3094, 'mov dword ptr [ebx + 0x1ac], eax'),         # instance array (stride 0x80)
        (0x096fbe, 'mov edx, dword ptr [esi + 0x1ac]'),         # immediate traversal: step
        (0x096fc7, 'mov ecx, dword ptr [esi + 0x1a8]'),
        (0x096fcd, 'call 0x5139a0'),
        (0x096fd6, 'or dword ptr [esi + 0x12c], 0x2000'),       # stepper true -> node finished
        (0x0976b7, 'call 0x5139a0'),                            # deferred queue: step
        (0x0976c0, 'or dword ptr [edi + 0x12c], 0x2000'),
        (0x1139f9, 'shl edi, 7'),                               # stepper: stride 0x80
        (0x113aa2, 'mov eax, dword ptr [0x5a83b0]'),
        (0x113aa7, 'mov ebx, dword ptr [eax + 0x708]'),         # game clock
    ],
    'billboard and batch / queue routing': [
        (0x094b72, 'test dword ptr [edi + 0x12c], 0x4000'),
        (0x094b7e, 'lea eax, [edi + 0xc0]'),
        (0x094b85, 'lea edx, [esi + 0x40]'),                    # camera rotation
        (0x094b88, 'lea ecx, [edi + 0x40]'),                    # node roll
        (0x094b8b, 'call 0x50ebe0'),
        (0x085706, 'cmp dword ptr [esi + 0x144], 0'),
        (0x085715, 'test cl, 2'),
        (0x08571a, 'test ecx, 0x800'),
        (0x085732, 'test ecx, 0x4000000'),
        (0x08573a, 'test dword ptr [esi + 0x130], 0x800000'),
        (0x0836cd, 'or dword ptr [esi + 0x130], 0x10800000'),   # bullet nodes only
        (0x09700a, 'test ecx, 0x8000'),
        (0x097016, 'test dword ptr [esi + 0x12c], 0x800'),
        (0x097022, 'test cl, 0x20'),
        (0x097027, 'test byte ptr [esi + 0x130], 0x10'),        # NOZTEST node -> deferred
        (0x097035, 'call 0x4e6ef0'),                            # immediate
        (0x09703f, 'lea ecx, [esp + 0x80]'),                    # else: view-space point for the deferred sort
        (0x097702, 'call 0x4e6ef0'),                            # deferred
        (0x0e7142, 'call 0x4e0f30'),                            # material submit (object-trace seam)
    ],
    'material draw 0x0e0f30: effect and technique': [
        (0x0e1677, 'cmp dword ptr [ecx + 0x20], 1'),            # g_CullMode 1 ...
        (0x0e167d, 'mov dword ptr [ebp - 0x14], 1'),            # ... -> two-sided effect
        (0x0e1770, 'push 0x58fccc'),                            # "max_standard_lighting"
        (0x0e178e, 'push 0x58fce4'),                            # "standard_lighting"
        (0x0e17aa, 'mov esi, 0x58fce4'),
        (0x0e17b6, 'cmp dword ptr [ebp - 0x14], 0'),
        (0x0e17ce, 'push 0x58fcf8'),                            # "2s"
        (0x0e1880, 'mov dword ptr [ebp - 0x20], 0x58fcfc'),     # BUMPMAP (bump id > 0)
        (0x0e18ad, 'mov dword ptr [ebp - 0x20], 0x58fd04'),     # DEFAULT
    ],
    'material draw 0x0e0f30: animation frame, g_TexMatrix, fade': [
        (0x0e10ca, 'mov esi, dword ptr [ecx + eax]'),           # group material index
        (0x0e10da, 'js 0x4e10f2'),                              # negative -> animation instance
        (0x0e10f4, 'cmp dword ptr [edx + 0x1a8], eax'),
        (0x0e10fc, 'mov edi, dword ptr [edx + 0x1ac]'),
        (0x0e1102, 'neg esi'),
        (0x0e111c, 'sub edx, -0x80'),
        (0x0e113f, 'movzx eax, word ptr [eax + edx + 0x70]'),   # current frame texture id
        (0x0e1159, 'lea eax, [edx + 0x40]'),                    # UV matrix
        (0x0e1162, 'mov dword ptr [ebp - 0x5c], eax'),
        (0x0e18ba, 'test dword ptr [eax + 0x28], 0x8000000'),   # TexAnim material
        (0x0e18c3, 'test dword ptr [ecx + 0x130], 0x1000000'),
        (0x0e3e2b, 'cmp dword ptr [esi + 0x134], 0'),           # g_TexMatrix handle present
        (0x0e3e34, 'mov eax, dword ptr [ebp - 0x5c]'),
        (0x0e3e67, 'push 0x10000'),                             # identity when no instance
        (0x0e3e86, 'push 0x58ff60'),                            # "g_TexMatrix"
        (0x0e3e8b, 'call 0x4d9580'),
        (0x0e3cc6, 'mov dword ptr [esi + 0x188], eax'),         # g_AlphaValue handle
        (0x0e1038, 'movsd xmm0, qword ptr [0x591a88]'),         # 1/255
        (0x0e4a86, 'lea ecx, [ebp - 0x9c]'),                    # GetBool(g_AlphaBlendEnable)
        (0x0e4abd, 'mov eax, dword ptr [ecx + 0x13c]'),         # node alpha
        (0x0e4ad9, 'mulsd xmm0, qword ptr [ebp - 0xf8]'),
        (0x0e4b0e, 'cmp dword ptr [esi + 0x1a4], 0'),           # classic blended material?
        (0x0e4b17, 'cmp dword ptr [ebp - 0x9c], 0'),            # material blend already on?
        (0x0e4b2c, 'push 5'),                                   # else SrcBlend SRCALPHA
        (0x0e4b6f, 'push 2'),                                   # and DestBlend ONE
        (0x0e4b7d, 'mov ecx, dword ptr [esi + 0x188]'),
        (0x0e4b94, 'mov eax, dword ptr [eax + 0x78]'),          # SetFloat(g_AlphaValue)
        (0x0e1ee5, 'mov dword ptr [eax + 0x1a4], 0'),
        (0x0e2ef2, 'mov dword ptr [eax + 0x1a4], 1'),           # classic blend-flag branch
        (0x0e2e95, 'push 0x58fdf8'),                            # classic: g_AlphaValue ...
        (0x0e2eca, 'mov dword ptr [esp], 0x3f800000'),          # ... = 1.0
        (0x0e1a03, 'cmp eax, dword ptr [ecx + 0x1c]'),          # classic: cockpit view -> "effects"
        (0x0e1a0f, 'mov ecx, 0x58fce4'),                        # else "standard_lighting"
        (0x0e1a58, 'mov dword ptr [ebp - 0x20], 0x58fd04'),     # classic DEFAULT
    ],
    'cited range boundaries and routine entries': [
        (0x014440, 'push ebp'), (0x0144f5, 'jne 0x414682'), (0x0144fb, 'mov eax, dword ptr [esp + 0x10]'),
        (0x014595, 'mov eax, dword ptr [edi + 0x70]'), (0x0145b7, 'movups xmmword ptr [ecx + 0x90], xmm0'),
        (0x014696, 'ret'), (0x014c10, 'push ebp'), (0x0150e0, 'push ebp'), (0x015270, 'push ecx'),
        (0x0152dd, 'push dword ptr [esp + 0x24]'), (0x0153a0, 'mov eax, dword ptr [ecx + 8]'), (0x015400, 'push esi'),
        (0x015490, 'push ebp'), (0x0155c0, 'jge 0x41555b'), (0x015654, 'mov dword ptr [ecx + esi + 0x10], eax'),
        (0x015da7, 'jmp 0x415ded'), (0x015e04, 'mov eax, dword ptr [ebx + 0xc]'),
        (0x016258, 'mov eax, dword ptr [ebx + 0xc]'), (0x0163bd, 'movups xmmword ptr [edx], xmm0'),
        (0x0163ea, 'movups xmmword ptr [ecx + 0x60], xmm0'), (0x0167c4, 'jmp 0x416984'), (0x016828, 'jmp 0x41674d'),
        (0x016967, 'jmp 0x416755'), (0x016f40, 'push ebp'), (0x0968c0, 'push ebp'), (0x0978e0, 'push ebp'),
        (0x0a2200, 'push ecx'), (0x0a2eb0, 'sub esp, 0x2f0'), (0x0a35e0, 'push ebp'), (0x0a3920, 'sub esp, 8'), (0x0a365d, 'mov dword ptr [esi + 0x144], edi'),
        (0x0a3cd0, 'push dword ptr [esp + 0xc]'), (0x0e0f30, 'push ebx'), (0x0e175a, 'cmp dword ptr [ecx + 0x18], 0x10'),
        (0x0e4b99, 'call eax'), (0x0e604d, 'call eax'), (0x0e6ef0, 'push ebx'), (0x1139a0, 'push ebp'),
    ],
}
STRINGS = {0x18fccc: 'max_standard_lighting', 0x18fce4: 'standard_lighting', 0x18fcf8: '2s', 0x18fcfc: 'BUMPMAP',
           0x18fd04: 'DEFAULT', 0x18ff60: 'g_TexMatrix', 0x18fdf8: 'g_AlphaValue', 0x174344: 'effects'}
DOUBLES = {0x191a88: 1 / 255}
# The loader's EEDF name table (RVA 0x174398: name pointer, value), the bits the note quotes.
EEDF = {'EEDF_GROWANDSHRINK': 0x8, 'EEDF_TIMEOUT': 0x10, 'EEDF_CUT': 0x20, 'EEDF_LIGHT': 0x80, 'EEDF_GROW': 0x100,
        'EEDF_RANDOMROT': 0x200, 'EEDF_FADEOUT': 0x400, 'EEDF_FADEOUTBODY': 0x800, 'EEDF_BULLETROT': 0x1000,
        'EEDF_PROPAGATE': 0x4000, 'EEDF_RANDOMSIZE': 0x8000, 'EEDF_HULLDAMAGE': 0x80000, 'EEDF_NOZTEST': 0x1000000,
        'EEDF_PUSH': 0x10000000}
EEDF_TABLE, EEDF_ROWS = 0x174398, 30


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3FL/X3FL.exe'
    img = w.Image(path, w.FL_SHA256)
    print(f'X3FL.exe sha256 {img.sha256}')
    for group, items in ANCHORS.items():
        for rva, text in items:
            img.at(group, rva + B, text)
        print(f'{group}: {len(items)} anchors')
    for rva, s in STRINGS.items():
        o = img.pe.offset(rva)
        got = img.data[o:o + len(s) + 1]
        img.checks['strings'] += 1
        if got != s.encode() + b'\0':
            img.fails.append(f'string {rva:#08x}: {got!r} != {s!r}')
    for rva, v in DOUBLES.items():
        got = struct.unpack('<d', img.read(rva + B, 8))[0]
        img.checks['constants'] += 1
        if abs(got - v) > 1e-15:
            img.fails.append(f'double {rva:#08x}: {got} != {v}')
    table = {}
    for k in range(EEDF_ROWS):
        ptr, val = struct.unpack('<II', img.read(EEDF_TABLE + B + 8 * k, 8))
        o = img.pe.offset(ptr - B)
        table[img.data[o:img.data.index(b'\0', o)].decode()] = val
    for name, val in EEDF.items():
        img.checks['eedf'] += 1
        if table.get(name) != val:
            img.fails.append(f'EEDF table {name}: {table.get(name)} != {val:#x}')
    img.checks['eedf'] += 1
    if 'EEDF_ACTUALSIZE' in table:
        img.fails.append('EEDF_ACTUALSIZE unexpectedly in the table')
    print(f'strings: {len(STRINGS)}; constants: {len(DOUBLES)}; EEDF table {len(table)} names, {len(EEDF)} checked, '
          f'EEDF_ACTUALSIZE absent')
    print(f'total {sum(img.checks.values())} checks, failures {len(img.fails)}')
    for f in img.fails:
        print('FAIL', f)
    return 1 if img.fails else 0


if __name__ == '__main__':
    sys.exit(main())
