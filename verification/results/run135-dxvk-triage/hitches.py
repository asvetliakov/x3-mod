"""In-flight frames over a threshold (load frame = largest frame_end dt_ms; +10 .. last-10) annotated with what sits in
them: capture burst (capture_event rows), frame_phases_slow phases (largest two), hdr_frame readback_lock_us and
meter_event_ready, lod_switch count, engine_light ships change. usage: hitches.py LOG [threshold_ms]"""
import sys, re, collections
log = sys.argv[1]; th = float(sys.argv[2]) if len(sys.argv) > 2 else 100
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)')
dt = {}; cap = set(); slow = {}; hdr = {}; lod = collections.Counter(); lodf = collections.Counter()
for raw in open(log, 'rb'):
    k = raw.split(b' ', 1)[0]
    if k not in (b'frame_end', b'capture_event', b'frame_phases_slow', b'hdr_frame', b'lod_switch', b'lod_switch_frame'): continue
    d = dict(KV.findall(raw.decode('latin1')))
    if 'frame' not in d: continue
    f = int(d['frame'])
    if k == b'frame_end': dt[f] = float(d['dt_ms'])
    elif k == b'capture_event': cap.add(f)
    elif k == b'frame_phases_slow': slow[f] = d
    elif k == b'hdr_frame': hdr[f] = (d.get('readback_lock_us'), d.get('meter_event_ready'), d.get('readback_us'))
    elif k == b'lod_switch': lod[f] += 1
    else: lodf[f] += 1
fs = sorted(dt); L = max(fs, key=dt.get); lo, hi = L + 10, fs[-1] - 10
rows = [f for f in fs if lo <= f <= hi and dt[f] > th]
print(f'load_frame={L} inflight={lo}-{hi} over_{th:g}ms={len(rows)} in_capture={sum(f in cap for f in rows)} '
      f'next_to_capture(+-1)={sum((f - 1 in cap or f + 1 in cap) and f not in cap for f in rows)} with_slow_row={sum(f in slow for f in rows)}')
for f in sorted(rows, key=dt.get, reverse=True):
    s = slow.get(f)
    ph = ''
    if s:
        us = sorted(((float(v), n) for n, v in s.items() if n.endswith('_us') and n != 'dt_us'), reverse=True)[:3]
        ph = ' '.join(f'{n}={v / 1000:.1f}ms' for v, n in us)
    h = hdr.get(f, (None, None, None))
    print(f'frame={f} dt_ms={dt[f]:.0f} capture={int(f in cap)} lod_switch={lod[f]} lock_us={h[0]} meter_ready={h[1]} slow:{ph}')
