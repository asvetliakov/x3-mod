"""Painted star-body textures and nebula parts of the FL install (2026-11-04, for sector-background-modern.md).
Star textures: how much is background haze (median luma) versus stars (bright local maxima count).
Nebula parts: banding baseline = mean run length of identical luma along x over non-black texels, and the
share of 8x8 blocks that hold at most 2 distinct colours (DXT1 plateau blocks) among non-black blocks.
Run from the repository root: python3 -W ignore verification/results/sector-background-textures/sky_texture_stats.py"""
import sys, collections
from pathlib import Path
import numpy as np
sys.path.insert(0, 'tools/analysis')
import sector_fog_census as sfc
import lod_atlas

def luma(rgb):
    return (0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2])

def runs_x(l):
    same = (l[:, 1:] == l[:, :-1])
    nonblack = l[:, 1:] > 0
    # mean run length of identical values along x: pixels / run starts, over non-black pixels
    starts = (~same) & nonblack
    return nonblack.sum() / max(1, starts.sum())

def plateau_share(rgb):
    h, w, _ = rgb.shape
    b = rgb[:h // 8 * 8, :w // 8 * 8].reshape(h // 8, 8, w // 8, 8, 3).transpose(0, 2, 1, 3, 4).reshape(-1, 64, 3)
    key = b[..., 0].astype(np.int32) << 16 | b[..., 1].astype(np.int32) << 8 | b[..., 2].astype(np.int32)
    nonblack = key.max(1) > 0
    distinct = np.array([len(np.unique(k)) for k in key[nonblack]])
    return (distinct <= 2).mean() if distinct.size else 0.0, nonblack.mean()

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3FL')
edition = 'fl' if 'FL' in root.name.upper() else 'ap'
a = sfc.Assets(root, edition=edition)
stars, parts = [], []
for key, entries in a.entries.items():
    p = entries[0]['path'].replace('\\', '/').lower()
    if not p.startswith('dds/nebula_') or not p.endswith('.pck'): continue
    if '_stars_' in p: stars.append(entries[0])
    elif '_background_part_' in p: parts.append(entries[0])
print(f'{edition}: {len(stars)} star textures, {len(parts)} nebula parts')
rows = []
for e in stars:
    d = a.read_entry(e)
    try: img = lod_atlas.decode_dds(d)
    except Exception as exc: print('  skip', e['path'], exc); continue
    l = luma(img[..., :3].astype(np.float32))
    med, p50_nonzero = np.median(l), np.median(l[l > 0]) if (l > 0).any() else 0
    lit = (l > 24).mean()
    # bright local maxima: 3x3 maximum equals the pixel and luma above 96
    pad = np.pad(l, 1, 'edge')
    mx = np.max(np.stack([pad[i:i + l.shape[0], j:j + l.shape[1]] for i in range(3) for j in range(3)]), 0)
    peaks = int(((l == mx) & (l > 96)).sum())
    rows.append((e['path'], img.shape[1], img.shape[0], round(float(med), 2), round(float(p50_nonzero), 1), round(float(lit) * 100, 2), peaks))
rows.sort(key=lambda r: r[3])
print('star textures: path, w, h, median luma (0-255), median non-zero luma, % texels luma>24, bright peaks (>96)')
for r in rows[:4] + rows[len(rows) // 2 - 2:len(rows) // 2 + 2] + rows[-4:]:
    print('  ', r)
meds = np.array([r[3] for r in rows]); peaks = np.array([r[6] for r in rows]); lit = np.array([r[5] for r in rows])
print(f'  median luma: min {meds.min()} median {np.median(meds)} max {meds.max()}; textures with median luma > 4: {(meds > 4).sum()} of {len(meds)}')
print(f'  bright peaks per texture: min {peaks.min()} median {int(np.median(peaks))} max {peaks.max()}; % texels > 24: median {np.median(lit):.2f} max {lit.max():.2f}')
prow = []
for e in parts:
    d = a.read_entry(e)
    w, h, _, fmt = lod_atlas.dds_format(d)
    if not isinstance(fmt, str) or fmt != 'DXT1': continue
    img = lod_atlas.decode_dds(d)
    l = luma(img[..., :3].astype(np.int32)).astype(np.int32)
    share, nb = plateau_share(img[..., :3])
    prow.append((e['path'], round(float(runs_x(l)), 2), round(float(share) * 100, 1), round(float(nb) * 100, 1), round(float((l == 0).mean()) * 100, 1)))
prow.sort(key=lambda r: -r[1])
print(f'DXT1 nebula parts measured: {len(prow)}: path, mean identical-luma run along x (px, non-black), % non-black 8x8 blocks with <=2 colours, % non-black blocks, % black texels')
for r in prow[:3] + prow[len(prow) // 2 - 1:len(prow) // 2 + 2] + prow[-3:]:
    print('  ', r)
run = np.array([r[1] for r in prow]); pl = np.array([r[2] for r in prow]); blk = np.array([r[4] for r in prow])
print(f'  run length: median {np.median(run):.2f} p90 {np.percentile(run, 90):.2f}; plateau-block share: median {np.median(pl):.1f}% p90 {np.percentile(pl, 90):.1f}%; black texels: median {np.median(blk):.1f}%')
