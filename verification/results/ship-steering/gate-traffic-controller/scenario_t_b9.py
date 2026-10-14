"""B.9: scenario T per variant (9 runs each) from `--traffic` outputs: member warns and E's suppressions, 0x12 episodes, yields,
hand-backs (unresolved), certified frames with capsule contact, the closest capsule gap, the TARGET MISS rows and the check totals.
Usage: python3 -I scenario_t_b9.py <label>=<traffic output> ...   (column_b9.py's scratch builds: before/host, after/host --traffic;
X3M_SIBLING_OFF=1 for the old partner filter)"""
import collections
import re
import sys

KEYS = ('warn member', 'E suppress_ctl', 'episodes', 'yields', 'unresolved', 'contact')
for arg in sys.argv[1:]:
    label, path = arg.split('=', 1)
    tot = collections.OrderedDict()
    miss = 0
    checks = ''
    for l in open(path):
        if l.startswith('TARGET MISS traffic T'):
            miss += 1
        if l.startswith('ship_steering_host checks='):
            checks = l.split()[1] + ' ' + l.split()[2]
        m = re.match(r'traffic T (.+?)\s+fps=\s*\d+ seed=\d+: capitals=', l)
        if not m:
            continue
        tag = m.group(1).strip()
        d = tot.setdefault(tag, collections.Counter(min_capsule=1e18))
        d['runs'] += 1
        d['warn member'] += int(re.search(r'warn member=(\d+)', l).group(1))
        d['E suppress_ctl'] += int(re.search(r'E suppress_ctl (\d+)', l).group(1))
        d['episodes'] += int(re.search(r' episodes=(\d+)', l).group(1))
        d['yields'] += int(re.search(r' yields=(\d+)', l).group(1))
        d['unresolved'] += int(re.search(r' unresolved=(\d+)', l).group(1))
        d['contact'] += int(re.search(r'certified frames=\d+ contact=(\d+)', l).group(1))
        d['min_capsule'] = min(d['min_capsule'], float(re.search(r'min_capsule=(-?\d+) m', l).group(1)))
    print('== %s (%s; TARGET MISS rows %d)' % (label, checks, miss))
    for tag, d in tot.items():
        print('  %-12s runs %d  %s  min_capsule %.0f m' % (tag, d['runs'], '  '.join('%s %d' % (k, d[k]) for k in KEYS), d['min_capsule']))
