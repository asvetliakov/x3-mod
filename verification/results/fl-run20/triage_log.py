#!/usr/bin/env python3
"""FL run 20 culls triage. Usage: triage_log.py LOG. Prints figures only.
Fog state per frame: last volumetric_fog_frame applied= value, carried forward
(the logger writes sparse/state-change rows), cleared by a volumetric_fog_sector
row with profile=0. Captures: frames carrying capture_event rows, grouped by run."""
import re, sys, statistics as st
from collections import Counter, defaultdict
KV = re.compile(r'(\w+)=(\S+)')
BAD = re.compile(r'\b(\w*(?:refused|problem|fault|invalid|mismatch|rollback|disarm)\w*)=(\S+)')
def pct(v, p):
    v = sorted(v); return v[min(len(v)-1, int(p*(len(v)-1)+0.5))] if v else None
def dist(v): return f"n={len(v)} min={min(v)} med={st.median(v)} p90={pct(v,.9)} max={max(v)}" if v else "n=0"
path = sys.argv[1]
frames = {}; fogrows = {}; fog_state = 0; capfr = set(); kinds = Counter()
bad = Counter(); badex = {}; cull = defaultdict(list); es = {}; ee = {}
WATCH = ('cull_small_parts', 'terran_station_lod', 'lod_occlusion')
with open(path, errors='replace') as f:
    for line in f:
        k = line.split(' ', 1)[0]
        if k == 'frame_end':
            d = dict(KV.findall(line)); n = int(d['frame'])
            frames[n] = (int(d['draws']), int(d['dt_ms']), fog_state, d.get('capture'))
            continue
        if k in ('constant','sampler','state','stream','transform','texture','constants','vertex_element','texture_desc'):
            continue
        if k == 'volumetric_fog_frame':
            d = dict(KV.findall(line)); fog_state = int(d['applied']); fogrows[int(d['frame'])] = fog_state
        elif k == 'volumetric_fog_sector' and ' profile=0 ' in line:
            fog_state = 0
        elif k == 'capture_event':
            capfr.add(int(KV.search(line[14:]).group(0) and dict(KV.findall(line))['frame']))
        elif k.startswith(WATCH):
            kinds[k] += 1
            if k.endswith('_frame'):
                d = dict(KV.findall(line)); cull[k].append(d)
        elif k in ('engine_stage', 'engine_effects'):
            d = dict(KV.findall(line))
            if 'frame' in d: (es if k == 'engine_stage' else ee)[int(d['frame'])] = d
        for m in BAD.finditer(line):
            key, val = m.groups()
            if val in ('none', '0', '0.0', '0.000', '00000000'): continue
            if k == 'window_mode' and 'reason=fullscreen' in line: continue
            sig = f"{k}:{key}"; bad[sig] += 1; badex.setdefault(sig, line.strip()[:240])
caps = []
for n in sorted(capfr):
    if caps and n - caps[-1][-1] <= 2: caps[-1].append(n)
    else: caps.append([n])
print(f"log={path} frames={len(frames)} fog_rows={len(fogrows)} fog_rows_applied={sum(fogrows.values())}")
print("== Q1 captures")
for i, c in enumerate(caps, 1):
    n0 = c[0]; win = [frames[j][1] for j in range(n0-30, n0+30) if j in frames]
    dr = [frames[j][0] for j in c if j in frames]
    print(f"capture{i} frames={c[0]}-{c[-1]} draws_first={dr[0] if dr else None} draws_range={min(dr)}-{max(dr)} "
          f"dt_median60={st.median(win) if win else None} fog={frames[n0][2]} fogrow_near={ {k:v for k,v in fogrows.items() if abs(k-n0)<700} }")
fogd = [v for v in frames.values() if v[2] == 1]
fogn = sorted(n for n, v in frames.items() if v[2] == 1)
print(f"fogged_span={fogn[0] if fogn else None}-{fogn[-1] if fogn else None}")
print("fogged draws:", dist([v[0] for v in fogd]))
print("== Q5 fogged dt_ms:", dist([v[1] for v in fogd]))
print("== Q2 watch kinds:", dict(kinds))
for k, rows in cull.items():
    for fld in ('culled', 'dock_culled', 'exempt_bullet', 'kept', 'projectiles_kept'):
        vals = [int(r[fld]) for r in rows if fld in r and r[fld].lstrip('-').isdigit()]
        if vals: print(f"{k} {fld}: {dist(vals)}")
    print(f"{k} frames: {sorted(set(int(r['frame']) for r in rows if 'frame' in r))[:3]}... sources={Counter(r.get('source') for r in rows)} fallbacks={Counter(r.get('fallback') for r in rows)}")
print("== Q3 nonzero flags")
for s, c in bad.most_common(): print(f"{c:8d} {s} ex: {badex[s]}")
print("== Q4 engine_stage/effects in capture frames")
for i, c in enumerate(caps, 1):
    a = [(es[j].get('armed'), es[j].get('records'), es[j].get('far_records')) for j in c if j in es]
    b = [ee[j] for j in c if j in ee]
    print(f"capture{i} engine_stage armed/records/far_records={Counter(a)} engine_effects_rows={len(b)}"
          + (f" ex={ {k:v for k,v in b[0].items() if k in ('armed','records','drawn','plumes','reason')} }" if b else ""))
