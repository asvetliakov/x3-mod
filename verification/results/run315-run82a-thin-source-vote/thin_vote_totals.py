"""Q3: thin_vote_frame totals per session and voted/draws on given frames. usage: thin_vote_totals.py RUN [frame ...]"""
import glob, re, sys, statistics as st
run = sys.argv[1]; frames = set(int(x) for x in sys.argv[2:])
log = sorted(glob.glob(f'/tmp/x3-bottleX3-run{run}/session-*.log'))[0]
keys = ['draw_timing', 'sampled', 'queued', 'deferred_cap', 'already_queued', 'missed', 'dropped', 'unreadable', 'no_scale', 'refused', 'voted', 'draws']
tot = dict.fromkeys(keys, 0); su = []; n = 0; voted_nz = 0; per = []
for l in open(log, 'rb'):
    if not l.startswith(b'thin_vote_frame '): continue
    d = dict(re.findall(r'(\w+)=(\S+)', l.decode('latin1'))); n += 1
    for k in keys: tot[k] += float(d.get(k, 0))
    if d.get('sampled') == '1': su.append(float(d['sample_us']))
    if int(d['voted']): voted_nz += 1
    per.append(int(d['voted']))
    if int(d['frame']) in frames: print(f"  frame={d['frame']} draws={d['draws']} opaque={d['opaque']} known={d['known']} voted={d['voted']} missed={d['missed']} queued={d['queued']} deferred_cap={d['deferred_cap']} already_queued={d['already_queued']}")
print(f'run{run} frames={n} ' + ' '.join(f'{k}={int(v)}' for k, v in tot.items()) + f' frames_voted>0={voted_nz} voted_per_frame_median(nonzero)={st.median([v for v in per if v] or [0])} max={max(per)}')
if su: print(f'  sample_us n={len(su)} median={st.median(su):.2f} max={max(su):.2f}')
