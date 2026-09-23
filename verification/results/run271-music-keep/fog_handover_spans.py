"""Run 72 B (run271), fog hand-over design input. Usage: fog_handover_spans.py <session.log>
Prints (a) frames and fps over each sector entry's density_filling and far-ramp span (frame numbers from
fog_entry_timeline_out.txt), (b) the length of the frames around every volumetric_fog_sector row, which shows
that a gate/jump transit is one synchronous stall frame of several seconds. Reads only frame_end rows."""
import re, sys
spans = [('no_cockpit span', 2359, 2633), ('fill1', 2634, 2662), ('ramp1', 2663, 2752), ('fill2', 5329, 5343),
         ('ramp2', 5344, 5433), ('fill3', 8984, 9050), ('ramp3', 9051, 9140)]
around = [1070, 2358, 2359, 2633, 2634, 5327, 5328, 5329, 8982, 8983, 8984]
want = {f for _, a, b in spans for f in (a, b)} | {f + d for f in around for d in (-1, 0, 1)}
fe = {}
with open(sys.argv[1], errors='replace') as f:
    for line in f:
        if not line.startswith('frame_end '): continue
        m = re.search(r'frame=(\d+) .*?qpc=(\d+)', line)
        if not m: continue
        fr = int(m.group(1))
        if fr in want: fe[fr] = int(m.group(2))
        if fr > max(want): break
for label, a, b in spans:
    if a in fe and b in fe:
        s = (fe[b] - fe[a]) / 1e7
        print(f"{label}: frames {a}-{b} = {b - a} frames in {s:.2f} s = {(b - a) / s:.1f} fps")
prev = None
for fr in sorted(fe):
    if prev is not None and fr == prev + 1: print(f"frame {prev}->{fr}: {(fe[fr] - fe[prev]) / 1e4:.0f} ms")
    prev = fr
