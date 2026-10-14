# Run166 Q4 (Run164 Q4/Q5 (Run163 Q6): SETA share (engine_stage seta=) overall and in frame windows. Usage: python3 -I seta.py rows.txt
import sys, re, collections as C
W = [(0, 99999, 'all'), (850, 22273, 'gate 5150 convoy 1 (24965/25287/25763) entry..last X'), (28662, 55471, 'gate 6878 convoy (3011/3652/4059) entry..last X'),
     (36934, 52049, 'gate 5150 convoy 2 (5117/5580/5748) entry..last X'), (50481, 99999, 'gate 8828 convoy (7513/7834/8510) to log end')]  # Run166 windows (transits.txt, crowd.txt)
c = {w[2]: C.Counter() for w in W}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('engine_stage ') and 'seta=' in l:
        f = int(re.search(r' frame=(\d+)', l)[1]); s = re.search(r'seta=(\d)', l)[1]
        for a, b, n in W:
            if a <= f < b: c[n][s] += 1
for n, v in c.items(): print(n, dict(v), 'seta share %.2f' % (v['1'] / max(1, sum(v.values()))))
