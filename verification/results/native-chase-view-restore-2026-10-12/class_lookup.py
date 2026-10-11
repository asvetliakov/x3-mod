"""Parse CLAS of an extracted L/x3story.obj with the loader 0x0049d030 layout (BE count; per class:
id, parent id, word, nA, nA x 16-byte method rows (entry, name, locals, argc), nB, nB x 16-byte rows)
and report, for class id <id> (the key 0x0042102e..0x00421039 passes to 0x004b06f0), its name word,
parent chain and whether <method> resolves on it by the 0x00421058 walk (own rows, then parent).
Also lists every class whose own rows define <method>.
Usage: python3 -I class_lookup.py <id_hex> <method> <x3story.obj> [...]"""
import struct, sys
def decode(raw):
    out = bytearray(len(raw)); out[0] = (~raw[0]) & 255
    for i in range(1, len(raw)): out[i] = (~raw[i] - raw[i - 1]) & 255
    return bytes(out)
def section(d, tag):
    at = d.find(tag); n = struct.unpack_from('>I', d, at + 4)[0]; return d[at + 8:at + 8 + n]
want, method = int(sys.argv[1], 16), sys.argv[2].encode()
for path in sys.argv[3:]:
    d = open(path, 'rb').read(); s = decode(section(d, b'STRG')); c = section(d, b'CLAS')
    name = lambda o: s[o:s.find(b'\0', o)]
    p = 0
    def w():
        global p
        v = struct.unpack_from('>I', c, p)[0]; p += 4; return v
    n = w(); classes = {}
    for _ in range(n):
        cid, parent, word, na = w(), w(), w(), w()
        rows = [(w(), w(), w(), w()) for _ in range(na)]
        nb = w(); [w() for _ in range(4 * nb)]
        classes[cid] = (parent, word, rows)
    def resolves(cid):
        chain = []
        while cid in classes:
            parent, _, rows = classes[cid]; chain.append(hex(cid))
            if any(name(r[1]) == method for r in rows): return True, chain
            cid = parent
        return False, chain
    owners = [hex(k) for k, v in classes.items() if any(name(r[1]) == method for r in v[2])]
    has = want in classes
    ok, chain = resolves(want) if has else (False, [])
    first = [name(r[1]).decode(errors='replace') for r in classes[want][2][:4]] if has else []
    print(f"{path.rsplit('/',1)[-1]} parsed={p}/{len(c)} classes={n} id_max={hex(max(classes))} "
          f"class_{hex(want)}_present={int(has)} chain={chain} {method.decode()}_resolves={int(ok)} "
          f"first_methods={first} owners_of_{method.decode()}={owners}")
