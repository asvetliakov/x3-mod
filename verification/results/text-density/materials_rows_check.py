"""The Materials rows the text-density row edit touches (docs/verification/text-density.md): the GENERATED|WRITEABLE rows
(the text targets) with their sizes, the file-backed FONTSCALE rows, and the 1x/2x/3x surface bytes of the targets.
Reads the bottle catalogues read-only through tools/analysis; writes materials_rows_check_out.txt beside this file.

    PYTHONPATH=tools/analysis python3 verification/results/text-density/materials_rows_check.py
"""
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/results/lod-overlay-batch'))
import lod_overlay  # noqa
import texture_lookup_rows as t  # noqa
from sector_fog_census import unpack  # noqa

assets, skipped = lod_overlay.original_assets(t.GAME)
data, info = assets.get('addon/types/Materials.pck')
lines = [l for l in unpack(data).decode('latin1').splitlines() if l.strip() and not l.lstrip().startswith('/')]
count = int(lines[0].split(';')[0])
gen_writ, file_fs, gen_fs, gen_nofilter = [], [], [], []
sizes = {}
for i, line in enumerate(lines[1:count + 1]):
    f = [x.strip() for x in line.split(';')]
    flags = 0
    for tok in f[15].split('|'):
        flags |= t.MPF.get(tok.strip(), 0)
    texid = int(f[12], 0)
    if flags & 0x800000 and flags & 0x40000:
        gen_writ.append(i)
        sizes[i] = (f[16], f[17], flags & 0x10000 != 0, flags & 0x100 != 0)
    if flags & 0x10000 and not flags & 0x800000:
        file_fs.append(i)
    if flags & 0x800000 and not flags & 0x40000:
        gen_fs.append((i, hex(flags), texid))
print('rows', count, info)
print('GENERATED|WRITEABLE', len(gen_writ), gen_writ)
print('sizes/fontscale/nofilter', sizes)
print('file-backed FONTSCALE', len(file_fs), file_fs)
print('GENERATED not WRITEABLE', gen_fs)
total = sum(int(w) * int(h) * 4 for w, h, _, _ in sizes.values())
print('bytes at 1x', total, 'at 2x', total * 4, 'at 3x', total * 9)
