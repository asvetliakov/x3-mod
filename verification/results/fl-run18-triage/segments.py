#!/usr/bin/env python3
"""FL runs 17/18: frame-interval distribution per sector visit and fog state, and binned by game draws / plume records /
shadow replay draws, with mean per-stage CPU fields. Usage: python3 -I segments.py frames.pkl <x3m.log> <label>"""
import pickle, sys, re
F = pickle.load(open(sys.argv[1], 'rb'))['frames']; label = sys.argv[3]
ns = sorted(n for n in F if 'qpc' in F[n])
dt = {b: (F[b]['qpc'] - F[a]['qpc']) / 1e4 for a, b in zip(ns, ns[1:]) if b == a + 1}
ev = []  # (frame, kind, value)
prev = None
for line in open(sys.argv[2], errors='replace'):
    if line.startswith('sector_background '):
        m = re.search(r' frame=(\d+).*name="([^"]*)"', line)
        if m and m[2] != prev: ev.append((int(m[1]), 'sector', m[2])); prev = m[2]
    elif line.startswith('volumetric_fog_frame '):
        m = re.search(r' frame=(\d+) applied=(\d) reason=(\S+) strength=(\S+) density_scale=(\S+)', line)
        ev.append((int(m[1]), 'fog', '%s:%s:%s' % (m[2], m[3], m[4])))
ev.sort()
t0 = F[ns[0]]['qpc']
state = {'sector': '-', 'fog': '-'}; seg_of = {}; i = 0; visit = 0; segs = {}
for n in sorted(dt):
    while i < len(ev) and ev[i][0] <= n:
        if ev[i][1] == 'sector' and ev[i][2] != state['sector']: visit += 1
        state[ev[i][1]] = ev[i][2]; i += 1
    key = '%d:%s fog=%s' % (visit, state['sector'] or '(load)', state['fog'])
    segs.setdefault(key, []).append(n)
COST = ['engine_stage.stage_us', 'motion_output_frame.taa_run_us', 'hdr_frame.readback_us', 'hdr_frame.writeback_us',
        'shadow_replay_depth.apply_us', 'shadow_replay_candidates.select_us', 'engine_shimmer.cpu_us']
def stats(fr):
    v = sorted(dt[n] for n in fr if dt[n] < 100)
    if not v: return None
    p = lambda q: v[int(q * (len(v) - 1))]
    return len(v), p(.5), p(.9), sum(v) / len(v)
def mean(fr, k): return sum(F[n].get(k, 0.0) for n in fr) / len(fr)
print('== %s: frames %d' % (label, len(dt)), 'all: n %d p50 %.2f p90 %.2f mean %.2f ms' % stats(list(dt)))
print('segment | frames | dt p50 | p90 | mean ms | draws | plume records | candidates | shadow replay draws | ' + ' | '.join(COST))
for k, fr in segs.items():
    s = stats(fr)
    if not s or s[0] < 300: continue
    print('%s | %d | %.2f | %.2f | %.2f | %.0f | %.1f | %.0f | %.0f | ' % ((k,) + s + (mean(fr, 'draws'), mean(fr, 'engine_stage.records'),
          mean(fr, 'engine_frame.candidates'), mean(fr, 'shadow_replay_depth.draws'))) + ' | '.join('%.1f' % mean(fr, c) for c in COST))
for field, edges in (('draws', [0, 200, 400, 600, 800, 1000, 1300, 1700, 99999]), ('engine_stage.records', [0, 1, 5, 10, 20, 40, 9999]),
                     ('shadow_replay_depth.draws', [0, 100, 200, 300, 400, 600, 9999])):
    print('-- bins by', field)
    for a, b in zip(edges, edges[1:]):
        fr = [n for n in dt if a <= F[n].get(field, 0) < b]
        s = stats(fr) if fr else None
        if s and s[0] >= 200: print('[%d,%d) n %d p50 %.2f p90 %.2f mean %.2f draws %.0f' % (a, b, s[0], s[1], s[2], s[3], mean(fr, 'draws')))
