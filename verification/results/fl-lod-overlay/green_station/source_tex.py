"""Format, size and channel means of named textures as mounted on FL (winner and every mounted copy).
Usage: X3M_EDITION=fl source_tex.py GAME NAME..."""
import sys, io, struct
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools/analysis'))
import numpy as np
from PIL import Image
import bob1
sfc = bob1._archive_modules(); assets = sfc.Assets(Path(sys.argv[1]))
for n in sys.argv[2:]:
    for k in ('dds/' + n.lower(), 'addon2/dds/' + n.lower()):
        for e in assets.entries.get(k, []):
            if not bob1.mounted(assets, e):
                continue
            d = assets.read_entry(e)
            fourcc = d[84:88]; w, h = struct.unpack_from('<II', d, 16)[::-1]
            try:
                a = np.asarray(Image.open(io.BytesIO(d)).convert('RGBA')).astype(float).reshape(-1, 4)
                m = np.round(a.mean(0), 1); c = np.round(np.corrcoef(a[:, 0], a[:, 1])[0, 1], 3) if a[:, 0].std() > 0 and a[:, 1].std() > 0 else None
            except Exception as ex:
                m, c = repr(ex)[:60], None
            print('%-45s %-14s %s %dx%d mean RGBA=%s corr(R,G)=%s' % (n, e['source'], fourcc, w, h, m, c))
