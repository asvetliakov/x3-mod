#!/usr/bin/env python3
"""Run 38 (FL) music triage: music rows plus frame stalls (dt_ms >= 400) and a
per-stall tag of the queue state (orphan playing under a deferred hand-over or
not). Usage: python3 -I triage_music.py <log> [--rows]"""
import re, sys
log = sys.argv[1]
show_rows = '--rows' in sys.argv
KEYS = ('music_keep_stop ', 'music_keep_seek ', 'music_keep_resume ',
        'music_queue ', 'music_queue_kc ', 'music_keep_stop_movie ',
        'music_keep_active ', 'music_alert edge')
FIELDS = ('frame', 'event', 'reason', 'old', 'new', 'caller', 'name', 'mode',
          'id', 'action', 'saved_ms', 'resume_ms', 'at', 'held', 'from', 'to',
          'method', 'applied', 'refused')
kv = re.compile(r'(\w+)=(\S+)')
state = 'idle'
stalls = []
with open(log, 'rb') as f:
    for raw in f:
        if raw.startswith(b'frame_end device'):
            m = re.search(rb' frame=(\d+) .* dt_ms=(\d+) ', raw)
            if m and int(m.group(2)) >= 400:
                stalls.append((int(m.group(1)), int(m.group(2)), state))
            continue
        if not raw.startswith(b'music_'):
            continue
        line = raw.decode('latin-1').rstrip()
        if not line.startswith(KEYS):
            continue
        d = dict(kv.findall(line))
        if line.startswith('music_queue '):
            state = d.get('state', state)
        if show_rows:
            print(line.split()[0], ' '.join(f'{k}={d[k]}' for k in FIELDS if k in d))
print('stalls dt_ms>=400 (frame, dt_ms, queue_state_at_stall):')
for s in stalls:
    print(' ', *s)
