"""Scenario T (the gate traffic controller, stage A) per variant over its 9 runs (3 fps x 3 seeds), from the host fixture's stdout.
Usage: python3 -I scenario_t_summary.py <ship_steering_host stdout> (the tracked verification/results/ship-steering/ship_steering_host.txt)"""
import re
import sys


def main(path):
    agg = {}
    for line in open(path, errors='replace'):
        if not line.startswith('traffic T ') or 'fps=' not in line:
            continue
        tag = line[10:23].strip()

        def num(key):
            m = re.search(key + r'=(-?[0-9.]+)', line)
            return float(m.group(1)) if m else 0.0
        ep = re.search(r'episodes=(\d+) \(grid-bound (\d+), other (\d+)\)', line)
        es = re.search(r'E suppress_ctl (\d+), pass_ctl (\d+)', line)
        fm = re.search(r'frame_max=(\d+)/(\d+)', line)
        a = agg.setdefault(tag, {'runs': 0, 'ep': [], 'grid': 0, 'other': 0, 'cap': [], 'cap_other': [], 'cert': 0, 'contact': 0, 'cap_cert': [],
                                 'decisions': 0, 'yields': 0, 'capped': 0, 'unresolved': 0, 'sup': 0, 'pass': 0, 'fmax': [0, 0], 'box': []})
        a['runs'] += 1
        a['ep'].append(int(ep.group(1)))
        a['grid'] += int(ep.group(2))
        a['other'] += int(ep.group(3))
        a['cap'].append(num('min_capsule'))
        a['cap_other'].append(num('min_capsule_other'))
        a['box'].append(num('min_box'))
        a['cert'] += int(num('certified frames'))
        a['contact'] += int(re.search(r'certified frames=\d+ contact=(\d+)', line).group(1))
        cc = num('min_capsule_certified')
        if cc >= 0:
            a['cap_cert'].append(cc)
        for k in ('decisions', 'yields', 'capped', 'unresolved'):
            a[k] += int(num(k))
        a['sup'] += int(es.group(1))
        a['pass'] += int(es.group(2))
        a['fmax'] = [max(a['fmax'][0], int(fm.group(1))), max(a['fmax'][1], int(fm.group(2)))]
    print('variant      runs | 0x12 episodes per run min..max (sum grid-bound / other) | min capsule m (all / not grid-bound) min box m | '
          'certified frames, contact, min capsule m | decisions yields capped unresolved | member warns E suppress / pass | frame max dec/rollouts')
    for tag, a in agg.items():
        print('%-12s %4d | %d..%d (%d / %d) | %.0f / %.0f %.0f | %d, %d, %s | %d %d %d %d | %d / %d | %d/%d' % (
            tag, a['runs'], min(a['ep']), max(a['ep']), a['grid'], a['other'], min(a['cap']), min(a['cap_other']), min(a['box']), a['cert'],
            a['contact'], '%.0f' % min(a['cap_cert']) if a['cap_cert'] else '-', a['decisions'], a['yields'], a['capped'], a['unresolved'],
            a['sup'], a['pass'], a['fmax'][0], a['fmax'][1]))


if __name__ == '__main__':
    main(sys.argv[1])
