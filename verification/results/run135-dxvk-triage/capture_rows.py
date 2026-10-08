"""engine_light_frame / engine_stage / engine_frame key fields around the capture bursts of a session log.
usage: capture_rows.py LOG first-last [first-last ...] (frames inclusive)"""
import re, sys
log = sys.argv[1]
frames = set()
for a in sys.argv[2:]:
    lo, hi = map(int, a.split('-')); frames.update(range(lo, hi + 1))
keys = {
 'engine_light_frame': 'ships ships_drawn nodes candidates draws_lit no_twin no_rows records main rcs brake other_view invalid orphan ships_dropped logged log_dropped matched singular nodes_dropped plates plates_none unfloored plates_dropped lights own_lights most_lights'.split(),
 'engine_stage': 'records nozzles discs floored unfloored capped faded culled_small culled_behind culled_rows culled_idle skipped_other_view view_rule far far_records'.split(),
 'engine_frame': 'candidates not_jet records suppressed rows forwarded_overflow'.split(),
}
fr = re.compile(r' frame=(\d+) ')
with open(log, errors='replace') as f:
    for line in f:
        k = line.split(' ', 1)[0]
        if k not in keys: continue
        m = fr.search(line)
        if not m or int(m.group(1)) not in frames: continue
        d = dict(t.split('=', 1) for t in line.split()[1:] if '=' in t)
        print(k, 'frame=' + m.group(1), ' '.join(f'{x}={d.get(x)}' for x in keys[k]))
