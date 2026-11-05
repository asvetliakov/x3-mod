#!/usr/bin/env python3
"""Offline census of the weapon, impact and explosion Effects references (read-only; counts and ids only).

Owning note: docs/reverse-engineering/weapon-effect-spawn.md. Two views of /mnt/c/X3 (native Windows host):
`stock` = STOCK_AP_CATALOGUES, `installed` = every mounted layer (Mayhem 3).

Columns are the EXE's (type loader 0x00434e40, case per class; column 0 = body, class block from column 7):
  TBullets  col 12 +0x58 flags, col 20 +0x78 impact / area-death row, col 21 +0x7c muzzle row,
            col 19 +0x74 flight effect (eff of 0x004148a0 via 0x0041467a -> 0x004146c0),
            col 23 +0x84 trail (a Particles3 id, not checked against Effects), col 24 +0x88 (not an Effects id:
            body/texture override via 0x00488070), col 13-15 +0x5c..+0x64 light RGB
  TShips    col 47 +0xbc death row, col 48 +0xc0 break-up row   (12-column loop at cols 31..42)
  TDocks / TFactories  col 14 +0x60 death row, col 15 +0x64 break-up row
  TAsteroids col 8 +0x48 death row, col 9 +0x4c break-up row
  TMissiles col 20 +0x68 impact row, col 21 +0x7c death row (file version >= 0x31)
Fallbacks in the EXE: death row 48 (0x00413040) and break-up row 36 (0x00413140) when the column is 0.
The last block lists every death and break-up row the tables can reach (defaults included) with its element
counts of EEDF_RELATIVESIZE and EEDF_BREAKUP, and the rows missing from Effects.
"""
import collections
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / 'tools/analysis'))
from sector_fog_census import Assets, STOCK_AP_CATALOGUES  # noqa: E402

ROOT = pathlib.Path('/mnt/c/X3')


def rows(assets, name):
    data, prov = assets.get(f'addon/types/{name}.txt')
    lines = [l.split('//')[0].strip() for l in data.decode('latin1').splitlines()]
    lines = [l for l in lines if l]
    head = lines[0].split(';')
    return int(head[0]), int(head[1]), [l.split(';') for l in lines[1:]], prov['source']


def effects(assets):
    data, prov = assets.get('addon/types/Effects.txt')
    out, cur = {}, None
    for l in data.decode('latin1').splitlines():
        s = l.split('//')[0].strip()
        if not s:
            continue
        m = re.match(r'^(\d+);\s*(\d+);\s*([A-Z_|0-9]+);\s*(-?\d+);\s*(-?\d+\.\d*);$', s)
        if m:
            cur = int(m.group(1))
            out[cur] = dict(n=int(m.group(2)), flags=m.group(3), el=[])
        elif cur is not None:
            out[cur]['el'].append(s)
    return out, prov['source']


def element_flags(row):
    # element first lines are `flags;i3dflags;` (2 fields); count EEDF tokens over the row's elements
    c = collections.Counter()
    for line in row['el']:
        f = [x for x in line.rstrip(';').split(';')]
        if len(f) == 2 and not re.match(r'^-?[\d.]+$', f[0]):
            for tok in f[0].split('|'):
                c[tok.strip()] += 1
    return dict(sorted(c.items()))


def ival(row, col):
    try:
        return int(float(row[col]))
    except (IndexError, ValueError):
        return None


def refs(eff, values):
    vals = [v for v in values if v is not None]
    nz = sorted({v for v in vals if v > 0})
    return dict(rows=len(vals), zero=sum(1 for v in vals if v == 0), distinct=len(nz),
                present=sum(1 for v in nz if v in eff), missing=[v for v in nz if v not in eff], ids=nz)


def main():
    views = {'stock': Assets(ROOT, catalogues=STOCK_AP_CATALOGUES, edition='ap'), 'installed': Assets(ROOT, edition='ap')}
    for label, a in views.items():
        report(label, a)


def report(label, a):
    if True:
        eff, esrc = effects(a)
        ver, count, b, src = rows(a, 'TBullets')
        print(f'== {label}: TBullets {src} version {ver} rows {len(b)} (header {count}); columns per row '
              f'{sorted(collections.Counter(len(r) for r in b).items())}; Effects {esrc} rows {len(eff)}')
        flags = [ival(r, 12) for r in b]
        bits = collections.Counter()
        for f in flags:
            for k in range(32):
                if f and f >> k & 1:
                    bits[1 << k] += 1
        print('  col 12 flags: distinct values', sorted(collections.Counter(flags).items()))
        print('  col 12 bit counts:', ', '.join(f'0x{k:x}:{v}' for k, v in sorted(bits.items())))
        for col, name in ((20, 'impact +0x78'), (21, 'muzzle +0x7c'), (19, 'flight +0x74'), (24, '+0x88')):
            r = refs(eff, [ival(x, col) for x in b])
            print(f'  col {col} {name}: rows {r["rows"]}, zero {r["zero"]}, distinct non-zero {r["distinct"]} '
                  f'(in Effects {r["present"]}, missing {r["missing"]}), ids {r["ids"]}')
        trail = sorted({ival(x, 23) for x in b if ival(x, 23)})
        print(f'  col 23 trail +0x84 (Particles3 ids, not Effects): rows {len(b)}, zero '
              f'{sum(1 for x in b if not ival(x, 23))}, ids {trail}')
        area = [(x[-2] if x[-1] == '' else x[-1], ival(x, 20)) for x in b if (ival(x, 12) or 0) & 0x100]
        death, breakup = {48: ['default'], }, {36: ['default']}
        for name, row in area:
            death.setdefault(row, []).append('TBullets 0x100')
        print(f'  flag 0x100 rows (death explosion = col 20): {len(area)} {area}')
        beams = [(x[-2] if x[-1] == '' else x[-1], hex(ival(x, 12))) for x in b if (ival(x, 12) or 0) & 0x2006]
        print(f'  beam-type rows (flags & 0x2006): {len(beams)} {beams}')
        light = sum(1 for x in b if any(ival(x, c) for c in (13, 14, 15)))
        print(f'  rows with a light colour (col 13-15 not all 0): {light}')
        for table, cols, minlen in (('TShips', (47, 48), 52), ('TDocks', (14, 15), 16), ('TFactories', (14, 15), 16),
                                    ('TAsteroids', (8, 9), 10), ('TMissiles', (20, 21), 22)):
            ver, count, t, src = rows(a, table)
            t = [x for x in t if len(x) > minlen]
            parts = []
            for c in cols:
                r = refs(eff, [ival(x, c) for x in t])
                parts.append(f'col {c}: zero {r["zero"]}, distinct {r["distinct"]} (missing {r["missing"]}) ids {r["ids"][:40]}')
            print(f'  {table} {src} v{ver} rows {len(t)}: ' + '; '.join(parts))
            if table == 'TShips':   # col 52 = ship class (record +0xc8, text id in the EXE)
                tier = collections.Counter((x[52], ival(x, 47), ival(x, 48)) for x in t)
                print('    (class col 52, death col 47, break-up col 48): ' +
                      ', '.join(f'{k[0]} {k[1]}/{k[2]} x{v}' for k, v in sorted(tier.items())))
            roles = {'TMissiles': ((21, death),)}.get(table, ((cols[0], death), (cols[1], breakup)))
            for c, target in roles:   # TMissiles: col 20 is the impact row, col 21 the death row
                for v in sorted({ival(x, c) or (48 if target is death else 36) for x in t}):
                    target.setdefault(v, []).append(f'{table} col {c}')
            for c in cols:
                for v in refs(eff, [ival(x, c) for x in t])['ids'][:12]:
                    if v in eff:
                        print(f'      row {v}: {eff[v]["n"]} elements, row flags {eff[v]["flags"]}, element flags '
                              f'{element_flags(eff[v])}')

        print('  death / break-up rows reachable (zero column -> EXE default; TMissiles +0x80 has no loader store -> 36 [i]):')
        breakup.setdefault(36, []).append('TMissiles +0x80')
        for kind, rows_ in (('death', death), ('break-up', breakup)):
            for v in sorted(rows_):
                if v in eff:
                    f = element_flags(eff[v])
                    print(f'    {kind} row {v}: {eff[v]["n"]} elements, RELATIVESIZE {f.get("EEDF_RELATIVESIZE", 0)}, '
                          f'BREAKUP {f.get("EEDF_BREAKUP", 0)}; from {sorted(set(rows_[v]))}')
                else:
                    print(f'    {kind} row {v}: MISSING from Effects; from {sorted(set(rows_[v]))}')


if __name__ == '__main__':
    main()
