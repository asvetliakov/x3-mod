"""List CODE sites whose 4-byte big-endian operand equals the STRG offset of each named
native/method, with the enclosing CLAS method (largest entry <= site) and its class.
Same STRG/CLAS contract as method_rows.py. Usage: python3 -I native_callsites.py <x3story.obj> name..."""
import struct, sys, re
d = open(sys.argv[1], 'rb').read()
def dec(raw):
    o = bytearray(len(raw)); o[0] = (~raw[0]) & 255
    for i in range(1, len(raw)): o[i] = (~raw[i] - raw[i - 1]) & 255
    return bytes(o)
def sec(tag):
    a = d.find(tag); n = struct.unpack_from('>I', d, a + 4)[0]; return d[a + 8:a + 8 + n]
s = dec(sec(b'STRG')); c = sec(b'CLAS'); code = sec(b'CODE')
def name_at(o): return s[o:s.index(b'\0', o)].decode('latin1')
# method rows: scan CLAS for 16-byte rows whose name offset points at a NUL-preceded string and entry < len(code)
rows = []
for i in range(0, len(c) - 15):
    e, nm, loc, argc = struct.unpack_from('>IIII', c, i)
    if e < len(code) and 0 < nm < len(s) and s[nm - 1] == 0 and argc < 16 and loc < 256 and s[nm:nm+1].isalpha():
        rows.append((e, name_at(nm)))
rows = sorted(set(rows))
import bisect
ents = [r[0] for r in rows]
for nm in sys.argv[2:]:
    offs = [m.start() + 1 for m in re.finditer(b'\0' + re.escape(nm.encode()) + b'\0', s)]
    for off in offs:
        k = struct.pack('>I', off); p = 0; hits = []
        while (p := code.find(k, p)) >= 0:
            j = bisect.bisect_right(ents, p) - 1
            hits.append((hex(p), code[p-1:p].hex(), rows[j][1] if j >= 0 else '?', hex(rows[j][0]) if j >= 0 else '?'))
            p += 1
        print(nm, hex(off), len(hits), hits)
