"""Run216: match every sun_lens_draw centre (rows_known=1) to the lens records the engine holds in the
main view that frame (sun_probe rows, view == main): u = 0.5 + x/65536, v = 0.5 - y/65536.
Also tests the ghost line (centre collinear with record and screen centre) per record.
Usage: python3 match_lens_to_records.py /mnt/c/X3/x3m.log"""
import re, sys, collections, math
kv = re.compile(r'(\w+)=(\S+)')
probes = collections.defaultdict(dict)   # frame -> record -> (u, v, group, size)
lens = []
with open(sys.argv[1], errors='replace') as f:
    for line in f:
        if line.startswith('sun_probe '):
            d = dict(kv.findall(line))
            if d['view'] != d['main']: continue
            probes[int(d['frame'])][d['record']] = (.5 + int(d['x']) / 65536, .5 - int(d['y']) / 65536, d['group'], int(d['size']), d['owner'] != d['view'])
        elif line.startswith('sun_lens_draw '):
            d = dict(kv.findall(line))
            if d.get('rows_known') == '1': lens.append(d)
AR = 5120 / 1440
tally = collections.Counter(); groups = collections.Counter(); dists = []
for r in lens:
    fr = int(r['frame']); cu, cv = float(r['centre_u']), float(r['centre_v'])
    recs = probes.get(fr, {})
    best = None
    for rec, (u, v, g, size, sunlike) in recs.items():
        du, dv = cu - u, (cv - v) / AR
        dist = math.hypot(du, dv)
        sx, sy = u - .5, (v - .5) / AR; bx, by = cu - .5, (cv - .5) / AR
        s = math.hypot(sx, sy); line = abs(bx * sy - by * sx) / s if s > 1e-6 else 9
        cand = ('core' if dist <= .005 else 'ghost' if line <= .005 else 'none', dist, line, g, rec)
        if best is None or (cand[0] != 'none', -min(dist, line)) > (best[0] != 'none', -min(best[1], best[2])): best = cand
    if best is None: tally[(r['vs'][:8], r['body'], 'no_probe_rows')] += 1; continue
    tally[(r['vs'][:8], r['body'], best[0])] += 1
    groups[(r['body'], best[0], best[3])] += 1
    dists.append(min(best[1], best[2]))
import statistics as st; print('match distance_u median %.6f max %.6f' % (st.median(dists), max(dists)))
print('rows', len(lens), 'frames_with_probes', len(probes))
for k, n in sorted(tally.items()): print('  vs/body/best_match', k, n)
for k, n in groups.most_common(15): print('  body/match/group', k, n)
sun_groups = collections.Counter(g for recs in probes.values() for (u, v, g, s, o) in recs.values())
print('record groups seen in main view (frames):', sun_groups.most_common(10))
