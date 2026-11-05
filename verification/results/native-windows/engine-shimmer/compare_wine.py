"""Native Windows record against the retained Wine record of run_engine_shimmer.py: check verdicts that differ, the
STATE row (stream 0 offset / stride, index buffer, s5 texture, c60 after the pass's restore), GPU cost and displacement
side by side. Prints only those rows."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
wine = json.loads((ROOT / 'verification/results/bottle-X3/engine-shimmer/summary.json').read_text())
native = json.loads((Path(__file__).parent / 'summary.json').read_text())
for name, r in (('wine', wine), ('native', native)):
    print(name, 'passed', r['passed'], 'checks', r['report']['check_count'], 'failed', r['report']['failed_checks'],
          'commit', r['source']['commit'][:12])
w, n = dict(map(tuple, wine['report']['checks'])), dict(map(tuple, native['report']['checks']))
print('same labels', set(w) == set(n), 'verdicts differ', [k for k in w if n.get(k) != w[k]])
print('state wine', wine['report']['state'])
print('state native', native['report']['state'])
for k, v in wine['summary']['timing_gpu_ms'].items():
    print('gpu_ms', k, v, '->', native['summary']['timing_gpu_ms'].get(k))
for size, a in wine['summary']['displace'].items():
    b = native['summary']['displace'][size]
    print('displace', size, {k: (a[k], b[k]) for k in a})
