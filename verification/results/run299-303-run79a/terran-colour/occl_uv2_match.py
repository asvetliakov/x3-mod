"""Does a lower LOD record carry the same occlusion unwrap (second UV pair) as record 0? Every point of record i>0 is
matched to the record-0 points at the exactly equal position (seam duplicates included); the UV2 distance is the minimum
over those. Prints the share of points with an exact position match and, among them, the share whose UV2 lies within
1/512 and 1/64 of a record-0 UV2 at that position, and the record's UV2 range. Read-only.
Usage: python3 occl_uv2_match.py BODY [BODY ...]"""
import sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools' / 'analysis'))
import bob1

def points(lod):
    for p in lod['points']:
        if p[0] & 7 == 7:                                  # position, uv, second uv (point_struct order)
            yield tuple(p[1:4]), np.array(p[6:8], float) / 65536.0

for body in sys.argv[1:]:
    data, prov = bob1.load(body)
    tree = bob1.parse(data) if bob1.kind(data) == 'BOB1' else bob1.parse_text(data)
    recs = bob1.lods(tree)
    at = {}
    for k, u in points(recs[0]):
        at.setdefault(k, []).append(u)
    at = {k: np.array(v) for k, v in at.items()}
    print('==', body, prov)
    for i, lod in enumerate(recs[1:], 1):
        du, n, lo, hi = [], 0, np.inf, -np.inf
        for k, u in points(lod):
            n += 1; lo = min(lo, u.min()); hi = max(hi, u.max())
            if k in at:
                du.append(np.abs(at[k] - u).max(1).min())
        du = np.array(du)
        print(f'  record{i} value={lod["value"]} points={n} pos_exact={len(du) / max(n, 1):.3f} '
              f'uv2_within_1/512={np.mean(du <= 1/512):.3f} within_1/64={np.mean(du <= 1/64):.3f} '
              f'uv2_range=[{lo:.3f},{hi:.3f}]')
