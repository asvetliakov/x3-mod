#!/usr/bin/env python3
"""FL 71 A (2026-11-13): the LOD swap that dropped a static caster, from the flight log (grep only).

Prints, per capture frame, the retained station's records (handle 52942): count, lod, class, streak range,
static_mask; the retention frame rows' moving_dropped and the depth rows' replayed_retained1 around the swap;
and the two records present in both LOD sets (same primitive count) with their world AABB centre distance.
Usage: log_witness.py [/mnt/c/X3FL/x3m.log]
"""
import collections
import re
import subprocess
import sys

LOG = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3FL/x3m.log'
HANDLE = '52942'
FIELD = re.compile(r'(\w+)=(\S+)')
pattern = (rf'shadow_retention_caster .*handle={HANDLE} |shadow_retention_frame .* frame=391(19|2[0-3]) '
           r'|shadow_replay_depth .* frame=(39121|39122|39123|39158) ')
out = subprocess.run(['grep', '-E', pattern, LOG], capture_output=True, text=True, check=True).stdout.splitlines()
casters = collections.defaultdict(list)
for line in out:
    f = dict(FIELD.findall(line))
    if line.startswith('shadow_retention_caster '):
        casters[int(f['frame'])].append(f)
    elif line.startswith('shadow_retention_frame '):
        print(f"frame_row frame={f['frame']} moving_dropped={f['moving_dropped']} lod_replaced={f['lod_replaced']} "
              f"superseded={f['superseded']} static={f['static']} nodes_unseen={f['nodes_unseen']}")
    else:
        print(f"depth_row frame={f['frame']} replayed_retained1={f.get('replayed_retained1', '-')}")
for frame in sorted(casters):
    rows = casters[frame]
    streaks = [int(r['streak']) for r in rows]
    print(f"caster frame={frame} records={len(rows)} lod={sorted({r['lod'] for r in rows})} class={sorted({r['class'] for r in rows})} "
          f"unseen={sorted({r['unseen'] for r in rows})} streak={min(streaks)}..{max(streaks)} static_mask={sorted({r['static_mask'] for r in rows})}")
# Records with one primitive count in both LOD sets (39120 lod 1, 39121 lod 0): their world AABB centres.
if 39120 in casters and 39121 in casters:
    before = {r['primitives']: r for r in casters[39120]}
    for r in casters[39121]:
        o = before.get(r['primitives'])
        if o and o['half'] == r['half']:
            a = [float(v) for v in o['centre'].split(',')]
            b = [float(v) for v in r['centre'].split(',')]
            d = sum((x - y) ** 2 for x, y in zip(a, b)) ** .5
            print(f"shared primitives={r['primitives']} vb={o['vb']}->{r['vb']} centre_distance={d:.4f}")
