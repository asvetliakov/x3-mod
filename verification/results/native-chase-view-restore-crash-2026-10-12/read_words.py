#!/usr/bin/env python3
"""Read dwords from a minidump's captured memory. Usage: read_words.py <dmp> <addr_hex>[:count] ..."""
import struct, sys
d = open(sys.argv[1], 'rb').read(); u32 = lambda o: struct.unpack_from('<I', d, o)[0]
st = {}
for i in range(u32(8)):
    t, sz, sr = struct.unpack_from('<III', d, u32(12) + 12 * i); st.setdefault(t, []).append(sr)
mem = []
r = st[5][0]
for i in range(u32(r)):
    o = r + 4 + 16 * i; mem.append((struct.unpack_from('<Q', d, o)[0], u32(o + 12), u32(o + 8)))
def rd(a):
    for s, rv, n in mem:
        if s <= a and a + 4 <= s + n: return u32(rv + a - s)
for arg in sys.argv[2:]:
    a, _, n = arg.partition(':'); a = int(a, 16); n = int(n or 1)
    print(f'{a:08x}: ' + ' '.join('--------' if rd(a + 4 * k) is None else f'{rd(a + 4 * k):08x}' for k in range(n)))
