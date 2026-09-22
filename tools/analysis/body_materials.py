#!/usr/bin/env python3
"""Material census of one LOD record of a BOB1 body, and draw counts under grouping rules.

For the chosen record (default the original coarsest record n-1) every material used by
a group is listed with its effect, texture slots, light-map status, face count and summed
face area (body units, from the record's points) as a share of the record. Then per-part
grouping rules give the draw count a collapsed record would have:
  a  groups merged by identical full texture tuple (effect file + every texture slot)
  b  groups merged by identical (diffuse, light map) pair
  c  the pilot's two-group collapse (lod_overlay.coarse_record, --collapse two)
  d  keep real light maps: groups whose material has a real light map keep their own
     material; the rest collapse like c (opaque -> dominant opaque material of the rest,
     alpha-tested/blended -> dominant alpha material of the rest)
  d+b  rule d with the kept light-map groups further merged by (diffuse, light map)
  e  keep glow light maps only (= lod_overlay --collapse glow): like d, but only materials
     whose light map is mostly bright keep their own material
Draws are counted per part (a part's groups are the subsets; parts are never merged);
empty groups count like any other, as coarse_record keeps them.
Light-map status: placeholder = NULL/empty (the engine binds its 32x32 placeholder,
run255 id 196); stock = a shipped NONE_* 32x32 map (NONE_BLACK, NONE_WHITE: not kept by
rule d, but counted for glow by its brightness, so NONE_WHITE is a glow map); real;
missing; error (e.g. the name resolves to more than one format; printed as an error line).

Bodies are read from the shipped catalogues: any catalogue with a lod_overlay marker
(<slot>.x3m-lod.json beside it) is skipped, so the original resolves with an overlay
installed. Read-only; prints numbers and names only.

  python3 tools/analysis/body_materials.py ships/argon/argon_TL [--lod N] [--game DIR]
"""
import argparse
import math
import re
import struct
import sys
from collections import OrderedDict
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bob1  # noqa: E402
import lod_overlay  # noqa: E402

SLOT_NAMES = OrderedDict([(b't_diffusetexture', 'diffuse'), (b't_bumptexture', 'bump'),
                          (b't_speculartexture', 'specular'), (b't_lightmaptexture', 'light'),
                          (b't_alphatexture', 'alpha'), (b't_cubemaptexture', 'cube')])
GLOW_NAME = re.compile(r'engine|(^|[_/\\. -])eng([_/\\. -]|$)|glow|thrust|exhaust|nozzle|burner', re.I)
BRIGHT_LUMA = lod_overlay.GLOW_LUMA       # texel counts as bright above this Rec.601 luma
MOSTLY_BRIGHT = lod_overlay.GLOW_SHARE    # bright-texel share for "mostly bright" (glow candidate)
original_assets = lod_overlay.original_assets


def is_null(name):
    """NULL / empty / '0': no texture (a NULL light map is the engine's 32x32 placeholder,
    run255 id 196). NONE_* names (NONE_BLACK, NONE_WHITE, ...) are real shipped 32x32
    textures in dds/ and are resolved like any other name."""
    return not name or name.upper() in (b'NULL', b'0')


def is_stock(name):
    return bool(name) and name.replace(b'\\', b'/').rsplit(b'/', 1)[-1].upper().startswith(b'NONE_')


def slots(material):
    """OrderedDict slot -> texture name (bytes); effect materials by t_* STRING params."""
    out = OrderedDict()
    if 'params' in material:
        for name, typ, val in material['params']:
            if typ == 8 and name.lower().startswith(b't_'):
                out[SLOT_NAMES.get(name.lower(), name.decode('latin1'))] = val
    else:                              # classic MAT6/MAT5: slot meaning not traced
        tex = material.get('texture', b'')
        out['texture'] = tex if isinstance(tex, bytes) else str(tex).encode()
        for i, (m, _) in enumerate(material.get('maps', []) + material.get('extra', [])):
            out[f'map{i}'] = m if isinstance(m, bytes) else str(m).encode()
    return out


# --- texture statistics (DDS; smallest mip with max side >= 64) --------------------------

def _rgb565(c):
    return ((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31


def _dxt_color_block(b, o, dxt1):
    c0, c1, bits = struct.unpack_from('<HHI', b, o)
    p0, p1 = _rgb565(c0), _rgb565(c1)
    if c0 > c1 or not dxt1:
        pal = [p0, p1, tuple((2 * x + y) // 3 for x, y in zip(p0, p1)), tuple((x + 2 * y) // 3 for x, y in zip(p0, p1))]
    else:
        pal = [p0, p1, tuple((x + y) // 2 for x, y in zip(p0, p1)), (0, 0, 0)]
    return [pal[(bits >> (2 * i)) & 3] for i in range(16)]


def texel_rgb(data):
    """(width, height, [rgb...]) of one decoded mip, or None if the format is not handled."""
    if len(data) < 128 or data[:4] != b'DDS ':
        return None
    h, w = struct.unpack_from('<II', data, 12)
    mips = max(1, struct.unpack_from('<I', data, 28)[0])
    pf_flags, fourcc, bits = struct.unpack_from('<I4sI', data, 80)
    if pf_flags & 4 and fourcc in (b'DXT1', b'DXT3', b'DXT5'):
        bsize = 8 if fourcc == b'DXT1' else 16
        size = lambda mw, mh: max(1, (mw + 3) // 4) * max(1, (mh + 3) // 4) * bsize
    elif not pf_flags & 4 and bits in (24, 32):
        size = lambda mw, mh: mw * mh * bits // 8
    else:
        return None
    off, mw, mh, level = 128, w, h, 0
    while level + 1 < mips and max(mw, mh) // 2 >= 64:
        off += size(mw, mh); mw, mh, level = max(1, mw // 2), max(1, mh // 2), level + 1
    if off + size(mw, mh) > len(data):
        return None
    px = []
    if pf_flags & 4:
        coff = 0 if fourcc == b'DXT1' else 8
        for i in range(size(mw, mh) // bsize):
            px += _dxt_color_block(data, off + i * bsize + coff, fourcc == b'DXT1')
    else:
        step = bits // 8
        rmask, gmask, bmask = struct.unpack_from('<III', data, 92)
        def chan(v, m):
            if not m:
                return 0
            sh = (m & -m).bit_length() - 1
            return ((v & m) >> sh) * 255 // (m >> sh)
        for i in range(mw * mh):
            v = int.from_bytes(data[off + i * step:off + i * step + step], 'little')
            px.append((chan(v, rmask), chan(v, gmask), chan(v, bmask)))
    return mw, mh, px


def texture_info(assets, name, cache, bright_luma=BRIGHT_LUMA):
    """dict(status, size, fmt, luma, bright, p99) for a material texture name; bright is the
    share of texels with luma above bright_luma."""
    if (name, bright_luma) in cache:
        return cache[name, bright_luma]
    if is_null(name):
        info = dict(status='null')
    else:
        stem = 'dds/' + PurePosixPath(name.decode('latin1').replace('\\', '/')).stem
        error = None
        try:
            data, _ = assets.logical(stem, ('.pck', '.dds', '.tga'))
        except ValueError as exc:                 # e.g. ambiguous alternate formats
            data, error = None, str(exc)
        if error is not None:
            info = dict(status='error', error=error)
        elif data is None:
            info = dict(status='missing')
        elif data[:4] != b'DDS ':
            info = dict(status='real', size=None, fmt='non-DDS')
        else:
            h, w = struct.unpack_from('<II', data, 12)
            info = dict(status='real', size=(w, h), fmt=data[84:88].decode('latin1').strip('\0') or 'rgb',
                        stock=is_stock(name))
            dec = texel_rgb(data)
            if dec:
                lum = [(0.299 * r + 0.587 * g + 0.114 * b) / 255 for r, g, b in dec[2]]
                lum.sort()
                info.update(luma=sum(lum) / len(lum), bright=sum(x > bright_luma for x in lum) / len(lum),
                            p99=lum[min(len(lum) - 1, int(0.99 * len(lum)))])
    cache[name, bright_luma] = info
    return info


def light_status(info):
    if info is None:
        return 'n/a'
    if info['status'] == 'null':
        return 'placeholder'
    if info['status'] in ('missing', 'error'):
        return info['status']
    if info.get('stock'):
        return 'stock'             # NONE_* 32x32 map; counts for glow by brightness, not for rule d
    return 'real'


# --- geometry -----------------------------------------------------------------------------

def face_area(points, face):
    ps = []
    for i in face[:3]:
        p = points[i]
        if not p[0] & 1:
            return 0.0
        ps.append(p[1:4])
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = ps
    ux, uy, uz, vx, vy, vz = bx - ax, by - ay, bz - az, cx - ax, cy - ay, cz - az
    return 0.5 * math.sqrt((uy * vz - uz * vy) ** 2 + (uz * vx - ux * vz) ** 2 + (ux * vy - uy * vx) ** 2)


# --- census and rules ---------------------------------------------------------------------

def census(tree, lod_index=None, assets=None):
    """dict(lod, value, draws, materials={mi: row}, rules={...})."""
    ladder = bob1.lods(tree)
    k = len(ladder) - 1 if lod_index is None else lod_index
    rec = ladder[k]
    mats = bob1.materials(tree)
    alpha = lod_overlay.alpha_materials(mats)
    cache = {}
    rows, total = OrderedDict(), 0.0
    for part in rec['parts']:
        for g in part['groups']:
            area = sum(face_area(rec['points'], f) for f in g['faces'])
            total += area
            r = rows.setdefault(g['material'], dict(groups=0, faces=0, area=0.0))
            r['groups'] += 1; r['faces'] += len(g['faces']); r['area'] += area
    for mi, r in rows.items():
        m = mats[mi] if 0 <= mi < len(mats) else {}
        s = slots(m)
        lm = s.get('light')
        linfo = texture_info(assets, lm, cache) if assets is not None and lm is not None else (
            dict(status='null') if lm is not None and is_null(lm) else None)
        if linfo is None and lm is not None:
            linfo = dict(status='real', size=None)          # no assets: name only
        names = b' '.join([m.get('effect', b'')] + list(s.values())).decode('latin1')
        glow_name = bool(GLOW_NAME.search(names))
        glow_bright = bool(linfo and linfo.get('bright', 0) >= MOSTLY_BRIGHT)
        r.update(record_index=m.get('index'), effect=m.get('effect', b'(classic)').decode('latin1'),
                 technique=m.get('technique'), slots=s, alpha=mi in alpha, light=light_status(linfo),
                 light_info=linfo, share=r['area'] / total if total else 0.0, glow_name=glow_name,
                 glow_bright=glow_bright)
    real = {mi for mi, r in rows.items() if r['light'] == 'real'}
    return dict(lod=k, lods=len(ladder), value=rec['value'], total_area=total, materials=rows,
                draws=sum(len(p['groups']) for p in rec['parts']), real_light=real,
                rules=dict(rules(rec, mats, alpha, real),
                           e=rules(rec, mats, alpha, {mi for mi, r in rows.items() if r['glow_bright']})['d']))


def _key_full(m):
    return (m.get('effect'),) + tuple(slots(m).items())


def _key_pair(m):
    s = slots(m)
    return (s.get('diffuse', s.get('texture')), s.get('light'))


def rules(rec, mats, alpha, real):
    """Per-rule draw counts (per part) plus merge diagnostics."""
    get = lambda mi: mats[mi] if 0 <= mi < len(mats) else {}
    out = dict(a=0, b=0, c=0, d=0, db=0, b_mixes_alpha=0, a_param_diffs=0)
    for part in rec['parts']:
        used = [g['material'] for g in part['groups']]   # empty groups count, as in coarse_record
        if not part['groups']:
            continue
        ka, kb = {}, {}
        for mi in used:
            ka.setdefault(_key_full(get(mi)), set()).add(mi)
            kb.setdefault(_key_pair(get(mi)), set()).add(mi)
        out['a'] += len(ka); out['b'] += len(kb)
        out['b_mixes_alpha'] += sum(len({mi in alpha for mi in v}) > 1 for v in kb.values())
        for v in ka.values():
            params = {repr([p for p in get(mi).get('params', []) if p[1] != 8]) for mi in v}
            out['a_param_diffs'] += len(params) > 1
        out['c'] += len(lod_overlay.coarse_record({'value': 0, 'flags': 0, 'points': [], 'parts': [part]},
                                                  None, alpha, 'two')['parts'][0]['groups'])
        kept = {mi for mi in used if mi in real}
        rest = [mi for mi in used if mi not in real]
        classes = int(any(mi not in alpha for mi in rest)) + int(any(mi in alpha for mi in rest))
        out['d'] += len(kept) + classes
        out['db'] += len({_key_pair(get(mi)) for mi in kept}) + classes
    return out


RULE_LABELS = [('a', 'full texture tuple'), ('b', '(diffuse, light map) pair'), ('c', 'pilot two-group collapse'),
               ('d', 'keep real light maps, rest -> dominant'), ('db', 'd, kept groups merged by (diffuse, light)'),
               ('e', 'keep only mostly-bright (glow) light maps, rest -> dominant')]


def short(name):
    if not name:
        return '-'
    return name.decode('latin1').replace('\\', '/').rsplit('/', 1)[-1]


def format_census(c, origin='', out=None):
    out = out or sys.stdout
    p = lambda *a: print(*a, file=out)
    p(f'{origin}  record LOD{c["lod"]} of {c["lods"]} (value={c["value"]})  draws={c["draws"]}'
      f'  materials={len(c["materials"])}  total_area={c["total_area"]:.4g}')
    p('mat  rec  effect/tech  alpha  light-map-status  faces  groups  area_share  glow  textures')
    for mi, r in sorted(c['materials'].items(), key=lambda kv: -kv[1]['area']):
        li = r['light_info'] or {}
        stat = r['light']
        if li.get('size'):
            stat += f' {li["size"][0]}x{li["size"][1]}'
        if 'luma' in li:
            stat += f' luma={li["luma"]:.2f} bright={li["bright"]:.2f} p99={li["p99"]:.2f}'
        glow = ','.join(x for x, f in (('name', r['glow_name']), ('bright', r['glow_bright'])) if f) or '-'
        tex = ' '.join(f'{k}={short(v)}' for k, v in r['slots'].items())
        p(f'{mi:3d}  {r["record_index"]!s:>4}  {r["effect"]}/{r["technique"]}  {"A" if r["alpha"] else "-"}'
          f'  {stat}  {r["faces"]}  {r["groups"]}  {r["share"]:.3f}  {glow}  {tex}')
    rl = c['materials']
    real_share = sum(rl[mi]['share'] for mi in c['real_light'])
    p(f'real light maps: {len(c["real_light"])} materials, area share {real_share:.3f};'
      f' placeholder/null: {sum(r["light"] == "placeholder" for r in rl.values())} materials;'
      f' stock NONE_*: {sum(r["light"] == "stock" for r in rl.values())};'
      f' missing/error: {sum(r["light"] in ("missing", "error") for r in rl.values())}')
    for mi, r in rl.items():
        if r['light'] == 'error':
            p(f'error: material {mi} light map {short(r["slots"].get("light"))}: {r["light_info"]["error"]}')
    glow = [mi for mi, r in rl.items() if r['glow_name'] or r['glow_bright']]
    p(f'glow candidates: {glow or "none"} area share {sum(rl[mi]["share"] for mi in glow):.3f}'
      f' (name match: {[mi for mi in glow if rl[mi]["glow_name"]]}, light map bright share >= {MOSTLY_BRIGHT}:'
      f' {[mi for mi in glow if rl[mi]["glow_bright"]]})')
    r = c['rules']
    p('draws per rule: ' + '  '.join(f'{k}={r[k]}' for k, _ in RULE_LABELS) + f'  (original record {c["draws"]})')
    for k, label in RULE_LABELS:
        p(f'  {k}: {label}')
    p(f'  diagnostics: b merges mixing alpha and opaque {r["b_mixes_alpha"]};'
      f' a merges whose non-texture params differ {r["a_param_diffs"]}')


def main(argv=None):
    ap = argparse.ArgumentParser(description='Material census of one BOB1 LOD record (prints numbers only).')
    ap.add_argument('body', nargs='+', help='archive body name (ships/argon/argon_TL) or a decoded body file')
    ap.add_argument('--lod', type=int, help='record index (default: the original coarsest record n-1)')
    ap.add_argument('--game', type=Path, default=bob1.DEFAULT_GAME)
    a = ap.parse_args(argv)
    assets, skipped = original_assets(a.game)
    if skipped:
        print(f'skipped overlay catalogues: {skipped}')
    for name in a.body:
        path = Path(name)
        if path.is_file():
            data, origin = bob1.load(path)
        else:
            entry = bob1.resolve_body(assets, name)
            data, origin = assets.read_entry(entry), f'{entry["source"]}:{entry["path"]}'
        tree = bob1.parse(data)
        n = len(bob1.lods(tree))
        if a.lod is not None and not 0 <= a.lod < n:
            raise SystemExit(f'{name}: --lod {a.lod} outside 0..{n - 1}')
        format_census(census(tree, a.lod, assets), origin)
    return 0


if __name__ == '__main__':
    sys.exit(main())
