#!/usr/bin/env python3
"""What the 2026-10-25 flight log can say about the SETA key restart: the SETA row kinds it carries and the frame
rate (a key press of a given length spans proportionally more ring reads). Streams the log; prints counts only.

    python3 -I verification/results/seta-key-toggle/log_facts.py [/mnt/c/X3/x3m.log] > log_facts.txt
"""
import collections, re, sys

path = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3/x3m.log'
kinds = collections.Counter(); fps = []; restored = []; arms = collections.Counter(); debug = None
with open(path, 'r', encoding='latin1', errors='replace') as f:
    for line in f:
        if line.startswith('seta_'):
            w = line.split()
            kinds[' '.join(w[:2]) if len(w) > 1 and '=' not in w[1] else w[0]] += 1
            if w[1] == 'restored':
                m = re.search(r'frames_after_transit=(\d+)', line); restored.append(int(m.group(1)))
            if w[1] in ('armed', 'skipped'):
                arms[(w[1], re.search(r'ret_name=(\S+)', line).group(1), re.search(r'autopilot=(\d)', line).group(1))] += 1
            if w[0] == 'seta_manual_flight' and 'debug=' in line:
                debug = re.search(r'debug=(\d)', line).group(1)
        elif line.startswith('frame_end '):
            m = re.search(r'dt_ms=(\d+)', line)
            if m and int(m.group(1)) > 0: fps.append(3600000.0 / int(m.group(1)))
print('log %s' % path)
for k, v in sorted(kinds.items()): print('  %-28s %d' % (k, v))
print('  seta_manual_flight debug=%s' % debug)
for k, v in sorted(arms.items()): print('  %s ret_name=%s autopilot=%s: %d' % (k[0], k[1], k[2], v))
print('  restored frames_after_transit values: %s' % sorted(set(restored)))
fps.sort()
q = lambda p: fps[min(len(fps) - 1, int(p * len(fps)))]
print('  frame_end windows (3600 frames each): n=%d fps p10=%.0f median=%.0f p90=%.0f' % (len(fps), q(0.1), q(0.5), q(0.9)))
print('  rows that would show a user SETA toggle, a ramp step or a key read: none exist at this tier')
