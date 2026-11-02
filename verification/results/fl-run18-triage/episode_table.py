#!/usr/bin/env python3
"""FL run 18: drop episodes = runs of >= 3 consecutive seconds under 120 fps (load seconds < 20 fps excluded),
with per-episode means of the cost fields, against the capped seconds (>= 230 fps).
Usage: python3 -I episode_table.py frames.pkl <x3m.log>"""
import pickle, sys, re
D = pickle.load(open(sys.argv[1], 'rb')); F = D['frames']; C = D['counts']
ns = sorted(n for n in F if 'qpc' in F[n])
dt = {b: (F[b]['qpc'] - F[a]['qpc']) / 1e4 for a, b in zip(ns, ns[1:]) if b == a + 1}
t0 = F[ns[0]]['qpc']; sec = {}
for n, d in dt.items(): sec.setdefault(int((F[n]['qpc'] - t0) / 1e7), []).append(n)
fps = {s: len(v) / (sum(dt[n] for n in v) / 1e3) for s, v in sec.items()}
# fog applied per frame and sector changes from the log (rate-limited rows: carried forward)
fog = []; sect = []
for line in open(sys.argv[2], errors='replace'):
    if line.startswith('volumetric_fog_frame '):
        m = re.search(r' frame=(\d+) applied=(\d) reason=(\S+)', line); fog.append((int(m[1]), int(m[2]), m[3]))
    elif line.startswith(('volumetric_fog_sector ', 'sector_background ')):
        m = re.search(r' frame=(\d+)', line)
        if m: sect.append((int(m[1]), line.split()[0], ' '.join(t for t in line.split() if t.split('=')[0] in ('sector', 'key', 'sector_key', 'reason', 'event', 'family', 'name'))[:90]))
def fog_at(n):
    st = (0, 'none')
    for f, a, r in fog:
        if f <= n: st = (a, r)
        else: break
    return st
FIELDS = ['draws', 'engine_stage.stage_us', 'engine_stage.records', 'motion_output_frame.taa_run_us', 'hdr_frame.readback_us',
          'hdr_frame.writeback_us', 'shadow_replay_depth.draws', 'shadow_replay_depth.apply_us', 'shadow_replay_candidates.select_us',
          'engine_shimmer.cpu_us']
def means(fr):
    return [sum(F[n].get(k, 0.0) for n in fr) / len(fr) for k in FIELDS]
secs = sorted(sec); eps = []; cur = []
for s in secs:
    if 20 <= fps[s] < 120 and (not cur or s == cur[-1] + 1): cur.append(s)
    else:
        if len(cur) >= 3: eps.append(cur)
        cur = [s] if 20 <= fps[s] < 120 else []
if len(cur) >= 3: eps.append(cur)
fast = [n for s in secs if fps[s] >= 230 for n in sec[s]]
print('fields:', ' | '.join(FIELDS))
print('capped (>=230 fps) frames %d mean dt %.2f ms:' % (len(fast), sum(dt[n] for n in fast) / len(fast)), ' '.join('%.1f' % x for x in means(fast)))
allf = [n for n in dt]
print('session frames %d mean dt %.2f ms:' % (len(allf), sum(dt[n] for n in allf) / len(allf)), ' '.join('%.1f' % x for x in means(allf)))
for e in eps:
    fr = [n for s in e for n in sec[s]]
    mdt = sum(dt[n] for n in fr) / len(fr)
    p99 = sorted(dt[n] for n in fr)[int(.99 * (len(fr) - 1))]
    fa = fog_at(fr[0]), fog_at(fr[-1])
    print('t=%d-%ds (%ds) f%d-%d fps mean %.0f min %.0f dt mean %.2f p99 %.1f fog %s/%s:' % (e[0], e[-1], len(e), fr[0], fr[-1],
          1e3 / mdt, min(fps[s] for s in e), mdt, p99, fa[0], fa[1]), ' '.join('%.1f' % x for x in means(fr)))
# per-second correlation: frame time vs draws
xs = [sum(F[n]['draws'] for n in sec[s]) / len(sec[s]) for s in secs if fps[s] >= 20]
ys = [1e3 / fps[s] for s in secs if fps[s] >= 20]
mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
r = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / (sum((x - mx) ** 2 for x in xs) * sum((y - my) ** 2 for y in ys)) ** .5
b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs)
print('per-second r(frame_ms, draws) = %.3f over %d seconds; slope %.4f ms per draw, intercept %.2f ms' % (r, len(xs), b, my - b * mx))
for k in FIELDS[1:]:
    xs2 = [sum(F[n].get(k, 0) for n in sec[s]) / len(sec[s]) for s in secs if fps[s] >= 20]
    m2 = sum(xs2) / len(xs2); d = (sum((x - m2) ** 2 for x in xs2) * sum((y - my) ** 2 for y in ys)) ** .5
    print('per-second r(frame_ms, %s) = %.3f' % (k, sum((x - m2) * (y - my) for x, y in zip(xs2, ys)) / d if d else 0))
for s in sect[:40]: print('sector', s)
