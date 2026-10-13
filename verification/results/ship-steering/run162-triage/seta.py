# Run160b Q7: SETA share (engine_stage seta=) overall and in frame windows, and game/real time ratio per window.
# Usage: python3 -I seta.py rows.txt
import sys, re, collections as C
W = [(0, 99999, 'all'), (40700, 52000, 'arrivals 0x8fa9d688'), (52000, 63300, 'convoys to 0x8fa9eee8'), (63300, 69100, 'exit 0x8fa9eee8')]  # Run161 windows
c = {w[2]: C.Counter() for w in W}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('engine_stage ') and 'seta=' in l:
        f = int(re.search(r' frame=(\d+)', l)[1]); s = re.search(r'seta=(\d)', l)[1]
        for a, b, n in W:
            if a <= f < b: c[n][s] += 1
for n, v in c.items(): print(n, dict(v), 'seta share %.2f' % (v['1'] / max(1, sum(v.values()))))
