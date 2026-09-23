#!/usr/bin/env python3
"""Classic material switches (blend; twosided; wire) against the flag bits, read-only.

  PYTHONPATH=tools/analysis python3 material_flags.py GAME

1. Every classic MATERIAL6 record (named texture, 30 fields) of the winning text bodies and, as a
   second line, of every text member: is `flags` == 0x2*blend | 0x10*twosided | 0x8*wire, and
   which bits are extra or missing?
2. Every binary MAT5 material of the winning .pbb/.bob bodies: its flag word, and the switches of
   the text member of the same stem if one exists.
"""
import collections
import re
import sys
from pathlib import Path

import bob1
import sector_fog_census as sfc

BITS = (0x2, 0x10, 0x8)


def classic_mat6(text):
    for m in re.finditer(r'^[ \t]*MATERIAL6:([^\n]*)', text, re.M):
        code = re.split(r'/', re.sub(r'/!.*?!/', '', m.group(1)))[0]
        f = [x.strip() for x in code.split(';')]
        if f and f[-1] == '':
            f = f[:-1]
        if len(f) != 30 or re.fullmatch(r'-?[0-9]+', f[2]):
            continue
        flags = int(f[1], 16) if f[1].lower().startswith('0x') else int(f[1])
        switches = [int(x) for x in f[16:19]]
        yield flags, switches


def main():
    a = sfc.Assets(Path(sys.argv[1]))
    for label, members in (('winning', [v[-1] for v in a.entries.values()]),
                           ('all members', [e for v in a.entries.values() for e in v])):
        c, extra = collections.Counter(), collections.Counter()
        for e in members:
            if not e['path'].lower().endswith(('.pbd', '.bod')):
                continue
            t = a.read_entry(e).decode('latin1')
            a.cache.clear()
            for flags, sw in classic_mat6(t):
                want = sum(b for b, on in zip(BITS, sw) if on)
                c['records'] += 1
                if flags == want:
                    c['equal'] += 1
                else:
                    extra[f'extra {flags & ~want:#x} missing {want & ~flags:#x}'] += 1
        print(f'{label} text members: classic MATERIAL6 records with a named texture {c["records"]},'
              f' flags == switch bits {c["equal"]}; others {dict(extra)}')
    for k, v in sorted(a.entries.items()):
        e = v[-1]
        if not e['path'].lower().endswith(('.pbb', '.bob')):
            continue
        d = a.read_entry(e)
        a.cache.clear()
        if bob1.kind(d) != 'BOB1':
            continue
        try:
            tree = bob1.parse(d, 8)
        except bob1.FormatError:
            continue
        for tag, mats in tree['sections']:
            if tag == 'MAT5':
                for m in mats:
                    print(f'binary MAT5 {e["source"]}:{e["path"]} material {m["index"]} flag word {m["flagword"]:#x}'
                          f' colors[9..11] {m["colors"][9:]} w24/w26 {m["w24"]}/{m["w26"]}')
                twin = a.candidates(k[:-4] + '.bod')
                print('  text twin:', f'{twin[-1]["source"]}:{twin[-1]["path"]}' if twin else 'none')


if __name__ == '__main__':
    main()
