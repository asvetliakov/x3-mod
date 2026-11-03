#!/usr/bin/env python3
"""FL run 29-32 log triage (streaming). Usage: triage_log.py LOG [--short]
Prints frame_end draws/issued/dt distributions (overall, per sector, fog-applied windows,
pre-capture windows), capture-frame overlay draws per (model,lod), ui_scale/text_density,
node walk windows, far jets, chase rows and an error sweep. Works on run 27 logs too."""
import sys, re, collections, bisect
L = sys.argv[1]
kv = lambda s: dict(re.findall(r'(\w+)=("[^"]*"|\S+)', s))
q = lambda v, p: sorted(v)[int(p * (len(v) - 1))] if v else None
fe = {}                       # frame -> (draws, issued, dt, capture)
sect = []                     # (frame, name) on change
fogf = []                     # (frame, applied, cards)
es = []; nww = []; nwr = collections.Counter(); single = collections.defaultdict(list)
ev = collections.Counter(); vr = collections.Counter(); inst = collections.Counter()
bad = collections.Counter(); badsample = {}; ctx = collections.Counter(); ctxframes = set()
SINGLE = ('ui_scale','ui_scale_install','text_density','text_density_install','text_density_rows',
          'text_density_filter','text_density_recorder','engine_nozzle_source','engine_plumes_hidden_emitters',
          'cull_small_parts','terran_station_lod','engine_plumes_state','engine_plumes_device')
lastname = None
for line in open(L, errors='replace'):
    t = line.split(' ', 1)[0]
    if t in ('constant', 'sampler', 'state', 'stream', 'resource', 'transform', 'texture', 'constants'): continue
    if t == 'frame_end':
        d = kv(line)
        if 'frame' in d: fe[int(d['frame'])] = (int(d['draws']), int(d.get('issued', -1)), int(d['dt_ms']), d.get('capture', '0') != '0')
        continue
    if t == 'engine_stage':
        d = kv(line); es.append({k: (float(v) if '.' in v else int(v)) for k, v in d.items() if re.fullmatch(r'-?[\d.]+', v)}); continue
    if t == 'object_context':
        d = kv(line); f = int(d['frame']); ctxframes.add(f); ctx[(f, d['model'], d['lod'])] += 1; continue
    if t == 'sector_background':
        d = kv(line); n = d.get('name', '""')
        if n != lastname: sect.append((int(d['frame']), n)); lastname = n
    elif t == 'volumetric_fog_frame':
        d = kv(line); fogf.append((int(d['frame']), d['applied'], int(d['cards']), d['reason']))
    elif t == 'engine_node_walk_window':
        nww.append(kv(line))
    elif t == 'engine_node_walk_refused':
        nwr[kv(line).get('reason', '?') + '/' + kv(line).get('end', '?')] += 1
    elif t == 'chase_transition_event':
        d = kv(line); ev[(d.get('kind'), d.get('result'))] += 1
    elif t == 'chase_view_restore':
        vr[kv(line).get('result')] += 1
    if t in SINGLE: single[t].append(line.strip()[:400])
    if 'installed=' in line and t.startswith('chase'): inst[(t, kv(line).get('installed'))] += 1
    for pat in ('installed=0', r'refused=[1-9]', r'status=refused', 'rollback=failed', 'writer_mismatch', 'late_claim',
                'late_window', 'umeplusclp_missing', r'exception=[1-9]', 'crash', r'error=[1-9]', r'failed=[1-9]',
                r'failures=[1-9]', 'patched_unverified', 'window_mismatch', 'first_pass', r'unreadable=[1-9]'):
        if re.search(r'\b' + pat, line): bad[(t, pat)] += 1; badsample.setdefault((t, pat), line[:260].strip())
frames = sorted(fe)
def dist(name, fs):
    fs = [f for f in fs if f in fe and fe[f][0] > 0 and not fe[f][3]]
    D = [fe[f][0] for f in fs]; I = [fe[f][1] for f in fs]; T = [fe[f][2] for f in fs]
    print(f'{name}: n={len(fs)} draws p50/p90 {q(D,.5)}/{q(D,.9)} issued p50/p90 {q(I,.5)}/{q(I,.9)} dt p50/p90 {q(T,.5)}/{q(T,.9)}')
dist('overall', frames)
# per sector (frame ranges between sector_background name changes)
sf = [s[0] for s in sect]
bys = collections.defaultdict(list)
for f in frames:
    i = bisect.bisect_right(sf, f) - 1
    bys[sect[i][1] if i >= 0 else '?'].append(f)
for n, fs in sorted(bys.items(), key=lambda x: -len(x[1])): dist(f' sector {n}', fs)
# fog applied windows: frames within 300 after an applied=1 fog row
fa = [f for f, a, c, r in fogf if a == '1']
print('fog rows', len(fogf), 'applied=1', len(fa), 'reasons', dict(collections.Counter(r for *_, r in fogf)))
fogset = set(); [fogset.update(range(f, f + 300)) for f in fa]
dist(' fog-applied (+300 after applied row)', [f for f in frames if f in fogset])
dist(' no-fog', [f for f in frames if f not in fogset])
caps = sorted(f for f in frames if fe[f][3]); groups = []
for f in caps:
    if groups and f - groups[-1][-1] <= 1: groups[-1].append(f)
    else: groups.append([f])
for g in groups:
    print('capture', g[0], '-', g[-1], 'cap frames draws/issued/dt', [fe[f][:3] for f in g[:3]])
    dist('  pre-capture 600 frames', range(g[0] - 600, g[0]))
for f in sorted(ctxframes):
    ov = sorted(((m, l, n) for (ff, m, l), n in ctx.items() if ff == f and l != '00000000'), key=lambda x: -x[2])
    print(' ctx frame', f, 'draws', sum(n for (ff, m, l), n in ctx.items() if ff == f), 'lod!=0 (model,lod,draws):', ov[:40])
for t, rows in single.items(): print('ROW', t, len(rows), rows[0][:330])
print('node_walk_window rows', len(nww), 'unreadable max', max((int(r['unreadable']) for r in nww), default=None),
      'refused sum', sum(int(r['refused']) for r in nww), 'roots_max', max((int(r['roots_max']) for r in nww), default=None),
      'p99 max', max((float(r['walk_us_p99']) for r in nww), default=None), 'max max', max((float(r['walk_us_max']) for r in nww), default=None))
act = [r for r in nww if int(r.get('samples', 0)) > 0]
for k in ('walk_us_p50', 'walk_us_p99', 'walk_us_max', 'walked', 'appends'):
    v = [float(r[k]) for r in act]; print(f'  {k} over {len(v)} rows with samples: p50 {q(v,.5)} p90 {q(v,.9)} max {max(v) if v else None}')
print('node_walk_refused', dict(nwr))
pos = [d for d in es if d.get('far_jets', 0) > 0]
print('engine_stage', len(es), 'far_jets>0', len(pos), 'node_unreadable max', max((d.get('node_unreadable', 0) for d in es), default=0),
      'node_anchors max', max((d.get('node_anchors', 0) for d in es), default=0))
for k in ('far_jets', 'far_records', 'far_dropped', 'far_disarmed', 'far_overflow'):
    v = [d.get(k, 0) for d in pos]
    if v: print(f'  {k} min/p50/p90/max {min(v)}/{q(v,.5)}/{q(v,.9)}/{max(v)} sum {sum(v)}')
if pos: print('  dropped/jets', round(sum(d['far_dropped'] for d in pos) / sum(d['far_jets'] for d in pos), 4))
print('chase events (kind,result)', dict(ev), 'view_restore', dict(vr), 'installed', dict(inst))
print('bad', {f'{a}|{b}': n for (a, b), n in sorted(bad.items())})
if '--short' not in sys.argv:
    for k, s in sorted(badsample.items()): print(' ', k, s)
