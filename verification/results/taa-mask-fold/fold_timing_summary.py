#!/usr/bin/env python3
"""Mean and range per build / motion / config of fold_timing_pairs_out.txt (docs/verification/temporal-resolve.md, "Mask fold")."""
import collections
import re
from pathlib import Path

rows = collections.defaultdict(list)
for line in (Path(__file__).with_name('fold_timing_pairs_out.txt')).read_text().splitlines():
    f = dict(re.findall(r'(\w+)=(\S+)', line))
    rows[(f['build'], f['motion'], f['config'])].append({k: float(f[k]) for k in ('mask_ms', 'box_ms', 'resolve_ms', 'taa_ms', 'run_ms')})
base = {m: rows[('baseline', m, 'today')] for m in ('rest', 'pan')}
for key, values in rows.items():
    stats = ' '.join(f'{k}={sum(v[k] for v in values) / len(values):.3f}[{min(v[k] for v in values):.3f}..{max(v[k] for v in values):.3f}]' for k in ('mask_ms', 'box_ms', 'resolve_ms', 'taa_ms'))
    reference = base[key[1]]
    delta = sum(v['resolve_ms'] for v in values) / len(values) - sum(v['resolve_ms'] for v in reference) / len(reference)
    total = sum(v['taa_ms'] for v in values) / len(values) - sum(v['taa_ms'] for v in reference) / len(reference)
    print(f'build={key[0]} motion={key[1]} config={key[2]} rounds={len(values)} {stats} resolve_vs_baseline_ms={delta:+.3f} taa_vs_baseline_ms={total:+.3f}')
