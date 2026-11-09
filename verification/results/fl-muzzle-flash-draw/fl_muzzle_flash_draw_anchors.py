"""Whole-instruction anchors for docs/reverse-engineering/fl/muzzle-flash-draw.md (FL X3FL.exe, RVAs).

Reuses the PE/Capstone reader of verification/results/fl-weapon-effect-spawn/fl_weapon_effect_sites.py (hash-checked).
Prints one line per anchor group and the failure list; exit status 1 on any mismatch.

    python3 -I verification/results/fl-muzzle-flash-draw/fl_muzzle_flash_draw_anchors.py [/mnt/c/X3FL/X3FL.exe]
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/results/fl-weapon-effect-spawn'))
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import fl_weapon_effect_sites as w  # noqa: E402

B = 0x400000
ANCHORS = {
    'spawn and re-arm': [
        (0x04f639, 'call 0x4150e0'),
        (0x015114, 'cmp dword ptr [esi + 0x10], ecx'),     # find: obj
        (0x015119, 'cmp dword ptr [esi + 0x20], eax'),     #       kind (ECX)
        (0x01511e, 'cmp dword ptr [esi + 0x24], ebx'),     #       gun slot (EDX)
        (0x015156, 'jmp 0x41520a'),                        # found -> same tail as a new instance
        (0x015253, 'call 0x415400'),                       # spawner -> re-arm (both paths)
        (0x015406, 'test byte ptr [eax + 4], 4'),          # row EDF & 4 (REINIT): tear down element nodes
        (0x01543b, 'call 0x4a3300'),                       # node destroy
        (0x015478, 'mov dword ptr [esi + 0x28], eax'),     # clock base = now + t0
        (0x01547b, 'call 0x415490'),                       # element update
    ],
    'CUT element start': [
        (0x015673, 'test al, 0x20'),                       # EEDF_CUT
        (0x01567b, 'call 0x4a2200'),                       # scene root node
        (0x0156a3, 'or dword ptr [edi + 0x12c], ecx'),     # |= element I3DF
        (0x0156d8, 'mov dword ptr [edi + 0x144], 0xffffffff'),  # root has no body
        (0x0156fa, 'call 0x4a3920'),                       # root scale = size
        (0x015831, 'mov ecx, dword ptr [edx + eax]'),      # element +0 = scene id
        (0x015834, 'call 0x4aeaf0'),                       # scene record
        (0x015870, 'cmp dword ptr [eax + 0x30], -1'),      # part body id (stride 0x58)
        (0x01587a, 'call 0x4a2200'),                       # part node
        (0x015898, 'push dword ptr [eax + ecx + 0x30]'),
        (0x01589e, 'call 0x4a35e0'),                       # node body = part body (node +0x144)
        (0x0158b9, 'call 0x4a57d0'),                       # attach part under the root
        (0x0158cc, 'mov dword ptr [esi + 0x258], eax'),    # scene id
        (0x0158d6, 'mov dword ptr [esi + 0x25c], eax'),    # part index
        (0x0159d2, 'or dword ptr [esi + 0x12c], 0x40'),
        (0x015a6b, 'call 0x4aa950'),                       # part key animation from the scene
        (0x015dfc, 'push dword ptr [eax + 0x1c]'),         # parent = ship node's parent
        (0x015dff, 'call 0x4a56b0'),                       # attach root
    ],
    'node placement per update': [
        (0x01626b, 'movups xmm0, xmmword ptr [ebx + 0x30]'),   # inst pos (ship frame)
        (0x016337, 'mov eax, dword ptr [edx]'),
        (0x016339, 'add eax, dword ptr [ecx + edi + 0x20]'),   # + element offset (ints, no RELATIVESIZE)
        (0x016379, 'call 0x50e0c0'),                           # x ship rotation (node +0x40)
        (0x01638b, 'mov eax, dword ptr [ecx + 0x30]'),         # + ship position
        (0x0163f0, 'test dword ptr [ecx + 0x12c], 0x4000'),    # I3DF_2D keeps its own orientation
        (0x01640c, 'call 0x50ebe0'),                           # orientation = inst orient x ship
    ],
    'GROWANDSHRINK size': [
        (0x016600, 'test al, 8'),
        (0x01661f, 'sub eax, dword ptr [edx + edi + 0x40]'),   # age = now - start - base
        (0x016623, 'sub eax, dword ptr [ebx + 0x28]'),
        (0x01662a, 'mov eax, 0x20000'),                        # t = 2 age / timeout - 1
        (0x01663b, 'lea edx, [eax - 0x10000]'),
        (0x0167e2, 'imul edx'),                                # t^2
        (0x0167e8, 'sub esi, ebx'),                            # b - a
        (0x016811, 'sub edx, eax'),                            # b - (b - a) t^2
        (0x0169ae, 'call 0x4a3920'),                           # node scale
        (0x0169bd, 'and eax, 0x4400'),                         # FADEOUT without 2D -> alpha ramp
        (0x016a3e, 'mov dword ptr [ecx + 0x13c], edx'),
    ],
    'scale setter 0x0a3920': [
        (0x0a3965, 'mov dword ptr [edi + 0x70], eax'),
        (0x0a3968, 'mov dword ptr [edi + 0x80], 0x10000'),
        (0x0a3955, 'call 0x4a3920'),                           # children scaled proportionally
    ],
    'body setter 0x0a35e0': [
        (0x0a365d, 'mov dword ptr [esi + 0x144], edi'),
    ],
    'batch eligibility 0x085700 (not taken)': [
        (0x085706, 'cmp dword ptr [esi + 0x144], 0'),
        (0x08570d, 'jl 0x48575a'),
        (0x085715, 'test cl, 2'),
        (0x097968, 'cmp dword ptr [eax + 0x1c0], ecx'),
        (0x097973, 'call 0x485700'),
    ],
    'traversal and material submit': [
        (0x097aa1, 'call 0x4968c0'),
        (0x097ac5, 'call 0x4968c0'),
        (0x097583, 'call 0x4968c0'),                           # recursion into children
        (0x097594, 'call 0x4968c0'),
        (0x097035, 'call 0x4e6ef0'),                           # immediate
        (0x097702, 'call 0x4e6ef0'),                           # deferred
        (0x0e7142, 'call 0x4e0f30'),
    ],
    'classic state block in 0x0e0f30': [
        (0x0e1948, 'mov ecx, dword ptr [edx + 0x28]'),        # per-body record flags
        (0x0e1957, 'mov eax, dword ptr [ecx + eax*4 + 0x10]'), # or global Materials row (stride 0x3c) flags
        (0x0e1a03, 'cmp eax, dword ptr [ecx + 0x1c]'),        # cockpit view -> "effects"
        (0x0e1a0f, 'mov ecx, 0x58fce4'),                       # "standard_lighting"
        (0x0e2d5b, 'mov eax, dword ptr [0x61b418]'),
        (0x0e2d63, 'mov eax, dword ptr [ecx + eax + 0x10]'),   # blend flags = row +0x10
        (0x0e2eda, 'test dword ptr [ebp - 0x20], 0x4c7'),
        (0x0e2f2a, 'push 0'),                                   # g_ZWriteEnable 0
        (0x0e2f6b, 'push 1'),                                   # g_AlphaBlendEnable 1
        (0x0e33a0, 'test al, 2'),                               # DESTINATIONBLEND
        (0x0e33de, 'push 2'),                                   # g_SrcBlend ONE
        (0x0e3425, 'push 2'),                                   # g_DestBlend ONE
        (0x0e603b, 'mov eax, dword ptr [edi + 0x148]'),         # device vtable +0x148 DrawIndexedPrimitive
        (0x0e6045, 'push 4'),                                   # D3DPT_TRIANGLELIST
        (0x0e604d, 'call eax'),
    ],
}
STRINGS = {0x174344: 'effects', 0x18fce4: 'standard_lighting', 0x18fc3c: 'g_ZWriteEnable', 0x18f948: 'g_AlphaBlendEnable',
           0x18fc5c: 'g_SrcBlend', 0x18fc68: 'g_DestBlend'}


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3FL/X3FL.exe'
    img = w.Image(path, w.FL_SHA256)
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
    print(f'strings: {len(STRINGS)}')
    print(f'total {sum(img.checks.values())} checks, failures {len(img.fails)}')
    for f in img.fails:
        print('FAIL', f)
    return 1 if img.fails else 0


if __name__ == '__main__':
    sys.exit(main())
