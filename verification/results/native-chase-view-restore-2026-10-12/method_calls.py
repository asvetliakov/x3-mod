"""List every native (0x82), dynamic (0x85), class-static (0x86) and static (0x88) call inside one CLAS method body
[entry, next method entry), in CODE order. Usage: python3 -I method_calls.py <x3story.obj> 0xENTRY"""
import struct, sys, bisect, re
d = open(sys.argv[1], 'rb').read()
def dec(raw):
    o = bytearray(len(raw)); o[0] = (~raw[0]) & 255
    for i in range(1, len(raw)): o[i] = (~raw[i] - raw[i - 1]) & 255
    return bytes(o)
def sec(tag):
    a = d.find(tag); n = struct.unpack_from('>I', d, a + 4)[0]; return d[a + 8:a + 8 + n]
s = dec(sec(b'STRG')); c = sec(b'CLAS'); code = sec(b'CODE')
nm = lambda o: s[o:s.index(b'\0', o)].decode('latin1') if 0 < o < len(s) and s[o - 1] == 0 else None
rows = set()
for i in range(0, len(c) - 15):
    e, n, loc, argc = struct.unpack_from('>IIII', c, i)
    if e < len(code) and nm(n) and argc < 16 and loc < 256 and s[n:n+1].isalpha(): rows.add((e, nm(n)))
rows = sorted(rows); ents = [r[0] for r in rows]; by = dict(rows)
t = int(sys.argv[2], 16); j = bisect.bisect_right(ents, t); end = ents[j] if j < len(ents) else len(code)
print(hex(t), by.get(t), 'end', hex(end))
for m in re.finditer(rb'[\x82\x85\x86\x88][\x00-\xff]{4}', code[t:end]):
    op = m.group()[0]; v = struct.unpack('>I', m.group()[1:])[0]
    lab = by.get(v) if op in (0x86, 0x88) else nm(v)
    if lab: print('  ', hex(t + m.start()), hex(op), lab)
