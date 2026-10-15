# Run173: per-ship order/target changes from ship_trace rows (v, des, engine des, fwd, steer point vs engine point).
# Usage: grep -a 'ship_trace ' x3m.log | grep -a OBJ | python3 -I order_changes.py OBJ [from_frame] [to_frame]
import sys, re
obj = sys.argv[1]; lo = int(sys.argv[2]) if len(sys.argv) > 2 else 0; hi = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 30
prev = None
for l in sys.stdin:
    d = dict(re.findall(r'(\w+)=(\S+)', l))
    if d.get('obj') != obj: continue
    f = int(d['frame'])
    if not lo <= f <= hi: continue
    k = (d['order'], d['tgt'])
    if k != prev:
        print(f, d['t_ms'], d['order'], d['tgt'], 'v %.1f des %.1f eng %.1f' % (int(d['v']) / 500, int(d['des']) / 500, int(d['des_engine']) / 500),
              'fwd', d['fwd'], 'point', d['point'], 'given', d['given'])
        prev = k
