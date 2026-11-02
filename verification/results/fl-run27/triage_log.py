#!/usr/bin/env python3
"""Run 27 log triage (streaming). Usage: triage_log.py LOG"""
import sys, re, collections, statistics as st
L = sys.argv[1]
kv = lambda s: dict(re.findall(r'(\w+)=(\S+)', s))
c = collections.Counter(); ev = collections.Counter(); restores = []; vr = collections.Counter()
win = {'chase_central_hud_window': [], 'chase_lead_window': []}
es = []; dts = []; capf = set(); capev = collections.Counter(); bad = collections.Counter(); badsample = {}
fe = {}
for line in open(L, errors='replace'):
    t = line.split(' ', 1)[0]
    if t in ('constant', 'sampler', 'state', 'stream', 'resource'): continue
    if t == 'engine_stage':
        d = kv(line); es.append((int(d.get('frame', -1)), {k: int(d.get(k, 0)) for k in ('far_jets','far_records','far_dropped','far_disarmed','far_overflow','far','far_engine')})); continue
    if t == 'frame_end':
        d = kv(line); f = int(d['frame']); dts.append(int(d['dt_ms'])); fe[f] = int(d['dt_ms'])
        if d.get('capture', '0') != '0': capf.add(f)
        continue
    for pat in ('installed=0', 'refused', 'rollback=failed', 'writer_mismatch', 'status=late_claim', 'error', 'fail'):
        if pat in line: bad[(t, pat)] += 1; badsample.setdefault((t, pat), line[:300].strip())
    if t == 'chase_transition_event':
        d = kv(line); ev[(d.get('kind'), d.get('kind_name'))] += 1
        if d.get('kind') == '9': restores.append({k: d.get(k) for k in ('frame','result','cancel','attempts','hud_refresh','mode_seen')})
    elif t == 'chase_view_restore':
        vr[kv(line).get('result')] += 1
    elif t in win:
        win[t].append(kv(line))
    elif t == 'capture_event':
        d = kv(line); capev[d.get('event', d.get('kind', '?'))] += 1
        if 'frame' in d: capf.add(int(d['frame'])) if d.get('event','') in ('begin','start','armed','captured') else None
print('events', dict(ev)); print('restores', restores); print('view_restore', dict(vr))
for t, rows in win.items():
    lt = [int(r.get('lifetime', 0)) for r in rows if r.get('lifetime', '').isdigit()]
    z = [r for r in rows if r.get('admitted') == '0']
    print(t, 'rows', len(rows), 'lifetime first/max/last', lt[:1], max(lt) if lt else None, lt[-1:], 'admitted0', len(z),
          'admitted0_frames', [r.get('frame') for r in z][:20], 'keys', sorted(rows[0].keys()) if rows else None)
pos = [d for f, d in es if d['far_jets'] > 0]
print('engine_stage rows', len(es), 'far_jets>0', len(pos), 'far_records>0', sum(d['far_records']>0 for f,d in es))
for k in ('far_jets','far_records','far_dropped','far_disarmed','far_overflow'):
    v = [d[k] for d in pos]
    if v: print(k, min(v), st.median(v), max(v), 'sum', sum(v))
if pos: print('dropped/jets', sum(d['far_dropped'] for d in pos) / sum(d['far_jets'] for d in pos))
q = lambda v, p: sorted(v)[int(p * (len(v) - 1))]
print('dt p50/p90', q(dts, .5), q(dts, .9), 'n', len(dts)); print('capture frames', sorted(capf), 'capev', dict(capev))
esd = dict(es)
for f in sorted(capf): print(' cap', f, esd.get(f), 'dt', fe.get(f))
print('bad', {f'{a}|{b}': n for (a, b), n in bad.items()})
for k, s in badsample.items(): print(' ', k, s)
