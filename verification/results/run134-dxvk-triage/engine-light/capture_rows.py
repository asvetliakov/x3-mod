"""Print engine_light_frame / engine_stage / engine_frame key fields for the capture bursts of run12."""
import re, sys
log = sys.argv[1]
frames = set()
for a, b in ((9830, 9837), (10055, 10062), (10387, 10394)):
    frames.update(range(a, b + 1))
keys = {
 'engine_light_frame': 'ships ships_drawn nodes candidates draws_lit no_twin no_rows records main rcs other_view invalid orphan ships_dropped logged matched singular nodes_dropped twins plates plates_none unfloored plates_dropped'.split(),
 'engine_stage': 'records nozzles discs floored unfloored capped faded culled_small culled_behind skipped_other_view view_rule'.split(),
 'engine_frame': 'candidates not_jet records suppressed rows'.split(),
 'hull_lightmap_frame': 'admitted toggled'.split(),
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
