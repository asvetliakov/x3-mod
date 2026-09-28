#!/usr/bin/env python3
"""Sum of frame_end dt_ms over frames (a, b]: time from the pan burst's last frame to the after-pan burst. Usage: gap_ms.py dir a b"""
import glob, re, sys
d, a, b = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); s = 0
for l in open(glob.glob(f'{d}/session-*.log')[0], errors='replace'):
    if l.startswith('frame_end device=1 '):
        f = int(re.search(r'frame=(\d+)', l).group(1))
        if a < f <= b: s += float(re.search(r'dt_ms=([\d.]+)', l).group(1))
print(d, a, b, f'{s:.0f} ms')
