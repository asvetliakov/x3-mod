# Run168 windows (Run167 before). Run166 Q4 (Run164 Q4/Q5 (Run163 Q6): SETA share (engine_stage seta=) overall and in frame windows. Usage: python3 -I seta.py rows.txt
import sys, re, collections as C
W = [(0, 999999, 'all'), (701, 23368, 'gate 62b8 convoy (24965/25287/25763) entry..last launch'), (43055, 70886, 'gate 94b0 (3920; 4900/5412/5612) entry..last launch'),
     (44228, 75518, 'gate 6db0 (7087/4248/4454, re-targeted from 94b0) entry..last launch'), (80688, 99331, 'gate 7de8 convoy (10107/10048/10149) entry..last launch'),
     (99331, 999999, 'last launch .. log end')]  # Run168 windows (transits.txt, ctl_rows.txt)
c = {w[2]: C.Counter() for w in W}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('engine_stage ') and 'seta=' in l:
        f = int(re.search(r' frame=(\d+)', l)[1]); s = re.search(r'seta=(\d)', l)[1]
        for a, b, n in W:
            if a <= f < b: c[n][s] += 1
for n, v in c.items(): print(n, dict(v), 'seta share %.2f' % (v['1'] / max(1, sum(v.values()))))
