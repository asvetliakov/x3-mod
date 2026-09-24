"""Census rows (all census logs) of 06-marker bodies that are NOT race-18 scene parts, split by whether the logged
ladder equals the 06 marker ladder (overlay loaded) and by logged lod; plus loop-predicted final for those rows.
usage: slot06_nonterran_rows.py MARKER06 RACE18_PARTS LOG..."""
import sys, re, json, collections
mk = {b['name'].lower().replace('\\', '/'): b for b in json.load(open(sys.argv[1]))['bodies']}
parts = {l.strip() for l in open(sys.argv[2]) if l.strip()}
CEN = re.compile(r'^cull_census device=\d+ frame=(\d+) .*? s=(-?\d+) .*? lod=(-?\d+) verdict=kept(?: scope=\w+)? lods=(\d+) thr=(\S+) body=(\S+)')
c = collections.Counter()
for log in sys.argv[3:]:
    run = re.search(r'run(\d+)', log).group(1)
    for line in open(log, errors='replace'):
        if not line.startswith('cull_census device') or ' body=' not in line: continue
        m = CEN.match(line)
        if not m: continue
        b = m.group(6).lower().replace('\\', '/')
        if b not in mk or b in parts: continue
        T = [int(v) for v in m.group(5).split(',')]; s = int(m.group(2))
        overlay = T[1:] == mk[b]['thresholds'][1:]
        sel = next((i for i in range(len(T) - 1, 0, -1) if s < T[i]), 0); lp = max(0, min(sel - 1, len(T) - 1))
        c[(f'run{run}', b, 'overlay' if overlay else 'vanilla', f'lod={m.group(3)}', f'loop={lp}')] += 1
for k in sorted(c): print(' '.join(k), c[k])
print('rows', sum(c.values()))
