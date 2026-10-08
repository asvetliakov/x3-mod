#!/usr/bin/env python3
"""Does the draw path's object_context camera= equal the pass's cull_census view= for the same node and frame?
Streams a session log; prints per captured frame: joined draws, of them with camera == census view, the distinct
census views of the frame and the census views that carry a drawn node (the main sector view's pointer).
Usage: view_identity.py <session.log> [frame ...]
"""
import collections, re, sys

F = re.compile(r' (frame|node|view|camera)=(\S+)')

def main(log, *frames):
    want = set(frames)
    cen = collections.defaultdict(dict)        # frame -> node -> view
    views = collections.defaultdict(set)
    draws = collections.defaultdict(list)      # frame -> (node, camera)
    with open(log, errors='replace') as s:
        for line in s:
            if line.startswith('cull_census device='):
                d = dict(F.findall(line))
                if want and d.get('frame') not in want: continue
                cen[d['frame']].setdefault(d['node'].lower(), d['view'].lower())
                views[d['frame']].add(d['view'].lower())
            elif line.startswith('object_context device='):
                d = dict(F.findall(line))
                if want and d.get('frame') not in want: continue
                draws[d['frame']].append((d['node'].lower().removeprefix('0x').zfill(8),
                                          d['camera'].lower().removeprefix('0x').zfill(8)))
    print('frame draws joined camera==view census_views views_with_drawn_nodes')
    for fr in sorted(draws, key=int):
        C = cen.get(fr, {})
        j = [(n, c) for n, c in draws[fr] if n in C]
        same = sum(1 for n, c in j if C[n] == c)
        vd = collections.Counter(C[n] for n, c in j)
        print(fr, len(draws[fr]), len(j), same, len(views[fr]), dict(vd))

if __name__ == '__main__':
    main(*sys.argv[1:])
