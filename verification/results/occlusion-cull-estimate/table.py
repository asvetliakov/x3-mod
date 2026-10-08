#!/usr/bin/env python3
"""Per-frame table from analyze.py summaries. Usage: table.py <tol> <summary.json>..."""
import json, sys
SUB = ('turret', 'dock', 'jet', 'other_part')
tol = sys.argv[1]
print('run frame draws boxes sub sub_box | full mostly | full_prims full_verts mostly_prims | hull_full | us/draw full_ms mostly_ms | eng_small eng_dock proxy_props | covered_px')
for path in sys.argv[2:]:
    for s in json.load(open(path)):
        st = s['stats'][tol]
        g = lambda k, cls=SUB: sum(st.get(c, {}).get(k, 0) for c in cls)
        sub = sum(s['class_draws'].get(c, 0) for c in SUB); subb = sum(s['class_boxes'].get(c, 0) for c in SUB)
        us = s['cost']['us_per_draw']; ac = s['already_culled']
        print(s['run'], s['frame'], s['draws'], s['boxes'], sub, subb, '|', g('full'), g('mostly'), '|', g('full_prims'), g('full_verts'), g('mostly_prims'), '|',
              g('full', ('hull',)), '|', round(us, 1), round(g('full') * us / 1000, 3), round(g('mostly') * us / 1000, 3), '|',
              ac['engine_small'], ac['engine_dock'], ac['proxy_small_props'], '|', round(s['covered_px_frac'], 3))
