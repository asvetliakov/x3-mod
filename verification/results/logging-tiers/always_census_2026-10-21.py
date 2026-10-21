#!/usr/bin/env python3
"""Classify one plain (no-group) session log's rows into always / moved-to-perf under the gates of 2026-10-21
(log_tier::perf_rows()), streamed. Usage: always_census_2026-10-21.py <x3m.log>. Prints JSON.
Moved: shadow_replay_sun_point (capture=0 flight: every row), shadow_replay_sun_point_summary, the 300-frame
chase_camera status row (`chase_camera frame=... status=`), media_cue_window without failures, lens_flare_gain_frame,
occlusion_cull_session, cull_small_props_frame, motion_unmatched_static_frame, volumetric_fog_memory reason=periodic,
volumetric_fog_frame on frame % 600 == 0 (inferred: a change row that lands on such a frame is counted as moved; the
log does not say which condition fired), and every volumetric_fog_cards row except one whose fault= or refusal= differs
from the last cards row kept in the always tier (follow-up of 2026-10-21; inferred from the logged superset, the plain
tier's 60-frame spacing not re-simulated)."""
import json, re, sys
from collections import Counter
ALWAYS_MOVED = {'shadow_replay_sun_point', 'shadow_replay_sun_point_summary', 'lens_flare_gain_frame',
                'occlusion_cull_session', 'cull_small_props_frame', 'motion_unmatched_static_frame'}
frame_re = re.compile(r' frame=(\d+)')
last_card = [('0', 'none')]  # the (fault, refusal) of the last always-tier cards row; a fresh session starts clean
def moved(name, line):
    if name in ALWAYS_MOVED: return True
    if name == 'chase_camera': return line.startswith('chase_camera frame=') and ' status=' in line
    if name == 'media_cue_window':
        f = re.search(r' failures=(\d+)', line); v = re.search(r' video_failures=(\d+)', line)
        return not (f and int(f.group(1))) and not (v and int(v.group(1)))
    if name == 'volumetric_fog_memory': return ' reason=periodic ' in line
    if name == 'volumetric_fog_cards':
        key = (re.search(r' fault=(\d+)', line).group(1), re.search(r' refusal=(\S+)', line).group(1))
        if key != last_card[0]:
            last_card[0] = key; return False
        return True
    if name == 'volumetric_fog_frame':
        m = frame_re.search(line); return bool(m) and int(m.group(1)) % 600 == 0
    return False
lines = Counter(); byts = Counter(); keep = Counter(); keep_b = Counter(); total = [0, 0]; elapsed = 0; frames = 0
with open(sys.argv[1], 'rb') as f:
    for raw in f:
        line = raw.decode('utf-8', 'replace'); name = re.split(r'[ =\r\n]', line, maxsplit=1)[0]
        tier = 'perf' if moved(name, line) else 'always'
        lines[tier] += 1; byts[tier] += len(raw)
        if tier == 'always': keep[name] += 1; keep_b[name] += len(raw)
        if name == 'session_end':
            e = re.search(r' elapsed_ms=(\d+)', line); n = re.search(r'frames=(\d+)', line)
            elapsed = int(e.group(1)) if e else elapsed; frames = int(n.group(1)) if n else frames
h = elapsed / 3.6e6 if elapsed else 0
out = {'file': sys.argv[1], 'session_frames': frames, 'elapsed_ms': elapsed,
       'rows': dict(lines), 'bytes': dict(byts),
       'always_rows_per_hour': round(lines['always'] / h) if h else None,
       'always_mb_per_hour': round(byts['always'] / h / 1e6, 3) if h else None,
       'perf_moved_mb_per_hour': round(byts['perf'] / h / 1e6, 3) if h else None,
       'always_top': [{'name': k, 'lines': keep[k], 'bytes': keep_b[k]} for k, _ in keep_b.most_common(15)]}
json.dump(out, sys.stdout, indent=1); print()
