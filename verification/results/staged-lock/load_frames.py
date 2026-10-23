"""Frames of 200 ms or more in an x3m.log and the music_trace rows inside each (a savegame load spans
`music_trace_stop ... name=load` to the next `MOV_PlayMovie`), seconds from the frame's start; QPC at 10 MHz.

    python3 verification/results/staged-lock/load_frames.py /mnt/c/X3/x3m.log /mnt/c/X3/x3m.prev.log
"""
import re
import sys

for path in sys.argv[1:]:
    frames, music = [], []
    for line in open(path, 'rb'):
        if line.startswith(b'frame_end '):
            d = dict(re.findall(rb'(\w+)=(\S+)', line))
            frames.append((int(d[b'frame']), int(d[b'dt_ms']), int(d[b'qpc'])))
        elif line.startswith((b'music_trace_stop ', b'music_trace_play ')):
            d = dict(re.findall(rb'(\w+)=(\S+)', line))
            music.append((line.split()[0].decode(), int(d[b'qpc']), d.get(b'name', b'').decode()))
    qpc = {f: q for f, _, q in frames}
    print(path, len(frames), 'frames')
    for frame, dt, q in frames:
        if dt < 200 or frame - 1 not in qpc:
            continue
        inside = [(kind, name, round((m - qpc[frame - 1]) / 1e7, 3)) for kind, m, name in music if qpc[frame - 1] <= m <= q]
        print(f'  frame {frame} dt_ms {dt}', inside)
