#!/usr/bin/env python3
"""Run147 native crash: print stack dwords from ESP that fall in a module image.
Usage: stack_walk.py <dmp> [nwords]  (prints offset, value, module+rva)."""
import struct, sys
d = open(sys.argv[1], 'rb').read(); N = int(sys.argv[2]) if len(sys.argv) > 2 else 512
u32 = lambda o: struct.unpack_from('<I', d, o)[0]
ns, rva = u32(8), u32(12); st = {}
for i in range(ns):
    t, sz, sr = struct.unpack_from('<III', d, rva + 12 * i); st.setdefault(t, []).append((sz, sr))
r = st[4][0][1]; mods = []
for i in range(u32(r)):
    o = r + 4 + 108 * i; b = struct.unpack_from('<Q', d, o)[0]; s = u32(o + 8); nr = u32(o + 20)
    mods.append((b, s, d[nr + 4:nr + 4 + u32(nr)].decode('utf-16le').split('\\')[-1]))
mem = []
r = st[5][0][1]
for i in range(u32(r)):
    o = r + 4 + 16 * i; mem.append((struct.unpack_from('<Q', d, o)[0], d[u32(o + 12):u32(o + 12) + u32(o + 8)]))
r = st[6][0][1]; crva = u32(r + 4 + 4 + 152 + 4)
esp = u32(crva + 0x8c + 14 * 4)
for b, s, nm in mods:
    if nm.lower() in ('x3ap.exe', 'd3d9.dll'): print(f'module {b:08x}+{s:#x} {nm}')
for start, b in mem:
    if start <= esp < start + len(b):
        for k in range(N):
            off = esp - start + 4 * k
            if off + 4 > len(b): break
            v = struct.unpack_from('<I', b, off)[0]
            for mb, ms, nm in mods:
                if mb <= v < mb + ms:
                    print(f'esp+{4*k:#05x} {esp+4*k:08x}: {v:08x} {nm}+{v-mb:#x}'); break
