#!/usr/bin/env python3
"""Dump the name -> id tables the type loader's text-id reader 0x004ea440 uses (read-only; names and ids only).

  python3 class_id_tables.py <X3AP.exe> [<exe> ...]   (extra EXEs: OBJ_SHIP table by anchor, then the data census)

0x0043cb60 (ECX = class) selects the table: class 5 TDocks 0x00552840, 6 TFactories 0x005527e8, 7 TShips
0x00552758, 8 TLaser 0x00552888, 9 TShields none (0), 10 TMissiles 0x00552998. Each table is {char *name, int id}
pairs (stride 8, `[esi+ebp*8]` / `[esi+ebp*8+4]` at 0x004ea510/0x004ea599) ending at the entry whose name equals
the empty string at 0x00554ef0. Text starting with a digit or '%' bypasses the table (0x004ea4dc..0x004ea4ea ->
0x004eb930 number parse). The common column 5 (+0x14, 0x004364e9) uses the per-class table; TShips column 52 uses
0x00552a28 and the station/missile/laser text-id columns use 0x00550bc0. Owning note: docs/reverse-engineering/weapon-effect-spawn.md section 8.
"""
import hashlib
import struct
import sys

SECS = [(0x401000, 1246768, 1024), (0x532000, 264013, 1248256), (0x573000, 981848, 1512448)]
TABLES = {'col 5 (+0x14) class 5 TDocks': 0x00552840, 'col 5 (+0x14) class 6 TFactories': 0x005527e8,
          'col 5 (+0x14) class 7 TShips': 0x00552758, 'col 5 (+0x14) class 8 TLaser': 0x00552888,
          'col 5 (+0x14) class 10 TMissiles': 0x00552998,
          'TShips col 52 (+0xc8, ECX = 0x00552a28 at 0x004376fc)': 0x00552a28}
# (TDocks col 17, TFactories col 18, TMissiles col 26 and TLaser col 12 use 0x00550bc0, the ICON_* picture table.)


def main():
    data = open(sys.argv[1], 'rb').read()

    def off(va):
        for start, size, raw in SECS:
            if start <= va < start + size:
                return va - start + raw
        return None

    def cstr(va):
        o = off(va)
        return None if o is None else data[o:data.index(b'\0', o)].decode('latin1')

    # selector jump table check: 0x0043cb90, six entries for classes 5..10
    sel = [struct.unpack_from('<I', data, off(0x0043cb90) + 4 * i)[0] for i in range(6)]
    print('selector 0x0043cb60 targets (class 5..10):', ' '.join(f'0x{t:08x}' for t in sel))
    for name, va in TABLES.items():
        rows = []
        for i in range(400):
            p, v = struct.unpack_from('<Ii', data, off(va + 8 * i))
            s = cstr(p)
            if s is None or s == '':
                break
            rows.append(f'{s}={v}')
        print(f'{name}: table 0x{va:08x}, {len(rows)} entries: ' + ', '.join(rows))


if __name__ == '__main__':
    main()


def anchored_table(path, anchor=b'OBJ_SHIP_M0'):
    """{name: id} of the {char*, int} table holding `anchor`, found by pointer search (any X3 EXE; FL check)."""
    data = open(path, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count, opt = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    secs = []
    for i in range(count):
        o = pe + 24 + opt + 40 * i
        vsize, va, rsize, raw = struct.unpack_from('<IIII', data, o + 8)
        secs.append((base + va, min(vsize, rsize), raw))

    def off(va):
        return next((va - s + r for s, n, r in secs if s <= va < s + n), None)

    def va_of(o):
        return next((o - r + s for s, n, r in secs if r <= o < r + n), None)

    def cstr(va):
        o = off(va)
        return None if o is None else data[o:data.index(b'\0', o)].decode('latin1')

    sva = va_of(data.index(anchor + b'\0'))
    entry = va_of(data.index(struct.pack('<I', sva)))
    while (cstr(struct.unpack_from('<I', data, off(entry - 8))[0]) or '').startswith('OBJ_'):
        entry -= 8
    out = {}
    while True:
        p, v = struct.unpack_from('<Ii', data, off(entry))
        s = cstr(p)
        if not s or s == 'NULL':
            break
        out[s] = v
        entry += 8
    return hashlib.sha256(data).hexdigest()[:12], out


def census():
    """Distinct class tokens per view: TShips col 5 / col 52, TDocks, TFactories, TMissiles, TAsteroids col 5."""
    import collections
    import pathlib
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / 'tools/analysis'))
    from sector_fog_census import Assets, STOCK_AP_CATALOGUES
    views = [('AP stock', Assets(pathlib.Path('/mnt/c/X3'), catalogues=STOCK_AP_CATALOGUES, edition='ap')),
             ('AP installed', Assets(pathlib.Path('/mnt/c/X3'), edition='ap'))]
    if pathlib.Path('/mnt/c/X3FL').is_dir():
        views.append(('FL installed', Assets(pathlib.Path('/mnt/c/X3FL'), edition='fl')))
    for label, a in views:
        for table, cols in (('TShips', (5, 52)), ('TDocks', (5,)), ('TFactories', (5,)), ('TMissiles', (5,)),
                            ('TAsteroids', (5,))):
            try:
                data, prov = a.get(f'{a.expansion}/types/{table}.txt')
            except FileNotFoundError:
                continue
            lines = [l.split('//')[0].strip() for l in data.decode('latin1').splitlines()]
            rows = [l.split(';') for l in [l for l in lines if l][1:]]
            for c in cols:
                n = collections.Counter(r[c].strip() for r in rows if len(r) > c)
                print(f'{label} {table} {prov["source"]} col {c}: ' + ', '.join(f'{k} x{v}' for k, v in sorted(n.items())))
            if table == 'TShips':
                num = collections.Counter((r[52].strip(), r[5].strip(), r[47].strip()) for r in rows
                                          if len(r) > 52 and r[52].strip()[:1].isdigit())
                print(f'{label} TShips numeric col 52 as (col 52, col 5, col 47 death row): ' +
                      ', '.join(f'{k} x{v}' for k, v in sorted(num.items())))


if __name__ == '__main__' and len(sys.argv) > 2:
    for exe in sys.argv[2:]:
        h, t = anchored_table(exe)
        print(f'{exe} (sha256 {h}...) OBJ_SHIP table, {len(t)} entries: ' + ', '.join(f'{k}={v}' for k, v in t.items()))
    census()
