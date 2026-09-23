#!/usr/bin/env python3
"""Fog-family inventory of a mod tree overlaid on the installed game (read-only).

Usage: python3 verification/results/fog-family-data/mod_family_census.py /tmp/x3-mod1 [/tmp/x3-mod2 ...]
Prints, per tree: TBackgrounds source, positive-dust records/families, families not among the
14 compiled profiles, name-length bound, dust-part counts and formats, diffuse-texture
resolution (per family distinct textures, DDS pixel formats), the map's fog sectors.
Uses the same catalogue resolver as bob1.py / lod_batch_census.py (sector_fog_census.Assets).
"""
import collections, struct, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import sector_fog_census as sfc  # noqa: E402
GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
COMPILED = set('bluewell foggreenoutlands fogbluedistance fogcyancorner fogdeepred foggreeneye fogparanid '
               'fogred uranus uranus3 whitenexus fogblue fogkhaak khaakhive'.split())


def pixel_format(data):
    flags, fourcc, bits = struct.unpack_from('<I4sI', data, 80)
    return fourcc.decode('ascii', 'replace').strip('\0') or f'uncompressed-{bits}bit'


def census(tree):
    cats = sorted((tree / 'addon').glob('[0-9][0-9].cat'))
    a = sfc.Assets(GAME, mods=cats)
    bg, src = a.logical('types/tbackgrounds', ('.pck', '.txt'))
    rows = sfc.backgrounds(bg)
    pos = [r for r in rows if r['dust'] > 0]
    fams = sorted({r['family'] for r in pos})
    out = dict(tree=str(tree), cats=[c.name for c in cats], tbackgrounds=src['source'], records=len(rows),
               positive_records=len(pos), positive_families=len(fams),
               not_compiled=sorted(f for f in fams if f not in COMPILED),
               name_length=[min(map(len, fams)), max(map(len, fams))],
               rate_vectors=collections.Counter(tuple(r['body_rates']) for r in pos).most_common(3))
    parts = collections.Counter(); formats = collections.Counter(); status = collections.Counter()
    distinct = collections.Counter(); shas = set(); effects = collections.Counter()
    for fam in fams:
        fam_tex = set(); n = 0
        for slot in range(1, 9):
            d, s = a.logical(f'objects/environments/nebulae/{fam}/nebula_{fam}_dust_part{slot:02d}', ('.pbd', '.bod', '.pbb', '.bob'))
            if d is None:
                continue
            n += 1
            if not s['member'].lower().endswith(('.pbd', '.bod')):
                formats['binary'] += 1; continue
            for mat in sfc.body_metadata(d)['materials']:
                effects[mat['effect']] += 1
                sfc.resolve_material_textures(a, mat)
                t = mat['texture']; status[t['status']] += 1
                if t['status'] == 'resolved':
                    fam_tex.add(t['decoded_sha256']); shas.add(t['decoded_sha256'])
                    raw, _ = a.logical('dds/' + Path(mat['parameters']['t_DiffuseTexture'].replace('\\', '/')).stem, ('.pck', '.dds', '.tga'))
                    formats[f'{pixel_format(raw)} {t["width"]}x{t["height"]}'] += 1
        parts[n] += 1; distinct[len(fam_tex)] += 1
    out.update(dust_parts_per_family=dict(parts), texture_status=dict(status), effects=dict(effects),
               distinct_textures=len(shas), distinct_textures_per_family=dict(distinct), texture_formats=dict(formats))
    try:
        c = sfc.census(GAME, mods=cats)
        used = collections.Counter(s['family'] for s in c['sectors'] if s['dust'] > 0)
        out.update(map=c['map']['source'], sectors=len(c['sectors']), fog_sectors=sum(used.values()), fog_families_mapped=len(used))
    except Exception as exc:  # a generated-galaxy mod may ship no map
        out.update(map_error=f'{type(exc).__name__}: {exc}'[:160])
    return out


if __name__ == '__main__':
    for tree in sys.argv[1:] or ['/tmp/x3-mod1', '/tmp/x3-mod2']:
        for k, v in census(Path(tree)).items():
            print(f'{k}: {v}' if k != 'not_compiled' else f'{k}: {len(v)} {v[:6]}{"..." if len(v) > 6 else ""}')
        print()
