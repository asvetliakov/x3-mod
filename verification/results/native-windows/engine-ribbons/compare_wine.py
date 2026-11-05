"""Native Windows record against the retained Wine record of run_engine_ribbons.py: check verdicts that differ, the
resolve rows side by side, and the gate numbers. Prints only those rows."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
wine = json.loads((ROOT / 'verification/results/bottle-X3/engine-ribbons/summary.json').read_text())
native = json.loads((Path(__file__).parent / 'summary.json').read_text())
for name, r in (('wine', wine), ('native', native)):
    print(name, 'passed', r['passed'], 'checks', r['report']['check_count'], 'failed', len(r['report']['failed_checks']),
          'commit', r['source']['commit'][:12])
w, n = dict(map(tuple, wine['report']['checks'])), dict(map(tuple, native['report']['checks']))
print('same labels', set(w) == set(n), 'only wine', sorted(set(w) - set(n)), 'only native', sorted(set(n) - set(w)))
print('verdicts differ', [k for k in w if k in n and n[k] != w[k]])
for a, b in zip(wine['report']['ribbon_resolve'], native['report']['ribbon_resolve']):
    keys = [k for k in a if a.get(k) != b.get(k)]
    print('resolve', a.get('width'), a.get('sky'), a.get('speed_px'), {k: (a[k], b.get(k)) for k in keys})
for tag in ('ribbon_seta', 'ribbon_cut'):
    for a, b in zip(wine['report'][tag], native['report'][tag]):
        print(tag, {k: (a[k], b.get(k)) for k in a if a.get(k) != b.get(k)})
gw, gn = wine['gates_met'], native['gates_met']
for key in ('near_survival_min', 'stage_gpu_ms', 'cpu_us', 'fog'):
    print(key, gw[key], '->', gn[key])
