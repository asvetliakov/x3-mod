"""Run375 (Mayhem 3, 5120x1440, F8 frames 4400-4407): ships\\props\\ nodes by census verdict, and the kept prop draws
whose object_bounds screen box has a half-side under 2 / 4 px (what X3M_CULL_SMALL_PROPS would skip at that
X3M_CULL_SMALL_PARTS_PX, ignoring the own-ship/target exemption). Reads the session log with a streaming grep-like
pass; prints only counts. Usage: run375_prop_rows.py [session.log]"""
import collections
import glob
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else sorted(glob.glob('/tmp/x3-bottleX3-run375/session-*.log'))[0]
KV = re.compile(r'(\w+)=(\S+)')
FRAMES = set(range(4400, 4408))
body, verdicts, bounds, context = {}, collections.defaultdict(collections.Counter), {}, {}
for line in open(path, errors='replace'):
    kind = line.split(' ', 1)[0]
    if kind not in ('cull_census', 'object_bounds', 'object_context'):
        continue
    d = dict(KV.findall(line))
    frame = int(d.get('frame', -1))
    if frame not in FRAMES:
        continue
    if kind == 'cull_census':
        b = d.get('body', '')
        body[d['node'].lower()] = b
        if b.lower().startswith('ships\\props\\'):
            verdicts[frame][(b.split('\\')[-1], d['verdict'])] += 1
    elif kind == 'object_bounds':
        r = 0.5 * max(float(d['sx1']) - float(d['sx0']), float(d['sy1']) - float(d['sy0']))
        bounds[(frame, int(d['index']))] = (d['node'].lower().replace('0x', '').zfill(8), r, 'offscreen' in d)
    else:
        context[(frame, int(d['index']))] = d['node'].lower().replace('0x', '').zfill(8)
for frame in sorted(verdicts):
    by = collections.Counter()
    for (name, verdict), n in verdicts[frame].items():
        by[verdict] += n
    draws = [(i, context.get((f, i))) for (f, i) in context if f == frame]
    props = [i for i, n in draws if body.get(n, '').lower().startswith('ships\\props\\')]
    radii = [bounds[(frame, i)][1] for i in props if (frame, i) in bounds]
    print(f'frame {frame} draws={len(draws)} prop_draws={len(props)} prop_draws_with_bounds={len(radii)} '
          f'under_2px={sum(r < 2 for r in radii)} under_4px={sum(r < 4 for r in radii)} max_radius_px={max(radii, default=0):.2f} '
          f'census_prop_rows={dict(sorted(by.items()))}')
first = min(verdicts)
engine_culled = collections.Counter()
for (name, verdict), n in verdicts[first].items():
    if verdict != 'kept':
        engine_culled[name] += n
print(f'frame {first} prop nodes not rendered by the engine pass (culled_size/culled_min by the engine, culled_small by '
      f'--cull-small-parts): {sum(engine_culled.values())} over {len(engine_culled)} bodies: {dict(engine_culled.most_common())}')
