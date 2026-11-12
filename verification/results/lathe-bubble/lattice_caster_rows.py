"""FL 69 A hull-lattice triage: do the shadow replay's caster counts follow the Titan's shield hits?

Per log: the Titan's shield-hit frames (weapon_event role=shield_hit, owner radius 545751), the
shadow_replay_candidates c2 (cascade-2 membership) and routed counts at the nearest row, split by
whether a Titan hit fell within +-60 frames; the fade arm's routed/caster sums; the alpha-tested caster
counts. Usage: python3 -I lattice_caster_rows.py [fl69.log] [fl68.log]"""
import bisect, re, sys
logs = sys.argv[1:3] or ['/mnt/c/X3FL/x3m.log', '/mnt/c/X3FL/x3m.prev.log']
kv = re.compile(r'(\w+)=(\S+)')
for path in logs:
    hits, cand, fade, alpha = [], [], [0, 0], set()
    with open(path, errors='replace') as f:
        for line in f:
            if line.startswith('weapon_event') and 'radius=545751 ' in line and 'role=shield_hit' in line:
                hits.append(int(re.search(r'frame=(\d+)', line).group(1)))
            elif line.startswith('shadow_replay_candidates device'):
                d = dict(kv.findall(line))
                cand.append((int(d['frame']), int(d['routed']), int(d['c2']), int(d['c3'])))
            elif line.startswith('fade_route_frame'):
                d = dict(kv.findall(line))
                fade[0] += int(d['fade_routed']); fade[1] += int(d['fade_caster'])
            elif line.startswith('shadow_alpha_casters device'):
                d = dict(kv.findall(line))
                alpha.add((d['seen'], d['tested'], d['opaque']))
    hitset = sorted(set(hits))
    near, far = [], []
    for fr, routed, c2, c3 in cand:
        i = bisect.bisect_left(hitset, fr - 60)
        (near if i < len(hitset) and hitset[i] <= fr + 60 else far).append(c2)
    hi = lambda xs: sum(1 for x in xs if x >= 50)
    print(f'{path}: titan_hits={len(hits)} first={hitset[0] if hitset else None} last={hitset[-1] if hitset else None}')
    print(f'  candidate rows={len(cand)}; rows with a Titan hit within 60 frames={len(near)} (c2>=50 in {hi(near)}); '
          f'rows without={len(far)} (c2>=50 in {hi(far)})')
    print(f'  fade arm sums: fade_routed={fade[0]} fade_caster={fade[1]}; distinct alpha (seen,tested,opaque) tuples={len(alpha)}')
