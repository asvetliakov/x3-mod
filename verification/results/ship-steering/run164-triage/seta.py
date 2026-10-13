# Run164 Q4/Q5 (Run163 Q6): SETA share (engine_stage seta=) overall and in frame windows. Usage: python3 -I seta.py rows.txt
import sys, re, collections as C
W = [(0, 99999, 'all'), (5729, 30670, 'convoy 1 at 0x8e051030 (24965/25287/25763)'), (47795, 63841, 'convoy 2 at 0x8e04ff20 (3884/4200/4662)'),
     (58168, 99999, 'convoy 3 at 0x8e051030 (4849/5315/5475) to log end'), (66793, 99999, 'after 4849 launch')]  # Run164 windows (ctl_rows.txt)
c = {w[2]: C.Counter() for w in W}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('engine_stage ') and 'seta=' in l:
        f = int(re.search(r' frame=(\d+)', l)[1]); s = re.search(r'seta=(\d)', l)[1]
        for a, b, n in W:
            if a <= f < b: c[n][s] += 1
for n, v in c.items(): print(n, dict(v), 'seta share %.2f' % (v['1'] / max(1, sum(v.values()))))
