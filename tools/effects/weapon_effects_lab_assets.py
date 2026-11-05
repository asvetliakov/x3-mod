#!/usr/bin/env python3
"""Extract the textures and body geometry the weapon-effects lab page draws (vanilla half), per catalogue view.

  python3 tools/effects/weapon_effects_lab_assets.py [--views stock_ap,mayhem3,fl]

Inputs: verification/results/weapon-effects-census/<view>.json (tools/effects/weapon_effects_census.py) and the
view's catalogues (weapon_effects_census.views()). Per view it writes, under the untracked
verification/results/weapon-effects-lab/<view>/:
- tex/<name>-<sha8>.png: every texture (and every frame of a texture animation -N) bound by a weapon body or an
  effect element body, decoded from DDS (tools/analysis/lod_atlas.decode_dds, DXT1/3/5 and 24/32-bit; the mip
  whose longer side is <= MAX_SIDE) or TGA/JPG (Pillow, then box-reduced to <= MAX_SIDE). Names resolve as the
  engine binds them (lod_atlas.texture_source: dds/<basename>, the path, the NONE_* placeholder; id:N through
  types/Materials; -N through types/Animations). A texture bound as t_AlphaTexture also gets <name>-<sha8>-mask.png
  (white, alpha = its alpha channel, or its luminance when that is constant: mask_image).
- assets.js: `LAB_ASSETS[view] = {bodies, textures}` for the page, loaded by <script src> so file:// works.
  bodies: per body key the LOD-0 triangles per material (a scene: the parts' triangles at their first-key
  positions, scene_record; an absent v\\NNNNN falls back to the scene objects/cut/NNNNN, inferred) (x, y, z in body units = point/65536 x LOD value/65536;
  u, v) with blend and the diffuse/alpha texture names; textures: per name its distinct PNG images and the
  frame list [image index, du, dv] (an animation's sprite-sheet UV offsets).
- manifest.json: per texture name status, member, source, decoded sha, original and PNG size, PNG paths and
  sha256, used_by (weapon ids and effect rows); per body its textures and status; extra_effects: Effects rows
  reached only through EEDF_PROPAGATE children (not in the census), parsed like the census rows.
Idempotent: a file whose bytes are unchanged is not rewritten; a PNG no texture binds any more is removed. Prints counts per view, including how many texture
names failed to resolve or decode.
"""
import argparse
import hashlib
import json
import pathlib
import re
import struct
import sys
import zlib

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tools/analysis'))
sys.path.insert(0, str(REPO / 'tools/effects'))
import numpy as np  # noqa: E402
import bob1  # noqa: E402
import lod_atlas  # noqa: E402
import weapon_effects_census as wec  # noqa: E402

CENSUS = REPO / 'verification/results/weapon-effects-census'
OUT = REPO / 'verification/results/weapon-effects-lab'
VIEWS = ('stock_ap', 'mayhem3', 'fl')
MAX_SIDE = 512
MAX_TRIANGLES = 600          # per body; the rest is dropped and the body marked truncated
MAX_SCENE_TRIANGLES = 3000   # per scene, shared evenly by its parts (at least 24 each)


# --- decode and PNG --------------------------------------------------------------------------------------------

def dds_level_for(data, max_side=MAX_SIDE):
    """First mip level whose longer side is <= max_side (the last level when none is)."""
    w, h, mips, _ = lod_atlas.dds_format(data)
    level = 0
    while level + 1 < mips and max(w, h) > max_side:
        w, h, level = max(1, w // 2), max(1, h // 2), level + 1
    return level


def dds_rgba(data, max_side=MAX_SIDE):
    """RGBA uint8 (h, w, 4) of the DDS mip chosen by dds_level_for; lod_atlas.AtlasError when unsupported."""
    return lod_atlas.decode_dds(data, dds_level_for(data, max_side))


def reduce(img, max_side=MAX_SIDE):
    """Box-reduce by 2 until the longer side is <= max_side (for TGA/JPG members without mips)."""
    while max(img.shape[:2]) > max_side and min(img.shape[:2]) >= 2:
        h, w = img.shape[0] // 2 * 2, img.shape[1] // 2 * 2
        img = img[:h, :w].reshape(h // 2, 2, w // 2, 2, 4).astype(np.uint16).mean((1, 3)).round().astype(np.uint8)
    return img


def png_bytes(rgba):
    """Deterministic RGBA8 PNG (filter 0, zlib level 9) of a (h, w, 4) uint8 array."""
    rgba = np.ascontiguousarray(rgba, np.uint8)
    h, w = rgba.shape[:2]
    raw = b''.join(b'\0' + rgba[y].tobytes() for y in range(h))

    def chunk(tag, body):
        return struct.pack('>I', len(body)) + tag + body + struct.pack('>I', zlib.crc32(tag + body) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def mask_image(img):
    """White RGBA image whose alpha is the coverage of an alpha-role texture: its alpha channel when that varies,
    else its Rec.601 luminance (an 8-bit alpha TGA decodes as grey with constant alpha)."""
    a = img[:, :, 3]
    if a.min() == a.max():
        a = np.rint(img[:, :, :3].astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32))
    out = np.full(img.shape[:2] + (4,), 255, np.uint8)
    out[:, :, 3] = np.clip(a, 0, 255).astype(np.uint8)
    return out


def write_if_changed(path, data):
    if path.exists() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True


def safe_stem(name):
    s = re.split(r'[\\/]', name)[-1]
    s = re.sub(r'\.\w{3}$', '', s)
    return re.sub(r'[^A-Za-z0-9_.-]+', '_', s)[:48] or 'tex'


# --- texture resolution ----------------------------------------------------------------------------------------

def engine_name(name):
    """Census texture name -> the name bytes the engine reads (id:N -> 'N')."""
    return name[3:] if name.startswith('id:') else name


TAT_NAMES = {v: k for k, v in lod_atlas.TAT.items()}


def texture_frames(assets, name):
    """(frames, animation) the engine draws for a texture name. frames: [(frame name, (du, dv))]; an animation -N
    gives every frame of Animations row N with its TATF_COORDS UV offset (a sprite-sheet tile; (0, 0) without),
    animation its TAT_* type; a plain name gives [(name, (0, 0))] and None."""
    s = lod_atlas.strip_texture_name(name)
    if s is None:
        return [], None
    m = re.match(r'-(\d+)', s)
    if m:
        rows = lod_atlas.animation_rows(assets)
        if rows is None or int(m.group(1)) >= len(rows):
            raise lod_atlas.AtlasError(f'animation row {m.group(1)} unavailable')
        row = rows[int(m.group(1))]
        frames = [(f[1], tuple(f[2]) if f[2] else (0.0, 0.0)) for f in row['frames']] or [(row['first'], (0.0, 0.0))]
        return frames, TAT_NAMES.get(row['type'], str(row['type']))
    return [(name, (0.0, 0.0))], None


def extract_texture(assets, name, tex_dir, cache, alpha_role=False):
    """Manifest record of one census texture name; writes the PNG images. cache: frame name -> image record.
    Record: status, animation, images {frame name: image record}, frames [[frame name, du, dv]]."""
    rec = dict(name=name, animation=None, images={}, frames=[])
    try:
        frames, rec['animation'] = texture_frames(assets, engine_name(name))
    except (lod_atlas.AtlasError, ValueError) as err:
        return dict(rec, status='unresolved', error=str(err)[:200])
    if not frames:
        return dict(rec, status='no_texture')
    for fname, (du, dv) in frames:
        if (fname, alpha_role) not in cache:
            cache[fname, alpha_role] = extract_frame(assets, fname, tex_dir, alpha_role)
        fr = cache[fname, alpha_role]
        if fr['status'] != 'ok':
            return dict(rec, status=fr['status'], error=fr.get('error'), frame=fname, images={}, frames=[])
        rec['images'][fname] = fr
        rec['frames'].append([fname, du, dv])
    rec['status'] = 'ok'
    return rec


def extract_frame(assets, fname, tex_dir, alpha_role=False):
    try:
        src = lod_atlas.texture_source(assets, fname.encode('latin1'))
    except (lod_atlas.AtlasError, ValueError) as err:
        return dict(status='unresolved', error=str(err)[:200])
    if src is None:
        return dict(status='no_texture')
    data, kind, info = src
    try:
        if kind == 'dds':
            w, h, _, fmt = lod_atlas.dds_format(data)
            img = dds_rgba(data)
            fmt = fmt if isinstance(fmt, str) else f'RGB{fmt[1]}'
        else:
            img = lod_atlas.decode_image(data, fname.encode('latin1'))
            h, w, fmt = img.shape[0], img.shape[1], pathlib.PurePosixPath(info['member']).suffix.lstrip('.').upper()
            img = reduce(img)
    except (lod_atlas.AtlasError, ValueError, OSError) as err:
        return dict(status='decode_failed', error=str(err)[:200], member=info['member'], source=info['source'])
    png = png_bytes(img)
    sha = hashlib.sha256(png).hexdigest()
    stem = f'{safe_stem(info["member"])}-{info["decoded_sha256"][:8]}'
    path = tex_dir / f'{stem}.png'
    written = write_if_changed(path, png)
    rec = dict(status='ok', frame=fname, member=info['member'], source=info['source'],
               placeholder=info['placeholder'], decoded_sha256=info['decoded_sha256'][:16], format=fmt,
               original_size=[int(w), int(h)], png=f'tex/{path.name}', png_size=[img.shape[1], img.shape[0]],
               png_sha256=sha[:16], alpha_constant=bool(img[:, :, 3].min() == img[:, :, 3].max()),
               mean_rgb=[round(float(c), 1) for c in img[:, :, :3].reshape(-1, 3).mean(0)], written=written)
    if alpha_role:
        mask = png_bytes(mask_image(img))
        rec['mask'] = f'tex/{stem}-mask.png'
        rec['written'] |= write_if_changed(tex_dir / f'{stem}-mask.png', mask)
    return rec


# --- body geometry ---------------------------------------------------------------------------------------------

def body_tree(data):
    try:
        return bob1.parse(data)
    except bob1.FormatError:
        return bob1.parse(wec.MATERIAL3.sub(b'', data))


def triangles(tree, max_triangles=MAX_TRIANGLES):
    """{group material index: [[x, y, z, u, v] x 3, ...]} of LOD 0 and a truncated flag. Positions in body units
    (point/65536 x LOD value/65536), u, v in texture units; a face's first three indices (a, b, c)."""
    lod = bob1.lods(tree)[0]
    scale = lod['value'] / 65536.0 / 65536.0
    pts = lod['points']
    out, count, truncated = {}, 0, False
    for part in lod['parts']:
        for g in part['groups']:
            for face in g['faces']:
                if count >= max_triangles:
                    truncated = True
                    break
                tri = []
                for i in face[:3]:
                    p = pts[i]
                    f = p[0]
                    xyz = p[1:4] if f & 1 else (0, 0, 0)
                    uv = p[4:6] if f & 1 and f & 2 else (p[1:3] if f & 2 else (0, 0))
                    tri.append([round(xyz[0] * scale, 5), round(xyz[1] * scale, 5), round(xyz[2] * scale, 5),
                                round(uv[0] / 65536.0, 4), round(uv[1] / 65536.0, 4)])
                out.setdefault(g['material'], []).append(tri)
                count += 1
    return out, truncated


def material_textures(m):
    """(diffuse name, alpha name) of a census material summary, lower case, None for none."""
    def clean(v):
        if v is None or (isinstance(v, str) and v.upper() in ('', 'NULL', '0')):
            return None
        return f'id:{v}' if isinstance(v, int) else v.lower()
    if m['kind'] == 'effect':
        t = {k.lower(): v for k, v in m['textures'].items()}
        return clean(t.get('t_diffusetexture')), clean(t.get('t_alphatexture'))
    if m['kind'] == 'classic':
        return clean(m['texture']), None
    return clean(m.get('texture_id_inferred')), None


def negative_material(gi, mats):
    """A text body face with a negative material number (the loader flags the LOD, body-text-loader.md section 3;
    its binding is not traced): the material whose diffuse name is that number (-73 -> '-73.tga'), else the body's
    only material (inferred), else None."""
    if gi >= 0:
        return None
    for m in mats:
        d = material_textures(m)[0]
        if d and re.sub(r'\.\w{3}$', '', d) == str(gi):
            return m
    return mats[0] if len(mats) == 1 else None


def mesh_materials(data, summary, offset=(0.0, 0.0, 0.0), max_triangles=MAX_TRIANGLES):
    """([material record], truncated) of one body's LOD 0, every position shifted by offset (body units)."""
    tris, truncated = triangles(body_tree(data), max_triangles)
    mats = summary.get('materials', [])
    by_index = {m.get('index'): m for m in mats}
    out = []
    for gi, tl in sorted(tris.items()):
        m = by_index.get(gi) or (mats[gi] if 0 <= gi < len(mats) else None) or negative_material(gi, mats)
        diffuse, alpha = material_textures(m) if m else (None, None)
        if any(offset):
            tl = [[[round(p[0] + offset[0], 5), round(p[1] + offset[1], 5), round(p[2] + offset[2], 5), p[3], p[4]]
                   for p in tri] for tri in tl]
        out.append(dict(index=gi, kind=m['kind'] if m else None,
                        blend=m.get('blend') if m and m['kind'] == 'effect' else None,
                        diffuse=diffuse, alpha=alpha, triangles=tl))
    return out, truncated


SCENE_PART = re.compile(r'^[ \t]*P[ \t]+(\d+)[ \t]*;[ \t]*B[ \t]+([^;\r\n]+);', re.M)
SCENE_KEY = re.compile(r'\{([^}]*)\}')
CUT1_NAME = re.compile(rb'NAMEB(\d+)')


def scene_parts(data):
    """[(body token, first-key position or None)] of a scene: a text scene (`P n; B id;` then `{ flags; [x; y; z;]
    ...; time; next; }` keys; a key of 10 or more values carries the position after its flag word (hex or
    decimal), a 7-value key rotation only: inferred from the shipped files, the loader 0x00491b10 is not traced) or a binary CUT1 scene (part bodies from its `NAME` records `B<id>`; key positions not
    decoded, None). Inline `L { ... }` part bodies are returned under their B token and do not resolve."""
    if data[:4] == b'CUT1':
        return [(m.group(1).decode(), None) for m in CUT1_NAME.finditer(data)]
    text = data.decode('latin1')
    found = list(SCENE_PART.finditer(text))
    parts = []
    for i, m in enumerate(found):
        stop = found[i + 1].start() if i + 1 < len(found) else len(text)
        key = SCENE_KEY.search(text, m.end(), stop)
        pos = None
        if key:
            toks = [x.strip() for x in key.group(1).split(';') if x.strip()]
            try:
                vals = [float(x) for x in toks[1:4]]
            except ValueError:
                vals = []
            if len(toks) >= 10 and len(vals) == 3:     # a key with a position (7-value keys carry rotation only)
                pos = tuple(vals)
        parts.append((m.group(2).strip(), pos))
    return parts


def scene_record(data, read, member, source):
    """Page record of a scene drawn as one body: the first key of every part, each part body at its first-key
    position (raw scene units / 65536 = body units, inferred; a part without a decoded position sits at the
    origin) and without its key rotation; MAX_SCENE_TRIANGLES shared by the parts. Part bodies resolve like element bodies (numeric -> v\\%05d); a part
    that is itself a scene, inline or absent is counted unresolved."""
    rec = dict(materials=[], source=source, member=member, parts=0, parts_drawn=0, positions_decoded=0)
    truncated = False
    parts = scene_parts(data)
    budget = max(24, MAX_SCENE_TRIANGLES // max(1, len(parts)))
    for token, pos in parts:
        rec['parts'] += 1
        name = wec.element_body_name(token) if token.isdigit() else token
        rec['positions_decoded'] += pos is not None
        try:
            pdata, _ = read(name)
            mats, cut = mesh_materials(pdata, wec.body_summary(pdata),
                                       tuple(v / 65536.0 for v in pos) if pos else (0.0, 0.0, 0.0), budget)
        except (FileNotFoundError, bob1.FormatError, IndexError):
            continue
        rec['materials'] += mats
        rec['parts_drawn'] += 1
        truncated |= cut
    if truncated:
        rec['truncated'] = True
    if not rec['parts_drawn']:
        rec['error'] = f'{source}: {member}: none of {rec["parts"]} parts resolves to a body'
    return rec


def cut_member(assets, name):
    """(decoded bytes, member) of objects/cut/NNNNN for an absent numeric body v\\NNNNN, else None. The engine's
    search order for effect element bodies is not traced: objects/v first, objects/cut second is inferred (the
    cut-scene loader 0x00492970 names cut ids cut\\%05d, ship-scene-parts.md)."""
    m = re.fullmatch(r'v\\(\d{5})', name or '')
    if not m:
        return None
    try:
        e = bob1.resolve_body(assets, 'cut\\' + m.group(1))
    except FileNotFoundError:
        return None
    return assets.read_entry(e), f'{e["source"]}:{e["path"]}'


def body_record(assets, key, summary, read):
    """Page record of one body: materials (blend, diffuse, alpha, triangles) or an error. An absent v\\NNNNN falls
    back to the scene objects/cut/NNNNN (source cut_fallback_inferred); a body that resolved to a scene is drawn as
    its parts (source scene)."""
    rec = dict(name=summary.get('name'), materials=[], source='body')
    err = summary.get('error')
    if err:
        if err.startswith('FileNotFoundError'):
            found = cut_member(assets, summary.get('name'))
            if found:
                return dict(scene_record(found[0], read, found[1], 'cut_fallback_inferred'), name=rec['name'])
        elif 'scene' in err and summary.get('resolved'):
            try:
                data, prov = read(summary['name'])
                return dict(scene_record(data, read, prov, 'scene'), name=rec['name'])
            except (FileNotFoundError, bob1.FormatError):
                pass
        return dict(rec, error=err)
    try:
        data, _ = read(summary['name'])
        rec['materials'], truncated = mesh_materials(data, summary)
    except (FileNotFoundError, bob1.FormatError, IndexError) as err:
        return dict(rec, error=f'{type(err).__name__}: {str(err)[:160]}')
    if truncated:
        rec['truncated'] = True
    rec['extents'] = summary.get('extents')
    return rec


# --- view ------------------------------------------------------------------------------------------------------

def propagated_rows(census, effects_all):
    """Effects rows reached only through EEDF_PROPAGATE children, recursively, that the census does not list."""
    have = set(census['effects'])
    extra, todo = {}, [e for e in census['effects'].values()]
    while todo:
        eff = todo.pop()
        for el in eff.get('elements', []):
            pid = el.get('prop_effect_id')
            if 'EEDF_PROPAGATE' in el.get('flag_names', []) and isinstance(pid, int) and pid > 0:
                k = str(pid)
                if k in have or k in extra:
                    continue
                row = effects_all.get(pid)
                extra[k] = dict(missing=True) if row is None else row
                if row is not None:
                    todo.append(row)
    return extra


def used_by(census, extra, body_textures):
    """{texture name: sorted users} where a user is 'weapon:<SS_BULLET_*>' or 'effect:<row>'; body_textures maps a
    body key to the texture names its page record binds (scene parts included)."""
    out = {}
    for w in census['weapons']:
        for t in body_textures.get(w['body_key'], ()):
            out.setdefault(t, set()).add('weapon:' + w['weapon_id'])
    for k, e in list(census['effects'].items()) + list(extra.items()):
        for el in e.get('elements', []):
            for t in body_textures.get(el.get('body_key'), ()):
                out.setdefault(t, set()).add('effect:' + k)
    return {t: sorted(u) for t, u in sorted(out.items())}


def run_view(view, assets, out_dir=OUT, census_dir=CENSUS):
    census = json.loads((census_dir / f'{view}.json').read_text())
    data, _ = assets.get('types/Effects.txt')
    extra = propagated_rows(census, wec.parse_effects(data.decode('latin1')))
    read = wec.assets_reader(assets)
    bodies = dict(census['bodies'])
    for e in extra.values():                      # element bodies of the propagated rows, summarised like the census
        for el in e.get('elements', []):
            bn = wec.element_body_name(el.get('bodyid'))
            if not bn:
                continue
            key = bob1.body_stem(bn).lower()
            el['body_key'] = key
            if key not in bodies:
                try:
                    d, prov = read(bn)
                    bodies[key] = dict(name=bn, resolved=prov, **wec.body_summary(d))
                except (FileNotFoundError, bob1.FormatError) as err:
                    bodies[key] = dict(name=bn, error=f'{type(err).__name__}: {str(err)[:160]}')
                bodies[key]['textures'] = wec.body_textures(bodies[key])
    census['bodies'] = bodies
    vdir = out_dir / view
    tex_dir = vdir / 'tex'
    page_bodies = {k: body_record(assets, k, s, read) for k, s in sorted(bodies.items())}
    names = sorted({n for b in page_bodies.values() for m in b['materials'] for n in (m['diffuse'], m['alpha']) if n})
    alpha_names = {m['alpha'] for b in page_bodies.values() for m in b['materials'] if m['alpha']}
    cache, textures = {}, {}
    for n in names:
        textures[n] = extract_texture(assets, n, tex_dir, cache, n in alpha_names)
    body_tex = {k: {n for m in b['materials'] for n in (m['diffuse'], m['alpha']) if n} for k, b in page_bodies.items()}
    users = used_by(census, extra, body_tex)
    manifest = dict(view=view, max_side=MAX_SIDE, census=f'verification/results/weapon-effects-census/{view}.json',
                    census_tables=census['tables'],
                    textures={n: dict({k: v for k, v in r.items() if k != 'images'},
                                      images={f: {k: v for k, v in i.items() if k != 'written'}
                                              for f, i in r['images'].items()},
                                      used_by=users.get(n, [])) for n, r in textures.items()},
                    bodies={k: dict(name=b['name'], error=b.get('error'), truncated=b.get('truncated', False),
                                    source=b.get('source'), member=b.get('member'), parts=b.get('parts'),
                                    parts_drawn=b.get('parts_drawn'), positions_decoded=b.get('positions_decoded'),
                                    triangles=sum(len(m['triangles']) for m in b['materials']),
                                    textures=sorted({n for m in b['materials'] for n in (m['diffuse'], m['alpha'])
                                                     if n})) for k, b in page_bodies.items()},
                    extra_effects=extra)
    js_tex = {}
    for n, r in textures.items():
        order = list(r['images'])
        js_tex[n] = dict(status=r['status'], animation=r['animation'],
                         images=[dict(png=r['images'][f]['png'], size=r['images'][f]['png_size'],
                                      alpha_constant=r['images'][f]['alpha_constant'],
                                      placeholder=r['images'][f]['placeholder'],
                                      mask=r['images'][f].get('mask')) for f in order],
                         frames=[[order.index(f), du, dv] for f, du, dv in r['frames']])
    js_bodies = {k: dict(error=b.get('error'), extents=b.get('extents'), source=b.get('source'),
                         materials=[{k2: m[k2] for k2 in ('index', 'kind', 'blend', 'diffuse', 'alpha', 'triangles')}
                                    for m in b['materials']]) for k, b in page_bodies.items()}
    js = ('// Generated by tools/effects/weapon_effects_lab_assets.py; untracked (game-derived geometry).\n'
          'window.LAB_ASSETS = window.LAB_ASSETS || {};\n'
          f'window.LAB_ASSETS[{json.dumps(view)}] = '
          + json.dumps(dict(bodies=js_bodies, textures=js_tex), separators=(',', ':'), sort_keys=True) + ';\n')
    write_if_changed(vdir / 'assets.js', js.encode())
    write_if_changed(vdir / 'manifest.json', (json.dumps(manifest, indent=1, sort_keys=True) + '\n').encode())
    frames = {p for r in textures.values() for i in r['images'].values() for p in (i['png'], i.get('mask')) if p}
    pruned = 0
    for stale in sorted(tex_dir.glob('*.png')) if tex_dir.is_dir() else []:
        if f'tex/{stale.name}' not in frames:      # a PNG no current texture binds (an older census)
            stale.unlink()
            pruned += 1
    status = {}
    for r in textures.values():
        status[r['status']] = status.get(r['status'], 0) + 1
    return dict(view=view, texture_names=len(textures), status=status, pngs=len(frames),
                pngs_written=sum(1 for f in cache.values() if f.get('written')), pngs_pruned=pruned,
                bodies=len(page_bodies), body_errors=sum(1 for b in page_bodies.values() if b.get('error')),
                placeholders=sum(1 for r in textures.values() if any(i['placeholder'] for i in r['images'].values())),
                truncated=sum(1 for b in page_bodies.values() if b.get('truncated')), extra_effects=len(extra),
                scenes={src: [sum(1 for b in page_bodies.values() if b.get('source') == src),
                              sum(1 for b in page_bodies.values() if b.get('source') == src and not b.get('error')),
                              sum(b.get('parts', 0) for b in page_bodies.values() if b.get('source') == src),
                              sum(b.get('parts_drawn', 0) for b in page_bodies.values() if b.get('source') == src)]
                        for src in ('cut_fallback_inferred', 'scene')},
                failures=sorted((n, r['status'], (r.get('error') or '')[:90]) for n, r in textures.items()
                                if r['status'] not in ('ok', 'no_texture')))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--views', default=','.join(VIEWS))
    args = ap.parse_args(argv)
    makers = wec.views()
    for view in args.views.split(','):
        r = run_view(view, makers[view]())
        print(f'{view}: texture names={r["texture_names"]} {r["status"]} pngs={r["pngs"]} written={r["pngs_written"]} '
              f'pruned={r["pngs_pruned"]} placeholder-bound={r["placeholders"]} '
              f'bodies={r["bodies"]} body errors={r["body_errors"]} truncated={r["truncated"]} '
              f'propagated rows={r["extra_effects"]}')
        for src, (n, ok, parts, drawn) in r['scenes'].items():
            print(f'  {src}: scenes={n} drawn={ok} parts={parts} parts drawn={drawn}')
        for n, s, e in r['failures']:
            print(f'  {s}: {n}: {e}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
