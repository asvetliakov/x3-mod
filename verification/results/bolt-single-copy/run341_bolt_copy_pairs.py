#!/usr/bin/env python3
"""Run 92 A run341 (lod-overlay.md, "Run 91 A: bolts behind distant objects"): per F8 capture frame, the bolt_copy rows
of the bullet draws grouped by vertex buffer; the early copy (first draw of a buffer) and the late copy (last draw of
the same buffer) compared by hash, qsum, bbox and prims. Prints one line per frame and the count of frames whose pairs
all match. Reads only the bolt_copy rows of the session log (the log itself stays local).

    python3 verification/results/bolt-single-copy/run341_bolt_copy_pairs.py /tmp/x3-bottleX3-run341/session-*.log
"""
import re
import sys
from collections import defaultdict

rows = defaultdict(list)
for path in sys.argv[1:]:
    with open(path, errors='replace') as handle:
        for line in handle:
            at = line.find('bolt_copy ')
            if at < 0:
                continue
            f = dict(re.findall(r'(\w+)=([^\s]+)', line[at:]))
            rows[int(f['frame'])].append(f)
matched = 0
for frame in sorted(rows):
    by_vb = defaultdict(list)
    for r in rows[frame]:
        by_vb[r['vb']].append(r)
    pairs = [(v[0], v[-1]) for v in by_vb.values() if len(v) >= 2]
    same = bool(pairs) and all(a.get('hash') != 'none' and all(a.get(k) == b.get(k) for k in ('hash', 'qsum', 'bbox', 'prims'))
                               for a, b in pairs)
    matched += same
    print(f'frame={frame} draws={[int(r["draw"]) for r in rows[frame]]} buffers={len(by_vb)} pairs={len(pairs)} identical={int(same)} '
          f'revisions={[(a["revision"], b["revision"]) for a, b in pairs]}')
print(f'frames={len(rows)} identical_pairs={matched}')
