"""Run 72 B: pair each music_keep_stop with its seek and the frame_end timeline.
Usage: python3 music_keep_pairs.py <session.log>
Prints per stop: seq, name, id, mode, stop->seek ms, seek action, frame_end gap
(largest frame_end qpc gap in the 10 frames after the stop, i.e. the inactive span),
and the ms from the seek to the first frame_end after that gap (resume)."""
import re, sys, bisect
log = sys.argv[1]
kv = re.compile(r'(\w+)=(\S+)')
music, fe, win = [], [], []
with open(log, errors='replace') as f:
    for line in f:
        if line.startswith('frame_end '):
            d = dict(kv.findall(line)); fe.append((int(d['qpc']), int(d['frame'])))
        elif line.startswith('music_keep_'):
            d = dict(kv.findall(line)); d['k'] = line.split()[0]; music.append(d)
        elif line.startswith('telemetry_window '):
            d = dict(kv.findall(line)); win.append((int(d['qpc']), int(d['frame']), d['foreground'] == d['window']))
fe.sort(); q = [a for a, _ in fe]
music.sort(key=lambda d: int(d['seq']))
print('frames', len(fe), 'music_keep lines', len(music), 'window samples', len(win), 'not_foreground', sum(1 for w in win if not w[2]))
for i, d in enumerate(music):
    if d['k'] != 'music_keep_stop':
        continue
    s = int(d['qpc']); nxt = music[i + 1] if i + 1 < len(music) else None
    seek_ms = (int(nxt['qpc']) - s) / 1e4 if nxt and nxt['k'] == 'music_keep_seek' else None
    j = bisect.bisect_left(q, s)
    prev_fe = q[j - 1] if j else None
    seq = q[j - 1:j + 10] if j else q[j:j + 10]
    gaps = [(seq[k + 1] - seq[k]) / 1e4 for k in range(len(seq) - 1)]
    gap = max(gaps) if gaps else None
    first_after = (q[j] - s) / 1e4 if j < len(q) else None
    print(f"seq={d['seq']} frame={d['frame']} name={d['name']} id={d['id']} mode={d['mode']} "
          f"stop_to_seek_ms={seek_ms} seek_action={nxt.get('action') if nxt else None} "
          f"stop_to_next_frame_end_ms={first_after} max_frame_gap_ms_next10={gap}")
