"""Count entries in a DXVK v8+ state cache (12-byte header 'DXVK', version, 0; per entry u32 {stageMask:8, size:24},
20-byte SHA-1, size bytes). usage: dxvk_cache_count.py FILE"""
import sys, struct, collections
b = open(sys.argv[1], 'rb').read()
magic, ver, esz = b[:4], *struct.unpack_from('<II', b, 4)
at, n, masks = 12, 0, collections.Counter()
while at + 24 <= len(b):
    h, = struct.unpack_from('<I', b, at); size = h >> 8
    if at + 24 + size > len(b): break
    masks[h & 0xff] += 1; n += 1; at += 24 + size
print(f'magic={magic} version={ver} entries={n} parsed_bytes={at} file_bytes={len(b)} stage_masks={dict(masks)}')
