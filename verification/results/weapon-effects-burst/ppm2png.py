#!/usr/bin/env python3
"""ppm2png.py <src_dir> <dst_dir>: every P6 .ppm in src -> .png in dst (Pillow, else zlib by hand)."""
import sys, zlib, struct
from pathlib import Path

def read_ppm(p):
    b = p.read_bytes()
    parts, i = [], 0
    while len(parts) < 4:
        while b[i:i+1].isspace(): i += 1
        j = i
        while not b[j:j+1].isspace(): j += 1
        parts.append(b[i:j]); i = j
    assert parts[0] == b'P6' and parts[3] == b'255'
    w, h = int(parts[1]), int(parts[2])
    return w, h, b[i+1:i+1+w*h*3]

def write_png(path, w, h, rgb):
    try:
        from PIL import Image
        Image.frombytes('RGB', (w, h), rgb).save(path, optimize=False)
        return
    except ImportError:
        pass
    raw = b''.join(b'\0' + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

src, dst = Path(sys.argv[1]), Path(sys.argv[2])
dst.mkdir(parents=True, exist_ok=True)
n = 0
for p in sorted(src.glob('*.ppm')):
    w, h, rgb = read_ppm(p)
    write_png(dst / (p.stem + '.png'), w, h, rgb)
    n += 1
print('converted', n)
