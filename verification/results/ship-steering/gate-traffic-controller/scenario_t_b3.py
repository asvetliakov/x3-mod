"""Scenario T, B.3 (gate-traffic-controller.md "B.3 Run163 fixes"): the `run162 seta` variant (game steps of 1,500-2,000 ms on 2 % of
frames) per run (3 fps x 3 seeds): the SETA steps flown, the own-gate verdicts pass_not_fresh and unsuppressed, the escorts' launch slack
against 120 s + their own funnel time, the frame budget's largest decision count, launches and the last launch, the TARGET MISS rows of the
variant; and the B.3 unit rows. Usage: python3 -I scenario_t_b3.py <ship_steering_host stdout>"""
import re
import sys

rows, b2, targets, units = [], {}, [], []
for line in open(sys.argv[1], errors='replace'):
    m = re.match(r'traffic T run162 seta\s+fps=\s*(\d+) seed=(\d+) B\.3: (.*)', line)
    if m:
        d = dict(re.findall(r'(\w+)=(-?[\d.]+)', m.group(3)))
        rows.append((int(m.group(1)), int(m.group(2)), d))
        continue
    m = re.match(r'traffic T run162 seta\s+fps=\s*(\d+) seed=(\d+) B\.2: (.*)', line)
    if m:
        b2[(int(m.group(1)), int(m.group(2)))] = dict(re.findall(r'(\w+)=(-?[\d.]+)', m.group(3)))
        continue
    if line.startswith('TARGET MISS traffic T run162 seta'):
        targets.append(line.strip()[len('TARGET MISS traffic T '):])
    elif line.startswith('sb B.3'):
        units.append(line.strip())
print('run162 seta (%d runs)' % len(rows))
print('  fps seed  steps  max_ms  pass_not_fresh  unsuppressed  escorts  late  slack_min_s  budget_max  launched  last_launch_s')
for fps, seed, d in rows:
    e = b2.get((fps, seed), {})
    print('  %3d %7d %6s %7s %15s %13s %8s %5s %12s %11s %9s %14s' % (fps, seed, d.get('seta_steps'), d.get('step_max'), d.get('own_gate_pass_not_fresh'),
          d.get('own_gate_unsuppressed'), d.get('escorts_timed'), d.get('late'), d.get('slack_min'), d.get('budget_max'), e.get('launched', '-'),
          e.get('last_launch', '-')))
print('TARGET MISS rows of the variant: %d' % len(targets))
for t in targets:
    print('  ' + t[:220])
print('B.3 unit rows:')
for u in units:
    print('  ' + u)
