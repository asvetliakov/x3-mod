#!/usr/bin/env python3
"""Run 33 (FL, hidden-emitter anchors) triage: streams the session log, prints only aggregates.
Usage: python3 triage_log.py /home/asvetl/x3m-logs/fl-run33-emitters.log"""
import sys, collections

def kv(line):
    return dict(t.split('=', 1) for t in line.split() if '=' in t)

path = sys.argv[1]
win = []; walk = []; exc = collections.Counter(); stage_n = 0
st = collections.Counter(); st_max = collections.Counter(); hist_roots = collections.Counter()
probe_frames = 0; notship_frames = 0; deferred_frames = 0; other_view = 0
hull_rows = 0; refused = []
keys = ['node_roots', 'node_walked', 'node_not_ship', 'node_probes', 'node_anchors', 'node_anchor_roots',
        'node_other_view', 'node_no_scale', 'node_children', 'node_hidden', 'node_records', 'node_unreadable', 'node_no_boundary']
with open(path, errors='replace') as f:
    for line in f:
        tag = line.split(' ', 1)[0]
        if tag == 'engine_plumes_hidden_emitters' and 'event=window' in line:
            win.append(kv(line))
        elif tag == 'engine_plumes_hidden_emitter_hull':
            hull_rows += 1
        elif tag == 'engine_node_walk_window':
            walk.append(kv(line))
        elif tag == 'engine_node_walk_refused':
            refused.append(line.strip()[:200])
        elif tag == 'engine_stage':
            d = kv(line); stage_n += 1
            for k in keys:
                v = int(d.get(k, 0)); st[k] += v; st_max[k] = max(st_max[k], v)
            hist_roots[min(int(d.get('node_roots', 0)), 9)] += 1
            probe_frames += int(d.get('node_probes', 0)) > 0
            notship_frames += int(d.get('node_not_ship', 0)) > 0
        if 'exception' in line.lower() or 'fault=1' in line:
            exc[tag] += 1

print('== hidden-emitter window rows:', len(win))
for k in ['frames', 'probes', 'probe_deferred', 'glow', 'not_emitter', 'rejected', 'no_floor', 'mode_reads', 'new_hulls', 'anchors', 'ships']:
    v = [int(r[k]) for r in win]
    print(f'  {k}: sum={sum(v)} max={max(v)} rows_nonzero={sum(1 for x in v if x)}')
print('  rows with probes==1:', sum(1 for r in win if r['probes'] == '1'), ' last 5 probes:', [r['probes'] for r in win[-5:]])
print('  walk_us_max max:', max(float(r['walk_us_max']) for r in win))
print('== engine_plumes_hidden_emitter_hull rows:', hull_rows)
print('== engine_stage rows:', stage_n)
for k in keys:
    print(f'  {k}: sum={st[k]} max={st_max[k]}')
print('  frames with node_probes>0:', probe_frames, ' frames with node_not_ship>0:', notship_frames)
print('  node_roots histogram (9 = >=9):', dict(sorted(hist_roots.items())))
print('== engine_node_walk_window rows:', len(walk))
for k in ['appends', 'walked', 'refused', 'unreadable', 'no_scale', 'samples']:
    v = [int(r[k]) for r in walk]
    print(f'  {k}: sum={sum(v)} max={max(v)} rows_nonzero={sum(1 for x in v if x)}')
print('  roots_max max:', max(int(r['roots_max']) for r in walk),
      ' rows by roots_max:', dict(collections.Counter(int(r['roots_max']) for r in walk)))
print('  walk_us_p99 max:', max(float(r['walk_us_p99']) for r in walk), ' walk_us_max max:', max(float(r['walk_us_max']) for r in walk))
print('== engine_node_walk_refused rows:', len(refused), refused[:3])
print('== rows mentioning exception or fault=1 by tag:', dict(exc))

# Silent-drop counters an anchor could hit after the walk (engine_stage, session sums).
extra = collections.Counter()
with open(path, errors='replace') as f:
    for line in f:
        if line.startswith('engine_stage '):
            d = kv(line)
            for k in ('node_overflow', 'node_invalid', 'node_dupes', 'node_guard_rejected', 'far_overflow', 'far_dropped'):
                extra[k] += int(d.get(k, 0))
print('== engine_stage silent-drop sums:', dict(extra))

# Could an anchor have been dropped as far_record 'invalid' (node_invalid) rather than counted? Split by probe frames.
split = collections.Counter()
with open(path, errors='replace') as f:
    for line in f:
        if line.startswith('engine_stage '):
            d = kv(line)
            p = int(d.get('node_probes', 0)) > 0
            inv = int(d.get('node_invalid', 0))
            split[('probe_frame' if p else 'no_probe_frame', 'frames')] += 1
            split[('probe_frame' if p else 'no_probe_frame', 'invalid_frames')] += inv > 0
            split[('probe_frame' if p else 'no_probe_frame', 'invalid_sum')] += inv
print('== node_invalid by probe frame:', {f'{a}.{b}': v for (a, b), v in sorted(split.items())})

# frame_end dt_ms p50 (the kill criterion's 10 % bound).
dts = []
with open(path, errors='replace') as f:
    for line in f:
        if line.startswith('frame_end '):
            v = kv(line).get('dt_ms')
            if v is not None:
                dts.append(float(v))
dts.sort()
if dts:
    print('== frame_end dt_ms: n=%d p50=%.3f -> 10%% = %.1f us' % (len(dts), dts[len(dts) // 2], dts[len(dts) // 2] * 100))
