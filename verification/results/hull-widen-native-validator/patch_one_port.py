#!/usr/bin/env python3
"""Apply the proposed one-read-port encoding to a generated widened variant
(validation witness only; the generator fix belongs in linear_material.cpp):
  def c210 = (1/K, eps, B-1, 1)        was (1/K, eps, B-1, 1/0.15)
  def c203 = (32, 1, 1/0.15, 9)        was (32, 1, 0, 0)
  def c211 = (lumas, 0)                was (lumas, 9)
  mad_sat rU.w, rT.x, c210.x, -c210.w  was ..., c210.x, -c203.y
  mad_sat rT.x, rT.x, c203.z, -c203.w  was ..., c210.w, -c211.w
usage: patch_one_port.py in.bin out.bin  (prints the patched dword offsets)"""
import struct, sys
w = list(struct.unpack(f'<{len(open(sys.argv[1], "rb").read()) // 4}I', open(sys.argv[1], 'rb').read()))
f = lambda x: struct.unpack('<I', struct.pack('<f', x))[0]
DEF = 0x05000051
def def_at(reg):
    hits = [i for i in range(len(w) - 5) if w[i] == DEF and w[i + 1] == (0xA00F0000 | reg)]
    assert len(hits) == 1, (reg, hits); return hits[0]
a, b, c = def_at(210), def_at(203), def_at(211)
assert w[b + 4:b + 6] == [0, 0] and w[c + 5] == f(9.0)
gate_scale = w[a + 5]  # the generator's float 1.0f / 0.15f, moved as is
w[a + 5] = f(1.0); w[b + 4] = gate_scale; w[b + 5] = f(9.0); w[c + 5] = 0
old1, new1 = [0xA00000D2, 0xA15500CB], [0xA00000D2, 0xA1FF00D2]   # c210.x, -c203.y -> c210.x, -c210.w
old2, new2 = [0xA0FF00D2, 0xA1FF00D3], [0xA0AA00CB, 0xA1FF00CB]   # c210.w, -c211.w -> c203.z, -c203.w
done = []
for old, new in ((old1, new1), (old2, new2)):
    hits = [i for i in range(len(w) - 4) if w[i] == 0x04000004 and w[i + 3:i + 5] == old]
    assert len(hits) == 1, (old, hits)
    w[hits[0] + 3:hits[0] + 5] = new; done.append(hits[0])
open(sys.argv[2], 'wb').write(struct.pack(f'<{len(w)}I', *w))
print(sys.argv[2], 'defs', a, b, c, 'mads', *done)
