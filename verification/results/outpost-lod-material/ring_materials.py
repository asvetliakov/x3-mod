#!/usr/bin/env python3
"""Material share of the ring box (hdr_1_2410 x 2505..2625, y 607..667) in the record-0 projection written by
project_record.py (OUT.npz of frame 2410 draw 22). Approximate: the projection fits the logged bounds to
25.6 px summed error. Usage: ring_materials.py proj_2410.npz"""
import sys
from collections import Counter
import numpy as np
d = np.load(sys.argv[1]); x0, y0 = int(d['x0']), int(d['y0']); ids, fm = d['id_src'], d['fmat']
reg = ids[607 - y0:668 - y0, 2505 - x0:2626 - x0]
c = Counter(fm[reg[reg >= 0]].tolist()); n = sum(c.values())
print('ring px', n, [(m, round(v / n, 3)) for m, v in c.most_common()])
