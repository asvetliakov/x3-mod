"""Small PNG writer for 8-bit RGB images (stdlib zlib; numpy, when importable, picks a filter per row).

read_ppm(path) -> (width, height, rgb bytes) for a binary P6 file with maxval 255; write_png(path, width, height, rgb)
writes a colour-type-2 PNG at zlib level 9. With numpy each row takes the filter (None, Sub, Up, Average, Paeth) with
the smallest sum of absolute signed residuals (the PNG specification's heuristic); without it every row is filter 0.
"""
import struct
import zlib
from pathlib import Path

try:
    import numpy as np
except ImportError:  # pragma: no cover - the filter heuristic is an optimisation only
    np = None


def read_ppm(path):
    data = Path(path).read_bytes()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b'#':
            pos = data.index(b'\n', pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    if fields[0] != b'P6' or int(fields[3]) != 255:
        raise ValueError(f'{path}: not an 8-bit binary PPM')
    width, height = int(fields[1]), int(fields[2])
    rgb = data[pos + 1:pos + 1 + width * height * 3]
    if len(rgb) != width * height * 3:
        raise ValueError(f'{path}: short pixel data')
    return width, height, rgb


def _filtered(width, height, rgb):
    stride = width * 3
    if np is None:
        return b''.join(b'\x00' + rgb[y * stride:(y + 1) * stride] for y in range(height))
    x = np.frombuffer(rgb, dtype=np.uint8).reshape(height, stride).astype(np.int16)
    a = np.zeros_like(x)  # left (3 bytes back)
    a[:, 3:] = x[:, :-3]
    b = np.zeros_like(x)  # up
    b[1:] = x[:-1]
    c = np.zeros_like(x)  # up-left
    c[1:, 3:] = x[:-1, :-3]
    p = a + b - c
    pa, pb, pc = np.abs(p - a), np.abs(p - b), np.abs(p - c)
    paeth = np.where((pa <= pb) & (pa <= pc), a, np.where(pb <= pc, b, c))
    candidates = np.stack([x, x - a, x - b, x - (a + b) // 2, x - paeth]).astype(np.uint8)  # mod 256
    cost = np.abs(candidates.view(np.int8).astype(np.int32)).sum(axis=2)  # (5, height)
    choice = cost.argmin(axis=0)
    rows = candidates[choice, np.arange(height)]
    out = np.empty((height, stride + 1), dtype=np.uint8)
    out[:, 0] = choice
    out[:, 1:] = rows
    return out.tobytes()


def _chunk(kind, payload):
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload) & 0xffffffff)


def write_png(path, width, height, rgb):
    raw = _filtered(width, height, rgb)
    png = (b'\x89PNG\r\n\x1a\n' + _chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
           _chunk(b'IDAT', zlib.compress(raw, 9)) + _chunk(b'IEND', b''))
    Path(path).write_bytes(png)
    return len(png)
