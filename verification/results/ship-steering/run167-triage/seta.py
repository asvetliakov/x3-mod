# Run167 windows. Run166 Q4 (Run164 Q4/Q5 (Run163 Q6): SETA share (engine_stage seta=) overall and in frame windows. Usage: python3 -I seta.py rows.txt
import sys, re, collections as C
W = [(0, 99999, 'all'), (1347, 24913, 'gate faf8 convoy 1 (24965/25287/25763) entry..last X'), (29988, 52189, 'gate 3a58 3071 entry..X'),
     (36168, 64218, 'gate 0ad0 convoy (4579/4908/5361) entry..last X'), (39067, 79603, 'gate faf8 convoy 2 (5549/6010/6168, 7527) entry..last X'),
     (79603, 99999, 'last X .. log end (3725/3947 re-targeted, 10962)')]  # Run167 windows (transits.txt, crowd.txt)
c = {w[2]: C.Counter() for w in W}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('engine_stage ') and 'seta=' in l:
        f = int(re.search(r' frame=(\d+)', l)[1]); s = re.search(r'seta=(\d)', l)[1]
        for a, b, n in W:
            if a <= f < b: c[n][s] += 1
for n, v in c.items(): print(n, dict(v), 'seta share %.2f' % (v['1'] / max(1, sum(v.values()))))
