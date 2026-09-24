"""Per-part lod_switch statistics for run305 (--lod-switch-log): switches, max switches of one part in one frame,
frames between consecutive switches of the part (min/median/max), the s values at 1->0 and 0->1, and the max
switches in any lod_switch_frame row. Streaming, read-only. Usage: python3 switch_intervals.py LOG"""
import re, sys, statistics
from collections import defaultdict, Counter
KV = re.compile(r'(\w+)=(\S*)')
rows, frame_rows = defaultdict(list), []
for line in open(sys.argv[1], errors='replace'):
    if line.startswith('lod_switch '):
        d = dict(KV.findall(line)); rows[d['body'].rsplit('\\', 1)[-1]].append((int(d['frame']), d['from'], d['to'], int(d['s']), int(d['T_pad'])))
    elif line.startswith('lod_switch_frame'):
        frame_rows.append(int(dict(KV.findall(line))['switches']))
print(f'lod_switch_frame rows={len(frame_rows)} max_switches_in_a_frame={max(frame_rows)}')
for body, r in sorted(rows.items(), key=lambda kv: -len(kv[1])):
    fr = [x[0] for x in r]; gaps = [b - a for a, b in zip(fr, fr[1:])]
    per_frame = max(Counter(fr).values())
    up = sorted({x[3] for x in r if x[1] == '1' and x[2] == '0'}); down = sorted({x[3] for x in r if x[1] == '0' and x[2] == '1'})
    print(f'{body}: switches={len(r)} frames {fr[0]}..{fr[-1]} max_per_frame={per_frame} T_pad={r[0][4]} s(1->0)={up} s(0->1)={down}'
          + (f' gap_frames min={min(gaps)} median={statistics.median(gaps)} max={max(gaps)}' if gaps else ''))
