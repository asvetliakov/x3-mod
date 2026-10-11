"""Print every CLAS method row named <name> (argc, CODE entry) in each extracted L/x3story.obj.
Same STRG/CLAS contract as ../native-chase-view-restore-2026-10-11/check_selectmode.py.
Usage: python3 -I method_rows.py <name> <x3story.obj> [...]"""
import struct, sys
def decode(raw):
    out = bytearray(len(raw))
    if raw: out[0] = (~raw[0]) & 255
    for i in range(1, len(raw)): out[i] = (~raw[i] - raw[i - 1]) & 255
    return bytes(out)
def section(d, tag):
    at = d.find(tag); n = struct.unpack_from('>I', d, at + 4)[0]; return d[at + 8:at + 8 + n]
name = sys.argv[1].encode()
for path in sys.argv[2:]:
    d = open(path, 'rb').read(); s = decode(section(d, b'STRG')); c = section(d, b'CLAS')
    offs, p = [], 0
    while (p := s.find(b'\0' + name + b'\0', p)) >= 0: offs.append(p + 1); p += 1
    rows = []
    for off in offs:
        k = struct.pack('>I', off)
        for i in range(4, len(c) - 12):
            if c[i:i + 4] == k:
                e, nm, loc, argc = struct.unpack_from('>IIII', c, i - 4)
                if nm == off: rows.append((i - 4, hex(e), loc, argc))
    print(path.rsplit('/', 1)[-1], 'name', name.decode(), 'strg_offsets', offs, 'rows', rows)
