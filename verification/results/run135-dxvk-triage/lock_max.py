"""In-flight hdr_frame readback_lock_us max/p99 and meter_event_ready counts. usage: lock_max.py LOG"""
import sys, re, collections
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)')
dt = {}; h = {}
for raw in open(sys.argv[1], 'rb'):
    k = raw.split(b' ', 1)[0]
    if k not in (b'frame_end', b'hdr_frame'): continue
    d = dict(KV.findall(raw.decode('latin1')))
    if k == b'frame_end': dt[int(d['frame'])] = float(d['dt_ms'])
    else: h[int(d['frame'])] = (float(d.get('readback_lock_us', 0)), d.get('meter_event_ready'))
L = max(dt, key=dt.get); lo, hi = L + 10, max(dt) - 10
x = sorted((v[0], f) for f, v in h.items() if lo <= f <= hi)
c = collections.Counter(v[1] for f, v in h.items() if lo <= f <= hi)
print(f'n={len(x)} lock_us_max={x[-1][0]:.1f}@{x[-1][1]} p99={x[int(len(x) * .99)][0]:.1f} over_1ms={sum(v > 1000 for v, f in x)} meter_event_ready={dict(c)}')
