#!/usr/bin/env python3
"""Per-row-name line counts and bytes of one proxy session log, streamed (never loaded whole).
Usage: row_volume.py <session.log> [top]. Prints JSON: frames (frame_end rows / max frame=), elapsed_ms,
total bytes/lines and the top row names by bytes with bytes-per-frame at the observed frame count."""
import json, re, sys
from collections import defaultdict
path = sys.argv[1]; top = int(sys.argv[2]) if len(sys.argv) > 2 else 60
counts = defaultdict(int); byts = defaultdict(int)
frames = 0; max_frame = 0; elapsed = 0; total = 0; lines = 0
frame_re = re.compile(r' frame=(\d+)'); elapsed_re = re.compile(r' elapsed_ms=(\d+)')
with open(path, 'rb') as f:
    for raw in f:
        lines += 1; n = len(raw); total += n
        line = raw.decode('utf-8', 'replace')
        name = re.split(r'[ =]', line, 1)[0]
        counts[name] += 1; byts[name] += n
        if name == 'frame_end':
            frames += 1
            m = frame_re.search(line); e = elapsed_re.search(line)
            if m: max_frame = max(max_frame, int(m.group(1)))
            if e: elapsed = max(elapsed, int(e.group(1)))
rows = sorted(byts, key=lambda k: -byts[k])[:top]
out = {'file': path.rsplit('/', 1)[-1], 'lines': lines, 'bytes': total, 'frame_end_rows': frames,
       'max_frame': max_frame, 'elapsed_ms': elapsed, 'distinct_rows': len(byts),
       'rows': [{'name': k, 'lines': counts[k], 'bytes': byts[k],
                 'bytes_per_line': round(byts[k] / counts[k], 1),
                 'lines_per_frame': round(counts[k] / max(max_frame, 1), 3),
                 'bytes_per_frame': round(byts[k] / max(max_frame, 1), 1)} for k in rows]}
json.dump(out, sys.stdout, indent=1); print()
