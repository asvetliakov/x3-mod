"""Contact sheet of a scratch sky bake (docs/architecture/sector-background-modern.md answer 4): up to eight nebula
parts spanning the source banding range (sorted by the source's mean identical-luma run, worst first), each as a
256x256 source crop at 4x nearest beside the same region of the bake at 2x nearest (the bake is 2x the source, so both
tiles show one source texel as 4x4 screen pixels). The crop is the 256x256 window (stride 128) holding the most
non-black 8x8 plateau blocks of the source. stdlib zlib PNG writer, no Pillow.

  python3 verification/results/sector-background-textures/sky_contact_sheet.py BAKE_DIR --game GAME [--out PNG]

Writes BAKE_DIR/sky-contact-sheet.png (or --out) and prints the row order. Keep the PNG local (it shows game art).
"""
import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import lod_atlas  # noqa: E402
import sky_textures as st  # noqa: E402

CROP, SCALE, COUNT = 256, 4, 8


def write_png(path, rgb):
    """8-bit RGB PNG (filter 0 per row, one IDAT)."""
    h, w, _ = rgb.shape
    raw = np.concatenate([np.zeros((h, 1), np.uint8), rgb.reshape(h, w * 3)], 1).tobytes()

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def crop_origin(src):
    """Top-left of the CROP window (stride CROP/2) with the most non-black plateau blocks (<= 2 colours per 8x8)."""
    h, w, _ = src.shape
    b = src[:h // 8 * 8, :w // 8 * 8].astype(np.int32).reshape(h // 8, 8, w // 8, 8, 3).transpose(0, 2, 1, 3, 4)
    key = (b[..., 0] << 16 | b[..., 1] << 8 | b[..., 2]).reshape(h // 8, w // 8, 64)
    k = np.sort(key, -1)
    plateau = ((1 + (np.diff(k, axis=-1) != 0).sum(-1)) <= 2) & (k[..., -1] > 0)
    best, at = -1, (0, 0)
    n = CROP // 8
    for y in range(0, max(1, h - CROP + 1), CROP // 2):
        for x in range(0, max(1, w - CROP + 1), CROP // 2):
            s = int(plateau[y // 8:y // 8 + n, x // 8:x // 8 + n].sum())
            if s > best:
                best, at = s, (y, x)
    return at


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('bake', type=Path)
    ap.add_argument('--game', type=Path, required=True)
    ap.add_argument('--out', type=Path)
    a = ap.parse_args(argv)
    record = json.loads((a.bake / 'sky-bake.json').read_text())
    assets = st.source_view(a.game.resolve())
    items = []
    for r in record['rows']:
        if r['metrics']['kind'] != 'part':
            continue
        entry = next(e for e in assets.candidates(r['member']) if e['source'] == r['source'])
        src = lod_atlas.decode_dds(assets.read_entry(entry))[..., :3]
        items.append((st.mean_run_x(src), r, src))
    items.sort(key=lambda x: -x[0])
    if len(items) > COUNT:                  # worst, best and evenly spaced between
        pick = np.unique(np.rint(np.linspace(0, len(items) - 1, COUNT)).astype(int))
        items = [items[i] for i in pick]
    tile = CROP * SCALE
    sheet = np.zeros((tile * len(items), tile * 2, 3), np.uint8)
    for i, (run, r, src) in enumerate(items):
        bake = lod_atlas.decode_dds((a.bake / r['file']).read_bytes())[..., :3]
        f = bake.shape[0] // src.shape[0]
        y, x = crop_origin(src)
        s = src[y:y + CROP, x:x + CROP]
        b = bake[y * f:(y + CROP) * f, x * f:(x + CROP) * f]
        s = np.repeat(np.repeat(s, SCALE, 0), SCALE, 1)
        b = np.repeat(np.repeat(b, SCALE // f, 0), SCALE // f, 1)
        sheet[i * tile:i * tile + s.shape[0], :s.shape[1]] = s
        sheet[i * tile:i * tile + b.shape[0], tile:tile + b.shape[1]] = b
        print(f'row {i}: {r["member"]} source run {run:.2f} px, crop at (x {x}, y {y}); left source, right bake')
    out = a.out or a.bake / 'sky-contact-sheet.png'
    write_png(out, sheet)
    print(f'wrote {out} ({sheet.shape[1]}x{sheet.shape[0]}, {out.stat().st_size} B)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
