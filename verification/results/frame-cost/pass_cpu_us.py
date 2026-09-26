#!/usr/bin/env python3
"""p50/p90 of every *_us field on the per-frame proxy rows, over flight frames (a shadow_replay_depth row on
the frame), optionally restricted to one background index. These are CPU QPC spans around the proxy's own
work in a flight WITHOUT --gpu-sync-timing (submission cost; the GPU work is asynchronous).
Usage: pass_cpu_us.py <session.log> [bgN]"""
import re, sys
path = sys.argv[1]; want = int(sys.argv[2]) if len(sys.argv) > 2 else None
ROWS = ('hdr_frame', 'motion_output_frame', 'volumetric_fog_frame', 'volumetric_fog_cards', 'sun_shadow_apply_frame',
        'sun_shadow_lane_frame', 'shadow_sun_frame', 'shadow_replay_depth', 'shadow_replay_candidates', 'shadow_replay_sun',
        'thin_vote_frame', 'screen_emission_additive_frame', 'fade_route_frame', 'hull_emission_frame', 'original_fill_frame',
        'hull_lightmap_frame', 'shadow_retention_frame', 'volumetric_fog_cache_frame', 'emission_source_gain_frame',
        'shadow_alpha_casters', 'hull_lightmap_widen_frame', 'hull_lightmap_far_fade_frame', 'shadow_lease_retirement',
        'sun_occlusion_frame', 'taa_frame', 'bloom_frame', 'lod_switch_frame')
rows = {}; replay = set(); sect = []
fr_rx = re.compile(r'\bframe=(\d+)'); us_rx = re.compile(r'\b(\w+_us|us)=(-?[0-9.]+)\b')
with open(path, errors='replace') as fh:
    for line in fh:
        k = line.split(' ', 1)[0]
        if k == 'volumetric_fog_sector':
            m = re.search(r'frame=(\d+).*reason=(\S+).* index=(-?\d+)', line)
            if m: sect.append((int(m.group(1)), m.group(2), int(m.group(3))))
            continue
        if k not in ROWS: continue
        m = fr_rx.search(line)
        if not m: continue
        f = int(m.group(1))
        if k == 'shadow_replay_depth': replay.add(f)
        for name, val in us_rx.findall(line):
            rows.setdefault((k, name), []).append((f, float(val)))
sect.sort()
def bg(f):
    b = None
    for fr, r, i in sect:
        if fr <= f: b = i
        else: break
    return b
bgc = {}
print('file', path, 'bg', want)
print('%-32s %-24s %6s %9s %9s %9s' % ('row', 'field', 'n', 'p50', 'p90', 'mean'))
for (k, name), vals in sorted(rows.items()):
    v = []
    for f, x in vals:
        if f not in replay or x < 0: continue
        if want is not None:
            if f not in bgc: bgc[f] = bg(f)
            if bgc[f] != want: continue
        v.append(x)
    if len(v) < 20: continue
    v.sort()
    print('%-32s %-24s %6d %9.1f %9.1f %9.1f' % (k, name, len(v), v[len(v) // 2], v[int(.9 * (len(v) - 1))], sum(v) / len(v)))
