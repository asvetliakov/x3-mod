"""List every E8/E9 rel32 call/jump and every absolute dword reference to each target in the EXE image,
with the return address (call site + 5) the stub at the target's entry reads as [esp].
Usage: python3 -I callers.py <exe> <target_hex> [...]"""
import struct, sys
d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]; n = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]; base = struct.unpack_from('<I', d, pe + 52)[0]
secs = []
for i in range(n):
    o = pe + 24 + opt + 40 * i; nm = d[o:o + 8].rstrip(b'\0').decode()
    vs, va, rs, ro, = struct.unpack_from('<IIII', d, o + 8); ch = struct.unpack_from('<I', d, o + 36)[0]
    secs.append((nm, base + va, rs, ro, ch))
for t in (int(x, 16) for x in sys.argv[2:]):
    out = []
    for nm, va, rs, ro, ch in secs:
        blob = d[ro:ro + rs]
        if ch & 0x20000000:
            for i in range(len(blob) - 5):
                if blob[i] in (0xe8, 0xe9) and (va + i + 5 + struct.unpack_from('<i', blob, i + 1)[0]) & 0xffffffff == t:
                    out.append(f"{'call' if blob[i]==0xe8 else 'jmp'}@{va+i:08x} ret={va+i+5:08x}")
        k = struct.pack('<I', t); p = blob.find(k)
        while p >= 0:
            out.append(f"abs@{va+p:08x}({nm})"); p = blob.find(k, p + 1)
    print(f"target={t:08x} refs={len(out)} " + ' '.join(out))
