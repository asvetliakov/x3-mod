#!/usr/bin/env python3
"""Run 109 A (run384) summary: dust_leak_fix hits, engine_nodes/registry_live, cutevent and frame-time trend.
Usage: python3 extract.py <session log>  -> writes summary.json next to this script."""
import sys, re, json, statistics
from pathlib import Path
log = Path(sys.argv[1]); out = Path(__file__).with_name('summary.json')
kv = lambda l: dict(p.split('=', 1) for p in l.split()[1:] if '=' in p)
status = hits = census = phases = None; hits = []; census = []; phases = []; dts = []
for l in open(log, errors='replace'):
    if l.startswith('dust_leak_fix site='): status = kv(l)
    elif l.startswith('dust_leak_fix hits='): hits.append(kv(l))
    elif l.startswith('scene_graph_census'): census.append(kv(l))
    elif l.startswith('loop_phases'): phases.append(kv(l))
    elif l.startswith('frame_end '):
        d = kv(l); dts.append((int(d['frame']), float(d['dt_ms'])))
sector = [c for c in census if c.get('engine_nodes', '-') != '-' and int(c['engine_nodes']) > 1000]
def window(lo, hi):
    v = [dt for f, dt in dts if lo <= f < hi]; return statistics.median(v) if v else None
summary = {
    'status': status, 'hits_rows': len(hits), 'hits_300_rows': sum(1 for h in hits if h['hits'] == '300'),
    'hits_total': hits[-1]['total'] if hits else None,
    'engine_nodes_first_sector_row': int(sector[0]['engine_nodes']) if sector else None,
    'engine_nodes_last_sector_row': int(sector[-1]['engine_nodes']) if sector else None,
    'engine_nodes_max': max(int(c['engine_nodes']) for c in sector) if sector else None,
    'registry_equals_engine_all_rows': all(c['registry_live'] == c['engine_nodes'] for c in sector),
    'sector_rows': len(sector), 'frames': dts[-1][0] if dts else None,
    'cutevent_p50_us_first_last': [int(phases[3]['cutevent_p50_us']), int(phases[-1]['cutevent_p50_us'])] if len(phases) > 3 else None,
    'input_p50_us_first_last': [int(phases[3]['input_p50_us']), int(phases[-1]['input_p50_us'])] if len(phases) > 3 else None,
    'dt_ms_p50_frames_1000_4000': window(1000, 4000), 'dt_ms_p50_frames_9000_12000': window(9000, 12000),
    'dt_ms_p50_last_3000': window(dts[-1][0] - 3000, dts[-1][0] + 1) if dts else None,
}
json.dump(summary, open(out, 'w'), indent=1); print(json.dumps(summary, indent=1))
