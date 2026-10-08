"""Run-length series of the Ocelot (most_lights root) plate set from engine_light_frame rows: per run of frames with
the same (most_lights, plates_dropped, records, main) print first-last frame and length; then toggle counts.
usage: plate_series.py LOG [root]"""
import sys, collections
log = sys.argv[1]; root = sys.argv[2] if len(sys.argv) > 2 else '3bdcd0e0'
runs = []
with open(log, errors='replace') as f:
    for line in f:
        if not line.startswith('engine_light_frame '): continue
        d = dict(t.split('=', 1) for t in line.split()[1:] if '=' in t)
        ml = d['most_lights']
        key = (ml if ml.startswith(root) else 'other:' + ml.split(':')[0], d['plates_dropped'], d['main'], d['ships'], d['nodes'])
        fr = int(d['frame'])
        if runs and runs[-1][0] == key and fr == runs[-1][2] + 1: runs[-1][2] = fr
        else: runs.append([key, fr, fr])
for key, a, b in runs:
    print(f'{a}-{b} n={b - a + 1} most_lights={key[0]} plates_dropped={key[1]} main={key[2]} ships={key[3]} nodes={key[4]}')
short = [r for r in runs if r[2] - r[1] + 1 <= 2]
print('runs', len(runs), 'runs_of_1-2_frames', len(short))
