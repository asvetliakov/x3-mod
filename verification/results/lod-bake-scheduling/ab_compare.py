#!/usr/bin/env python3
"""Compare two scratch batch bakes (lod_overlay.py --batch --out DIR --only ab_subset.txt) run under
run_monitored.py: wall, census and bake seconds (the batch record), worker count max/mean per phase (the monitor
timeline split at the census end = wall - record total_s + census_s, so approximate by the start-up time), peak
process-tree RSS, and byte identity of every member of the written addon catalogues (decoded .dat slices).

  python3 ab_compare.py LABEL=MONITOR_JSON:OUT_DIR LABEL=MONITOR_JSON:OUT_DIR
"""
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
from inspect_x3 import read_catalogue  # noqa: E402


def members(out):
    got = {}
    for cat in sorted((Path(out) / 'addon').glob('[0-9][0-9].cat')):
        raw = bytes(v ^ 0x33 for v in cat.with_suffix('.dat').read_bytes())
        for e in read_catalogue(cat):
            got[e['path']] = hashlib.sha256(raw[e['offset']:e['offset'] + e['size']]).hexdigest()
    return got


def phase(rows):
    if not rows:
        return '-'
    w = [r[2] for r in rows]
    return f'max {max(w)} mean {sum(w) / len(w):.1f}'


runs = {}
for arg in sys.argv[1:]:
    label, rest = arg.split('=', 1)
    mon, out = rest.split(':', 1)
    m = json.load(open(mon))
    rec = json.load(open(Path(out) / 'x3m-lod-batch.json'))
    t = rec['timing']
    split = m['wall_s'] - t['total_s'] + t['census_s']
    tl = m['timeline']
    runs[label] = members(out)
    sched = rec.get('scheduling') or {}
    print(f"{label}: wall {m['wall_s']} s (census {t['census_s']} s, bake {t['bake_s']} s for {rec['counts']['built']}"
          f" built, {rec['counts']['eligible']} eligible of {rec['counts']['enumerated']}); jobs {rec['jobs']}"
          + (f" rule {sched.get('rule')} budget {sched['budget'] / 2**30:.1f} GiB census workers"
             f" {sched.get('census_workers')} peak running {sched.get('peak_running')} predicted peak"
             f" {sched.get('peak_predicted_bytes', 0) / 2**30:.1f} GiB" if sched.get('budget') else '')
          + f"; workers census {phase([r for r in tl if r[0] < split])}, bake {phase([r for r in tl if r[0] >= split])};"
          f" tree RSS peak {m['whole']['rss_peak_gib']} GiB mean {m['whole']['rss_mean_gib']} GiB;"
          f" members {len(runs[label])}")
labels = list(runs)
for a, b in zip(labels, labels[1:]):
    x, y = runs[a], runs[b]
    same = sum(1 for k in x if y.get(k) == x[k])
    print(f'{a} vs {b}: {same} of {len(x)} members byte-identical; only in {a} {len(set(x) - set(y))},'
          f' only in {b} {len(set(y) - set(x))}, differing {sum(1 for k in x if k in y and y[k] != x[k])}')
