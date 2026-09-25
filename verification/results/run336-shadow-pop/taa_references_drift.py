#!/usr/bin/env python3
"""The per-frame taa_references field of motion_output_frame (the measured part of device_references(),
the final-Release probe's term that drifted): prints the frames where the value changes, compressed.
usage: taa_references_drift.py <session log> [<session log> ...]   (read-only; the logs stay local)
Output for the ledger (directional-shadows.md, "Final-Release probe drift"): run336 68 at 405 then -1 per
bracket frame, wrapping to 4,294,966,958 at 906 (the first flush=teardown); run334 the same from 790; run324 flat."""
import re
import sys

for path in sys.argv[1:]:
    previous = None
    changes = []
    rows = 0
    with open(path, 'rb') as log:
        for line in log:
            if not line.startswith(b'motion_output_frame '):
                continue
            match = re.search(rb'frame=(\d+).*?taa_references=(\d+)', line)
            if not match:
                continue
            rows += 1
            frame, value = int(match.group(1)), int(match.group(2))
            if value != previous:
                changes.append((frame, value))
                previous = value
    # Runs of -1 per frame are compressed to (first frame, first value) .. (last frame, last value).
    compressed = []
    i = 0
    while i < len(changes):
        j = i
        while j + 1 < len(changes) and changes[j + 1][0] == changes[j][0] + 1 and changes[j + 1][1] == (changes[j][1] - 1) % (1 << 32):
            j += 1
        compressed.append(changes[i] if i == j else (changes[i], changes[j], 'step -1 per frame'))
        i = j + 1
    print(f'{path} rows={rows} changes={len(changes)}')
    for item in compressed:
        print('  ', item)
