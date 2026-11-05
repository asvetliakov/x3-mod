"""Dark-level ladder of the stock DXT1 nebula parts (Decision C diagnosis, sector-background-modern.md).

Usage: PYTHONPATH=tools/analysis python3 verification/results/sector-background-textures/sky_dark_steps.py [/mnt/c/X3FL] [family ...]
Prints, per part: the distinct luma levels <= 24 and their texel counts, the per-channel values present
in 1..24 (the RGB565 ladder), the first non-zero luma next to a black texel along x (the edge step), and the
mean identical-luma run along x inside the dark band (0 < luma <= 24).
"""
import sys
from pathlib import Path
import numpy as np
import sector_fog_census as sfc
import lod_atlas

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3FL')
families = sys.argv[2:] or ['bluewell', 'reddawn', 'avdustysol']
edition = 'fl' if 'FL' in root.name.upper() else 'ap'
a = sfc.Assets(root, edition=edition)

def luma(rgb):
    return (rgb[..., 0] * 299 + rgb[..., 1] * 587 + rgb[..., 2] * 114 + 500) // 1000

def runs_dark(l):
    same = (l[:, 1:] == l[:, :-1]) & (l[:, 1:] > 0) & (l[:, 1:] <= 24)
    starts = (l > 0) & (l <= 24)
    starts[:, 1:] &= ~same
    n = starts.sum()
    return same.sum() / n + 1 if n else 0.0

for fam in families:
    for key, entries in sorted(a.entries.items()):
        p = entries[0]['path'].replace('\\', '/').lower()
        if not p.startswith(f'dds/nebula_{fam}_background_part_') or not p.endswith('.pck'): continue
        d = a.read_entry(entries[0])
        w, h, _, fmt = lod_atlas.dds_format(d)
        img = lod_atlas.decode_dds(d)[..., :3].astype(np.int32)
        l = luma(img)
        levels, counts = np.unique(l[(l > 0) & (l <= 24)], return_counts=True)
        chan = [sorted(set(np.unique(img[..., c][(img[..., c] > 0) & (img[..., c] <= 24)]).tolist())) for c in range(3)]
        left = (l[:, :-1] == 0) & (l[:, 1:] > 0)
        right = (l[:, 1:] == 0) & (l[:, :-1] > 0)
        edge = np.concatenate([l[:, 1:][left], l[:, :-1][right]])
        print(f'{p} {w}x{h} {fmt} black={float((l == 0).mean()) * 100:.1f}% dark(1..24)={float(((l > 0) & (l <= 24)).mean()) * 100:.1f}%')
        print('  luma levels 1..24 (level:texels):', ' '.join(f'{int(v)}:{int(c)}' for v, c in zip(levels, counts)))
        print('  channel values 1..24 R/G/B:', chan[0], chan[1], chan[2])
        if edge.size:
            q = np.percentile(edge, [50, 90])
            print(f'  edge step (first non-zero luma beside black, {edge.size} edges): median {q[0]:.0f} p90 {q[1]:.0f}'
                  f' share>=3: {float((edge >= 3).mean()) * 100:.1f}% share>=6: {float((edge >= 6).mean()) * 100:.1f}%')
        print(f'  mean identical-luma run along x inside 1..24: {runs_dark(l):.2f} texels')
