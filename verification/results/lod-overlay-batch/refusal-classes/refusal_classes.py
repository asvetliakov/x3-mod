#!/usr/bin/env python3
"""2026-09-24 refusal classes (dominant_slot_missing, no_diffuse, texture_unresolved) of the merged-LOD baker.

  refusal_classes.py fleet [BATCH_RECORD]      # the three classes in a batch record (default: the bottle's installed
                                               # addon/x3m-lod-batch.json, read-only): counts by category and effect detail
  refusal_classes.py effects | nodiff | textures | params   # per class detail from the bottle's catalogues (read-only)
  refusal_classes.py table RECORD...         # per body: eligible or reason, draws r0 -> C, atlas size, texel figures
  refusal_classes.py compare BEFORE AFTER      # overlay roots (--out): every member of every addon/NN.cat decoded and
                                               # compared by sha256, plus the per-body manifest rows without the settings

Output of the 2026-09-24 run: refusal_classes_out.txt beside this script.
"""
import collections
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
from inspect_x3 import read_catalogue   # noqa: E402

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
CLASSES = ('dominant_slot_missing', 'no_diffuse', 'texture_unresolved')


def fleet(path):
    rec = json.loads(Path(path).read_text())
    rows = [b for b in rec['bodies'] if not b.get('filter') and set(b.get('refuse') or ()) & set(CLASSES)]
    c = collections.Counter((r, b['cat'], 'only' if len(b['refuse']) == 1 else 'with ' + ','.join(
        x for x in b['refuse'] if x != r)) for b in rows for r in b['refuse'] if r in CLASSES)
    for k, v in sorted(c.items()):
        print(f'{v:4d} {k[0]:24s} {k[1]:8s} {k[2]}')
    for b in rows:
        print(f'  {",".join(b["refuse"]):40s} {b["name"]}  {b.get("error", "")}')


def _bodies(reason):
    """(name, mats, record 0, alpha set) of the installed record's bodies refused only for `reason`."""
    import bob1
    import lod_overlay
    rec = json.loads((GAME / 'addon/x3m-lod-batch.json').read_text())
    assets, _ = lod_overlay.original_assets(GAME)
    for b in rec['bodies']:
        if b.get('filter') or reason not in (b.get('refuse') or ()):
            continue
        try:
            entry = bob1.resolve_body(assets, b['name'])
        except bob1.FormatError:                          # ambiguous_body_ext: read the binary member
            entry = next(iter(assets.candidates('objects/' + b['name'].lower() + '.bob')))
        tree = bob1.parse(assets.read_entry(entry), 8)
        mats = bob1.materials(tree)
        yield b['name'], mats, bob1.lods(tree)[0], lod_overlay.alpha_materials(mats), assets


def effects():
    """dominant_slot_missing: per effect class, whether any of its materials declares t_LightMapTexture."""
    import lod_atlas
    c = collections.Counter()
    for name, mats, r0, alpha, _ in _bodies('dominant_slot_missing'):
        opaque = [g for p in r0['parts'] if not p['flags'] & lod_atlas.HIDDEN_PART for g in p['groups']
                  if g['material'] not in alpha]
        for eff, mis in lod_atlas.effect_classes(mats, opaque):
            if lod_atlas.SLOT_NAMES['light'] not in lod_atlas.declared(mats[lod_atlas.dominant(
                    [g for g in opaque if g['material'] in mis])]):
                some = any(lod_atlas.SLOT_NAMES['light'] in lod_atlas.declared(mats[m]) for m in mis)
                c[(eff, 'a sibling declares it' if some else 'no material declares it')] += 1
                print(f'  {name}: effect {eff} materials {mis}')
    for k, v in sorted(c.items()):
        print(f'{v:4d} {k[0]:24s} {k[1]}')


def nodiff():
    """no_diffuse: the materials without a diffuse map, their slot state and face-area share."""
    import body_materials
    import lod_atlas
    for name, mats, r0, alpha, _ in _bodies('no_diffuse'):
        area, bad = {}, []
        for p in r0['parts']:
            if not p['flags'] & lod_atlas.HIDDEN_PART:
                for g in p['groups']:
                    if g['material'] not in alpha:
                        area[g['material']] = area.get(g['material'], 0.0) + sum(
                            body_materials.face_area(r0['points'], f) for f in g['faces'])
        for m in area:
            d = lod_atlas.material_slots(mats[m], ('diffuse',))['diffuse'] if 'params' in mats[m] else b'?'
            if d is None or body_materials.is_null(d):
                bad.append((m, 'no parameter' if d is None else 'NULL',
                            lod_atlas.material_slots(mats[m], ('light',))['light']))
        share = sum(area[m] for m, _, _ in bad) / sum(area.values())
        print(f'  {name}: area share {share:.4f} {bad}')


def textures():
    """texture_unresolved: every unresolved name of the refused bodies and the catalogue members with its stem."""
    from pathlib import PurePosixPath
    import body_materials
    import lod_atlas
    unres, assets = {}, None
    for name, mats, r0, alpha, assets in _bodies('texture_unresolved'):
        used = sorted({g['material'] for p in r0['parts'] if not p['flags'] & lod_atlas.HIDDEN_PART for g in p['groups']})
        for m in used:
            if 0 <= m < len(mats) and 'params' in mats[m]:
                for slot, v in lod_atlas.material_slots(mats[m], ('diffuse', 'light', 'bump', 'specular')).items():
                    if v is not None and not body_materials.is_null(v):
                        try:
                            lod_atlas.texture_source(assets, v)
                        except lod_atlas.AtlasError:
                            unres.setdefault(v, set()).add(f'{name}#{m}.{slot}')
    for v, users in unres.items():
        stem = PurePosixPath(v.decode('latin1').replace('\\', '/')).stem.lower()
        hits = [e['source'] + ':' + e['path'] for k in assets.entries if PurePosixPath(k).stem.lower() == stem
                for e in assets.entries[k]]
        print(f'  {v.decode("latin1"):40s} in {len(hits)} member(s) {hits} used by {sorted(users)}')


def params():
    """Fleet survey (2 min): non-texture parameters named *diff* / *colo(u)r* over every binary ship/station body,
    and the materials with a NULL or absent t_DiffuseTexture by effect."""
    import bob1
    import body_materials
    import lod_overlay
    rec = json.loads((GAME / 'addon/x3m-lod-batch.json').read_text())
    assets, _ = lod_overlay.original_assets(GAME)
    names, nodiff_, n = collections.Counter(), collections.Counter(), 0
    for b in rec['bodies']:
        if b['cat'] not in ('ship', 'station'):
            continue
        try:
            entry = bob1.resolve_body(assets, b['name'])
            if entry['path'].lower().endswith(('.pbd', '.bod')):
                continue
            tree = bob1.parse(assets.read_entry(entry), 8)
        except Exception:
            continue
        n += 1
        for m in bob1.materials(tree):
            if 'params' in m:
                for pn, pt, _ in m['params']:
                    low = pn.lower()
                    if (b'diff' in low or b'colo' in low) and not low.startswith(b't_'):
                        names[(pn.decode(), pt)] += 1
                d = body_materials.slots(m).get('diffuse')
                if d is None or body_materials.is_null(d):
                    nodiff_[(m['effect'].decode().lower(), 'absent' if d is None else 'NULL')] += 1
    print(f'bodies {n}')
    for k, v in names.most_common():
        print(f'{v:6d} {k}')
    for k, v in nodiff_.most_common():
        print(f'{v:6d} no diffuse {k}')


def table(paths):
    print(f'{"body":58s} {"result":22s} {"draws":>9s} {"atlas":>5s} {"bytes":>9s} {"min":>6s} {"weighted":>8s} {"starved":>8s}')
    for path in paths:
        for b in json.loads(Path(path).read_text())['bodies']:
            res = 'ELIGIBLE' if b.get('eligible') else ','.join((b.get('refuse') or []) + (b.get('filter') or []))
            est, tex = b.get('estimate') or {}, b.get('texel') or {}
            draws = f'{b.get("r0_drawn")}->{b["draws"]}' if 'draws' in b else f'{b.get("r0_drawn")}->-'
            size = b.get('atlas_size') or est.get('size') or '-'
            print(f'{b["name"]:58s} {res:22s} {draws:>9s} {size!s:>5s} {b.get("atlas_bytes", "-")!s:>9s}'
                  f' {est.get("ratio", float("nan")):6.3f} {tex.get("weighted_texels_per_px") or float("nan"):8.3f}'
                  f' {100 * (tex.get("starved_share") or 0):7.3f}%')
            if b.get('error'):
                print(f'{"":58s} {b["error"]}')


def members(root):
    out = {}
    for cat in sorted((Path(root) / 'addon').glob('*.cat')):
        raw = bytes(v ^ 0x33 for v in cat.with_suffix('.dat').read_bytes())
        for e in read_catalogue(cat):
            out[f'{cat.name}:{e["path"]}'] = hashlib.sha256(raw[e['offset']:e['offset'] + e['size']]).hexdigest()
    return out


def manifests(root):
    out = {}
    for m in sorted((Path(root) / 'addon').glob('*.x3m-lod.json')):
        for b in json.loads(m.read_text()).get('bodies', []):
            out[b['name']] = b
    return out


def compare(before, after):
    a, b = members(before), members(after)
    same = sorted(k for k in a if b.get(k) == a[k])
    print(f'members before {len(a)} after {len(b)} identical {len(same)}')
    for k in sorted(set(a) | set(b)):
        print(f'  {"same" if a.get(k) == b.get(k) else "DIFF"} {k} {a.get(k, "-")[:16]} {b.get(k, "-")[:16]}')
    ma, mb = manifests(before), manifests(after)
    for name in sorted(set(ma) | set(mb)):
        print(f'  manifest {name}: {"same" if ma.get(name) == mb.get(name) else "DIFF"}')
    return 0 if a == b else 1


if __name__ == '__main__':
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == 'fleet':
        fleet(args[0] if args else GAME / 'addon/x3m-lod-batch.json')
    elif cmd in ('effects', 'nodiff', 'textures', 'params'):
        globals()[cmd]()
    elif cmd == 'table':
        table(args)
    elif cmd == 'compare':
        sys.exit(compare(*args))
    else:
        sys.exit(__doc__)
