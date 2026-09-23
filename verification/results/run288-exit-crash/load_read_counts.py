"""Engine-memory reads during the run287 save load, from the telemetry summaries.

One summary emission prints device 0's `engine_memory phase=summary` row (frame = the reader's
epoch, next_frame() calls), then `telemetry_summary device=1 ... qpc=` and device 1's row (frame =
the device's Present count). Prints the reads, epoch and Present deltas per summary interval
inside [save_load_begin, save_load_complete], and the totals. Streams the log, prints no rows.

    python3 verification/results/run288-exit-crash/load_read_counts.py [/tmp/x3-bottleX3-run287/session-*.log]
"""
import glob
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else sorted(glob.glob('/tmp/x3-bottleX3-run287/session-*.log'))[0]
FREQ = 10_000_000
phase = re.compile(r'^loading_phase name=(save_load_begin|save_load_complete) .* qpc=(\d+)')
summary = re.compile(r'^telemetry_summary device=1 .*? qpc=(\d+)')
memory = re.compile(r'^engine_memory phase=summary device=(\d) path=direct reads=(\d+) queries=(\d+) .* frame=(\d+)')
marks, samples, qpc, row = {}, [], None, {}
with open(path, 'rb') as log:
    for raw in log:
        if not raw.startswith((b'loading_phase', b'telemetry_summary', b'engine_memory phase=summary')):
            continue
        line = raw.decode('utf-8', 'replace')
        if m := phase.match(line):
            marks[m[1]] = int(m[2])
        elif m := summary.match(line):
            qpc = int(m[1])
        elif m := memory.match(line):
            # One emission prints device 0's row first, then device 1's telemetry_summary and row.
            row[m[1]] = (int(m[2]), int(m[3]), int(m[4]))
            if m[1] == '1' and qpc is not None and '0' in row:
                samples.append((qpc, row['1'][0], row['1'][1], row['0'][2], row['1'][2]))
                qpc, row = None, {}
begin, end = marks['save_load_begin'], marks['save_load_complete']
print(f'log={path} load_s={(end - begin) / FREQ:.3f}')
before = [s for s in samples if s[0] <= begin][-1]
after = next(s for s in samples if s[0] >= end)
inside = [before] + [s for s in samples if begin < s[0] < end] + [after]
for a, b in zip(inside, inside[1:]):
    print(f'  t={(a[0] - begin) / FREQ:+.2f}s..{(b[0] - begin) / FREQ:+.2f}s dt={(b[0] - a[0]) / FREQ:.2f}s '
          f'reads={b[1] - a[1]} queries={b[2] - a[2]} epoch_advances={b[3] - a[3]} presents={b[4] - a[4]}')
print(f'bracket dt={(after[0] - before[0]) / FREQ:.2f}s reads={after[1] - before[1]} queries={after[2] - before[2]} '
      f'epoch_advances={after[3] - before[3]} presents={after[4] - before[4]}')
