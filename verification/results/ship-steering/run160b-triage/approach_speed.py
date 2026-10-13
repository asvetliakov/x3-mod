# Run160b Q3: a traced capital's v / des (our write) / des_engine (the engine's law) in a frame range, every k-th row.
# Usage: python3 -I approach_speed.py rows.txt obj f0 f1 [k]
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'probe'))
import ship_trace_rows as T
o, f0, f1 = int(sys.argv[2], 16), int(sys.argv[3]), int(sys.argv[4]); k = int(sys.argv[5]) if len(sys.argv) > 5 else 25
n = 0; lower = 0; tot = 0
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('ship_trace frame='): continue
    r = T.parse_row(l)
    if not r or r['obj'] != o or not f0 <= r['frame'] <= f1: continue
    tot += 1; lower += r['des'] < r['des_engine']
    if n % k == 0: print(r['frame'], hex(r['order']), 'v %.1f des %.1f des_engine %.1f emg %s' % (r['v'] / 500, r['des'] / 500, r['des_engine'] / 500, r['emg']))
    n += 1
print('rows', tot, 'des < des_engine', lower)
