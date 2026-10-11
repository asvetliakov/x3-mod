"""List CODE sites of static calls (opcode 0x88, or 0x86 class-static with the class pushed, + big-endian CODE offset) to each given method entry,
with the enclosing method name and up to 24 preceding bytes (argument pushes).
Usage: python3 -I static_calls.py <x3story.obj> 0xENTRY..."""
import struct, sys, bisect
d = open(sys.argv[1], 'rb').read()
def dec(raw):
    o = bytearray(len(raw)); o[0] = (~raw[0]) & 255
    for i in range(1, len(raw)): o[i] = (~raw[i] - raw[i - 1]) & 255
    return bytes(o)
def sec(tag):
    a = d.find(tag); n = struct.unpack_from('>I', d, a + 4)[0]; return d[a + 8:a + 8 + n]
s = dec(sec(b'STRG')); c = sec(b'CLAS'); code = sec(b'CODE')
rows = set()
for i in range(0, len(c) - 15):
    e, nm, loc, argc = struct.unpack_from('>IIII', c, i)
    if e < len(code) and 0 < nm < len(s) and s[nm - 1] == 0 and argc < 16 and loc < 256 and s[nm:nm+1].isalpha():
        rows.add((e, s[nm:s.index(b'\0', nm)].decode('latin1')))
rows = sorted(rows); ents = [r[0] for r in rows]
for t in sys.argv[2:]:
    t = int(t, 16); out = []
    for op in (b'\x88', b'\x86'):
        k = op + struct.pack('>I', t); p = 0
        while (p := code.find(k, p)) >= 0:
            j = bisect.bisect_right(ents, p) - 1
            out.append((hex(p), op.hex(), rows[j][1], code[max(0, p - 24):p].hex()))
            p += 1
    print(hex(t), [r[1] for r in rows if r[0] == t], len(out))
    for o in out: print('  ', *o)
