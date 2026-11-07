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
counts of EEDF_RELATIVESIZE and EEDF_BREAKUP, and the rows missing from Effects. After it, the element timeline of
every reachable non-asteroid death row and of the rows its PROPAGATE elements spawn (weapon-effect-spawn.md section
10): per element the engine's dispatch at its start (0x00414d82..0x0041594b), the EEDF/I3DF flags, body, start
(+0x40), timeout (+0x10) and end, then the row's timeline (main-instance delay t0 from the propagation, carrier
life, FADEOUTBODY window, WRECK time) and the free Effects ids (slots a clone row could use).
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


EEDF = {'SPEEDDEPENDENT': 0x1, 'RELATIVESIZE': 0x2, 'LENSFLARE': 0x4, 'GROWANDSHRINK': 0x8, 'TIMEOUT': 0x10,
        'CUT': 0x20, 'CAMERARELATIVE': 0x40, 'LIGHT': 0x80, 'GROW': 0x100, 'RANDOMROT': 0x200, 'FADEOUT': 0x400,
        'FADEOUTBODY': 0x800, 'BULLETROT': 0x1000, 'BREAKUP': 0x2000, 'PROPAGATE': 0x4000, 'RANDOMSIZE': 0x8000,
        'PARTICLEGENERATOR': 0x10000, 'DISTANT': 0x20000, 'DEBRIS': 0x40000, 'HULLDAMAGE': 0x80000, 'WAVE': 0x100000,
        'NOREINIT': 0x200000, 'CUTACTUALSIZE': 0x400000, 'CUTKEEPBODY': 0x800000, 'NOZTEST': 0x1000000,
        'NOSKIP': 0x2000000, 'WRECK': 0x4000000, 'DEBRISMAP': 0x8000000, 'PUSH': 0x10000000}   # table 0x005530b8
BODY_OPS = ('WRECK', 'FADEOUTBODY', 'CUTKEEPBODY', 'PUSH', 'PROPAGATE', 'BREAKUP', 'DEBRIS', 'DEBRISMAP', 'HULLDAMAGE')


EDF = {'BREAKUP': 1, 'PROPAGATE': 2, 'REINIT': 4, 'NOROTATE': 8, 'NODRIFT': 0x10, 'HIDEOBJECT': 0x20}   # 0x00554d78


def runtime_row_flags(row):
    """Row +4 as the loader leaves it: the text flags, | 1 for a BREAKUP element (0x00412926), | 2 for a PROPAGATE
    element (0x0041293a)."""
    v = 0
    for tok in row['flags'].split('|'):
        v |= EDF.get(tok.strip().replace('EDF_', ''), 0)
    for e in elements(row):
        v |= (1 if e['bits'] & 0x2000 else 0) | (2 if e['bits'] & 0x4000 else 0)
    return v


def elements(row):
    """Pair each `flags; i3dflags;` line with its data line, as the loader 0x004126d0 reads them."""
    out, lines = [], [l.rstrip(';').split(';') for l in row['el']]
    i = 0
    while i + 1 < len(lines):
        f, d = [x.strip() for x in lines[i]], [x.strip() for x in lines[i + 1]]
        i += 2
        bits, unknown = 0, []
        for tok in f[0].split('|'):
            t = tok.strip().replace('EEDF_', '')
            bits |= EEDF.get(t, 0)
            if t not in EEDF and t not in ('NONE', '0'):
                unknown.append(t)   # not in 0x005530b8; how 0x004ea9d0 treats it is not traced
        e = dict(bits=bits, flags=f[0].replace('EEDF_', '').replace(' ', '') +
                 (f' (unknown token {",".join(unknown)} counted as 0)' if unknown else ''),
                 i3d=f[1] if len(f) > 1 else '?')
        num = lambda k: int(float(d[k])) if k < len(d) and re.match(r'^-?[\d.]+$', d[k]) else None
        if bits & 0x4000:   # PROPAGATE: +0x44 row, +0x48/+0x4c count, +0x50/+0x54 gap, +0x58 chain, +0x5c rnd, +0x60, +0x40
            e.update(body=-1, child=num(0), nmin=num(1), nmax=num(2), gap0=num(3), gap1=num(4), chain=num(5),
                     rnd=num(6), start=num(8) or 0, timeout=0)
        else:               # body, +8, +0xc, +0x10 timeout, +0x14, +0x20..+0x28, +0x30, +0x34, +0x38, +0x40 start
            e.update(body=d[0], size=(d[1], d[2]), timeout=num(3) or 0, start=num(11) or 0)
        out.append(e)
    return out


def dispatch(e):
    """What the element does at its start in 0x00414cf0 (order of the tests 0x00414d85, 0x00414e6e, 0x004155e5,
    0x0041574d, 0x0041577f)."""
    b = e['bits']
    if b & 0x20000:
        return 'none (DISTANT: state 2 at start)'
    if b & 0x20:
        return 'scene + part nodes (CUT)'
    if b & 0x80:
        return 'light node'
    if b & 0x2000:
        return 'break-up of +0x14'
    body = e['body']
    if b & 0x4000 or (isinstance(body, str) and re.match(r'^-?\d+$', body) and int(body) < 1):
        return 'empty node' + (' (PROPAGATE: work done at spawn)' if b & 0x4000 else '')
    return ('2D ' if 'I3DF_2D' in e['i3d'] else '') + 'body node' + (' + lens flare' if b & 0x4 else '')


def timeline(eff, rid, depth=0):
    """Print one row's elements and its derived timeline; recurse once into PROPAGATE child rows."""
    row = eff[rid]
    els = elements(row)
    ind = '      ' + '  ' * depth
    t0lo = t0hi = 0
    for k, e in enumerate(els):
        ops = [o for o in BODY_OPS if e['bits'] & EEDF[o]]
        if e['bits'] & 0x4000:
            gap = f'{e["gap0"]}' if e['gap0'] == e['gap1'] else f'{e["gap0"]}..{e["gap1"]} (unequal: t0 not computed)'
            print(f'{ind}[{k}] {dispatch(e)}; flags {e["flags"]}; child row {e["child"]} x {e["nmin"]}..{e["nmax"]}, '
                  f'gap {gap} ms, chain {e["chain"]}, random +{e["rnd"]} ms, start {e["start"]}')
            if e['gap0'] == e['gap1'] and e['nmax']:
                t0lo = max(t0lo, e['start'] + e['nmin'] * e['gap0'])
                t0hi = max(t0hi, e['start'] + e['nmax'] * e['gap0'] + max(e['rnd'] or 0, 0))
            continue
        end = e['start'] + e['timeout'] if e['bits'] & 0x10 else None
        print(f'{ind}[{k}] {dispatch(e)}; flags {e["flags"]}; i3d {e["i3d"] or 0}; body {e["body"]}; size '
              f'{e["size"][0]}..{e["size"][1]}; start {e["start"]} timeout {e["timeout"]} end '
              f'{end if end is not None else "open (no TIMEOUT)"}; body ops {"|".join(ops) or "-"}')
    ends = [e['start'] + e['timeout'] for e in els if e['bits'] & 0x10 and not e['bits'] & 0x24000]
    open_ = [k for k, e in enumerate(els) if not e['bits'] & 0x10 and not e['bits'] & 0x20000]
    fade = [e['start'] for e in els if e['bits'] & 0x800]
    wreck = [e['start'] + e['timeout'] for e in els if e['bits'] & 0x4000000]
    t0 = f'{t0lo}..{t0hi}' if t0hi else '0'
    life = f'{t0lo + max(ends)}..{t0hi + max(ends)}' if ends and t0hi else (str(max(ends)) if ends else 'none')
    if depth:   # a PROPAGATE child: its own clock starts at its spawn delay (k x gap + random, 0x00414410)
        print(f'{ind}per child (ms after its spawn delay): life {max(ends) if ends else "none"}')
        return
    print(f'{ind}timeline (ms after the kill): main-instance delay t0 {t0}; carrier life (last TIMEOUT end + t0) '
          f'{life}{"; elements without TIMEOUT " + str(open_) if open_ else ""}; FADEOUTBODY root fade '
          f'{"t0+" + str(fade[-1]) + " .. +1000" if fade else "none"}; WRECK at {["t0+" + str(w) for w in wreck] or "none"} '
          f'(only when the kill passes a non-zero third argument)')
    for e in els:
        if e['bits'] & 0x4000 and depth == 0:
            c = e['child']
            if c in eff:
                print(f'{ind}  child row {c} ({eff[c]["n"]} elements, row flags {eff[c]["flags"]}), one kind-3 '
                      f'instance per child on the PROPAGATE carrier:')
                timeline(eff, c, depth + 1)
            else:
                print(f'{ind}  child row {c}: MISSING from Effects')


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
        hd = []   # EEDF_HULLDAMAGE (0x80000) elements run only while the hit object's shield sum is < 100 (0x00414d98)
        for v in sorted({ival(x, 20) for x in b if ival(x, 20)}):
            if v in eff:
                f = element_flags(eff[v])
                hd.append(f'{v}:{f.get("EEDF_HULLDAMAGE", 0)}/{eff[v]["n"]}')
        print('  impact rows, EEDF_HULLDAMAGE elements / all elements: ' + ' '.join(hd))
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
        print('  death-row element timelines (section 10; asteroid-only rows skipped):')
        for v in sorted(death):
            src = sorted(set(death[v]))
            if v not in eff or src == ['TAsteroids col 8']:
                continue
            print(f'    death row {v}: {eff[v]["n"]} elements, row flags {eff[v]["flags"]} (runtime +4 = '
                  f'0x{runtime_row_flags(eff[v]):x}); from {src}')
            timeline(eff, v)
        free = [i for i in range(1000) if i not in eff]
        blocks, start = [], None
        for i in range(1001):
            if i < 1000 and i in free:
                start = i if start is None else start
            elif start is not None:
                blocks.append((start, i - 1))
                start = None
        big = sorted(blocks, key=lambda b: b[0] - b[1])[:3]
        print(f'  free Effects ids (0..999, table 0x00580068): {len(free)}; largest blocks '
              f'{", ".join(f"{a}-{b}" for a, b in big)}; top block {blocks[-1][0]}-{blocks[-1][1]}')


if __name__ == '__main__':
    main()
