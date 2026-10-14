"""Scenario T, B.7 (gate-traffic-controller.md B.7, Run167 escort staging): per variant over its 9 runs (3 fps x 3 seeds) the escorts
parked at their ring when their leader launches (held there at least one 60 m/s lap before launch + kEscortSwitchMs) on a petal with a
stage inside 9.5 km, their order 4 after the leader's launch (delay mean / max, late = beyond kEscortSwitchMs + the script's 5 s pass +
the wait's longest game step), the escorts at their ring at the launch but ringed later (reported apart), and the fresh writes of the
0x15 approach after the launch (parked staged escorts; all escorts), from the host fixture's "B.7:" rows.
Usage: python3 -I scenario_t_b7.py <before stdout> <after stdout>
The before stdout comes from this commit's fixture built against the B.6 controller: scenario_t_b7_pre_build.py writes that tree (the
B.7 escort branch of lap_speed removed, escort_stage_s kept for the fixture's window helper) and builds it; run with --traffic."""
import re
import sys
from collections import OrderedDict

ROW = re.compile(r'traffic T (.{12}) fps=\s*(\d+) seed=(\d+) B\.7: (.*?)(?:FAIL|$)')
KV = re.compile(r'([a-z_0-9]+)=(-?[0-9.]+)')


def agg(path):
    out = OrderedDict()
    for line in open(path, errors='replace'):
        m = ROW.search(line)
        if not m:
            continue
        kv = dict(KV.findall(m.group(4)))
        a = out.setdefault(m.group(1).strip(), {'runs': 0, 'parked': 0, 'o4': 0, 'dsum': 0.0, 'dmax': -1.0, 'late': 0, 'ring_late': 0,
                                                'appr_p': 0, 'appr_all': 0, 'far': 0, 'unr': 0, 'rng': 0})
        a['runs'] += 1
        a['parked'] += int(kv['staged'])
        o4 = int(kv['order4'])
        a['o4'] += o4
        a['dsum'] += float(kv['delay_mean']) * o4 if o4 else 0.0
        a['dmax'] = max(a['dmax'], float(kv['delay_max']))
        a['late'] += int(kv['late'])
        a['ring_late'] += int(kv.get('ringed_late', 0))
        a['appr_p'] += int(kv['parked_staged'])
        a['appr_all'] += int(kv['all'])
        a['far'] += int(kv.get('far_petal', 0))
        a['unr'] += int(kv.get('near_unringed', 0))
        a['rng'] += int(kv.get('near_ringed_unparked', 0))
    return out


def main():
    before, after = agg(sys.argv[1]), agg(sys.argv[2])
    print('variant      runs | parked staged escorts: order4 delay_mean delay_max s late | ringed late | 0x15 approach frames after the '
          'launch: parked staged, all escorts   (before -> after B.7)')
    for tag, a in after.items():
        b = before.get(tag)
        def f(x):
            return '%5.1f' % (x['dsum'] / x['o4']) if x['o4'] else '   -1'
        print('%-12s %4d | %3d->%-3d %s->%s %5.1f->%-5.1f %2d->%-2d | %2d->%-2d | %6d->%-6d %6d->%-6d' % (
            tag, a['runs'], b['o4'], a['o4'], f(b), f(a), b['dmax'], a['dmax'], b['late'], a['late'], b['ring_late'], a['ring_late'],
            b['appr_p'], a['appr_p'], b['appr_all'], a['appr_all']))
    print('the other approach frames by class (review F4), before -> after: far petal | near petal, not yet ringed | near, ringed, not parked')
    for tag, a in after.items():
        b = before.get(tag)
        print('%-12s %6d->%-6d | %6d->%-6d | %6d->%-6d' % (tag, b['far'], a['far'], b['unr'], a['unr'], b['rng'], a['rng']))


if __name__ == '__main__':
    main()
