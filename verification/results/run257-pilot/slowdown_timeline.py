#!/usr/bin/env python3
"""Per-60-frame medians with UTC wall time (clock_anchor): frame dt, draws and two
scene-independent proxy passes (hdr writeback = one fullscreen draw, exposure meter)
plus taa_run_us, to date a slowdown against host activity.
usage: slowdown_timeline.py <session.log> <first> <last> [step=60]"""
import re, sys, statistics as st
from datetime import datetime, timedelta
LOG, A, B = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); S = int(sys.argv[4]) if len(sys.argv) > 4 else 60
kv = re.compile(r'(\w+)=(\S+)'); fr = {}; anchor = None
want = {'frame_end': ('dt_ms', 'draws', 'qpc', 'capture'), 'hdr_frame': ('writeback_us', 'meter_us'), 'motion_output_frame': ('taa_run_us',),
        'shadow_replay_depth': ('us',)}
with open(LOG, errors='replace') as fh:
    for line in fh:
        t = line.split(' ', 1)[0]
        if t == 'clock_anchor':
            d = dict(kv.findall(line)); anchor = (datetime.strptime(d['utc'][:19], '%Y-%m-%dT%H:%M:%S'), int(d['qpc']), int(d['qpc_frequency']))
        elif t in want and ' frame=' in line:
            d = dict(kv.findall(line)); f = int(d['frame'])
            if A <= f <= B:
                r = fr.setdefault(f, {})
                for k in want[t]:
                    if k in d: r[t + '.' + k] = float(d[k])
print('first last utc_first dt_ms draws hdr_wb_us meter_us taa_us sdepth_us')
for s in range(A, B + 1, S):
    rows = [fr[f] for f in range(s, s + S) if f in fr and fr[f].get('frame_end.capture', 0) == 0 and 'frame_end.qpc' in fr[f]]
    if not rows: continue
    q = rows[0]['frame_end.qpc']; utc = (anchor[0] + timedelta(seconds=(q - anchor[1]) / anchor[2])).strftime('%H:%M:%S') if anchor else '-'
    med = lambda k: st.median([r[k] for r in rows if k in r]) if any(k in r for r in rows) else float('nan')
    print(s, s + S - 1, utc, f"{med('frame_end.dt_ms'):.1f}", f"{med('frame_end.draws'):.0f}", f"{med('hdr_frame.writeback_us'):.0f}",
          f"{med('hdr_frame.meter_us'):.0f}", f"{med('motion_output_frame.taa_run_us'):.0f}", f"{med('shadow_replay_depth.us'):.0f}")
