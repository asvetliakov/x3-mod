#!/usr/bin/env python3
"""Per 300-frame window: volumetric_fog_frame reason counts, and the applied frames' cpu_us p50 in W4 (frames 900-1199).
Evidence for docs/architecture/fog-gpu-cost.md: the second sector's frames are card_refused before execute (no fog_route pair).
Usage: fog_reasons_per_window.py SESSION_LOG"""
import re, sys, collections
c = collections.Counter(); cpu = []
for line in open(sys.argv[1], errors='replace'):
    if line.startswith('volumetric_fog_frame'):
        m = re.search(r'frame=(\d+) applied=(\d) reason=(\S+).*?cpu_us=([\d.]+)', line)
        if not m: continue
        fr = int(m.group(1)); c[(fr // 300 + 1, m.group(3))] += 1
        if 900 <= fr < 1200 and m.group(2) == '1': cpu.append(float(m.group(4)))
for (w, r), n in sorted(c.items()): print(f"W{w} {r} {n}")
cpu.sort(); print("W4 applied cpu_us p50", cpu[len(cpu) // 2] if cpu else None, "n", len(cpu))
