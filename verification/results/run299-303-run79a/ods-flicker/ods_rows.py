"""ODS (Orbital Defence Station = TDocks idx 26, stations\\station_scenes\\terran\\usc_dock_e_scene) flicker triage, Run 79 A.
Read-only. For each session log: census rows of the usc_dock_e parts per burst frame (s, lod, thr), and every
motion_route row whose node is one of those parts (gate, routed, matched, unmatched, lod, blend/atest, vs/ps, primitives,
rows_hash), plus motion_output_frame camera_rotation_deg for the burst frames.
usage: ods_rows.py LOG [LOG...]  -> prints compact tables."""
import sys, re, collections
KV = re.compile(r'(\w+)=(\S*)')
for path in sys.argv[1:]:
    parts = {}; census = collections.defaultdict(dict); routes = collections.defaultdict(list); frames = {}
    bounds = {}
    lines = open(path, 'r', errors='replace')
    pending = []
    for line in lines:
        if line.startswith('cull_census device') and 'usc_dock_e' in line:
            d = dict(KV.findall(line)); name = d['body'].split('\\')[-1]
            parts[d['node']] = name; census[int(d['frame'])][name] = (int(d['s']), int(d['lod']), d['thr'])
        elif line.startswith('motion_route device'):
            pending.append(line)
        elif line.startswith('object_bounds device'):
            d = dict(KV.findall(line)); bounds[(d['frame'], d['index'])] = d
        elif line.startswith('motion_output_frame'):
            d = dict(KV.findall(line)); frames[int(d['frame'])] = d
    for line in pending:
        d = dict(KV.findall(line))
        if d['node'] in parts:
            routes[int(d['frame'])].append(d)
    print(f'== {path}')
    prev = None
    for f in sorted(census):
        mo = frames.get(f, {})
        agg = collections.Counter()
        for d in routes.get(f, []):
            agg[(parts[d['node']].replace('usc_dock_e_', ''), d['lod'][-1], d['gate'], d['routed'], d['matched'], d['unmatched'],
                 d['blend'], d['atest'], d['zwrite'], d['ps'][:8], d['primitives'])] += 1
        sig = (tuple(sorted(census[f].items())), tuple(sorted(agg.items())))
        rsum = collections.Counter((k[0], k[3], k[4], k[5]) for k in agg.elements())
        print(f'frame {f} rot_deg={mo.get("camera_rotation_deg")} cut={mo.get("cut")} ods_draws={sum(agg.values())} '
              + ' '.join(f'{n}:r{r}m{m}{"" if u=="none" else "/"+u}x{c}' for (n, r, m, u), c in sorted(rsum.items()))
              + ('  (same census+route rows as previous frame)' if sig == prev else ''))
        if sig == prev: continue
        prev = sig
        print(f'frame {f} rot_deg={mo.get("camera_rotation_deg")} cut={mo.get("cut")} routed={mo.get("routed")}/{mo.get("draws")} '
              + ' '.join(f'{n.replace("usc_dock_e_","")}:s{v[0]}/lod{v[1]}/Tpad{v[2].split(",")[-1]}' for n, v in sorted(census[f].items())))
        for k, c in sorted(agg.items()):
            print('   ', c, 'x part=%s lod=%s gate=%s routed=%s matched=%s unmatched=%s blend=%s atest=%s zwrite=%s ps=%s prims=%s' % k)
