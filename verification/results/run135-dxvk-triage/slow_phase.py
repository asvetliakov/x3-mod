"""In-flight frame_phases_slow rows by dominant phase: count and frames (dominant phase >= 30 ms). usage: slow_phase.py LOG"""
import sys, re, collections
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)')
dt = {}; slow = {}
for raw in open(sys.argv[1], 'rb'):
    k = raw.split(b' ', 1)[0]
    if k == b'frame_end':
        d = dict(KV.findall(raw.decode('latin1'))); dt[int(d['frame'])] = float(d['dt_ms'])
    elif k == b'frame_phases_slow':
        d = dict(KV.findall(raw.decode('latin1'))); slow[int(d['frame'])] = d
L = max(dt, key=dt.get); lo, hi = L + 10, max(dt) - 10
by = collections.defaultdict(list)
for f, d in sorted(slow.items()):
    if not lo <= f <= hi: continue
    top = max(((float(v), n) for n, v in d.items() if n.endswith('_us') and n not in ('dt_us', 'views_us', 'view_submit_us', 'view_setup_us')), default=(0, ''))
    v, n = top
    vs = max(float(d.get('view_submit_us', 0)), float(d.get('view_setup_us', 0)))
    if vs > v: v, n = vs, 'view_submit/setup'
    if v >= 30000: by[n].append(f'{f}:{v / 1000:.0f}')
for n, fs in sorted(by.items()): print(n, len(fs), ' '.join(fs))
