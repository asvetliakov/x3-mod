"""Run216: compare the frame sets of (a) the sun record (group 174) in the main view (sun_probe rows),
(b) each lens-draw pair inside the lens bracket (sun_lens_draw rows), (c) sun_visibility rows.
Usage: python3 sun_chain_frames.py /mnt/c/X3/x3m.log"""
import re, sys, collections
kv = re.compile(r'(\w+)=(\S+)')
sun, other_groups, pair, vis = set(), collections.defaultdict(set), collections.defaultdict(set), set()
with open(sys.argv[1], errors='replace') as f:
    for line in f:
        if line.startswith('sun_probe '):
            d = dict(kv.findall(line))
            if d['view'] == d['main']:
                (sun if d['group'] == '174' else other_groups[d['group']]).add(int(d['frame']))
        elif line.startswith('sun_lens_draw '):
            d = dict(kv.findall(line)); pair[d['vs'][:8] + '/' + d['ps'][:8]].add(int(d['frame']))
        elif line.startswith('sun_visibility '):
            vis.add(int(dict(kv.findall(line))['frame']))
print('sun(group174) frames', len(sun), 'sun_visibility frames', len(vis))
for p, s in pair.items():
    print(p, 'frames', len(s), 'in_sun', len(s & sun), 'not_in_sun', len(s - sun), 'sun_without_pair', len(sun - s))
for g, s in other_groups.items():
    print('group', g, 'frames', len(s), '& d5e1 frames', len(s & pair.get('d5e1c753/8360f422', set())))
