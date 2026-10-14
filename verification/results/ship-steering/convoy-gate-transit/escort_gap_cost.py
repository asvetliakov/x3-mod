"""Review item 1 (convoy-gate-transit.md section 11): the escort gate's cost. Reads the fixture's `X3M_C_ALL=1 ship_steering_host --lane`
stdout and prints, per SETA step, the leader -> escort launch gap over the hull spacing (each run's largest) and the leader -> escort
launch gaps for script latencies below and above 9 s, over the casts whose escorts trail their leader on the lane (abreast, formed,
stragglers, the small escort). Usage: python3 escort_gap_cost.py <lane_all.txt>"""
import collections, re, sys
SKIP = ('stragglers_far', 'partner_block', 'parked', 'leader_killed', 'crossing', 'redeparture', 'slow_hull')
over, gaps = collections.defaultdict(list), collections.defaultdict(list)
for line in open(sys.argv[1]):
    m = re.match(r'scenario C (\S+) lane tf (\d+) .*leader->escort over spacing max ([-\d.]+) s \| pairs(.*)', line)
    if not m or m.group(1) in SKIP:
        continue
    tf = int(m.group(2))
    over[tf].append(float(m.group(3)))
    for p in re.finditer(r'(\d+)>(\d+) ([\d.]+)/([\d.]+)L lam ([\d.]+)', m.group(4)):
        gaps[tf].append((float(p.group(5)), float(p.group(3))))
for tf in sorted(over):
    v = sorted(over[tf])
    lo = [g for lam, g in gaps[tf] if lam < 9.0]
    hi = [g for lam, g in gaps[tf] if lam >= 9.0]
    print('tf %d: leader -> escort gap over the spacing, each run\'s largest: %.1f / %.1f / %.1f s (min / median / max, %d runs); '
          'leader -> escort launch gap: lambda < 9 s %.1f..%.1f s (%d pairs), lambda >= 9 s %.1f..%.1f s (%d pairs)'
          % (tf, v[0], v[len(v) // 2], v[-1], len(v), min(lo), max(lo), len(lo), min(hi), max(hi), len(hi)))
