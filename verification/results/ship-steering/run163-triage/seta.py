# Run163 Q6: SETA share (engine_stage seta=) overall and in frame windows. Usage: python3 -I seta.py rows.txt
import sys, re, collections as C
W = [(0, 99999, 'all'), (1135, 24400, 'outbound convoy at 0x8e7ff9d0'), (48100, 65810, 'return to 0x8e7ff4f0 until the leader launch'),
     (65810, 80830, 'escorts after the leader launch'), (70900, 82300, 'second convoy at 0x8e7ff9d0')]
c = {w[2]: C.Counter() for w in W}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('engine_stage ') and 'seta=' in l:
        f = int(re.search(r' frame=(\d+)', l)[1]); s = re.search(r'seta=(\d)', l)[1]
        for a, b, n in W:
            if a <= f < b: c[n][s] += 1
for n, v in c.items(): print(n, dict(v), 'seta share %.2f' % (v['1'] / max(1, sum(v.values()))))
