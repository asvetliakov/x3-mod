#!/usr/bin/env python3
"""Count entries in a DXVK (1.10, v>=8) .dxvk-cache: 12-byte header (magic, version, entrySize),
then entries of {u32 stageMask:8|size:24, sha1[20]} + payload."""
import struct, sys
b = open(sys.argv[1], 'rb').read()
magic, ver, esz = b[:4], *struct.unpack('<II', b[4:12])
off, n = 12, 0
while off + 24 <= len(b):
    w, = struct.unpack('<I', b[off:off+4]); size = w >> 8
    off += 24 + size; n += 1
print(f"magic={magic!r} version={ver} bytes={len(b)} entries={n} trailing={len(b)-off}")
