#!/usr/bin/env python3
"""Read-only census for docs/architecture/plumes-no-glow-hulls.md: ships whose scene has no main glow (JET) part and
therefore no plume today, on both editions, with the data the three nozzle-placement candidates would use.

Views: `ap` = C:\\X3 installed (Mayhem 3, x3m overlay slots excluded), `ap_stock` = STOCK_AP_CATALOGUES, `fl` = C:\\X3FL
installed (addon2, overlay slots excluded). Nothing is written into the game trees; no game launch, no Wine.

Per TShips row (X3 Editor 2 layout): scene parts (engine_effects_census.scene_parts), JET-list parts and their role
(main / emitter / rcs), col 11 effect row elements, col 49 trail, the hull part (first non-dummy body), the hull's
LOD-0 extent, the hull faces whose material names an engine (ENGINE_MAT), and for ships WITH glow parts the main jets'
position relative to the hull extent and their LOD-0 value against the hull half-width (the calibration a synthetic
nozzle law would use). Also a survey of part-body names in the no-glow scenes (candidate (a): engine/exhaust dummies).

  python3 verification/results/plumes-no-glow/census.py [--ap /mnt/c/X3] [--fl /mnt/c/X3FL]
Writes census_out.json and census_out.txt beside this script (~60 s).
"""
import argparse
import collections
import json
import re
import statistics
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/results/engine-effects-census'))
import bob1  # noqa: E402
import lod_overlay  # noqa: E402
from sector_fog_census import Assets, STOCK_AP_CATALOGUES  # noqa: E402
import engine_effects_census as C  # noqa: E402

SHIP_CLASSES = {'OBJ_SHIP_M0', 'OBJ_SHIP_M1', 'OBJ_SHIP_M2', 'OBJ_SHIP_M3', 'OBJ_SHIP_M4', 'OBJ_SHIP_M5', 'OBJ_SHIP_M6',
                'OBJ_SHIP_M7', 'OBJ_SHIP_M8', 'OBJ_SHIP_TL', 'OBJ_SHIP_TM', 'OBJ_SHIP_TP', 'OBJ_SHIP_TS',
                'OBJ_SHIP_GONER', 'OBJ_SHIP_DRONECARRIER', 'OBJ_SHIP_MISSILE_FRIGATE', 'OBJ_SHIP_SPECIALDRONE'}
ENGINE_MAT = re.compile(r'fx_engine|engine|thrust|exhaust|nozzle|glow|flare', re.I)
NAME_SURVEY = re.compile(r'engine|exhaust|thrust|nozzle|flare|glow|light|dummy|effect|jet|fx', re.I)
EEDF_LENSFLARE, EEDF_LIGHT, EEDF_PARTICLEGENERATOR = 4, 0x80, 0x10000


def cls_short(c):
    return c.replace('OBJ_SHIP_', '')


def hull_geometry(assets, ref, cache):
    """LOD-0 value, extent (scene units) and the engine-material faces' centroid / z-extent of a hull body."""
    if ref in cache:
        return cache[ref]
    out = dict(ok=False)
    try:
        tree, e, sha, _ = C.load_body(assets, ref)
        rec = bob1.lods(tree)[0]
        lo, hi = C.extent(rec)
        s = rec['value'] / 65536.0
        pts = np.array([q[1:4] for q in rec['points']], np.float64) * s
        mats = bob1.materials(tree)
        names = []
        for m in mats:
            names.append(' '.join([m.get('effect', b'').decode('latin1')] +
                                  [v.decode('latin1') for _, t, v in m.get('params', []) if t == 8] +
                                  [str(m.get('texture', ''))]))
        eng_faces = []
        for part in rec['parts']:
            for g in part['groups']:
                mi = g['material']
                if 0 <= mi < len(names) and ENGINE_MAT.search(names[mi]):
                    eng_faces.extend(f[:3] for f in g['faces'])
        out.update(ok=True, value=rec['value'], lo=[round(float(v)) for v in lo], hi=[round(float(v)) for v in hi],
                   materials=len(mats), engine_materials=sum(1 for n in names if ENGINE_MAT.search(n)),
                   engine_material_names=sorted({re.sub(r'\s+', ' ', n)[:60] for n in names if ENGINE_MAT.search(n)})[:6])
        if eng_faces:
            idx = np.unique(np.array(eng_faces, np.int64))
            q = pts[idx]
            cen = q.mean(0)
            out['engine_faces'] = int(len(eng_faces))
            out['engine_centroid'] = [round(float(v)) for v in cen]
            out['engine_z'] = [round(float(q[:, 2].min())), round(float(q[:, 2].max()))]
            L = hi[2] - lo[2]
            out['engine_centroid_zfrac'] = round(float((cen[2] - lo[2]) / L), 3) if L > 0 else None
    except Exception as exc:  # text body the parser refuses, missing member
        out['error'] = str(exc)[:80]
    cache[ref] = out
    return out


def jet_value(assets, key, cache):
    if key in cache:
        return cache[key]
    v = None
    try:
        tree, e, sha, _ = C.load_body(assets, key.replace('/', '\\'))
        v = bob1.lods(tree)[0]['value']
    except Exception:
        pass
    cache[key] = v
    return v


def effect_kind(eff, row):
    """What a types/Effects row shows: sprite (a body element), flare, light, generator."""
    e = eff.get(row)
    if not e:
        return 'absent'
    kinds = set()
    for el in e.get('elements', []):
        fl = el.get('flags', 0) if isinstance(el.get('flags', 0), int) else 0
        if el.get('body') not in (None, 0, '0', -1, '-1'):
            kinds.add('sprite')
        if fl & EEDF_LENSFLARE:
            kinds.add('flare')
        if fl & EEDF_LIGHT:
            kinds.add('light')
        if fl & EEDF_PARTICLEGENERATOR:
            kinds.add('generator')
    return '+'.join(sorted(kinds)) or 'empty'


def view(label, assets):
    ships, tprov = C.tships(assets)
    jet_declared, jet_listed, jets = C.jet_bodies(assets)
    bodies_prov = assets.get('types/Bodies.txt')[1].get('source')
    try:
        eff = C.effects(assets)
    except Exception as exc:
        eff = {}
        print(f'{label}: effects table unreadable: {exc}', file=sys.stderr)
    hull_cache, jet_cache = {}, {}
    rows, unreadable, non_ship = [], collections.Counter(), collections.Counter()
    survey = collections.Counter()
    calib = collections.defaultdict(list)   # class -> (zfrac, lateral xfrac, value/halfwidth, hull length)
    for s in ships:
        cls = s['cls']
        if cls not in SHIP_CLASSES:
            non_ship[cls] += 1
            continue
        try:
            data, prov = C.scene_member(assets, s['scene'])
        except FileNotFoundError:
            unreadable['missing'] += 1
            continue
        if data[:4] == b'CUT1':
            unreadable['binary'] += 1
            rows.append(dict(id=s['id'], cls=cls_short(cls), race=C.RACE.get(int(s['race'] or 0), s['race']), scene=s['scene'],
                             glow=None, binary=True, effect=s['effect'], trail=s['trail']))
            continue
        parts = C.scene_parts(data)
        main, emit, rcs, sites_a, sites_b = 0, 0, 0, 0, 0
        jet_parts = []
        for p in parts:
            k = C.body_key(p['body'])
            if k not in jets:
                continue
            c = p['c'] or 0
            role = C.role(k, c)
            if role == 'main':
                main += 1
                jet_parts.append((k, p))
            elif role == 'emitter':
                emit += 1
            else:
                rcs += 1
            if not c & 0xffe:
                sites_a += not c & 0x4000
                sites_b += not c & 0x2000
        hull = next((p for p in parts if not re.search(r'props|dummy|^effects|^-1$|^\d+$|animprops|cockpit', C.norm(p['body']))), None)
        hull_ref = hull['body'] if hull else None
        geo = hull_geometry(assets, hull_ref, hull_cache) if hull_ref else dict(ok=False, error='no hull part')
        f0 = hull['frames'][0] if hull and hull['frames'] else []
        hull_pos = [float(x) for x in f0[1:4]] if len(f0) >= 4 else [0., 0., 0.]
        hull_angle = float(f0[4]) if len(f0) >= 5 else 0.
        row = dict(id=s['id'], cls=cls_short(cls), race=C.RACE.get(int(s['race'] or 0), s['race']), scene=s['scene'],
                   glow=main, emitter=emit, rcs=rcs, sites_a=sites_a, sites_b=sites_b, parts=len(parts),
                   effect=s['effect'], effect_kind=effect_kind(eff, int(s['effect'])) if s['effect'].lstrip('-').isdigit() else '?',
                   trail=s['trail'], speed=s['speed'], hull=hull_ref, hull_geo=geo, hull_angle=hull_angle)
        if main == 0:
            for p in parts:
                stem = C.norm(p['body'])
                if NAME_SURVEY.search(stem):
                    survey[stem] += 1
            row['part_bodies'] = sorted({C.norm(p['body']) for p in parts})[:40]
        elif geo.get('ok') and hull_angle == 0.:
            lo, hi = geo['lo'], geo['hi']
            L, W = hi[2] - lo[2], (hi[0] - lo[0]) / 2
            for k, p in jet_parts:
                f = p['frames'][0] if p['frames'] else []
                if len(f) < 4:
                    continue
                pos = [float(x) - hp for x, hp in zip(f[1:4], hull_pos)]
                v = jet_value(assets, k, jet_cache)
                if L > 0 and W > 0 and v:
                    calib[cls_short(cls)].append((round((pos[2] - lo[2]) / L, 3), round(abs(pos[0]) / W, 3), round(v / W, 3), L))
        rows.append(row)
    return dict(label=label, tships=tprov.get('source'), bodies=bodies_prov, jet_listed=jet_listed, ships=len(ships),
                rows=rows, unreadable=dict(unreadable), non_ship=dict(non_ship), survey=survey, calib=calib)


def summarise(v):
    rows = [r for r in v['rows'] if r.get('glow') is not None]
    noglow = [r for r in rows if r['glow'] == 0]
    by_cls = collections.Counter(r['cls'] for r in noglow)
    tot_cls = collections.Counter(r['cls'] for r in rows)
    by_race = collections.Counter(r['race'] for r in noglow)
    cand_a = sum(1 for r in noglow if any(re.search(r'engine|exhaust|thrust|nozzle|flare', b) for b in r.get('part_bodies', [])))
    eng_mat = sum(1 for r in noglow if r['hull_geo'].get('engine_faces'))
    hull_ok = sum(1 for r in noglow if r['hull_geo'].get('ok'))
    kinds = collections.Counter(r['effect_kind'] for r in noglow)
    sites = collections.Counter((r['sites_a'] > 0, r['sites_b'] > 0) for r in noglow)
    trails = sum(1 for r in noglow if r['trail'] not in ('0', ''))
    emit = sum(1 for r in noglow if r['emitter'])
    calib = {}
    for c, vals in sorted(v['calib'].items()):
        if len(vals) >= 3:
            calib[c] = dict(jets=len(vals), zfrac_median=round(statistics.median(x[0] for x in vals), 3),
                            zfrac_p90=round(np.percentile([x[0] for x in vals], 90), 3),
                            xfrac_median=round(statistics.median(x[1] for x in vals), 3),
                            value_over_halfwidth_median=round(statistics.median(x[2] for x in vals), 3),
                            value_over_halfwidth_p10_p90=[round(float(np.percentile([x[2] for x in vals], q)), 3) for q in (10, 90)])
    return dict(label=v['label'], tships=v['tships'], bodies=v['bodies'], jet_listed=v['jet_listed'], tships_rows=v['ships'],
                ship_rows=len(v['rows']), readable=len(rows), unreadable=v['unreadable'], non_ship_rows=sum(v['non_ship'].values()),
                no_glow=len(noglow), no_glow_by_class={c: f'{by_cls[c]}/{tot_cls[c]}' for c in sorted(tot_cls)},
                no_glow_by_race=dict(by_race.most_common()), no_glow_effect_kind=dict(kinds),
                no_glow_sites=dict((f'a={a} b={b}', n) for (a, b), n in sites.items()), no_glow_with_trail_col=trails,
                no_glow_with_emitter_parts=emit, no_glow_hull_readable=hull_ok, no_glow_engine_material_faces=eng_mat,
                no_glow_candidate_a_named_parts=cand_a, name_survey=dict(v['survey'].most_common(25)), calibration=calib,
                no_glow_ships=[dict(id=r['id'], cls=r['cls'], race=r['race'], effect=r['effect'], kind=r['effect_kind'],
                                    trail=r['trail'], emitter=r['emitter'], rcs=r['rcs'], speed=r['speed'], hull=r['hull'],
                                    hull_value=r['hull_geo'].get('value'), hull_lo=r['hull_geo'].get('lo'), hull_hi=r['hull_geo'].get('hi'),
                                    engine_faces=r['hull_geo'].get('engine_faces', 0), engine_zfrac=r['hull_geo'].get('engine_centroid_zfrac'),
                                    engine_centroid=r['hull_geo'].get('engine_centroid'),
                                    engine_names=r['hull_geo'].get('engine_material_names', []),
                                    named_parts=[b for b in r.get('part_bodies', []) if re.search(r'engine|exhaust|thrust|nozzle|flare', b)],
                                    hull_error=r['hull_geo'].get('error')) for r in noglow],
                binary_scene_ships=[dict(id=r['id'], cls=r['cls'], race=r['race']) for r in v['rows'] if r.get('binary')])


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--ap', default='/mnt/c/X3')
    ap.add_argument('--fl', default='/mnt/c/X3FL')
    a = ap.parse_args(argv)
    out = {}
    assets, skipped = lod_overlay.original_assets(a.ap, edition='ap')
    out['ap'] = summarise(view('ap (C:\\X3 installed, Mayhem 3; skipped %s)' % skipped, assets))
    out['ap_stock'] = summarise(view('ap_stock (STOCK_AP_CATALOGUES)', Assets(Path(a.ap), catalogues=STOCK_AP_CATALOGUES, edition='ap')))
    assets, skipped = lod_overlay.original_assets(a.fl, edition='fl')
    out['fl'] = summarise(view('fl (C:\\X3FL installed; skipped %s)' % skipped, assets))
    (HERE / 'census_out.json').write_text(json.dumps(out, indent=1))
    lines = []
    for k, v in out.items():
        lines.append(f"== {v['label']}: TShips {v['tships']} ({v['tships_rows']} rows, {v['non_ship_rows']} non-ship), Bodies {v['bodies']} "
                     f"({v['jet_listed']} JET tokens); ship rows {v['ship_rows']}, readable scenes {v['readable']}, unreadable {v['unreadable']}")
        lines.append(f"   no glow: {v['no_glow']}  by class {v['no_glow_by_class']}")
        lines.append(f"   by race {v['no_glow_by_race']}")
        lines.append(f"   col-11 kind {v['no_glow_effect_kind']}  spawn sites {v['no_glow_sites']}  trail col set {v['no_glow_with_trail_col']}  "
                     f"emitter parts {v['no_glow_with_emitter_parts']}")
        lines.append(f"   hull readable {v['no_glow_hull_readable']}  engine-material faces {v['no_glow_engine_material_faces']}  "
                     f"named engine/exhaust parts (cand. a) {v['no_glow_candidate_a_named_parts']}")
        lines.append(f"   name survey (no-glow scenes): {v['name_survey']}")
        lines.append(f"   calibration from glow ships (per class: jets, rear z fraction median/p90, |x|/halfwidth, value/halfwidth):")
        for c, cal in v['calibration'].items():
            lines.append(f"      {c}: {cal}")
        lines.append('   no-glow ships:')
        for r in v['no_glow_ships']:
            lines.append(f"      {r['id']} {r['cls']} {r['race']} eff={r['effect']}({r['kind']}) trail={r['trail']} em={r['emitter']} rcs={r['rcs']} "
                         f"hull={r['hull']} value={r['hull_value']} z=[{(r['hull_lo'] or [None]*3)[2]},{(r['hull_hi'] or [None]*3)[2]}] "
                         f"engfaces={r['engine_faces']} engz={r['engine_zfrac']} named={r['named_parts']} {r['hull_error'] or ''}")
        if v['binary_scene_ships']:
            lines.append(f"   binary (CUT1) scenes, not read: {[(b['id'], b['cls']) for b in v['binary_scene_ships']]}")
    (HERE / 'census_out.txt').write_text('\n'.join(lines) + '\n')
    print('\n'.join(l for l in lines if not l.startswith('      ') or l.strip().startswith(('M', 'T'))))


if __name__ == '__main__':
    main()
