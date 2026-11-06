#!/usr/bin/env python3
"""Run236 Flight C (2026-11-06) from x3m.log: the weapon_effects_frame totals, stage failure rows, session_end, and
the region check behind the +-y finding (docs/verification/weapon-effects.md, "Run236 Flight C").

  python3 verification/results/weapon-effects-run236/flight_c_rows.py [/mnt/c/X3/x3m.log]

Reads the log line by line (never whole). The face check is an inference: per owner with at least 8 shield hits the
hull box is taken as the hits' own largest |pos| per owner axis (a lower bound of the real half-extents), each hit's
face as argmax |pos_k| / box_k, and compared with the axis core::travel_region chose (the largest |dir_k|), and with
the travel normalised by the same box (argmax |dir_k| / box_k).
"""
import collections
import sys

LOG = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3/x3m.log'
frames, fails, hits, end = [], [], [], None
for line in open(LOG, errors='replace'):
    if line.startswith('weapon_effects_frame '):
        frames.append(dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv))
    elif line.startswith('weapon_effects_stage') or line.startswith('weapon_effects_failed'):
        fails.append(line.strip()[:200])
    elif line.startswith('session_end'):
        end = line.strip()
    elif line.startswith('weapon_event') and 'role=shield_hit' in line:
        d = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
        hits.append((d['owner'], list(map(int, d['pos'].split(','))), list(map(int, d['dir'].split(','))),
                     int(d['travel_region'])))
print('frame_rows', len(frames))
print('stage_us_max', max(float(r['stage_us_max']) for r in frames))
for k in ('dropped', 'cells_dropped', 'hits_dropped', 'spills', 'depth_full', 'dir_fallback'):
    print(k, 'total', sum(int(r[k]) for r in frames))
print('stage_failure_rows', len(fails))
for f in fails[:5]:
    print(' ', f)
print(end)
print('shield_hit rows', len(hits), 'travel_region +-y', sum(1 for h in hits if h[3] in (4, 5)))
own = collections.defaultdict(list)
for h in hits:
    own[h[0]].append(h)
c = collections.Counter()
for lst in own.values():
    if len(lst) < 8:
        continue
    box = [max(abs(h[1][k]) for h in lst) or 1 for k in range(3)]
    def rule(d, e):  # core::travel_region(dir, ext): order z, x, y, the first of equal ratios winning; the axis
        best = None
        for a in (2, 0, 1):
            if best is None or abs(d[a]) * e[best] > abs(d[best]) * e[a]:
                best = a
        return best
    for _, p, d, logged in lst:
        face = max(range(3), key=lambda k: abs(p[k]) / box[k])
        plain = rule(d, (1, 1, 1))
        norm = rule(d, box)
        c['hits'] += 1
        c['plain_agrees'] += face == plain
        c['normalised_agrees'] += face == norm
        if face == 1:
            c['y_face'] += 1
            c['y_face_plain_y'] += plain == 1
            c['y_face_logged_y'] += logged in (4, 5)
            c['y_face_normalised_y'] += norm == 1
print(dict(c))
