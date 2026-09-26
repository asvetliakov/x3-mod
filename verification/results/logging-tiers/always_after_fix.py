#!/usr/bin/env python3
"""Always-tier volume of a player-mode x3m.log (no X3M_DEBUG / X3M_PERF) before and after the 2026-09-27 gates.
Streams the log (never loads it whole). Before = every row, measured. After = the same rows minus the ones the fix gates
(the six per-frame emitter rows moved to the family cadence, the resource identity rows moved to --debug / scheduled
captures): inferred from a log of the unfixed DLL, the other rows' cadence unchanged. Rates are per frame (frame count
from session_end frames=, else the largest frame=) and per hour at the session's measured frame rate and at 60 fps.
Usage: always_after_fix.py <x3m.log>  -> JSON on stdout."""
import json, re, sys
from collections import Counter
GATED = ('hull_emission_frame', 'original_fill_frame', 'hull_lightmap_frame', 'hull_lightmap_far_fade_frame',
         'hull_lightmap_widen_frame', 'emission_source_gain_frame', 'resource')
path = sys.argv[1]
rows, byts = Counter(), Counter()
frames = max_frame = elapsed = 0
steady_rows, steady_bytes, seen_frame_end = Counter(), Counter(), False
frame_re, elapsed_re = re.compile(r' frame=(\d+)'), re.compile(r' elapsed_ms=(\d+)')
with open(path, 'rb') as f:
    for raw in f:
        line = raw.decode('utf-8', 'replace')
        name = re.split(r'[ =\r\n]', line, 1)[0]
        rows[name] += 1; byts[name] += len(raw)
        if seen_frame_end and name != 'session_end':
            steady_rows[name] += 1; steady_bytes[name] += len(raw)
        if name == 'frame_end':
            seen_frame_end = True
        if name == 'session_end':
            m = re.search(r' frames=(\d+)', line); e = elapsed_re.search(line)
            frames = int(m.group(1)) if m else 0; elapsed = int(e.group(1)) if e else 0
        m = frame_re.search(line)
        if m: max_frame = max(max_frame, int(m.group(1)))
frames = frames or max_frame
seconds = elapsed / 1000.0 if elapsed else None
def rate(b, r):
    out = {'rows_per_frame': round(r / frames, 4), 'bytes_per_frame': round(b / frames, 2),
           'MB_per_h_at_60fps': round(b / frames * 60 * 3600 / 1e6, 3)}
    if seconds: out['MB_per_h_measured'] = round(b / seconds * 3600 / 1e6, 3)
    return out
kept = [n for n in steady_rows if n not in GATED]
print(json.dumps({
    'file': path.rsplit('/', 1)[-1], 'bytes': sum(byts.values()), 'lines': sum(rows.values()), 'frames': frames,
    'elapsed_s': seconds, 'fps': round(frames / seconds, 1) if seconds else None,
    'gated_rows': {n: rows[n] for n in GATED},
    'before_steady_measured': rate(sum(steady_bytes.values()), sum(steady_rows.values())),
    'after_steady_inferred': rate(sum(steady_bytes[n] for n in kept), sum(steady_rows[n] for n in kept)),
    'after_steady_top': sorted(((n, steady_rows[n], steady_bytes[n]) for n in kept), key=lambda x: -x[2])[:12],
    'header_rows': sum(rows.values()) - sum(steady_rows.values())}, indent=1))
