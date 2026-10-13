"""Scenario T, stage B (gate-traffic-controller.md B.1): per variant over its 9 runs (3 fps x 3 seeds) the worst value of each D.7 assertion
and of the Run161 order-0 gap checks, against the Run160b oracle (D.0), from the host fixture's stdout.
Usage: python3 -I scenario_t_b.py <ship_steering_host stdout> (the tracked verification/results/ship-steering/ship_steering_host.txt)"""
import re
import sys
from collections import defaultdict

MAX = [('episodes', 'D.7.2 0x12 episodes between members per run', None), ('contact', 'certified frames with capsule contact', None),
       ('nose_max', 'D.7.1 nose at X, deg (oracle 79-105)', None), ('not_kept', 'D.7.1 crossings not kept at P2', None),
       ('rotate_frames_max', 'D.7.1 0x1f frames (oracle: rotates 84 s)', None), ('x_refused', 'D.7.4 ctl_x_refused', None),
       ('late_vs_plan_max', 'D.7.4 launch after its sequence time, s (+60 s after a direction change)', None),
       ('o4_outside', 'D.7.6 order 4 ended outside X', None), ('petal_slow_frames', 'D.7.6 frames below 25 m/s on a petal', None),
       ('o4_slow_frames', 'D.7.6 frames below 10 m/s on order 4 (module)', None), ('engine_law', 'D.7.6 ... (engine law)', None),
       ('go_arounds', 'go-arounds', None), ('gap_warn_unsup', 'Run161: member warns unsuppressed in the order-0 gap', None),
       ('grid_zone_max', 'Run161: committed members past their X depth at once', None),
       ('late_vs_18s_max', 'reported: launch after t_launch(k-1) + 18 s (D.7 literal), s', None),
       ('span_first_cross_to_last_launch', 'D.7.7 reported: first crossing to last launch, s (oracle 484 for 6)', None),
       ('cross_to_launch_max', 'reported: cross (P2) -> launch per ship, s, max', None)]
MIN = [('cross_to_launch_min', 'reported: cross (P2) -> launch per ship, s, min'), ('cross_to_launch_mean', 'reported: ... the runs\' lowest mean, s'),
       ('min_small_spawn', 'D.4: a small arrival\'s spawn against the capital at the plane, surface clearance m'),
       ('min_capsule', 'D.7.3 closest capsule gap between capitals, m'), ('min_box', 'reported: closest box bound, m'),
       ('min_hold_gap', 'Run161: closest two petal holders, capsule gap m'), ('launched', 'departures launched'),
       ('from_petal', 'D.7.5 escorts starting order 4 from a petal')]
SUM = [('gap_warns', 'Run161: member warns in the order-0 gap (all suppressed when the row above is 0)'), ('escort_o4', 'escort order-4 starts')]


def main(path):
    rows = defaultdict(lambda: defaultdict(list))
    for line in open(path, errors='replace'):
        m = re.match(r'traffic T (\S+(?: w0\.6r)?)\s+fps=\s*(\d+) seed=(\d+)(:| stage B:| order-0 gap:)', line)
        if not m:
            continue
        for k, v in re.findall(r'([a-z_0-9]+)=(-?[0-9.]+)', line):
            rows[m.group(1)][k].append(float(v))
    for tag, d in rows.items():
        print('%s (%d runs)' % (tag, len(d.get('nose_max', []))))
        for k, label, _ in MAX:
            if d.get(k):
                print('  %-90s max %g' % (label, max(d[k])))
        for k, label in MIN:
            if d.get(k):
                vals = [v for v in d[k] if v >= 0 or k in ('min_capsule', 'min_box')]
                print('  %-90s min %g' % (label, min(vals) if vals else -1))
        for k, label in SUM:
            if d.get(k):
                print('  %-90s sum %g' % (label, sum(d[k])))
    miss = sum(1 for l in open(path, errors='replace') if l.startswith('TARGET MISS traffic T'))
    fail = sum(1 for l in open(path, errors='replace') if l.startswith('FAIL traffic T') or l.startswith('FAIL sb'))
    print('TARGET MISS rows (scenario T) %d, FAIL rows (scenario T and stage-B units) %d' % (miss, fail))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
