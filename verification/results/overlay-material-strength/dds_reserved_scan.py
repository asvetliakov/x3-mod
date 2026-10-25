#!/usr/bin/env python3
"""Read-only, streaming scan of the DDS headers in the game's catalogues for non-zero dwReserved1 words and for a
collision with the baker's 'X3ML' mark (lod_atlas.read_dds_mark; docs/architecture/overlay-material-strength.md
section 10). Every .dds member is read as its first 128 bytes at its DAT offset; a .pck member's gzip stream is
inflated only until 128 bytes are out (a 4 KB compressed read, more only if needed). Nothing else of a member is
read, no texel bytes are kept, memory stays flat. Overlay catalogues (the marker's own slots) are included on
purpose: they are the only headers expected to carry the mark. Prints counts: members scanned, DDS headers found,
headers with any non-zero reserved word, the distinct dwReserved1[0] values (text / hex, with counts), and the
members whose mark parses (expected: the installed overlay's atlases alone).
Usage (repo root): python3 dds_reserved_scan.py GAME [--limit N]"""
import struct
import sys
import zlib
from collections import Counter
from pathlib import Path
sys.path.insert(0, 'tools/analysis')
import inspect_x3
import lod_atlas

game = Path(sys.argv[1])
limit = int(sys.argv[sys.argv.index('--limit') + 1]) if '--limit' in sys.argv else None
cats = sorted(game.glob('*.cat')) + sorted((game / 'addon').glob('*.cat')) + sorted((game / 'addon' / 'mods').glob('*.cat'))
members = headers = nonzero = unreadable = 0
first_words = Counter()
marked = []
XOR33 = bytes(v ^ 0x33 for v in range(256))


def header_of(f, offset, size, packed):
    f.seek(offset)
    if not packed:
        return f.read(min(size, 160)).translate(XOR33)
    d = zlib.decompressobj(16 + zlib.MAX_WBITS)
    out, left = b'', size
    while len(out) < 128 and left > 0:
        chunk = f.read(min(4096, left)).translate(XOR33)
        left -= len(chunk)
        try:
            out += d.decompress(chunk, 128 - len(out))
        except zlib.error:
            return None
        if d.eof:
            break
    return out


for cat in cats:
    try:
        entries = inspect_x3.read_catalogue(cat)
    except Exception as exc:
        print(f'{cat}: unreadable ({exc})')
        continue
    dat = cat.with_suffix('.dat')
    with dat.open('rb') as f:
        for e in entries:
            low = e['path'].lower()
            if not low.endswith(('.dds', '.pck')):
                continue
            members += 1
            if limit and members > limit:
                break
            raw = header_of(f, e['offset'], e['size'], low.endswith('.pck'))
            if raw is None:
                unreadable += 1
                continue
            if len(raw) < 128 or raw[:4] != b'DDS ':
                continue
            headers += 1
            reserved = struct.unpack_from('<11I', raw, 32)
            if any(reserved):
                nonzero += 1
                w = raw[32:36]
                first_words[(w.decode('latin1') if all(32 <= b < 127 for b in w) else '?') + f' {reserved[0]:08x}'] += 1
            if lod_atlas.read_dds_mark(raw) is not None or raw[32:36] == lod_atlas.DDS_MARK:
                marked.append((f'{cat.parent.name}/{cat.name}', e['path']))
print(f'catalogues {len(cats)}; members (.dds/.pck) {members}; DDS headers {headers}; unreadable gzip {unreadable};'
      f' with a non-zero dwReserved1 word {nonzero}')
print('first reserved word (text hex: count):', sorted(first_words.items(), key=lambda kv: -kv[1])[:20])
print(f"headers carrying {lod_atlas.DDS_MARK!r}: {len(marked)}", marked[:12])
