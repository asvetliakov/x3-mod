#!/usr/bin/env python3
"""Offline census of ship engine effects (read-only, bottle X3; prints counts, names and hashes only).

Owning note: docs/reverse-engineering/engine-effects.md. Two views of the same game tree:
`installed` (Mayhem 3, every mounted layer) and `stock` (STOCK_AP_CATALOGUES only).

1. types/Bodies SBTYPE_JET / SBTYPE_SMALLJET lists (0x00434620 flags these bodies' nodes as jets).
2. Ship scene engine parts: glow parts (effects\\engines\\fx_engine_*) and emitter dummies
   (fx_engine_emitter) with their `C` value (part record +0x38 -> node +0x260, the jet mode word read
   at 0x0045ad85), the key rotation, and the Raptor scene as the worked example.
3. TShips col 11 (+0x54, Effects.txt id spawned at emitters by 0x00414590) and col 49 (+0xc4, Particles3
   id of the trail generator) distributions, the Effects rows and Particles3 rows they reference.
4. Bodies under objects/effects/engines: logical stems per view, installed-only and overridden stems,
   effect file / blend / diffuse texture per resolved body (engine winner, bob1.resolve_body).
5. Glow geometry along the node z axis (the axis 0x0045b09a scales): LOD 0 point z extent per resolved
   installed body, classified `neg` (|zmin| > 2 zmax), `pos`, `both`; and the Effects sprite bodies
   objects/v/00011, 00213 (material only).
6. Trail and sprite textures: tex/true/<id>.jpg of the Particles3 MatIDs in use and of the sprite bodies'
   legacy texture ids (JPEG frame size and SHA-256 prefix).
"""
import collections
import hashlib
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / 'tools/analysis'))
from sector_fog_census import Assets, STOCK_AP_CATALOGUES  # noqa: E402
import bob1  # noqa: E402

ROOT = pathlib.Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
PART = re.compile(r'^P (\d+); B ([^;]+);(?: C (-?\d+);)?[^\n]*\n\s*\{([^}]*)\}', re.M)
BLEND = {1: 'ZERO', 2: 'ONE', 3: 'SRCCOLOR', 4: 'INVSRCCOLOR', 5: 'SRCALPHA', 6: 'INVSRCALPHA',
         7: 'DESTALPHA', 8: 'INVDESTALPHA', 9: 'DESTCOLOR', 10: 'INVDESTCOLOR'}


def text(assets, path):
    data, prov = assets.get(path)
    return data.decode('latin1'), prov


def rows(t):
    return [l for l in t.splitlines() if l.strip() and not l.lstrip().startswith('/')]


def bodies_lists(t):
    out, cur = {}, None
    for l in t.splitlines():
        m = re.match(r'\s*(SBTYPE_\w+);(\d+);', l)
        if m:
            cur = m.group(1)
            out[cur] = []
            continue
        if cur and not l.lstrip().startswith('//'):
            out[cur] += [x.strip() for x in l.split(';') if x.strip()]
    return out


def effects_rows(t):
    eff, cur = {}, None
    for l in t.splitlines():
        s = l.split('//')[0].strip()
        if not s:
            continue
        m = re.match(r'^(\d+);\s*(\d+);\s*(\d+);\s*(\d+);\s*([\d.]+);$', s)
        if m:
            cur = int(m.group(1))
            eff[cur] = []
            continue
        if cur is not None:
            eff[cur].append(s)
    return eff


def particle_rows(t):
    out, cur = {}, None
    for l in t.splitlines():
        s = l.split('//')[0].strip()
        if not s:
            continue
        m = re.match(r'^(\d+);\s*(\d+);\s*NULL;', s)
        if m:
            cur = int(m.group(1))
            out[cur] = []
            continue
        if cur is not None and s.startswith('PEDF'):
            f = [x.strip() for x in s.split(';')]
            out[cur].append(dict(flags=f[0], mat=f[1], density=f[6], speed=(f[11], f[12]), life=f[13],
                                 size=(f[14], f[15])))
    return out


def scene_census(assets):
    glow = collections.Counter(); emit = collections.Counter(); rot = collections.Counter()
    scenes = with_parts = 0
    per_scene = []
    for key in sorted(assets.entries):
        if not key.startswith('objects/ships/') or not re.search(r'scene\.(bod|pbd)$', key):
            continue
        data, _ = assets.get(key)
        t = data.decode('latin1')
        if not t.lstrip().startswith('VER') and 'P 0;' not in t:
            continue
        scenes += 1
        g = e = 0
        for m in PART.finditer(t):
            body = m.group(2).strip().lower()
            if not body.startswith('effects\\engines\\'):
                continue
            c = int(m.group(3)) if m.group(3) else 0
            key_f = [x.strip() for x in m.group(4).split(';') if x.strip()]
            q = key_f[4:8]
            if body.endswith('fx_engine_emitter'):
                emit[c] += 1; e += 1
            else:
                glow[c] += 1; g += 1
                rot['identity' if all(float(x) == 0 for x in q) else 'rotated'] += 1
        if g or e:
            with_parts += 1
            per_scene.append((g, e))
    return scenes, with_parts, glow, emit, rot, per_scene


def engine_bodies(assets):
    stems = collections.defaultdict(list)
    for key in assets.entries:
        if key.startswith('objects/effects/engines/'):
            stems[bob1.body_stem(key[len('objects/'):])].append(key)
    return stems


def material_census(assets, stems, limit=None):
    out = collections.Counter(); tex = collections.Counter(); errors = 0; digest = {}
    for stem in sorted(stems)[:limit]:
        try:
            e = bob1.resolve_body(assets, stem)
            data = assets.read_entry(e)
            digest[stem] = hashlib.sha256(data).hexdigest()
            tree = bob1.parse(data)
        except Exception:
            errors += 1
            continue
        for m in bob1.materials(tree):
            if 'params' not in m:
                out[('legacy', '-', '-')] += 1
                continue
            p = {(n.decode() if isinstance(n, bytes) else n).lower(): v for n, _, v in m['params']}
            eff = m['effect'].decode('latin1').lower()
            src = BLEND.get(p.get('g_srcblend', [0])[0], '?')
            dst = BLEND.get(p.get('g_destblend', [0])[0], '?')
            on = p.get('g_alphablendenable', [0])[0]
            out[(eff, f'{src}/{dst}' if on else 'off', 'zwrite' if p.get('g_zwriteenable', [0])[0] else 'no-zwrite')] += 1
            d = p.get('t_diffusetexture', b'')
            tex[(d.decode('latin1') if isinstance(d, bytes) else str(d)).lower()] += 1
    return out, tex, errors, digest


def main():
    views = {'installed': Assets(ROOT), 'stock': Assets(ROOT, catalogues=STOCK_AP_CATALOGUES)}
    print('1. types/Bodies jet lists')
    for label, a in views.items():
        t, prov = text(a, 'types/Bodies.txt')
        lists = bodies_lists(t)
        jet = lists.get('SBTYPE_JET', []); small = lists.get('SBTYPE_SMALLJET', [])
        named = [x for x in jet if not x.isdigit()]
        print(f'  {label:9s} {prov["source"]} {prov["decoded_sha256"][:12]} JET entries={len(jet)} '
              f'(named {len(named)}, numeric {len(jet) - len(named)}) SMALLJET={small}')
        under = sum(1 for x in named if x.lower().replace('\\', '/').startswith('effects/engines/'))
        emitter = any(x.lower().endswith('fx_engine_emitter') for x in named)
        print(f'            JET under effects/engines: {under}, fx_engine_emitter listed: {emitter}')
    print('2. ship scene engine parts (text scenes under objects/ships)')
    for label, a in views.items():
        scenes, with_parts, glow, emit, rot, per = scene_census(a)
        print(f'  {label:9s} scenes={scenes} with engine parts={with_parts} glow parts={sum(glow.values())} '
              f'emitter parts={sum(emit.values())}')
        print(f'            glow C values: {[(hex(c), n) for c, n in glow.most_common()]}')
        print(f'            emitter C values: {[(hex(c), n) for c, n in emit.most_common()]}')
        print(f'            glow key rotation: {dict(rot)}')
        gl = [g for g, _ in per]; em = [e for _, e in per]
        print(f'            per scene: glow parts median {sorted(gl)[len(gl) // 2]} max {max(gl)}, '
              f'emitters median {sorted(em)[len(em) // 2]} max {max(em)}')
    print('3. TShips engine columns and the rows they reference')
    for label, a in views.items():
        t, prov = text(a, 'types/TShips.txt')
        r = [x.split(';') for x in rows(t)[1:]]
        r = [x for x in r if len(x) > 52]
        eff_t, eprov = text(a, 'types/Effects.txt')
        eff = effects_rows(eff_t)
        par_t, pprov = text(a, 'types/Particles3.txt')
        par = particle_rows(par_t)
        c11 = collections.Counter(x[11] for x in r); c49 = collections.Counter(x[49] for x in r)
        print(f'  {label:9s} TShips {prov["source"]} rows={len(r)}; Effects {eprov["source"]} '
              f'{eprov["decoded_sha256"][:12]} ids={len(eff)}; Particles3 {pprov["source"]} '
              f'{pprov["decoded_sha256"][:12]} ids={len(par)}')
        print(f'            col11 engine effect: distinct={len(c11)} top={c11.most_common(6)}')
        print(f'            col49 trail particles: distinct={len(c49)} zero={c49.get("0", 0)} top={c49.most_common(6)}')
        for eid, _ in c11.most_common(2):
            print(f'            Effects {eid}: {eff.get(int(eid), "missing")}')
        for pid, _ in [x for x in c49.most_common(3) if x[0] != '0'][:2]:
            print(f'            Particles3 {pid}: {par.get(int(pid), "missing")}')
        mats = collections.Counter(e['mat'] for pid in c49 if pid != '0' for e in par.get(int(pid), []))
        print(f'            trail emitter material ids (weighted by particle ids in use): {dict(mats)}')
        raptor = [x for x in r if 'raptor' in x[16].lower()]
        for x in raptor[:1]:
            print(f'            Raptor row: scene={x[16]} speed(col7)={x[7]} col11={x[11]} col12={x[12]} col49={x[49]}')
    print('4. bodies under objects/effects/engines')
    stems = {label: engine_bodies(a) for label, a in views.items()}
    inst, stock = stems['installed'], stems['stock']
    added = sorted(set(inst) - set(stock))
    print(f'  stems installed={len(inst)} stock={len(stock)} installed-only={len(added)} '
          f'stock-only={len(set(stock) - set(inst))}; examples added: {added[:4]}')
    results = {}
    for label, a in views.items():
        out, tex, errors, digest = material_census(a, stems[label])
        results[label] = digest
        print(f'  {label:9s} resolved materials (effect, blend, zwrite): {out.most_common(8)} parse errors={errors}')
        print(f'            distinct diffuse textures={len(tex)} top={tex.most_common(5)}')
    common = set(results['installed']) & set(results['stock'])
    changed = sorted(s for s in common if results['installed'][s] != results['stock'][s])
    print(f'  shared stems whose resolved bytes differ (Mayhem overrides): {len(changed)} of {len(common)}; '
          f'examples {changed[:4]}')

    print('5. glow geometry along node z (installed winners), and emitter sprite bodies')
    a = views['installed']
    cls = collections.Counter(); ex = []
    for stem in sorted(inst):
        try:
            lod = bob1.lods(bob1.parse(a.read_entry(bob1.resolve_body(a, stem))))[0]
        except Exception:
            cls['error'] += 1
            continue
        pts = lod['points']
        if not pts:
            cls['no points'] += 1
            continue
        xs = [p[1] for p in pts]; ys = [p[2] for p in pts]; zs = [p[3] for p in pts]
        zmin, zmax = min(zs), max(zs)
        dom = 'z-longest' if zmax - zmin >= max(max(xs) - min(xs), max(ys) - min(ys)) else 'xy-longest'
        side = 'neg' if -zmin > 2 * zmax else ('pos' if zmax > -2 * zmin else 'both')
        cls[(dom, side)] += 1
        if stem.endswith(('fx_engine_xtc_red_big2', 'fx_engine_argon_m3')):
            ex.append((stem.rsplit('/', 1)[1], 'radius', lod['value'], 'z', (zmin, zmax), 'x', (min(xs), max(xs))))
    print(f'  installed z-extent classes: {dict(cls)}')
    for e in ex:
        print(f'  example {e}')
    for name in ('v\\00011', 'v\\00213'):
        for label, av in views.items():
            try:
                e = bob1.resolve_body(av, name)
                tree = bob1.parse(av.read_entry(e))
            except Exception as err:      # legacy text body (MATERIALn rows): print the raw material row
                try:
                    data, prov = av.get('objects/' + name.replace('\\', '/') + '.bod')
                    t = data.decode('latin1')
                    mat = [l.strip() for l in t.splitlines() if l.startswith('MATERIAL')]
                    size = [l.split(';')[0] for l in t.splitlines() if 'Object Size' in l]
                    parts = re.findall(r'"([^"]+)"', t)
                    print(f'  {label:9s} {name} {prov["source"]}:{prov["member"]} {type(err).__name__} in bob1; '
                          f'materials={mat} size={size} parts={parts}')
                except FileNotFoundError:
                    print(f'  {label:9s} {name}: absent')
                continue
            for m in bob1.materials(tree):
                if 'params' in m:
                    p = {(n.decode() if isinstance(n, bytes) else n).lower(): v for n, _, v in m['params']}
                    d = p.get('t_diffusetexture', b'')
                    print(f'  {label:9s} {name} {e["source"]}:{e["path"]} {m["effect"].decode()} '
                          f'{BLEND.get(p.get("g_srcblend", [0])[0])}/{BLEND.get(p.get("g_destblend", [0])[0])} '
                          f'diffuse={d.decode("latin1") if isinstance(d, bytes) else d}')
                else:
                    print(f'  {label:9s} {name} {e["source"]}:{e["path"]} legacy material texture={m.get("texture")}')

    print('6. trail / sprite textures (installed view)')

    def jpeg_size(d):
        i = 2
        while i + 9 < len(d) and d[i] == 0xff:
            m, n = d[i + 1], int.from_bytes(d[i + 2:i + 4], 'big')
            if m in (0xc0, 0xc1, 0xc2):
                return int.from_bytes(d[i + 7:i + 9], 'big'), int.from_bytes(d[i + 5:i + 7], 'big')
            i += 2 + n
        return None
    for k in ('tex/true/406.jpg', 'tex/true/395.jpg', 'tex/true/369.jpg', 'tex/true/368.jpg',
              'textures/others/trail1_diff.jpg'):
        try:
            d, prov = views['installed'].get(k)
        except FileNotFoundError:
            print(f'  {k}: absent')
            continue
        print(f'  {k} {prov["source"]} {jpeg_size(d)} {hashlib.sha256(d).hexdigest()[:12]}')


if __name__ == '__main__':
    main()
