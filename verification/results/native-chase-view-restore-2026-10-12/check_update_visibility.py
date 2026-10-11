#!/usr/bin/env python3
"""Does the compiled script program carry a zero-argument UpdateVisibility on the monitor manager class,
and do the script's own class-static calls reach it with that class id as the lookup key?

Same STRG/CLAS contract as ../native-chase-view-restore-2026-10-11/check_selectmode.py (loader 0x0049d030:
BE class count; per class id, parent id, word, nA, nA x 16-byte rows (CODE entry, STRG name, locals,
argc), nB, nB x 16-byte rows). For every class whose own rows define UpdateVisibility it prints the class
id, argc and entry, which class owns SelectMode (the monitor class), and the CODE sites of opcode 0x86
(class-static call: the interpreter resolves the pushed key through 0x0049f1e0, non-negative ->
0x004b06f0 static record) to that entry with the pushed key (`06 hi lo` = push word) right before it.
Usage: check_update_visibility.py <x3story.obj> [...] (extract with
../native-chase-view-restore-2026-10-11/extract_x3story.py: addon/04.cat stock, addon/07.cat Mayhem)."""
import struct
import sys


def decode(raw):
    out = bytearray(len(raw))
    if raw:
        out[0] = (~raw[0]) & 255
    for i in range(1, len(raw)):
        out[i] = (~raw[i] - raw[i - 1]) & 255
    return bytes(out)


def section(d, tag):
    at = d.find(tag)
    n = struct.unpack_from('>I', d, at + 4)[0]
    return d[at + 8:at + 8 + n]


def main(paths):
    for path in paths:
        d = open(path, 'rb').read()
        s, c, code = decode(section(d, b'STRG')), section(d, b'CLAS'), section(d, b'CODE')
        name = lambda o: s[o:s.find(b'\0', o)].decode('latin1')
        p = 0

        def w():
            nonlocal p
            v = struct.unpack_from('>I', c, p)[0]
            p += 4
            return v
        classes = {}
        for _ in range(w()):
            cid, parent, _word, na = w(), w(), w(), w()
            rows = [(w(), w(), w(), w()) for _ in range(na)]
            nb = w()
            p += 16 * nb
            classes[cid] = rows
        select_owners = [hex(k) for k, rows in classes.items() if any(name(r[1]) == 'SelectMode' for r in rows)]
        print(f"{path.rsplit('/', 1)[-1]} parsed={p}/{len(c)} classes={len(classes)} SelectMode_owners={select_owners}")
        for cid, rows in sorted(classes.items()):
            for entry, nm, loc, argc in rows:
                if name(nm) != 'UpdateVisibility':
                    continue
                keys = {}
                k = b'\x86' + struct.pack('>I', entry)
                q = 0
                while (q := code.find(k, q)) >= 0:
                    key = struct.unpack('>H', code[q - 2:q])[0] if code[q - 3] == 0x06 else None
                    keys[key] = keys.get(key, 0) + 1
                    q += 1
                print(f"  class=0x{cid:x} UpdateVisibility argc={argc} locals={loc} entry=0x{entry:x} "
                      f"class_static_calls={sum(keys.values())} pushed_keys={{{', '.join(f'{hex(a) if a is not None else None}: {b}' for a, b in keys.items())}}}")


if __name__ == '__main__':
    main(sys.argv[1:])
