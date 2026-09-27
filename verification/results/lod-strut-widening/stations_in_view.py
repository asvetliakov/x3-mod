"""Station bodies kept in the cull census of the captured frames (run340 rest/pan/pan-moving, run346 rest/pan):
body, nodes, distance range (engine units and km at 505 u/m), radius, s range, LOD drawn, ladder thr.
Usage: python3 stations_in_view.py <session.log> <frame> [<frame> ...]  (reads the log line by line, prints ~20 rows)."""
import re, sys
from collections import defaultdict
UNITS_PER_M = 505.0
log, frames = sys.argv[1], set(sys.argv[2:])
rx = re.compile(r'frame=(\d+) .*? s=(\d+) measure=\d+ d=(\d+) radius=(\d+) .*? lod=(\d+) verdict=(\w+).*? lods=(\S+) thr=(\S+) body=(\S+)')
agg = defaultdict(lambda: dict(n=0, d=[], s=[], lod=set(), r=set(), thr=None))
with open(log, errors='replace') as f:
    for line in f:
        if not line.startswith('cull_census '):
            continue
        m = rx.search(line)
        if not m:
            continue
        fr, s, d, r, lod, verdict, lods, thr, body = m.groups()
        if fr not in frames or verdict != 'kept':
            continue
        if not body.lower().startswith('stations'):
            continue
        a = agg[(fr, body)]
        a['n'] += 1; a['d'].append(int(d)); a['s'].append(int(s)); a['lod'].add(int(lod)); a['r'].add(int(r)); a['thr'] = thr
for (fr, body), a in sorted(agg.items(), key=lambda kv: (kv[0][0], min(kv[1]['d']))):
    dmin, dmax = min(a['d']), max(a['d'])
    print(f'frame {fr} {body:60s} nodes {a["n"]:3d} d {dmin}-{dmax} ({dmin/UNITS_PER_M/1000:.1f}-{dmax/UNITS_PER_M/1000:.1f} km)'
          f' r {sorted(a["r"])} s {min(a["s"])}-{max(a["s"])} lod {sorted(a["lod"])} thr {a["thr"]}')
