#!/usr/bin/env python3
"""FL beam bullet draw: the beam refresh, hit test, per-frame order, scale writers, the draw-side matrix and the batch /
lone admission for the beam bodies (read-only; RVAs, anchors, data).

Owning note: docs/reverse-engineering/fl/beam-bullet-draw.md. AP twins: docs/reverse-engineering/weapon-effect-spawn.md.

  python3 -I verification/results/fl-beam-bullet-draw/fl_beam_bullet_draw.py \
      [--fl /mnt/c/X3FL/X3FL.exe] [--ap /mnt/c/X3AP-orig/X3AP.exe] [--game /mnt/c/X3FL] \
      > verification/results/fl-beam-bullet-draw/fl_beam_bullet_draw_out.txt

Asserts every instruction the note quotes as one whole instruction (Capstone over the fl-weapon-effect-spawn Image
reader, both SHA-256 checked), then the text-body census of the beam bodies the batch census could not parse
(objects/v/00300, 00525, 00526, 00527): the loader's normalised z range (max |coordinate| -> 1.0,
docs/reverse-engineering/body-text-loader.md section 4), the object size (LOD +0x14) and the faces per material.
Addresses are RVAs (VA - 0x400000). No game bytes are written; the output is deterministic.
"""
import argparse
import collections
import importlib.util
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/results/fl-weapon-effect-spawn'))
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from fl_weapon_effect_sites import Image, FL_SHA256, AP_SHA256, BASE  # noqa: E402

FL = [
    # beam A refresh 0x0514f0 (ECX = bullet, plain ret)
    ('beamA', 0x45150d, 'test byte ptr [edx + eax + 0x58], 2'),
    ('beamA', 0x45153a, 'add ecx, 0x10'),                       # ext+0x10 = muzzle offset in the ship frame
    ('beamA', 0x45153e, 'call 0x50e0c0'),                       # node+0x30 = R(ship node +0x40) * ext+0x10
    ('beamA', 0x451555, 'add ecx, 0x20'),                       # ext+0x20 = orientation relative to the ship
    ('beamA', 0x451559, 'call 0x50ebe0'),                       # node+0x40 = ext+0x20 composed with the ship basis
    ('beamA', 0x45158c, 'movups xmmword ptr [edx + 0x30], xmm0'),  # + ship node position
    ('beamA', 0x451593, 'mov eax, dword ptr [eax + 0x70]'),     # node scale (kept)
    ('beamA', 0x45159e, 'call 0x44dcd0'),                       # range = col 11 * col 10 / 1000
    ('beamA', 0x4515ae, 'sar ebx, 1'),                          # L = range / 2
    ('beamA', 0x4515b0, 'test al, 1'),                          # ext+0x74 bit 0: hit this frame
    ('beamA', 0x4515bd, 'movd xmm0, dword ptr [eax + 0x180]'),  # |node+0x180..0x188|
    ('beamA', 0x4515f7, 'cmp dword ptr [eax + 0x188], 0'),      # sign by z
    ('beamA', 0x4515fe, 'mov eax, dword ptr [edi + 0xa4]'),     # previous L
    ('beamA', 0x451615, 'sar ebx, 1'),                          # L = (L_prev + d) / 2
    ('beamA', 0x45161f, 'mov ebx, 1'),
    ('beamA', 0x451660, 'sar eax, 2'),                          # col 10 / 4
    ('beamA', 0x45166d, 'push 0x10'),                           # fade in from 16
    ('beamA', 0x45167f, 'push 1'),                              # fade out to 1
    ('beamA', 0x451685, 'call 0x419fc0'),
    ('beamA', 0x451690, 'mov dword ptr [ecx + 0x13c], eax'),
    ('beamA', 0x45169b, 'mov dword ptr [eax + 0x13c], 0xff'),
    ('beamA', 0x4516a5, 'mov dword ptr [edi + 0xa4], ebx'),
    ('beamA', 0x4516b4, 'mov eax, 0x3333'),                     # flicker x (1 - 0.2 rand)
    ('beamA', 0x4516e7, 'mov dword ptr [esi + 0x13c], eax'),
    ('beamA', 0x4516f1, 'mov ecx, 0x40000000'),
    ('beamA', 0x451709, 'push 0x40000000'),                     # z clamp 16384.0
    ('beamA', 0x451729, 'idiv ecx'),                            # z = (L << 16) / node scale
    ('beamA', 0x45172f, 'push 0x10000'),
    ('beamA', 0x451739, 'call 0x4a3ae0'),                       # axis scales (1, 1, z)
    ('beamA', 0x451743, 'imul dword ptr [ecx + 0x60]'),         # + forward row * L
    ('beamA', 0x451797, 'movups xmmword ptr [ecx + 0x30], xmm0'),
    ('beamA', 0x4517a2, 'movups xmmword ptr [eax + 0xb0], xmm0'),
    ('beamA', 0x4517b0, 'movups xmmword ptr [eax + 0xc0], xmm0'),
    # range 0x04dcd0 (ECX = bullet)
    ('range', 0x44dcf0, 'mov eax, dword ptr [ecx + 0x50]'),
    ('range', 0x44dcf7, 'mov eax, dword ptr [ecx + 0x54]'),
    ('range', 0x44dd04, 'mov ecx, 0x3e8'),
    # axis-scale setter 0x0a3ae0 (thiscall node; x, y, z; ret 0xc) and node-scale setter 0x0a3920 (node; scale; ret 4)
    ('setters', 0x4a3af8, 'mov dword ptr [esi + 0x80], edi'),
    ('setters', 0x4a3b01, 'mov dword ptr [esi + 0x84], ebx'),
    ('setters', 0x4a3b09, 'mov dword ptr [esi + 0x88], ebp'),
    ('setters', 0x4a3965, 'mov dword ptr [edi + 0x70], eax'),
    ('setters', 0x4a3968, 'mov dword ptr [edi + 0x80], 0x10000'),
    ('setters', 0x4a397c, 'mov dword ptr [edi + 0x88], 0x10000'),
    # body bind 0x0a35e0: node scale = LOD 0 +0x14 (the object size)
    ('bind', 0x4a3668, 'mov eax, dword ptr [eax + 0x14]'),
    ('bind', 0x4a366b, 'mov dword ptr [esi + 0x70], eax'),
    ('bind', 0x44a6de, 'mov dword ptr [eax + 0x70], 0x64'),
    # per frame: collision pass 0x066de0 (called at 0x043b42) then the sector update 0x063000 (called at 0x043b76)
    ('frame', 0x443b42, 'call 0x466de0'),
    ('frame', 0x443b76, 'call 0x463000'),
    ('frame', 0x466e8f, 'test byte ptr [ecx + eax + 0x58], 2'),
    ('frame', 0x466ea1, 'call 0x466660'),
    ('frame', 0x4641a2, 'call 0x4514f0'),
    ('frame', 0x4643d9, 'call 0x4517f0'),
    ('frame', 0x46450e, 'mov ecx, 0x7d0'),                      # plain bolt: scale + speed * dt / 2000 ...
    ('frame', 0x464536, 'mov edx, dword ptr [ecx + 0x14]'),     # ... capped at the object size
    ('frame', 0x464549, 'call 0x4a3920'),
    ('frame', 0x464cce, 'mov eax, dword ptr [ecx + eax + 0x90]'),  # node+0x124 = col 26 (sound volume)
    ('frame', 0x464cd5, 'mov dword ptr [edx + 0x124], eax'),
    ('frame', 0x464cf5, 'mov eax, dword ptr [eax + 0x13c]'),
    # hit test 0x066660
    ('hit', 0x466ae9, 'or dword ptr [eax + 0x74], 1'),
    ('hit', 0x466af0, 'mov dword ptr [eax + 0x7c], ebx'),
    ('hit', 0x466af6, 'mov dword ptr [eax + 0x180], 0'),
    ('hit', 0x466b03, 'mov dword ptr [eax + 0x184], 0'),
    ('hit', 0x466b14, 'sub eax, dword ptr [edi + 0xa4]'),
    # fire routine 0x04e310: the live-beam branch
    ('fire', 0x44f521, 'test al, 2'),
    ('fire', 0x44f52a, 'call 0x4514f0'),
    ('fire', 0x44f56d, 'call 0x466660'),
    ('fire', 0x44f57c, 'call 0x4514f0'),
    ('fire', 0x44f586, 'test al, 4'),
    ('fire', 0x44f593, 'call 0x4517f0'),
    ('fire', 0x44faf9, 'call 0x4514f0'),                       # live beam whose offset or aim changed
    ('fire', 0x44fb32, 'call 0x466660'),
    ('fire', 0x44fb3d, 'call 0x4514f0'),
    ('fire', 0x44fb6e, 'call 0x4517f0'),
    # beam B 0x0517f0: root scale / axis, then per chain segment
    ('beamB', 0x4519d1, 'call 0x4a3920'),
    ('beamB', 0x4519f5, 'call 0x4a3ae0'),
    ('beamB', 0x451ea2, 'call 0x4a3920'),
    ('beamB', 0x451ed7, 'call 0x4a3ae0'),
    # continuous 0x2000: hold at the gun, then the charge scale
    ('frame', 0x4642a5, 'call 0x451430'),
    ('frame', 0x464354, 'call 0x4a3920'),
    # batch admission 0x085660 (body 225 excluded; every LOD sub-mesh <= 0x37 triangles)
    ('batch', 0x485668, 'mov ecx, dword ptr [eax + 0x144]'),
    ('batch', 0x48566f, 'cmp ecx, 0xe1'),
    ('batch', 0x4856c0, 'cmp dword ptr [eax], 0x37'),
    # submit 0x0e0b40: "effects" unless body +0x50 & 0x10 and the material carries a named effect
    ('batch', 0x4e0c1f, 'test byte ptr [esi + 0x1c], 0x10'),
    ('batch', 0x4e0c25, 'mov esi, dword ptr [eax + 0x5c]'),
    ('batch', 0x4e0c4b, 'mov ecx, 0x574344'),
    ('batch', 0x4e0c5f, 'mov ecx, 0x574344'),
    ('batch', 0x4e0886, 'mov eax, dword ptr [edi + 0x13c]'),    # instance writer: COLOR0 alpha
    # material submit 0x0e0f30: node alpha -> g_AlphaValue (handle cache +0x188)
    ('lone', 0x4e3cb9, 'push 0x58fdf8'),
    ('lone', 0x4e3cc6, 'mov dword ptr [esi + 0x188], eax'),
    ('lone', 0x4e3c48, 'mov dword ptr [esi + 0x170], eax'),
    ('lone', 0x4e3c72, 'mov dword ptr [esi + 0x178], eax'),
    ('lone', 0x4e3c87, 'mov dword ptr [esi + 0x17c], eax'),
    ('lone', 0x4e4abd, 'mov eax, dword ptr [ecx + 0x13c]'),
    ('lone', 0x4e4ad9, 'mulsd xmm0, qword ptr [ebp - 0xf8]'),
    ('lone', 0x4e4b0e, 'cmp dword ptr [esi + 0x1a4], 0'),
    ('lone', 0x4e4b2c, 'push 5'),
    ('lone', 0x4e4b7d, 'mov ecx, dword ptr [esi + 0x188]'),
    # node sound 0x095930: node+0x124 volume, +0x138 + 4500 distance
    ('sound', 0x495a45, 'movzx ecx, word ptr [edi + 0x124]'),
    ('sound', 0x495a4c, 'mov eax, dword ptr [edi + 0x138]'),
    ('sound', 0x495a52, 'add eax, 0x1194'),
    ('sound', 0x44b173, 'mov dword ptr [eax + 0x138], 0x2710'),
]
AP = [
    ('ap', 0x44800d, 'test byte ptr [eax + ecx + 0x58], 2'),
    ('ap', 0x44809e, 'call 0x444bd0'),
    ('ap', 0x4480ab, 'test byte ptr [ecx + 0x74], 1'),
    ('ap', 0x448172, 'mov dword ptr [eax + 0x13c], 0xff'),
    ('ap', 0x4481f0, 'push 0x40000000'),
    ('ap', 0x4481f9, 'call 0x412450'),
    ('ap', 0x44820f, 'call 0x488270'),
    ('ap', 0x448222, 'call 0x40e780'),
    ('ap', 0x488286, 'mov dword ptr [ecx + 0x80], edi'),
    ('ap', 0x488292, 'mov dword ptr [ecx + 0x88], ebx'),
    ('ap', 0x48812d, 'mov dword ptr [edi + 0x80], eax'),
    ('ap', 0x45cefa, 'or dword ptr [eax + 0x74], 1'),
    ('ap', 0x46ce58, 'cmp dword ptr [esi + 0x140], 0xe1'),
]


def text_bodies(game):
    spec = importlib.util.spec_from_file_location(
        'mfc', ROOT / 'verification/results/fl-muzzle-flash-draw/fl_muzzle_flash_census.py')
    mfc = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mfc)
    a = mfc.mount(game, 'fl')
    for bid in (300, 525, 526, 527):
        data, src = mfc.text(a, 'objects/v/%05d.bod' % bid)
        size = re.search(r'^\s*(\d+)\s*;\s*/\s*Automatic Object Size', data, re.M)
        coords = data.split('-1;  -1;   -1;')[0]
        pts = [tuple(int(v) for v in m) for m in
               re.findall(r'^\s*(-?\d+);\s*(-?\d+);\s*(-?\d+);\s*/\d+', coords, re.M)]
        m = max(max(abs(c) for c in p) for p in pts)
        zs = [p[2] / m for p in pts]
        xy = max(max(abs(p[0]), abs(p[1])) for p in pts) / m
        faces = collections.Counter(int(f[0]) for f in
                                    re.findall(r'^\s*(-?\d+);\s*\d+;\s*\d+;\s*\d+;\s*-?\d+;\s*\d+;', data, re.M))
        mats = re.findall(r'^MATERIAL(\d):\s*(\d+);\s*(\d+);', data, re.M)
        print(f'  objects/v/{bid:05d} ({src}): object size {size.group(1) if size else "?"}; {len(pts)} points, '
              f'max |coord| {m}; normalised z {min(zs):.4f}..{max(zs):.4f}, max |x|,|y| {xy:.4f}; '
              f'faces per material {dict(sorted(faces.items()))}; materials '
              + ', '.join(f'MATERIAL{t} #{i} tex {tex}' for t, i, tex in mats))


def beam_rows(game):
    spec = importlib.util.spec_from_file_location(
        'mfc', ROOT / 'verification/results/fl-muzzle-flash-draw/fl_muzzle_flash_census.py')
    mfc = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mfc)
    rows, src = mfc.type_rows(mfc.mount(game, 'fl'), 'addon2', 'TBullets')
    print(f'TBullets beam rows ({src}): flags col 12; range = col 11 * col 10 / 1000 (0x04dcd0); beam A half-length '
          f'L = range / 2 before a hit; col 18 = beam B segment length unit')
    for i, r in enumerate(rows):
        f = int(r[12])
        if not f & 0x200e:
            continue
        rng = int(r[11]) * int(r[10]) // 1000
        print(f'  row {i:2d} {r[48]:28s} body {r[0]:34s} flags {f:#06x} col10 {r[10]:>5s} ms col11 {r[11]:>8s} '
              f'range {rng:>8d} L {rng // 2:>8d} col18 {r[18]} col26 {r[26]}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--fl', default='/mnt/c/X3FL/X3FL.exe')
    ap.add_argument('--ap', default='/mnt/c/X3AP-orig/X3AP.exe')
    ap.add_argument('--game', default='/mnt/c/X3FL')
    o = ap.parse_args()
    fl, apx = Image(o.fl, FL_SHA256), Image(o.ap, AP_SHA256)
    print(f'X3FL.exe sha256 {fl.sha256}')
    print(f'X3AP.exe sha256 {apx.sha256}')
    for img, rows, rel in ((fl, FL, True), (apx, AP, False)):
        for g, va, t in rows:
            img.at(g, va, t)
            print(f'  {g:8s} {va - BASE if rel else va:#08x}  {t}')
    for img, name in ((fl, 'FL'), (apx, 'AP')):
        n = sum(img.checks.values())
        print(f'{name} anchors {n}, failures {len(img.fails)}')
        for f in img.fails:
            print('  FAIL ' + f)
    beam_rows(o.game)
    print('text bodies (loader normalisation: max |coordinate| -> 1.0):')
    text_bodies(o.game)
    sys.exit(1 if fl.fails or apx.fails else 0)


if __name__ == '__main__':
    main()
