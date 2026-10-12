#!/usr/bin/env python3
"""Per-class ship dynamics and sizes from TShips (installed Mayhem 4 and stock AP), with derived steering numbers.

Column layout (X3 Editor 2 TShips, 0-based; checked against the engine loader, see the owning note
docs/reverse-engineering/game-scale-and-ship-classes.md section 3):
  0 body (root model)  2/3/4 rotation X/Y/Z (16.16 at record +8/+0xc/+0x10, read by 0x004eafc0)
  5 subtype SG_SH_*    6 name id (page 17)   7 speed (+0x44)   8 acceleration (+0x48)   16 scene (+0x68)
  26/27 max engine/rudder tunings  28/29 cargo min/max  44 cargo class  45 race  46 hull  50 variation
  52 class OBJ_SHIP_*
Units (EXE, owning note section 1): 500 engine units = 1 m and 1 m/s; rotation in turns/s (65536 angle units
per turn, rate x dt_ms/1000), so deg/s = 360 x value. All values are base (untuned); the engine multiplies
speed and acceleration by (1 + engine-tuning bonus) and rotation by (1 + rudder-tuning bonus).
Usage: ships.py  -> writes ships_by_class.json, ships_by_class.txt beside this script.
"""
import json, math, re, statistics, sys, zipfile, xml.etree.ElementTree as ET
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'tools/analysis')); sys.path.insert(0, str(HERE))
from sector_fog_census import Assets, names, STOCK_AP_CATALOGUES
import scene_radius as sr

X3 = Path('/mnt/c/X3')
U = 500.0


def table(assets, stem):
    for k in (f'addon/types/{stem}.txt', f'addon/types/{stem}.pck'):
        if assets.candidates(k):
            d, prov = assets.get(k)
            rows = [l.split(';') for l in d.decode('latin-1').splitlines() if l.strip() and not l.lstrip().startswith('/')]
            return rows[0], rows[1:], prov
    raise FileNotFoundError(stem)


def fnum(s):
    try: return float(s)
    except ValueError: return float('nan')


def ships(assets):
    head, rows, prov = table(assets, 'TShips')
    res, _ = names(assets)
    out = []
    for i, r in enumerate(rows):
        if len(r) < 53: continue
        rot = [fnum(r[c]) for c in (2, 3, 4)]
        rad = sr.radii(assets, r[16])
        nm = res(17, int(r[6])) if r[6].lstrip('-').isdigit() else ''
        out.append(dict(index=i, id=r[-2] if r[-1] == '' else r[-1], name=nm, subtype=r[5], cls=r[52],
                        variation=int(r[50]) if r[50].lstrip('-').isdigit() else None, race=r[45],
                        speed_raw=int(r[7]), accel_raw=int(r[8]), rot_raw=rot,
                        speed=int(r[7]) / U, accel=int(r[8]) / U, rot_dps=[360 * v for v in rot],
                        tunings=(r[26], r[27]), cargo_max=r[29], hull=int(r[46]) if r[46].isdigit() else None,
                        scene=r[16], radius_u=rad[0] if rad else None, hull_u=rad[1] if rad else None))
    return out, prov


def derived(s):
    w = min(s['rot_dps'][0], s['rot_dps'][1])           # slower of the two steering axes (X, Y)
    wr = math.radians(w) if w > 0 else float('nan')
    v, a = s['speed'], s['accel']
    d = dict(omega_dps=w, turn_radius_m=v / wr if wr else float('nan'), t90_s=90 / w if w else float('nan'),
             t180_s=180 / w if w else float('nan'),
             stop_m=v * v / (2 * a) if a > 0 else float('nan'), t_stop_s=v / a if a > 0 else float('nan'),
             arc180_m=v * 180 / w if w else float('nan'))
    d['diam180_m'] = 2 * d['turn_radius_m']
    return d


def stats(vals):
    v = sorted(x for x in vals if x is not None and not math.isnan(x))
    if not v: return None
    return dict(n=len(v), min=v[0], med=statistics.median(v), max=v[-1])


ORDER = ['M5', 'M4', 'M3', 'M6', 'M7', 'M1', 'M2', 'M8', 'TS', 'TP', 'TL', 'TM']


def by_class(ships_):
    groups = {}
    for s in ships_:
        if s['speed'] <= 0: continue
        key = s['subtype'].replace('SG_SH_', '')
        groups.setdefault(key, []).append(s)
    out = {}
    for k, g in groups.items():
        ds = [derived(s) for s in g]
        out[k] = dict(count=len(g), obj_classes=sorted({s['cls'] for s in g}),
                      speed=stats([s['speed'] for s in g]), accel=stats([s['accel'] for s in g]),
                      rot_x_raw=stats([s['rot_raw'][0] for s in g]), rot_y_raw=stats([s['rot_raw'][1] for s in g]),
                      rot_z_raw=stats([s['rot_raw'][2] for s in g]),
                      rot_x_dps=stats([s['rot_dps'][0] for s in g]), rot_y_dps=stats([s['rot_dps'][1] for s in g]),
                      rot_z_dps=stats([s['rot_dps'][2] for s in g]),
                      radius_m=stats([s['radius_u'] / U for s in g if s['radius_u']]),
                      hull_halfext_m=stats([s['hull_u'] / U for s in g if s['hull_u']]),
                      turn_radius_m=stats([d['turn_radius_m'] for d in ds]), t90_s=stats([d['t90_s'] for d in ds]),
                      t180_s=stats([d['t180_s'] for d in ds]), stop_m=stats([d['stop_m'] for d in ds]),
                      t_stop_s=stats([d['t_stop_s'] for d in ds]), arc180_m=stats([d['arc180_m'] for d in ds]),
                      diam180_m=stats([d['diam180_m'] for d in ds]),
                      slowest_turning=sorted(((round(derived(s)['omega_dps'], 2), s['name'] or s['id'], s['variation'])
                                              for s in g), key=lambda t: t[0])[:3])
    return out


def shipstats():
    """Mayhem 4 ShipStats.xlsx (mod author's table): name -> speed (m/s), class, hull."""
    z = zipfile.ZipFile(X3 / 'ShipStats.xlsx'); ns = '{http://schemas.openxmlformats.org/spreadsheetml/2006/main}'
    ss = [''.join(t.text or '' for t in si.iter(ns + 't')) for si in ET.fromstring(z.read('xl/sharedStrings.xml'))]
    root = ET.fromstring(z.read('xl/worksheets/sheet1.xml')); out = []
    for r in list(root.iter(ns + 'row'))[1:]:
        row = {}
        for c in r:
            v = c.find(ns + 'v')
            if v is None: continue
            row[re.sub(r'\d', '', c.get('r'))] = ss[int(v.text)] if c.get('t') == 's' else v.text
        if 'A' in row and 'E' in row:
            out.append(dict(name=row['A'].strip(), cls=row.get('B'), speed=float(row['E']),
                            hull=float(row['I']) if 'I' in row else None))
    return out


def fmt(st, p=1):
    if not st: return '-'
    return f"{st['min']:.{p}f}/{st['med']:.{p}f}/{st['max']:.{p}f}"


def main():
    inst = Assets(X3); stock = Assets(X3, catalogues=STOCK_AP_CATALOGUES)
    si, pi = ships(inst); ss_, ps = ships(stock)
    bi, bs = by_class(si), by_class(ss_)
    # validation 1: ShipStats.xlsx speed (mod author's m/s) vs col7/500, matched on ship name
    st = shipstats(); byname = {}
    for s in si: byname.setdefault(s['name'].strip().lower(), []).append(s)
    matched = within = 0
    for r in st:
        c = byname.get(r['name'].lower())
        if not c: continue
        matched += 1
        within += min(abs(x['speed'] - r['speed']) for x in c) <= 1
    val = dict(shipstats_rows=len(st), matched_on_name=matched, speed_within_1mps=within)
    # validation 2: named reference ships
    refs = {}
    for label, lst in (('installed', si), ('stock', ss_)):
        for want in ('Discoverer', 'Boreas', 'Mammoth', 'Mercury'):
            hits = [s for s in lst if s['name'].startswith(want) and s['variation'] in (0, None)]
            if hits:
                s = hits[0]; refs[f'{label}:{want}'] = dict(cls=s['cls'], speed=s['speed'], accel=s['accel'],
                                                         rot_dps=[round(x, 2) for x in s['rot_dps']],
                                                         radius_m=round(s['radius_u'] / U, 1) if s['radius_u'] else None)
    allspeed = sorted((s['speed'], s['name'], s['cls']) for s in si if s['speed'] > 0)
    out = dict(source=dict(installed=pi, stock=ps), units=dict(engine_units_per_m=500, rotation='turns/s, deg/s = 360*raw'),
               validation=val, reference_ships=refs,
               fastest_installed=allspeed[-3:], slowest_installed=allspeed[:3],
               installed=bi, stock=bs)
    (HERE / 'ships_by_class.json').write_text(json.dumps(out, indent=1, default=str))
    lines = [f"installed {pi['source']} {pi['decoded_sha256'][:12]}; stock {ps['source']} {ps['decoded_sha256'][:12]}",
             f"validation {val}", f"reference {refs}", '']
    hdr = ('class', 'n', 'speed m/s', 'accel m/s2', 'rotX dps', 'rotY dps', 'rotZ dps', 'radius m', 'hull m',
           'R_turn m', 't90 s', 't180 s', 'stop m', 'arc180 m')
    for label, b in (('INSTALLED (Mayhem 4)', bi), ('STOCK AP', bs)):
        lines.append(label); lines.append(' | '.join(hdr))
        keys = [k for k in ORDER if k in b] + sorted(k for k in b if k not in ORDER)
        for k in keys:
            c = b[k]
            lines.append(' | '.join([k, str(c['count']), fmt(c['speed'], 0), fmt(c['accel'], 0), fmt(c['rot_x_dps']),
                                     fmt(c['rot_y_dps']), fmt(c['rot_z_dps']), fmt(c['radius_m'], 0),
                                     fmt(c['hull_halfext_m'], 0), fmt(c['turn_radius_m'], 0), fmt(c['t90_s']),
                                     fmt(c['t180_s']), fmt(c['stop_m'], 0), fmt(c['arc180_m'], 0)]))
        lines.append('')
        for k in keys:
            lines.append(f"  {label[:5]} {k} slowest-turning {b[k]['slowest_turning']} obj={b[k]['obj_classes']}")
        lines.append('')
    (HERE / 'ships_by_class.txt').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
