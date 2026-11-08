#!/usr/bin/env python3
"""FL bullet draw batch: the per-batch submit loop, the submit routine's draw, the batch / material / VB records, the
bullet record fields and the per-body census of the FL installation (read-only; RVAs, anchors, data).

Owning note: docs/reverse-engineering/fl/bullet-draw-batch.md. AP twin: docs/reverse-engineering/view-submit-hot-path.md
("The instanced-batch path") and effects-engine-remaining-emission.md ("Bullet vertex buffer writer").

  python3 -I verification/results/fl-bullet-draw-batch/fl_bullet_draw_batch.py \
      [--fl /mnt/c/X3FL/X3FL.exe] [--ap /mnt/c/X3AP-orig/X3AP.exe] [--game /mnt/c/X3FL] \
      > verification/results/fl-bullet-draw-batch/fl_bullet_draw_batch_out.txt

Capstone over tools/analysis/inspect_x3.py's PE32 reader (the fl-weapon-effect-spawn Image class). Prints the batch
submit call site's window (bytes with relocated dwords zeroed, FNV-1a 32, relocations, qword geometry, direct branch
targets, image dwords into the call bytes), asserts every anchor quoted in the note as one whole instruction, decodes
the instancing vertex declaration and the technique / parameter strings from the image, and runs a census of the FL
installation's bullet bodies (LOD 0 tri-groups per texture: faces, vertices per instance, the 0x37-triangle instancing
limit, the first instance's UV-word signature; z extent). Addresses are RVAs (VA - 0x400000). No game bytes are
written; output is deterministic.
"""
import argparse
import collections
import hashlib
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/results/fl-weapon-effect-spawn'))
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'tools/effects'))
from fl_weapon_effect_sites import Image, fnv1a32, FL_SHA256, AP_SHA256, BASE  # noqa: E402

# Whole-instruction anchors: (group, image, preferred VA, `mnemonic op_str`). Every figure the note quotes.
FL_ANCHORS = [
    # view submit 0x0978e0 (thiscall scene; view, layer, flags; ret 0xc): scene kept in EBX and [esp+0x14]
    ('loop', 0x4978ea, 'mov ebx, ecx'),
    ('loop', 0x4978f4, 'mov dword ptr [esp + 0x14], ebx'),
    ('loop', 0x497945, 'call 0x4e0660'),                      # per material: VB record vertex count = 0
    ('loop', 0x497973, 'call 0x485700'),                      # pass 1: eligibility + batch lookup, count += 1
    ('loop', 0x497983, 'call 0x485780'),                      # priority, sort, select
    ('loop', 0x497ad9, 'mov ebx, dword ptr [ebx + 0x40]'),    # the batch loop: EBX = scene+0x40 list
    ('loop', 0x497adc, 'cmp dword ptr [ebx], 0'),
    ('loop', 0x497ae1, 'test byte ptr [ebx + 0x10], 2'),      # collected this call
    ('loop', 0x497ae7, 'mov esi, dword ptr [ebx + 0x30]'),    # ESI = material list
    ('loop', 0x497af0, 'mov ecx, dword ptr [esp + 0x14]'),    # ECX = scene
    ('loop', 0x497af4, 'mov edx, ebx'),                       # EDX = batch
    ('loop', 0x497af6, 'push edi'),                           # view
    ('loop', 0x497af7, 'push esi'),                           # material
    ('loop', 0x497af8, 'call 0x4e0b40'),
    ('loop', 0x497afd, 'mov esi, dword ptr [esi]'),
    ('loop', 0x497aff, 'add esp, 8'),
    ('loop', 0x497b02, 'cmp dword ptr [esi], 0'),
    ('loop', 0x497b05, 'jne 0x497af0'),
    ('loop', 0x497b07, 'mov ebx, dword ptr [ebx]'),
    ('loop', 0x497b0c, 'jne 0x497ae1'),
    ('loop', 0x497b14, 'ret 0xc'),
    # submit 0x0e0b40: ECX scene, EDX batch, [ebp+8] material, [ebp+0xc] view; plain ret, caller pops 8
    ('submit', 0x4e0b60, 'mov eax, ecx'),
    ('submit', 0x4e0b62, 'mov esi, edx'),
    ('submit', 0x4e0b7b, 'mov ebx, dword ptr [ebp + 8]'),
    ('submit', 0x4e0b90, 'mov edi, dword ptr [ebx + 0x18]'),  # EDI = VB record (material+0x18)
    ('submit', 0x4e0bca, 'push 0x2000'),                      # D3DLOCK_DISCARD
    ('submit', 0x4e0bdc, 'push dword ptr [edi + 8]'),         # SizeToLock = VB bytes
    ('submit', 0x4e0be9, 'call dword ptr [ecx + 0x2c]'),      # IDirect3DVertexBuffer9::Lock
    ('submit', 0x4e0bf4, 'mov eax, dword ptr [edi + 0xc]'),   # vertex count
    ('submit', 0x4e0bfa, 'shl eax, 3'),                       # x 24 bytes (lea x3, shl 3)
    ('submit', 0x4e0c04, 'call 0x5213a0'),                    # memcpy from the system copy [edi+4]
    ('submit', 0x4e0c11, 'call dword ptr [ecx + 0x30]'),      # Unlock
    ('submit', 0x4e0c14, 'mov eax, dword ptr [ebx + 0x14]'),  # material pointer
    ('submit', 0x4e0c1f, 'test byte ptr [esi + 0x1c], 0x10'), # batch+0x1c (body+0x50) bit 0x10: material effect
    ('submit', 0x4e0c4b, 'mov ecx, 0x574344'),                # "effects"
    ('submit', 0x4e0c73, 'cmp dword ptr [ebx + 0x10], 0'),    # vertices per instance 0: no draw
    ('submit', 0x4e0cc0, 'test dword ptr [eax + 0x18], 0x20000000'),
    ('submit', 0x4e0ccb, 'push 0x58fc74'),                    # "INSTANCE_BULLETS"
    ('submit', 0x4e0cd2, 'push 0x58fc88'),                    # "INSTANCE"
    ('submit', 0x4e0cd8, 'call dword ptr [eax + 0x34]'),      # GetTechniqueByName
    ('submit', 0x4e0d02, 'call dword ptr [eax + 0xe8]'),      # SetTechnique
    ('submit', 0x4e0d0a, 'push 0x58fa2c'),                    # "t_DiffuseTexture"
    ('submit', 0x4e0d12, 'call dword ptr [eax + 0x24]'),      # GetParameterByName
    ('submit', 0x4e0d15, 'movsx ebx, word ptr [ebx + 8]'),    # material texture id
    ('submit', 0x4e0d2d, 'call 0x5124f0'),                    # texture record
    ('submit', 0x4e0d32, 'test dword ptr [eax + 0x10], 0x2000000'),
    ('submit', 0x4e0d41, 'push dword ptr [eax + 0x34]'),
    ('submit', 0x4e0d44, 'call 0x4da490'),                    # cached effect SetTexture
    ('submit', 0x4e0d4e, 'push 0x58fc94'),                    # "g_mViewProjection"
    ('submit', 0x4e0def, 'call dword ptr [eax + 0x98]'),      # SetMatrix
    ('submit', 0x4e0dfe, 'call dword ptr [eax + 0xfc]'),      # Begin
    ('submit', 0x4e0e09, 'push dword ptr [0x5aaed8]'),        # vertex declaration
    ('submit', 0x4e0e15, 'call dword ptr [ecx + 0x60]'),      # state wrapper: declaration
    ('submit', 0x4e0e1d, 'push 0x18'),                        # stride 24
    ('submit', 0x4e0e21, 'push dword ptr [edi]'),             # IDirect3DVertexBuffer9*
    ('submit', 0x4e0e2b, 'call dword ptr [ecx + 0x64]'),      # state wrapper: stream 0
    ('submit', 0x4e0e3f, 'call dword ptr [ecx + 0x68]'),      # state wrapper: indices 0
    ('submit', 0x4e0e47, 'call dword ptr [eax + 0x100]'),     # BeginPass(0)
    ('submit', 0x4e0e4d, 'mov ebx, dword ptr [ebp - 0x1c]'),  # device
    ('submit', 0x4e0e50, 'mov eax, 0xaaaaaaab'),
    ('submit', 0x4e0e55, 'mul dword ptr [edi + 0xc]'),
    ('submit', 0x4e0e5a, 'shr edx, 1'),                       # count / 3
    ('submit', 0x4e0e5d, 'push 0'),                           # StartVertex
    ('submit', 0x4e0e5f, 'push 4'),                           # D3DPT_TRIANGLELIST
    ('submit', 0x4e0e62, 'call dword ptr [ecx + 0x144]'),     # IDirect3DDevice9::DrawPrimitive
    ('submit', 0x4e0e6b, 'call dword ptr [eax + 0x108]'),     # EndPass
    ('submit', 0x4e0e74, 'call dword ptr [eax + 0x10c]'),     # End
    ('submit', 0x4e0e8f, 'ret'),
    # collection: batch lookup by body id, material by texture id, VB create, vertex append
    ('collect', 0x4857f0, 'sub esp, 0x10'),
    ('collect', 0x485668, 'mov ecx, dword ptr [eax + 0x144]'),
    ('collect', 0x48566f, 'cmp ecx, 0xe1'),                   # body id 0xe1 is never instanced (FL only)
    ('collect', 0x485793, 'cmp edx, 1'),                      # count < 2: priority 0, not selected
    ('collect', 0x485798, 'imul eax, dword ptr [ecx + 0x28], 0x64'),
    ('collect', 0x4857d2, 'cmp ecx, 0x78f0'),                 # vertex budget of the selected batches
    ('collect', 0x4857fb, 'cmp dword ptr [ebx + 0x144], 0'),
    ('collect', 0x48586b, 'test cl, 1'),                      # batch not selected: not taken (traversed)
    ('collect', 0x485873, 'cmp dword ptr [ebp + 0x2c], eax'), # accepted above capacity: not taken
    ('collect', 0x485876, 'ja 0x48584c'),
    ('loop', 0x4979ac, 'call 0x4857f0'),                      # pass 2: collect; nonzero = taken
    ('loop', 0x4979b3, 'jne 0x497aca'),
    ('loop', 0x497aa1, 'call 0x4968c0'),                      # not taken: the ordinary traversal
    ('loop', 0x497ac5, 'call 0x4968c0'),
    ('collect', 0x4e07af, 'mov esi, 0x218'),
    ('collect', 0x4e07b4, 'mov dword ptr [esp + 0x18], 0x208'),
    ('collect', 0x4e08c2, 'call 0x4e8600'),                   # node world matrix (scene+0x2c in XMM2)
    ('collect', 0x4e08bb, 'movss xmm2, dword ptr [esi + 0x2c]'),
    ('collect', 0x4859bd, 'call 0x4e0700'),
    # bullet creation (object factory, class-0 case), fire path, mover
    ('bullet', 0x449b1d, 'mov dword ptr [ebp - 0x20], 0x20800000'),
    ('bullet', 0x449b24, 'mov ecx, dword ptr [edx + eax + 0x94]'),
    ('bullet', 0x449b2b, 'add ecx, dword ptr [edx + eax + 0x50]'),
    ('bullet', 0x449b34, 'add ecx, dword ptr [eax + 0x708]'),
    ('bullet', 0x449b3d, 'mov dword ptr [eax], ecx'),
    ('bullet', 0x449b4d, 'mov dword ptr [ecx + 8], eax'),
    ('bullet', 0x449b64, 'mov dword ptr [esi + 0x40], 0x40004801'),
    ('bullet', 0x449b70, 'movzx edi, word ptr [edx + eax]'),  # TBullets +0: the body id
    ('bullet', 0x449be2, 'call 0x4a35e0'),
    ('bullet', 0x4a365d, 'mov dword ptr [esi + 0x144], edi'),  # node body id
    ('bullet', 0x44a6de, 'mov dword ptr [eax + 0x70], 0x64'),
    ('bullet', 0x44a7b4, 'mov eax, dword ptr [eax + edi + 0x54]'),
    ('bullet', 0x44a7cb, 'mov edx, 0x1999'),
    ('bullet', 0x44a7d3, 'imul edx'),
    ('bullet', 0x44a7d5, 'idiv ecx'),
    ('bullet', 0x44a7da, 'mov dword ptr [ecx + 0x78], eax'),
    ('bullet', 0x44a7f6, 'call 0x498140'),
    ('bullet', 0x44a80d, 'or dword ptr [eax + 0x130], ecx'),
    ('bullet', 0x4982b6, 'or dword ptr [esi + 0x12c], 0x1000000'),
    ('bullet', 0x4982d8, 'mov ebx, dword ptr [eax + 0x5c]'),  # collision pair test reads body+0x5c
    ('bullet', 0x4982e3, 'mov edi, dword ptr [eax + 0x5c]'),
    ('bullet', 0x4a620b, 'test dword ptr [esi + 0x12c], 0x1000000'),
    ('fire', 0x44f738, 'mov edi, dword ptr [ebp - 0x3c]'),
    ('fire', 0x44f891, 'add esi, dword ptr [eax + 0x100]'),
    ('fire', 0x44f8bb, 'add eax, 0x54'),
    ('fire', 0x44f8ce, 'call 0x412c00'),
    ('fire', 0x44f915, 'movups xmmword ptr [eax + 0x100], xmm0'),
    ('fire', 0x44f960, 'mov dword ptr [edi + 0x10], eax'),
    ('fire', 0x44f96b, 'mov ecx, dword ptr [eax + 0x708]'),
    ('fire', 0x44f974, 'add ecx, dword ptr [esi + eax + 0x50]'),
    ('fire', 0x44f985, 'mov dword ptr [eax], ecx'),          # ext+0 expiry = now + col 10
    ('mover', 0x414da1, 'mov eax, dword ptr [eax + 0x704]'),
    ('mover', 0x414db4, 'mov eax, dword ptr [esi + 0x100]'),
    ('mover', 0x414dba, 'add eax, dword ptr [edi + 0x60]'),
    ('mover', 0x414dfe, 'mov ecx, 0x3e8'),
    ('mover', 0x414e03, 'imul edx'),
    ('mover', 0x414e05, 'idiv ecx'),
]
AP_ANCHORS = [
    ('ap', 0x46d08d, 'mov edi, dword ptr [ebp + 0x40]'),
    ('ap', 0x46d095, 'test byte ptr [edi + 0x10], 2'),
    ('ap', 0x46d09b, 'mov esi, dword ptr [edi + 0x30]'),
    ('ap', 0x46d0a7, 'call 0x4bfd40'),
    ('ap', 0x46d0c2, 'ret 8'),
    ('ap', 0x47eb7a, 'call 0x46d080'),
    ('ap', 0x4c007c, 'mov eax, dword ptr [esi + 0x144]'),
    ('ap', 0x4c008a, 'call eax'),
    ('ap', 0x4411fc, 'push 0x1999'),
    ('ap', 0x441202, 'call 0x469a30'),
    ('ap', 0x441229, 'call 0x47f0a0'),
    ('ap', 0x469a46, 'imul edx'),
    ('ap', 0x469a48, 'idiv ecx'),
    ('ap', 0x46ced0, 'cmp ecx, 0x78f0'),
]

SITE = ('batch_submit', 0x497af8, 0x4e0b40, 0x497af0, 0x497b07)   # call, target, window [start, end)
DECL_TYPES = {0: 'FLOAT1', 1: 'FLOAT2', 2: 'FLOAT3', 3: 'FLOAT4', 4: 'D3DCOLOR', 5: 'UBYTE4', 17: 'UNUSED'}
DECL_USAGE = {0: 'POSITION', 3: 'NORMAL', 5: 'TEXCOORD', 10: 'COLOR'}


def cstr(img, va):
    raw = img.read(va, 64)
    return raw.split(b'\0', 1)[0].decode('latin1')


def site_report(img, edges, dwords):
    name, call, target, start, end = SITE
    ins = img.insns(start, end)
    raw = bytearray(img.read(start, end - start))
    relocs = []
    for k in range(len(raw) - 3):
        if start + k in img.fixups:
            relocs.append((k, struct.unpack_from('<I', raw, k)[0] - BASE))
            raw[k:k + 4] = b'\0\0\0\0'
    print(f'site {name}: call {call - BASE:#08x} -> {target - BASE:#08x}, window {start - BASE:#08x}..{end - BASE:#08x}'
          f' ({end - start} bytes)')
    for i in ins:
        print(f'  {i.address - BASE:#08x} {i.bytes.hex():<14} {i.mnemonic} {i.op_str}')
    print(f'  masked bytes {bytes(raw).hex()} fnv1a32 {fnv1a32(bytes(raw)):#010x}')
    print(f'  relocated dwords in window: {relocs or "none"}')
    print(f'  call bytes in one aligned qword: {call // 8 == (call + 4) // 8}; rel32 in one qword: '
          f'{(call + 1) // 8 == (call + 4) // 8}')
    inside = sorted({(s, d) for s, m, d in edges if start <= d < end})
    print(f'  direct branch targets inside the window: '
          + ', '.join(f'{d - BASE:#08x} (from {s - BASE:#08x})' for s, d in inside))
    into_call = [d for s, m, d in edges if call < d < call + 5]
    print(f'  direct branches into the call bytes 1..4: {len(into_call)}')
    print(f'  image dwords naming call bytes 1..4: {sum(dwords.get(call + k, 0) for k in range(1, 5))}')
    callers = sorted(s for s, m, d in edges if m == 'call' and d == target)
    print(f'  E8 callers of {target - BASE:#08x}: {len(callers)} ' + ' '.join(f'{c - BASE:#08x}' for c in callers))
    callers_loop = sorted(s for s, m, d in edges if m == 'call' and d == 0x4978e0)
    print(f'  E8 callers of the view submit 0x0978e0: {len(callers_loop)} ' + ' '.join(f'{c - BASE:#08x}' for c in callers_loop))


def reloc_report(img, start, end, label):
    """Every base-relocated HIGHLOW dword inside [start, end): instruction RVA and the target RVA it names."""
    rows = []
    for i in img.insns(start, end):
        for k in range(i.size - 3):
            if i.address + k in img.fixups:
                rows.append(f'{i.address - BASE:#08x}+{k}->{struct.unpack_from("<I", i.bytes, k)[0] - BASE:#08x}')
    print(f'relocated dwords in {label} {start - BASE:#08x}..{end - BASE:#08x}: {len(rows)}: ' + ' '.join(rows))


def decl_report(img):
    decl_ptr_pushes = [i for i in img.insns(0x4eb053, 0x4eb05d)]
    elements_va = int(decl_ptr_pushes[1].op_str, 16)
    print(f'declaration slot [0x5aaed8] (RVA {0x5aaed8 - BASE:#08x}) <- CreateVertexDeclaration at '
          f'{0x4eb05e - BASE:#08x} with elements at RVA {elements_va - BASE:#08x}:')
    va = elements_va
    for _ in range(16):
        stream, off, typ, method, usage, idx = struct.unpack('<HHBBBB', img.read(va, 8))
        if stream == 0xff:
            print('  END')
            break
        print(f'  stream {stream} offset {off:2d} {DECL_TYPES.get(typ, typ)} method {method} '
              f'{DECL_USAGE.get(usage, usage)}{idx}')
        va += 8
    for va, what in ((0x58fc74, 'technique'), (0x58fc88, 'technique'), (0x58fa2c, 'parameter'),
                     (0x58fc94, 'parameter'), (0x574344, 'effect')):
        print(f'string RVA {va - BASE:#08x} ({what}): {cstr(img, va)!r}')


def body_census(game):
    import bob1
    import engine_bodies
    import weapon_effects_census as wec
    assets, _ = engine_bodies.load_assets(pathlib.Path(game), False, 'fl')
    result = wec.census(assets)
    weapons = result['weapons']
    bodies = collections.OrderedDict()
    for w in weapons:
        bodies.setdefault(w['body_key'], []).append(w['index'])
    print(f'FL bullets: {len(weapons)} rows, {len(bodies)} distinct bodies (layers {list(assets.layers)})')
    out = {}
    for key, rows in bodies.items():
        summ = result['bodies'].get(key) or {}
        try:
            entry = bob1.resolve_body(assets, summ.get('name') or key)
            tree = bob1.parse(assets.read_entry(entry))
        except Exception as err:  # text MATERIAL3 bodies and absent v\NNNNN bodies: geometry not modelled here
            print(f'  {key:<44} rows {rows} UNPARSED ({type(err).__name__}) textures {summ.get("textures")}')
            continue
        lod = bob1.lods(tree)[0]
        mats = bob1.materials(tree)
        points = lod['points']

        def tex_of(mi):
            m = mats[mi] if 0 <= mi < len(mats) else None
            if m is None:
                return f'material#{mi}'
            s = wec.material_summary(m)
            if s.get('kind') == 'effect':
                d = [v for k, v in (s.get('textures') or {}).items() if 'diffuse' in k.lower()]
                return (d[0] if d else 'effect:?').lower()
            t = s.get('texture')
            return (f'id:{t}' if isinstance(t, int) else (t or '?')).lower()

        def uv(index):
            flags, *fields = points[index]
            if not flags & 2:
                return None
            base = 3 if flags & 1 else 0
            return (fields[base], fields[base + 1])
        per_tex = collections.OrderedDict()
        max_tris = 0
        for part in lod['parts']:
            for g in part['groups']:
                t = tex_of(g['material'])
                rec = per_tex.setdefault(t, dict(faces=0, uv=[]))
                rec['faces'] += len(g['faces'])
                max_tris = max(max_tris, len(g['faces']))
                rec['uv'] += [uv(f[k]) for f in g['faces'] for k in range(3)]
        zs = [p[3] for p in points if p[0] & 1]
        zlen = (max(zs) - min(zs)) / 65536.0 if zs else None
        print(f'  {key:<44} rows {rows} member {entry["source"]}:{entry["path"]} lods {len(bob1.lods(tree))} '
              f'lod0_value {lod.get("value")} lod0 faces '
              f'{sum(r["faces"] for r in per_tex.values())} max_tris_per_group {max_tris} instanced '
              f'{"yes" if max_tris <= 0x37 else "no"} z_extent {zlen:.4f} z_range '
              f'{min(zs) / 65536.0:.4f}..{max(zs) / 65536.0:.4f}')
        used = sorted({g['material'] for part in lod['parts'] for g in part['groups']})
        for mi in used:
            if 0 <= mi < len(mats):
                s = wec.material_summary(mats[mi])
                print(f'      material {mi}: flags {s.get("flags")} kind {s.get("kind")} effect {s.get("effect")} '
                      f'technique {s.get("technique")} blend {s.get("blend")} zwrite {s.get("zwrite")}')
        for t, r in per_tex.items():
            sig = hashlib.sha256(repr(r['uv']).encode()).hexdigest()[:12]
            print(f'      texture {t:<52} vertices_per_instance {3 * r["faces"]:4d} uv_signature {sig}')
            out.setdefault((t, 3 * r['faces']), []).append((key, sig))
    by_tex = collections.defaultdict(list)
    for (t, n), lst in out.items():
        by_tex[t].append((n, lst))
    print('textures shared by more than one bullet body:')
    shared = 0
    for t, items in sorted(by_tex.items()):
        names = sorted({k for n, lst in items for k, s in lst})
        if len(names) > 1:
            shared += 1
            print(f'  {t}: ' + '; '.join(f'{k} ({n} v, uv {s})' for n, lst in items for k, s in lst))
    print(f'  {shared} shared textures')
    print('(texture, vertices per instance) pairs held by more than one body:')
    dup = 0
    for (t, n), lst in sorted(out.items()):
        if len({k for k, s in lst}) > 1:
            dup += 1
            print(f'  {t} {n}: ' + ', '.join(f'{k} uv {s}' for k, s in lst))
    print(f'  {dup} ambiguous pairs')
    ire = [w for w in weapons if w['body_key'] == weapons[0]['body_key']]
    print(f'IRE body {weapons[0]["body_key"]}: rows ' + ', '.join(f'{w["index"]} {w["weapon_id"]}' for w in ire))
    for w in ire:
        print(f'  row {w["index"]}: speed col11 {w["speed"]} lifetime col10 {w["lifetime_ms"]} ms size cols16/17 '
              f'{w["size"]} flags {w["flag_word"]} col24 {w["col24_not_effect"]}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fl', default='/mnt/c/X3FL/X3FL.exe')
    ap.add_argument('--ap', default='/mnt/c/X3AP-orig/X3AP.exe')
    ap.add_argument('--game', default='/mnt/c/X3FL')
    args = ap.parse_args()
    fl, apx = Image(args.fl, FL_SHA256), Image(args.ap, AP_SHA256)
    print(f'FL {args.fl} sha256 {fl.sha256}')
    print(f'AP {args.ap} sha256 {apx.sha256}')
    edges = fl.direct_edges()
    dwords = fl.dword_index()
    print(f'FL direct edges {len(edges)}')
    site_report(fl, edges, dwords)
    reloc_report(fl, 0x497ad9, 0x497b17, 'the batch loop')
    reloc_report(fl, 0x4e0b40, 0x4e0ea3, 'the submit routine')
    decl_report(fl)
    for group, va, text in FL_ANCHORS:
        fl.at(group, va, text)
    for group, va, text in AP_ANCHORS:
        apx.at(group, va, text)
    print('anchors FL ' + ' '.join(f'{g}={n}' for g, n in sorted(fl.checks.items())) + f' total {sum(fl.checks.values())}'
          f'; AP {sum(apx.checks.values())}; failures {len(fl.fails) + len(apx.fails)}')
    for f in fl.fails + apx.fails:
        print('  FAIL ' + f)
    body_census(args.game)
    return 1 if fl.fails or apx.fails else 0


if __name__ == '__main__':
    sys.exit(main())
