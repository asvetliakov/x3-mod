#!/usr/bin/env python3
"""Obstacle sizes and sector geometry from the type tables and the universe map (installed Mayhem 4 and stock AP).

Universe map: <o t="1" size=.. x= y=> sectors with children <o t=.. s=.. x= y= z=>; t is the engine class
(5 TDocks, 6 TFactories, 17 TAsteroids, 18 TGates, 28 TDebris, 3 suns, 4 planets; sector-collide.md section 9).
Station 's' is the type id (last TDocks/TFactories column); asteroid/gate/debris 's' is the row index.
Object radii: scene_radius.radii (static estimate of [obj+0xa4]); asteroids: root body value (TAsteroids col 0)
as the radius, the col-10 'dummypositions' scene reported separately (whether it is attached is not traced).
All positions and radii in engine units, 500 per metre. Writes map_stats.json beside this script.
"""
import itertools, json, math, statistics, sys, xml.etree.ElementTree as ET
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'tools/analysis')); sys.path.insert(0, str(HERE))
from sector_fog_census import Assets, names, STOCK_AP_CATALOGUES
import scene_radius as sr, ships

U = 500.0


def pct(v, q):
    v = sorted(v)
    return v[min(len(v) - 1, int(q * (len(v) - 1) + 0.5))] if v else None


def summ(v, scale=1 / U / 1000, nd=2):     # default: km
    v = [x for x in v if x is not None]
    if not v: return None
    return dict(n=len(v), min=round(min(v) * scale, nd), p10=round(pct(v, .1) * scale, nd),
                median=round(statistics.median(v) * scale, nd), p90=round(pct(v, .9) * scale, nd), max=round(max(v) * scale, nd))


def type_radii(a, stem, scene_col, body_col=None, key='id'):
    _, rows, prov = ships.table(a, stem)
    res, _ = names(a)
    out = {}
    for i, r in enumerate(rows):
        if len(r) <= scene_col: continue
        sc = r[scene_col]
        rad = sr.radii(a, sc) if sc and sc != '0' else None
        rv = rad[0] if rad else 0
        scene_u = rv
        if body_col is not None and r[body_col] not in ('', '0'):
            rv = sr.body_value(a, r[body_col])      # asteroid: root body half-extent (scene = break-up dummies)
            scene_u = max(scene_u, rv)
        ident = (r[-2] if r[-1] == '' else r[-1]) if key == 'id' else i
        out[ident] = dict(row=i, subtype=r[5] if len(r) > 5 else '', scene=sc, radius_u=rv, radius_with_scene_u=scene_u,
                          name=res(17, int(r[6])) if len(r) > 6 and r[6].isdigit() else '')
    return out, prov


def dist(p, q):
    return math.sqrt(sum((p[i] - q[i]) ** 2 for i in range(3)))


def run(label, a):
    docks, pd = type_radii(a, 'TDocks', 11); facs, pf = type_radii(a, 'TFactories', 11)
    gates, pg = type_radii(a, 'TGates', 7, key='row'); ast, pa = type_radii(a, 'TAsteroids', 10, body_col=0, key='row')
    stations = {**docks, **facs}
    # station radii per subtype family
    fam = {}
    for k, v in stations.items():
        f = v['subtype'].replace('SG_', '')
        if v['radius_u'] > 0: fam.setdefault(f, []).append(v['radius_u'])
    fam = {k: dict(n=len(v), min_m=round(min(v) / U), median_m=round(statistics.median(v) / U), max_m=round(max(v) / U))
           for k, v in sorted(fam.items())}
    biggest = sorted(((v['radius_u'] / U, v['name'] or k, v['subtype']) for k, v in stations.items()), reverse=True)[:6]
    d, prov = a.get('addon/maps/x3_universe.xml')
    root = ET.fromstring(d)
    sec_size, extent, gg, gs, nn_min, nn_p10, nn_med, nst, gcount, ast_r_placed, gate_r = [], [], [], [], [], [], [], [], [], [], []
    missing_types = set()
    ast_count, ast_nn_med, ast_gate_min = [], [], []
    for sec in (o for o in root.iter('o') if o.get('t') == '1'):
        sec_size.append(int(sec.get('size', '0')))
        objs = [(c.get('t'), c.get('s'), (int(float(c.get('x', 0))), int(float(c.get('y', 0))), int(float(c.get('z', 0)))))
                for c in sec if c.get('x') is not None]
        local = [p for t, s, p in objs if t in ('5', '6', '17', '18', '28')]
        if local: extent.append(max(max(abs(v) for v in p) for p in local))
        g = [p for t, s, p in objs if t == '18']
        gcount.append(len(g))
        for t, s, p in objs:
            if t == '18' and s and s.isdigit() and int(s) in gates: gate_r.append(gates[int(s)]['radius_u'])
            if t == '17' and s and s.isdigit() and int(s) in ast: ast_r_placed.append(ast[int(s)]['radius_u'])
        st = []
        for t, s, p in objs:
            if t in ('5', '6'):
                st.append(p)
                if s not in stations: missing_types.add(s)
        nst.append(len(st))
        ap = [p for t, s, p in objs if t == '17']
        ast_count.append(len(ap))
        if len(ap) >= 2 and len(ap) <= 3000:
            ast_nn_med.append(statistics.median(min(dist(p, q) for q in ap if q is not p) for p in ap))
        if ap and g:
            ast_gate_min += [min(dist(p, q) for q in ap) for p in g]
        gg += [dist(p, q) for p, q in itertools.combinations(g, 2)]
        if st:
            gs += [min(dist(p, q) for q in st) for p in g]
        if len(st) >= 2:
            nn = [min(dist(p, q) for q in st if q is not p) for p in st]
            nn_min.append(min(nn)); nn_p10.append(pct(nn, .1)); nn_med.append(statistics.median(nn))
    out = dict(sources=dict(TDocks=pd['source'], TFactories=pf['source'], TGates=pg['source'], TAsteroids=pa['source'],
                            universe=prov['source']),
               sectors=len(sec_size), sector_size_attr_km=summ(sec_size), object_extent_linf_km=summ(extent),
               gates_per_sector=dict(min=min(gcount), median=statistics.median(gcount), max=max(gcount)),
               gate_to_gate_km=summ(gg), gate_to_nearest_station_km=summ(gs),
               stations_per_sector=dict(min=min(nst), median=statistics.median(nst), max=max(nst), total=sum(nst)),
               station_nn_per_sector_min_km=summ(nn_min), station_nn_per_sector_p10_km=summ(nn_p10),
               station_nn_per_sector_median_km=summ(nn_med),
               station_radius_by_family_m=fam, biggest_station_types_m=biggest,
               gate_radius_m=summ(gate_r, 1 / U, 0), gate_types={k: dict(radius_m=round(v['radius_u'] / U), scene=v['scene'])
                                                                for k, v in gates.items()},
               asteroid_types={k: dict(radius_m=round(v['radius_u'] / U), with_scene_m=round(v['radius_with_scene_u'] / U), scene=v['scene']) for k, v in ast.items()},
               asteroid_radius_placed_m=summ(ast_r_placed, 1 / U, 0),
               asteroids_per_sector=dict(min=min(ast_count), median=statistics.median(ast_count), max=max(ast_count), total=sum(ast_count)),
               asteroid_nn_median_per_sector_km=summ(ast_nn_med), gate_to_nearest_asteroid_centre_km=summ(ast_gate_min), station_types_not_in_tables=len(missing_types))
    return out


def main():
    res = {}
    for label, a in (('installed', Assets(Path('/mnt/c/X3'))), ('stock', Assets(Path('/mnt/c/X3'), catalogues=STOCK_AP_CATALOGUES))):
        res[label] = run(label, a)
        r = res[label]
        print(f"== {label} {r['sources']}")
        for k in ('sectors', 'sector_size_attr_km', 'object_extent_linf_km', 'gates_per_sector', 'gate_to_gate_km',
                  'gate_to_nearest_station_km', 'stations_per_sector', 'station_nn_per_sector_min_km',
                  'station_nn_per_sector_p10_km', 'station_nn_per_sector_median_km', 'gate_radius_m',
                  'asteroid_radius_placed_m', 'asteroids_per_sector', 'asteroid_nn_median_per_sector_km', 'gate_to_nearest_asteroid_centre_km', 'station_types_not_in_tables', 'biggest_station_types_m'):
            print(f'  {k}: {r[k]}')
        print('  station_radius_by_family_m:', r['station_radius_by_family_m'])
        print('  gate types:', {k: v['radius_m'] for k, v in r['gate_types'].items()})
        print('  asteroid types (body/with scene):', {k: (v['radius_m'], v['with_scene_m']) for k, v in r['asteroid_types'].items()})
    (HERE / 'map_stats.json').write_text(json.dumps(res, indent=1))


if __name__ == '__main__':
    main()
