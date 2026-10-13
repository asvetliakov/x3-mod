"""Scenario T, B.2 (gate-traffic-controller.md "B.2 Run162 fixes"): per variant over its 9 runs (3 fps x 3 seeds) the B.2 row's measurements
(own-gate warns of stage-B members and the ones left to the engine, head skips and those short of the ring, frames a leader was sequenced
behind its escort, re-holds past X, the inbound cruise, the first ship's commit -> launch against its transit at v_max) and the stage-B
span and launches, from the host fixture's stdout. Usage: python3 -I scenario_t_b2.py <ship_steering_host stdout>"""
import re
import sys
from collections import defaultdict

KEYS = [('own_gate_warns o4', 'sum', 'own-gate warns on order 4 (funnel)'), ('unsup_o4', 'max', '... left to the engine, per run'),
        ('gap/hand-over', 'sum', 'own-gate warns on order 0 / 0x1f (the gap, the hand-over)'), ('unsup_gap', 'max', '... left to the engine, per run'),
        ('skips', 'max', 'head skips per run'), ('skips_short_of_ring', 'max', 'head skips short of the ring'),
        ('leader_behind_escort_frames', 'max', 'frames a leader was sequenced behind its escort'), ('reheld_past_x', 'max', 're-holds past 1.2 r_gate of X'),
        ('inbound_frames', 'min', 'inbound cruise frames (110 m/s class) per run, min'), ('inbound_v_min', 'min', 'inbound cruise speed, m/s, min'),
        ('inbound_v_mean', 'min', 'inbound cruise speed, m/s, the runs\' lowest mean'), ('inbound_slow', 'max', 'inbound cruise frames < 99.5 m/s'),
        ('inbound_yield_frames', 'max', 'inbound frames under (or 10 s after) a yield cap, excluded'),
        ('inbound_vs_holder_min_gap', 'min+', 'review N5: closest capsule gap inbound vs holder, m (pairs present)'),
        ('first_commit_to_launch', 'max', 'first ship commit -> launch, s, max'), ('transit_at_vmax', 'max', '... its transit at v_max, s'),
        ('last_launch', 'max', 'last launch, game s, max'), ('span_first_cross_to_last_launch', 'max', 'first crossing to last launch, s, max'),
        ('launches', 'min', 'launches per run, min'), ('releases', 'max', 'releases per run, max'), ('go_arounds', 'max', 'go-arounds per run, max')]


def main(path):
    rows = defaultdict(lambda: defaultdict(list))
    for line in open(path, errors='replace'):
        m = re.match(r'traffic T (\S+(?: w0\.6r| refused)?)\s+fps=\s*(\d+) seed=(\d+) (B\.2:|stage B:)', line)
        if not m:
            continue
        tag = m.group(1)
        line = line.replace('(unsuppressed ', 'unsup=').replace(') gap/hand-over', ' gap/hand-over').replace(') skips', ' skips')
        b2 = m.group(4) == 'B.2:'
        for key, _, _ in KEYS:
            k = key
            if key == 'unsup_o4' or key == 'unsup_gap':
                vals = re.findall(r'unsup=([-\d.]+)', line) if b2 else []
                if len(vals) == 2:
                    rows[tag][key].append(float(vals[0 if key == 'unsup_o4' else 1]))
                continue
            mm = re.search(r'(?:^|\s)' + re.escape(k) + r'=([-\d.]+)', line)
            if mm:
                rows[tag][key].append(float(mm.group(1)))
                if float(mm.group(1)) >= 0:
                    rows[tag][key + '@' + m.group(2)].append(float(mm.group(1)))
    for tag, d in rows.items():
        print(f'{tag} ({len(d.get("launches", []))} runs)')
        for key, agg, label in KEYS:
            v = d.get(key)
            if not v:
                continue
            if agg == 'min+':  # -1 = no such pair in the run
                v = [y for y in v if y >= 0]
                if not v:
                    continue
                print(f'  {label:<72} min {min(v):g} (per fps: ' + ', '.join(f'{f} {min(d[key + "@" + f]):g}' for f in ('30', '60', '144')
                                                                              if d.get(key + '@' + f)) + ')')
                continue
            x = {'sum': sum(v), 'max': max(v), 'min': min(v)}[agg]
            print(f'  {label:<72} {agg} {x:g}')


if __name__ == '__main__':
    main(sys.argv[1])
