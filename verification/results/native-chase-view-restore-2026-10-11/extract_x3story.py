"""Extract L/x3story.obj from a cat/dat pair (XOR 0x33 dat) to an untracked output path; print size, sha256, section tags.
Usage: python3 extract_x3story.py <cat> <out>"""
import sys, hashlib
cat, out = sys.argv[1], sys.argv[2]
b = open(cat, 'rb').read()
lines = bytes((c ^ ((0xDB + i) & 0xFF)) for i, c in enumerate(b)).decode('latin1').splitlines()
off = 0
for l in lines[1:]:
    name, size = l.rsplit(' ', 1); size = int(size)
    if name == 'L/x3story.obj':
        with open(cat[:-4] + '.dat', 'rb') as f:
            f.seek(off); raw = f.read(size)
        data = bytes(c ^ 0x33 for c in raw)
        open(out, 'wb').write(data)
        print(cat, 'offset', off, 'size', size, 'sha256', hashlib.sha256(data).hexdigest())
        for tag in (b'STRG', b'CLAS', b'CODE', b'DEFS', b'PROG'):
            print(' ', tag.decode(), [i for i in range(len(data)) if data.startswith(tag, i)][:3])
        break
    off += size
