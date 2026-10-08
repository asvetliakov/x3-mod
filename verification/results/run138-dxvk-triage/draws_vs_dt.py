#!/usr/bin/env python3
"""Per-frame draws vs frame time for the wined3d (run9) / DXVK (run10) A/B, binned by 10 draws (bins >= 20 frames).
In flight = after the load frame (largest dt_ms) + 10 and before the last 10. Feeds the Draw Calls vs Frame Time chart.
Usage: python3 draws_vs_dt.py /tmp/x3-bottleX3-run9 /tmp/x3-bottleX3-run10"""
import re, sys, glob, json, statistics as st
KV = re.compile(r'(\w+)=(-?\d+)')
for folder in sys.argv[1:]:
    fe = []
    with open(glob.glob(f'{folder}/session-*.log')[0], 'rb') as f:
        for raw in f:
            if raw.startswith(b'frame_end '):
                fe.append({k: int(v) for k, v in KV.findall(raw.decode('latin1'))})
    L = max(range(len(fe)), key=lambda i: fe[i]['dt_ms']); lo = fe[L]['frame'] + 10; hi = fe[-1]['frame'] - 10
    s = [d for d in fe if lo <= d['frame'] <= hi and d['dt_ms'] > 0]
    bins = {}
    for d in s: bins.setdefault(d['draws'] // 10 * 10, []).append(d['dt_ms'])
    print(folder, 'inflight', len(s), 'draws', min(d['draws'] for d in s), '-', max(d['draws'] for d in s))
    for b, v in sorted(bins.items()):
        if len(v) >= 20: print(json.dumps({'draws': b, 'n': len(v), 'p50': st.median(v), 'p95': sorted(v)[int(len(v) * .95)], 'mean': round(st.mean(v), 2)}))
