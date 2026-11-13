#!/usr/bin/env python3
"""AP bullet batch site: the twin of the FL batch scope stub's call (FL 0x097af8 -> 0x0e0b40) in X3AP.exe, the AP
node body id (+0x140) at the batch and at the material-submit scope, the INSTANCE_BULLETS / lone / standard_lighting
program hashes of the AP catalogue views, and the AP TBullets / muzzle body census (read-only; VAs, anchors, data).

Owning note: docs/reverse-engineering/ap-bullet-batch-site.md. FL contract: docs/reverse-engineering/fl/bullet-draw-batch.md.

  python3 -I verification/results/ap-bullet-batch-site/ap_bullet_batch_site.py \
      [--ap /mnt/c/X3/X3AP.exe] [--fl /mnt/c/X3FL/X3FL.exe] [--game /mnt/c/X3] [--flgame /mnt/c/X3FL] \
      > verification/results/ap-bullet-batch-site/ap_bullet_batch_site_out.txt

Capstone over the fl-weapon-effect-spawn Image reader (both SHA-256 checked). Asserts every instruction the note quotes
as one whole instruction, prints the AP and FL call-site windows (bytes, FNV-1a 32, relocations, qword geometry, direct
branch targets, image dwords into the call bytes, callers), checks that the AP submit reads no register argument, then
scans the shader/*.fb entries of three views (AP stock = 01..13.cat + addon/01..04.cat, AP Mayhem = every mounted
catalogue of --game, FL = root + addon2 of --flgame) for the program hashes (the proxy's FNV-1a 64), and runs the
TBullets and muzzle census of the two AP views with the FL readers. Exit 1 on any failed assertion. Prints derived
facts only; no game bytes are written; the output is deterministic.
"""
import argparse
import collections
import importlib.util
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/results/fl-weapon-effect-spawn'))
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from fl_weapon_effect_sites import Image, fnv1a32, FL_SHA256, AP_SHA256, BASE  # noqa: E402

AP = [
    # flush 0x0046d080 (stdcall scene, view; ret 8): EBP = scene, EBX = view, EDI = batch, ESI = material
    ('flush', 0x46d080, 'push ecx'),
    ('flush', 0x46d082, 'mov ebx, dword ptr [esp + 0x10]'),   # view (arg 2)
    ('flush', 0x46d087, 'mov ebp, dword ptr [esp + 0x10]'),   # scene (arg 1)
    ('flush', 0x46d08d, 'mov edi, dword ptr [ebp + 0x40]'),   # EDI = batch (scene+0x40 list)
    ('flush', 0x46d090, 'cmp dword ptr [edi], 0'),
    ('flush', 0x46d095, 'test byte ptr [edi + 0x10], 2'),     # collected in this call
    ('flush', 0x46d09b, 'mov esi, dword ptr [edi + 0x30]'),   # ESI = material list
    ('flush', 0x46d09e, 'cmp dword ptr [esi], 0'),
    ('flush', 0x46d0a3, 'push ebx'),                          # view
    ('flush', 0x46d0a4, 'push esi'),                          # material
    ('flush', 0x46d0a5, 'push edi'),                          # batch
    ('flush', 0x46d0a6, 'push ebp'),                          # scene
    ('flush', 0x46d0a7, 'call 0x4bfd40'),
    ('flush', 0x46d0ac, 'mov esi, dword ptr [esi]'),          # next material
    ('flush', 0x46d0ae, 'add esp, 0x10'),                     # the caller pops four words
    ('flush', 0x46d0b1, 'cmp dword ptr [esi], 0'),
    ('flush', 0x46d0b4, 'jne 0x46d0a3'),
    ('flush', 0x46d0b6, 'mov edi, dword ptr [edi]'),          # next batch
    ('flush', 0x46d0bb, 'jne 0x46d095'),
    ('flush', 0x46d0bd, 'pop edi'),
    ('flush', 0x46d0c1, 'pop ecx'),
    ('flush', 0x46d0c2, 'ret 8'),
    ('flush', 0x47eb7a, 'call 0x46d080'),                     # end of the view submit 0x0047e920
    ('flush', 0x47eb7f, 'pop esi'),
    ('flush', 0x47eb83, 'ret 0xc'),
    # submit 0x004bfd40 (cdecl scene, batch, material, view; plain ret, EAX 1 / 0; no SEH frame)
    ('submit', 0x4bfd40, 'sub esp, 0x11c'),
    ('submit', 0x4bfd4b, 'cmp dword ptr [esp + 0x12c], esi'), # arg 1 scene != 0
    ('submit', 0x4bfd59, 'cmp dword ptr [esp + 0x134], esi'), # arg 2 batch != 0
    ('submit', 0x4bfd66, 'mov ebp, dword ptr [esp + 0x138]'), # arg 3 material
    ('submit', 0x4bfd75, 'cmp dword ptr [esp + 0x13c], esi'), # arg 4 view != 0
    ('submit', 0x4bfd82, 'mov ebx, dword ptr [ebp + 0x18]'),  # VB record = material+0x18
    ('submit', 0x4bfdb0, 'mov eax, dword ptr [0x608b3c]'),    # first register write after the argument checks
    ('submit', 0x4bfdba, 'push 0x2000'),                      # D3DLOCK_DISCARD
    ('submit', 0x4bfdd9, 'call eax'),                         # VB Lock
    ('submit', 0x4bfdf9, 'call 0x515810'),                    # memcpy
    ('submit', 0x4bfe09, 'call edx'),                         # Unlock
    ('submit', 0x4bfe16, 'mov eax, dword ptr [esp + 0x134]'), # batch
    ('submit', 0x4bfe1d, 'test byte ptr [eax + 0x1c], 0x10'), # batch+0x1c bit 0x10: the material's own effect
    ('submit', 0x4bfe53, 'push 0x55608c'),                    # "effects"
    ('submit', 0x4bfe8f, 'fld dword ptr [0x5655cc]'),         # x87 in the submit (balanced fld / fstp)
    ('submit', 0x4bfe95, 'fstp dword ptr [eax + 0x28]'),
    ('submit', 0x4bfed2, 'mov eax, dword ptr [esp + 0x134]'),
    ('submit', 0x4bfed9, 'test dword ptr [eax + 0x18], 0x20000000'),
    ('submit', 0x4bfee8, 'push 0x5633dc'),                    # "INSTANCE_BULLETS"
    ('submit', 0x4bfef8, 'push 0x5633f0'),                    # "INSTANCE"
    ('submit', 0x4bff31, 'call eax'),                         # SetTechnique
    ('submit', 0x4bffcb, 'rep movsd dword ptr es:[edi], dword ptr [esi]'),  # string op: DF must be clear
    ('submit', 0x4c0016, 'call eax'),                         # Begin
    ('submit', 0x4c006c, 'call edx'),                         # BeginPass(0)
    ('submit', 0x4c0079, 'mul dword ptr [ebx + 0xc]'),
    ('submit', 0x4c007c, 'mov eax, dword ptr [esi + 0x144]'), # device vtable +0x144: DrawPrimitive
    ('submit', 0x4c0087, 'push 4'),                           # D3DPT_TRIANGLELIST
    ('submit', 0x4c008a, 'call eax'),
    ('submit', 0x4c00a4, 'mov eax, 1'),
    ('submit', 0x4c00b3, 'ret'),
    ('submit', 0x4c00b7, 'xor eax, eax'),
    ('submit', 0x4c00c0, 'ret'),
    # batch lookup 0x0046ca80 (EAX scene, [esp+4] node; ret 4): by batch +0x0c == node +0x140, creates one per body
    ('batch', 0x46ca8d, 'mov eax, dword ptr [edi + 0x40]'),
    ('batch', 0x46ca96, 'mov edx, dword ptr [ebp + 0x140]'),
    ('batch', 0x46caa0, 'cmp dword ptr [eax + 0xc], edx'),
    ('batch', 0x46caad, 'mov ebx, 0x3c'),                     # batch record size
    ('batch', 0x46cb35, 'mov eax, dword ptr [ebp + 0x140]'),
    ('batch', 0x46cb3b, 'mov dword ptr [esi + 0xc], eax'),    # batch +0x0c = node +0x140
    ('batch', 0x46cb3e, 'mov ecx, dword ptr [ebp + 0x12c]'),
    ('batch', 0x46cb44, 'mov dword ptr [esi + 0x14], ecx'),
    ('batch', 0x46cb47, 'mov edx, dword ptr [ebp + 0x130]'),
    ('batch', 0x46cb50, 'mov dword ptr [esi + 0x18], edx'),
    ('batch', 0x46cb86, 'mov eax, dword ptr [edi + 0x50]'),
    ('batch', 0x46cb89, 'mov dword ptr [esi + 0x1c], eax'),   # batch +0x1c = body +0x50
    ('batch', 0x46ce58, 'cmp dword ptr [esi + 0x140], 0xe1'), # admission 0x0046ce20
    ('batch', 0x46ced0, 'cmp ecx, 0x78f0'),
    ('batch', 0x46cf02, 'cmp dword ptr [edi + 0x140], ebx'),  # pass 2 0x0046cef0
    ('batch', 0x46cf15, 'call 0x46ce20'),
    ('batch', 0x46cf27, 'call 0x46ca80'),
    ('batch', 0x46cf5f, 'mov dword ptr [esi + 0x20], 0x200'),
    ('batch', 0x46cf76, 'add eax, 0x400'),
    # bullet creation (object factory 0x0043ffa0, class 0) and the body bind 0x00487e30 (EAX node, [esp+4] body id)
    ('bind', 0x44018e, 'mov dword ptr [ebp + 0x40], 0x40004801'),
    ('bind', 0x440195, 'mov ecx, dword ptr [0x606fb8]'),      # TBullets table
    ('bind', 0x44014e, 'imul eax, eax, 0xdb8'),
    ('bind', 0x44019b, 'movzx edx, word ptr [eax + ecx]'),    # TBullets +0: the body id
    ('bind', 0x4401a6, 'mov dword ptr [esp + 0x10], edx'),
    ('bind', 0x4401b6, 'jmp 0x4410aa'),
    ('bind', 0x4410aa, 'mov ax, word ptr [esp + 0x160]'),     # the class
    ('bind', 0x4410f0, 'cmp ax, bx'),                         # class 0
    ('bind', 0x4410f9, 'call 0x486d10'),                      # node alloc
    ('bind', 0x4410fe, 'movsx ecx, word ptr [esp + 0x10]'),
    ('bind', 0x441107, 'call 0x487e30'),
    ('bind', 0x441114, 'mov dword ptr [eax + 0x70], 0x64'),
    ('bind', 0x487e31, 'mov ebx, dword ptr [esp + 8]'),       # body id
    ('bind', 0x487e37, 'mov esi, eax'),                       # node
    ('bind', 0x487e8d, 'mov dword ptr [esi + 0x140], ebx'),   # node +0x140 = body id
    ('bind', 0x487e98, 'mov edx, dword ptr [ecx + 0x14]'),
    ('bind', 0x487e9b, 'mov dword ptr [esi + 0x70], edx'),    # node scale = LOD 0 +0x14
    # material-submit scope: 0x004c4fc0 reads its node argument's +0x140, passes it as arg 2 of 0x004c0150
    ('scope', 0x4c4fda, 'mov esi, ecx'),
    ('scope', 0x4c5004, 'mov eax, dword ptr [esp + 0x40]'),
    ('scope', 0x4c5008, 'mov eax, dword ptr [eax + 0x140]'),
    ('scope', 0x4c5013, 'call 0x4863c0'),                     # body by id
    ('scope', 0x4c51c5, 'mov edx, dword ptr [esp + 0x40]'),
    ('scope', 0x4c51c9, 'cmp dword ptr [edx + 0x140], esi'),
    ('scope', 0x4c5206, 'mov ecx, dword ptr [esp + 0x40]'),
    ('scope', 0x4c5213, 'push ecx'),
    ('scope', 0x4c5217, 'mov eax, dword ptr [esp + 0x40]'),
    ('scope', 0x4c5226, 'push eax'),
    ('scope', 0x4c5228, 'call 0x4c0150'),                     # object_trace's claimed call: args[1] = node
    ('scope', 0x4c522d, 'add esp, 0x18'),
]
SITES = {  # name: (image, call VA, target VA, window [start, end), view submit VA)
    'ap': (0x46d0a7, 0x4bfd40, 0x46d0a3, 0x46d0b6, 0x47e920, 0x46d080),
    'fl': (0x497af8, 0x4e0b40, 0x497af0, 0x497b07, 0x4978e0, None),
}
FL_CHECK = [('fl', 0x497af0, 'mov ecx, dword ptr [esp + 0x14]'), ('fl', 0x497af4, 'mov edx, ebx'),
            ('fl', 0x497af8, 'call 0x4e0b40'), ('fl', 0x497aff, 'add esp, 8'), ('fl', 0x4a365d, 'mov dword ptr [esi + 0x144], edi')]
PROGRAMS = collections.OrderedDict([
    ('ec1f5c4a2f4e1445', 'INSTANCE_BULLETS PS (bullet_programs[0])'),
    ('d4a26efb7c603931', 'INSTANCE_BULLETS PS (bullet_programs[1])'),
    ('84d3de8887c963c5', 'INSTANCE_BULLETS PS (bullet_programs[2])'),
    ('5e484a06672e28fb', 'INSTANCE_BULLETS VS (effects)'),
    ('21a2c13be7f989c3', 'INSTANCE_BULLETS VS (effects_0000/1)'),
    ('1b6863a088a177af', 'INSTANCE_BULLETS VS (engine_0000/1)'),
    ('d5e1c75351ed3f04', 'lone VS (effects DEFAULT)'),
    ('89193868c61c3846', 'lone VS (effects INSTANCE)'),
    ('8360f422de08b5bd', 'effects DEFAULT / INSTANCE PS'),
    ('494fe349b8bc12ec', 'standard_lighting DEFAULT VS (muzzle_reshade vertex_shader)'),
    ('ced7edef48059e84', 'standard_lighting DEFAULT PS (FL)'),
    ('0d5848c8489b6630', 'standard_lighting2s DEFAULT PS (FL)'),
    ('7c83ed50c9894e44', 'standard_lighting DEFAULT PS (AP)'),
    ('e70adc744a38ca59', 'standard_lighting2s DEFAULT PS (AP)'),
])


def site_report(img, key, edges, dwords):
    call, target, start, end, view_submit, flush = SITES[key]
    rel = (lambda v: v - BASE) if key == 'fl' else (lambda v: v)
    ins = img.insns(start, end)
    raw = bytearray(img.read(start, end - start))
    relocs = [k for k in range(len(raw) - 3) if start + k in img.fixups]
    for k in relocs:
        raw[k:k + 4] = b'\0\0\0\0'
    print(f'site {key}: call {rel(call):#010x} -> {rel(target):#010x}, window {rel(start):#010x}..{rel(end):#010x} '
          f'({end - start} bytes), call offset {call - start}, return {rel(call + 5):#010x}')
    for i in ins:
        print(f'  {rel(i.address):#010x} {i.bytes.hex():<14} {i.mnemonic} {i.op_str}')
    print(f'  bytes {bytes(raw).hex()} fnv1a32 {fnv1a32(bytes(raw)):#010x}')
    print(f'  relocated dwords in window: {relocs or "none"}')
    print(f'  call bytes in one aligned qword: {call // 8 == (call + 4) // 8} (call & 7 = {call & 7})')
    inside = sorted({(s, d) for s, m, d in edges if start <= d < end})
    print('  direct branch targets inside the window: '
          + (', '.join(f'{rel(d):#010x} (from {rel(s):#010x})' for s, d in inside) or 'none'))
    print(f'  direct branches into the call bytes 1..4: {sum(1 for s, m, d in edges if call < d < call + 5)}')
    print(f'  image dwords naming call bytes 1..4: {sum(dwords.get(call + k, 0) for k in range(1, 5))}')
    for name, va in (('target', target), ('view submit', view_submit), ('flush', flush)):
        if va is None:
            continue
        callers = sorted(s for s, m, d in edges if m == 'call' and d == va)
        print(f'  E8 callers of the {name} {rel(va):#010x}: {len(callers)} ' + ' '.join(f'{rel(c):#010x}' for c in callers))
        if name == 'view submit':
            for c in callers:
                nxt = next(img.md.disasm(img.read(c + 5, 16), c + 5))
                print(f'    after {rel(c):#010x}: {nxt.mnemonic} {nxt.op_str}')
    return fnv1a32(bytes(raw)), bytes(raw), relocs, call // 8 == (call + 4) // 8


def register_inputs(img):
    """The AP submit's instructions before its first register write must not name EAX, ECX or EDX."""
    bad = []
    for i in img.insns(0x4bfd40, 0x4bfdb0):
        text = f'{i.mnemonic} {i.op_str}'
        if any(r in text for r in ('eax', 'ecx', 'edx', 'ax', 'cx', 'dx', 'al', 'cl', 'dl')):
            bad.append(f'{i.address:#010x} {text}')
    print(f'AP submit 0x004bfd40..0x004bfdb0: instructions naming EAX/ECX/EDX before the first write: {len(bad)}'
          + (' ' + '; '.join(bad) if bad else ''))
    seh = any('fs:' in i.op_str for i in img.insns(0x4bfd40, 0x4c00c1))
    print(f'AP submit 0x004bfd40..0x004c00c1: fs: access (SEH frame): {seh}')
    return not bad and not seh


def program_scan(root, cats):
    from effect_passes import read_catalogue, parse_container, VERTEX_SHADER_STATE, PIXEL_SHADER_STATE
    from shader_coverage_offline import _program
    chosen = {}
    for c in cats:
        cat = root / c
        for e in read_catalogue(cat):
            if e['path'].startswith('shader/') and e['path'].endswith('.fb'):
                chosen[e['path']] = (cat, e)
    hits = collections.defaultdict(set)
    sl = set()
    for path, (cat, e) in sorted(chosen.items()):
        with cat.with_suffix('.dat').open('rb') as s:
            s.seek(e['offset'])
            data = bytes(v ^ 0x33 for v in s.read(e['size']))
        cont = parse_container(data)
        for ti, (tech, tps) in enumerate(cont['techniques']):
            for pi, (_, states) in enumerate(tps):
                pair = {}
                for si, st in enumerate(states):
                    if st[0] not in (VERTEX_SHADER_STATE, PIXEL_SHADER_STATE):
                        continue
                    p = _program(data, cont, ti, pi, si)
                    if not p:
                        continue
                    pair['vs' if st[0] == VERTEX_SHADER_STATE else 'ps'] = (p[0], p[1])
                    if p[0] in PROGRAMS:
                        hits[p[0]].add((path.split('/')[1], pathlib.Path(path).stem, tech,
                                        str(cat.relative_to(root)), p[1]))
                stem = pathlib.Path(path).stem
                if '/3_0/' in path and stem in ('standard_lighting', 'standard_lighting2s') and tech == 'DEFAULT':
                    sl.add((stem, pair.get('vs'), pair.get('ps')))
    return len(chosen), hits, sorted(sl, key=str)


def shaders(game, flgame):
    from catalogue_mounts import mounted_catalogues
    from sector_fog_census import STOCK_AP_CATALOGUES
    views = [('ap_stock', pathlib.Path(game), list(STOCK_AP_CATALOGUES)),
             ('ap_mayhem', pathlib.Path(game), [str(p.relative_to(game)) for p in mounted_catalogues(game, 'ap')]),
             ('fl', pathlib.Path(flgame), [str(p.relative_to(flgame)) for p in mounted_catalogues(flgame, 'fl')])]
    found = {}
    for name, root, cats in views:
        n, hits, sl = program_scan(root, cats)
        print(f'programs {name}: {len(cats)} catalogues, {n} shader/*.fb entries (last mount wins)')
        for h, what in PROGRAMS.items():
            s = hits.get(h, set())
            dirs = sorted({x[0] for x in s})
            techs = sorted({x[2] for x in s})
            stems = sorted({x[1] for x in s})
            src = sorted({x[3] for x in s})
            dw = sorted({x[4] for x in s})
            found[(name, h)] = len(s)
            print(f'  {h} {what}: {len(s)} (dir, effect, technique) rows, dwords {dw}, dirs {",".join(dirs) or "-"}, '
                  f'techniques {",".join(techs) or "-"}, {len(stems)} effects, from {",".join(src) or "-"}')
        for stem, vs, ps in sl:
            print(f'  3_0 {stem} DEFAULT: VS {vs[0]} ({vs[1]} dwords) PS {ps[0]} ({ps[1]} dwords)')
    return found


def census(game, flgame):
    spec = importlib.util.spec_from_file_location(
        'mfc', ROOT / 'verification/results/fl-muzzle-flash-draw/fl_muzzle_flash_census.py')
    mfc = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mfc)
    from sector_fog_census import Assets, STOCK_AP_CATALOGUES
    sys.path.insert(0, str(ROOT / 'tools/effects'))
    import weapon_effects_census as wec
    out = {}
    for name, make in (('ap_stock', lambda: Assets(pathlib.Path(game), catalogues=STOCK_AP_CATALOGUES)),
                       ('ap_mayhem', lambda: mfc.mount(game, 'ap'))):
        a = make()
        res = wec.census(a)
        kinds = collections.Counter()
        classic = []
        for w in res['weapons']:
            b = res['bodies'].get(w['body_key']) or {}
            ks = sorted({str(m.get('kind')) for m in b.get('materials', [])}) or ['unresolved']
            k = '+'.join(ks)
            kinds[k] += 1
            if k != 'effect':
                classic.append(f"{w['index']}:{w['weapon_id'].replace('SS_BULLET_', '')}:{w['body']}:{k}:"
                               f"{'beam' if w['flag_word'] & 0x200e else 'bolt'}")
        t = res['tables']['TBullets']
        print(f'census {name}: TBullets {t["source"]}:{t["member"]} {t["sha256"]}, {len(res["weapons"])} rows, '
              f'{len({w["body_key"] for w in res["weapons"]})} distinct bodies; body material kinds {dict(sorted(kinds.items()))}')
        print(f'  non-effect rows ({len(classic)}): ' + ', '.join(classic))
        tb, _ = mfc.type_rows(a, 'addon', 'TBullets')
        eff, esrc = mfc.effects(a, 'addon')
        rows = collections.OrderedDict()
        for i, r in enumerate(tb):
            if int(r[21]):
                rows.setdefault(int(r[21]), []).append(i)
        scenes = collections.defaultdict(set)
        missing = []
        for rid, users in rows.items():
            row = eff.get(rid)
            if row is None:
                missing.append(f'{rid} (rows {users})')
                continue
            for e in row['elements']:
                if 'EEDF_CUT' in e['eedf']:
                    scenes[e['body']].add(rid)
        print(f'  Effects {esrc}: {len(rows)} muzzle rows, missing {missing or "none"}')
        bodies = set()
        for sid in sorted(scenes):
            parts, nkeys, src = mfc.scene_bodies(a, sid)
            uniq = sorted(set(parts)) if parts else None
            bodies.update(uniq or ())
            print(f'  scene {sid:05d} ({src}): part bodies {uniq}, muzzle rows {sorted(scenes[sid])}')
        for b in sorted(bodies):
            info = mfc.body(a, b)
            data, src = mfc.text(a, 'objects/v/%05d.bod' % b)
            mat = next((l.strip().split(':')[0] + ':' + l.strip().split(':')[1].split(';')[3]
                        for l in (data or '').splitlines() if l.strip().startswith('MATERIAL6')), None)
            mats = []
            for typ, idx, tex in (info or {}).get('materials', []):
                _, flags, _ = mfc.materials_row(a, 'addon', tex)
                mats.append(f'MATERIAL{typ} tex {tex} {flags}')
            print(f'    body {b:05d} ({src}): ' + ('; '.join(mats) or (mat or 'no material decoded')))
        if name == 'ap_mayhem':
            stock = {w['body_key'] for w in out['ap_stock'][3]}
            new_rows = [w['index'] for w in res['weapons'] if w['body_key'] not in stock]
            new_bodies = {w['body_key'] for w in res['weapons'] if w['body_key'] not in stock}
            srcs = collections.Counter((res['bodies'].get(w['body_key']) or {}).get('resolved', '?').split(':')[0]
                                       for w in res['weapons'])
            print(f'  rows naming a body absent from the stock TBullets: {len(new_rows)} ({len(new_bodies)} distinct '
                  f'bodies); body files by catalogue {dict(sorted(srcs.items()))}')
        out[name] = (len(res['weapons']), classic, sorted(bodies), res['weapons'])
    fl = wec.census(Assets(pathlib.Path(flgame), edition='fl'))['weapons']
    keys = ('weapon_id', 'body', 'flag_word', 'muzzle_effect', 'flight_effect', 'impact_effect', 'speed', 'lifetime_ms')
    diff = [f"{a['index']}:{k} ap {a[k]} fl {b[k]}" for a, b in zip(out['ap_stock'][3], fl) for k in keys if a[k] != b[k]]
    print(f'census ap_stock vs fl TBullets ({len(out["ap_stock"][3])} / {len(fl)} rows; columns {", ".join(keys)}): '
          f'{len(diff)} differences: ' + ('; '.join(diff) or 'none'))
    out['fl_rows'] = len(fl)
    out['fl_diff'] = diff
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--ap', default='/mnt/c/X3/X3AP.exe')
    ap.add_argument('--fl', default='/mnt/c/X3FL/X3FL.exe')
    ap.add_argument('--game', default='/mnt/c/X3')
    ap.add_argument('--flgame', default='/mnt/c/X3FL')
    o = ap.parse_args()
    apx, fl = Image(o.ap, AP_SHA256), Image(o.fl, FL_SHA256)
    print(f'X3AP.exe sha256 {apx.sha256}')
    print(f'X3FL.exe sha256 {fl.sha256}')
    for g, va, t in AP:
        apx.at(g, va, t)
    for g, va, t in FL_CHECK:
        fl.at(g, va, t)
    fails = list(apx.fails) + list(fl.fails)
    print(f'AP anchors {sum(apx.checks.values())} ({dict(sorted(apx.checks.items()))}), FL anchors '
          f'{sum(fl.checks.values())}, failures {len(fails)}')
    for f in fails:
        print('  FAIL ' + f)
    ap_fnv, ap_bytes, ap_rel, ap_q = site_report(apx, 'ap', apx.direct_edges(), apx.dword_index())
    fl_fnv, _, fl_rel, fl_q = site_report(fl, 'fl', fl.direct_edges(), fl.dword_index())
    checks = [('FL window fnv 0xf86f890b (weapon_effects_sites_fl.h)', fl_fnv == 0xf86f890b),
              ('FL window has no relocation', not fl_rel), ('FL call in one qword', fl_q),
              ('AP window has no relocation', not ap_rel), ('AP call straddles a qword', not ap_q),
              ('AP submit takes no register argument and has no SEH frame', register_inputs(apx))]
    print('AP window as a C array: {' + ', '.join(f'0x{b:02x}' for b in ap_bytes) + '}')
    found = shaders(o.game, o.flgame)
    for h in ('ec1f5c4a2f4e1445', 'd4a26efb7c603931', '84d3de8887c963c5', 'd5e1c75351ed3f04', '89193868c61c3846',
              '494fe349b8bc12ec'):
        checks.append((f'{h} in every view', all(found[(v, h)] for v in ('ap_stock', 'ap_mayhem', 'fl'))))
    for h in ('ced7edef48059e84', '0d5848c8489b6630'):
        checks.append((f'{h} absent from both AP views', not found[('ap_stock', h)] and not found[('ap_mayhem', h)]))
    c = census(o.game, o.flgame)
    checks.append(('stock AP muzzle scene bodies are the FL set',
                   set(c['ap_stock'][2]) == {314, 444, 445, 446, 447, 450, 582, 10463, 12009}))
    checks.append(('stock AP TBullets = FL TBullets but row 9 speed / lifetime',
                   c['fl_rows'] == 45 and all(d.startswith('9:') for d in c['fl_diff'])))
    for name, ok in checks:
        print(f'check {"ok  " if ok else "FAIL"} {name}')
    sys.exit(1 if fails or not all(ok for _, ok in checks) else 0)


if __name__ == '__main__':
    main()
