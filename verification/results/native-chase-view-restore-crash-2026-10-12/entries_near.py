"""List CLAS method rows (entry, name, locals, argc) whose CODE entry lies in [lo, hi] (hex).
Same STRG/CLAS decode as ../native-chase-view-restore-2026-10-12/method_rows.py.
Usage: python3 -I entries_near.py <x3story.obj> <lo_hex> <hi_hex>"""
import struct, sys
def decode(raw):
    out = bytearray(len(raw))
    if raw: out[0] = (~raw[0]) & 255
    for i in range(1, len(raw)): out[i] = (~raw[i] - raw[i - 1]) & 255
    return bytes(out)
def section(d, tag):
    at = d.find(tag); n = struct.unpack_from('>I', d, at + 4)[0]; return d[at + 8:at + 8 + n]
d = open(sys.argv[1], 'rb').read(); s = decode(section(d, b'STRG')); c = section(d, b'CLAS')
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
for i in range(0, len(c) - 16):
    e, nm, loc, argc = struct.unpack_from('>IIII', c, i)
    if lo <= e <= hi and 0 < nm < len(s) and s[nm - 1] == 0 and argc < 16 and loc < 256:
        name = s[nm:s.find(b'\0', nm)]
        if name and all(32 < b < 127 for b in name):
            print(f'clas+{i} entry={e:#x} name={name.decode()} locals={loc} argc={argc}')
