"""Medians of every *_us field in per-frame rows, in-flight frames only (load frame = largest frame_end dt_ms; +10 .. last-10).
Usage: us_fields.py <session.log>   Prints ROW FIELD n median p95 mean."""
import sys, re, statistics as st
from collections import defaultdict
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)')
ROWS = {b'hdr_frame', b'motion_output_frame', b'engine_stage', b'thin_vote_frame', b'shadow_replay_depth',
        b'shadow_retention_frame', b'shadow_replay_candidates', b'engine_shimmer', b'frame_end', b'frame_phases_slow'}
vals = defaultdict(list); fe = []
for raw in open(sys.argv[1], 'rb'):
    name = raw.split(b' ', 1)[0]
    if name not in ROWS: continue
    d = dict(KV.findall(raw.decode('latin1')))
    if name == b'frame_end': fe.append((int(d['frame']), float(d['dt_ms']))); continue
    f = int(d.get('frame', -1))
    for k, v in d.items():
        if k.endswith('_us'): vals[(name.decode(), k)].append((f, float(v)))
L = max(fe, key=lambda x: x[1])[0]; lo = L + 10; hi = fe[-1][0] - 10
print('load_frame', L, 'lo', lo, 'hi', hi)
for (n, k), v in sorted(vals.items()):
    x = sorted(val for f, val in v if lo < f < hi)
    if not x: continue
    print(f'{n:26s} {k:32s} n={len(x):5d} p50={st.median(x):9.1f} p95={x[int(len(x)*.95)]:9.1f} mean={st.mean(x):9.1f}')
