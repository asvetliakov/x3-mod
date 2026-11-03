#!/usr/bin/env python3
"""Run 33b (FL, own Argon Centaur in chase view, no plumes) triage: streams the log, prints aggregates only.
Usage: python3 centaur_triage.py /home/asvetl/x3m-logs/fl-run33b-centaur.log"""
import sys, collections

def kv(line):
    return dict(t.split('=', 1) for t in line.split() if '=' in t)

path = sys.argv[1]
chase = []          # (frame, applied_cumulative) from chase_camera window rows
cascade = []        # own-ship rows (shadow_cascade_set own_node / own_radius)
stage = {}          # frame -> selected engine_stage fields
sb = collections.Counter()
keys = ['armed', 'records', 'nozzles', 'far_jets', 'far_records', 'culled_small', 'node_roots', 'node_walked',
        'node_children', 'node_hidden', 'node_dupes', 'node_records', 'node_not_ship', 'node_probes',
        'node_anchors', 'node_other_view', 'node_engine_culled', 'skipped_other_view']
with open(path, errors='replace') as f:
    for line in f:
        tag = line.split(' ', 1)[0]
        if tag == 'engine_stage':
            d = kv(line)
            stage[int(d['frame'])] = {k: int(float(d.get(k, 0))) for k in keys}
        elif tag == 'chase_camera' and 'frame=' in line and 'applied=' in line:
            d = kv(line); chase.append((int(d['frame']), int(d['applied']), d.get('mode'), d.get('distance')))
        elif tag == 'shadow_cascade_set':
            d = kv(line); cascade.append((d['frame'], d['own_node'], d['own_status'], d['own_radius']))
        elif tag == 'sector_background':
            sb[kv(line).get('status')] += 1

print('== own-ship rows (shadow_cascade_set frame, own_node, own_status, own_radius):', cascade)
print('== sector_background status counts:', dict(sb))
first = next((c for c in chase if c[1] > 0), None)
print('== chase_camera window rows:', len(chase), ' first with applied>0:', first, ' last:', chase[-1])
start = first[0] - 300 if first else 10**9  # applied is cumulative per 300-frame window
def summarise(name, frames):
    rows = [stage[f] for f in frames if f in stage and stage[f]['armed']]
    print(f'== {name}: armed stage frames={len(rows)}')
    if not rows: return
    for k in keys[1:]:
        v = [r[k] for r in rows]
        print(f'  {k}: sum={sum(v)} max={max(v)} frames_nonzero={sum(1 for x in v if x)}')
    print('  node_roots hist:', dict(sorted(collections.Counter(r['node_roots'] for r in rows).items())))
    print('  node_children hist (roots==1):',
          dict(sorted(collections.Counter(r['node_children'] for r in rows if r['node_roots'] == 1).items())))
    # Frames with hidden children: did the walk emit main jets (dupes) on the same frame?
    h = [r for r in rows if r['node_hidden']]
    print('  frames hidden>0:', len(h), ' of which dupes>0:', sum(1 for r in h if r['node_dupes']),
          ' roots>=2:', sum(1 for r in h if r['node_roots'] >= 2), ' far_jets>0:', sum(1 for r in h if r['far_jets']))
    print('  roots==1 frames with far_jets==0 and records==0:',
          sum(1 for r in rows if r['node_roots'] == 1 and not r['far_jets'] and not r['records']))
summarise('cockpit/other (before chase applied)', [f for f in stage if f < start])
summarise('chase applied', [f for f in stage if f >= start])

# A root walked with no record (draw or far) in this or the 3 previous frames cannot be a ShipRoots::recent root
# (recent_window = 2): the gather admitted it by the own-ship test (own_ship_node_). Its child count is the own hull's.
iso = collections.Counter()
for fr, r in stage.items():
    if fr >= start and r['armed'] and r['node_roots'] == 1 and r['node_probes'] == 0 and \
            all(fr - k in stage and stage[fr - k]['records'] == 0 for k in (0, 1, 2, 3)):
        iso[(r['node_children'], r['node_hidden'], r['node_dupes'], r['node_records'])] += 1
print('== chase frames: one non-probe root, no record in frames f-3..f -> (children, hidden, dupes, node_records):', dict(iso))
# The last 300-frame engine_node_walk_window rows (the brief's walked=14 row is the session's tail).
with open(path, errors='replace') as f:
    tail = [kv(l) for l in f if l.startswith('engine_node_walk_window ')][-3:]
print('== last engine_node_walk_window rows (frame, appends, walked, roots_max):',
      [(r['frame'], r['appends'], r['walked'], r['roots_max']) for r in tail])
