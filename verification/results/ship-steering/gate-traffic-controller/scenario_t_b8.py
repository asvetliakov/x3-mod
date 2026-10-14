"""Scenario T, B.8 (gate-traffic-controller.md B.8, Run168: the lap law counts the join distance): per variant over its 9 runs (3 fps x
3 seeds), before -> after, from the host fixture's "B.7:" and "B.8:" rows:
* every escort's order 4 after its leader's launch (count, mean, max s), and the ringed-late ones (at their ring at the launch but ringed
  less than a 60 m/s lap before: run162's escorts arriving with their leader, the Run168 case);
* the fresh 0x15 approach frames (60 m/s beyond 9.49 km) after the launch, all escorts (B.7's "all");
* the escorts in hold at the launch: count, on their track (d_join <= 300 m), the largest d_join; still inbound;
* the join assertion: escorts first seen in hold with a future target off their track and the target reachable at 60 m/s (tracked), at
  T + the frame step on track and in their window (ok), already on order 4, short after an order-0 gap within 10 s (gap, its largest
  shortfall), missed; not reachable at the first frame (unreach); the same escorts at their first order-4 chance (launch +
  kEscortSwitchMs + the step): on track and in window or on order 4 (ok), short (count, largest distance to go);
* the escorts off their track or not in hold at the launch by cause: still inbound, hold entered after the target, the stage not
  reachable at 60 m/s from the hold entry, other.
Usage: python3 -I scenario_t_b8.py <before stdout> <after stdout> [<no-lead stdout>]
The before stdout comes from this commit's fixture built against B.7's controller (scenario_t_b8_pre_build.py mode b7), the no-lead
one with only B.8's target lead removed (mode nolead); both run with --traffic."""
import re
import sys
from collections import OrderedDict

B7 = re.compile(r'traffic T (.{12}) fps=\s*(\d+) seed=(\d+) B\.7: .*? all=(\d+)')
B8 = re.compile(r'traffic T (.{12}) fps=\s*(\d+) seed=(\d+) B\.8: join tracked=(\d+) ok=(\d+) o4=(\d+) miss=(\d+) gap=(\d+) \(short_max (\d+) m\) '
                r'gone=(\d+) unreach=(\d+) d_join0_max=(\d+) d_join_T_max=(\d+) \| escort order4=(\d+) delay_mean=(-?[\d.]+) delay_max=(-?[\d.]+) s '
                r'ringed_late order4=(\d+) delay_mean=(-?[\d.]+) delay_max=(-?[\d.]+) s \| at the launch hold=(\d+) on_track=(\d+) '
                r'd_join_max=(-?\d+) inbound=(\d+) \| off at the launch: inbound=(\d+) hold_after_T=(\d+) unreachable=(\d+) other=(\d+) \| '
                r'at the order-4 chance: ok=(\d+) short=(\d+) \(max (\d+) m\)')


def agg(path):
    out = OrderedDict()
    for line in open(path, errors='replace'):
        m = B7.search(line)
        if m:
            out.setdefault(m.group(1).strip(), new())['appr'] += int(m.group(4))
            continue
        m = B8.search(line)
        if not m:
            continue
        g = m.groups()
        a = out.setdefault(g[0].strip(), new())
        a['runs'] += 1
        for k, i in (('tracked', 3), ('ok', 4), ('o4j', 5), ('miss', 6), ('gap', 7), ('gone', 9), ('unreach', 10), ('hold', 19), ('on', 20),
                     ('inbound', 22), ('c_inb', 23), ('c_late', 24), ('c_unr', 25), ('c_oth', 26), ('ch_ok', 27), ('ch_short', 28)):
            a[k] += int(g[i])
        a['ch_max'] = max(a['ch_max'], int(g[29]))
        a['gap_short'] = max(a['gap_short'], int(g[8]))
        a['dj0'] = max(a['dj0'], int(g[11]))
        a['djT'] = max(a['djT'], int(g[12]))
        a['dj_launch'] = max(a['dj_launch'], int(g[21]))
        for p, i in (('all', 13), ('rl', 16)):
            n = int(g[i])
            a[p + '_n'] += n
            a[p + '_sum'] += float(g[i + 1]) * n if n else 0.0
            a[p + '_max'] = max(a[p + '_max'], float(g[i + 2]))
    return out


def new():
    return {'runs': 0, 'appr': 0, 'tracked': 0, 'ok': 0, 'o4j': 0, 'miss': 0, 'gap': 0, 'gap_short': 0, 'gone': 0, 'unreach': 0, 'dj0': 0,
            'djT': 0, 'hold': 0, 'on': 0, 'dj_launch': -1, 'inbound': 0, 'all_n': 0, 'all_sum': 0.0, 'all_max': -1.0, 'rl_n': 0,
            'rl_sum': 0.0, 'rl_max': -1.0, 'c_inb': 0, 'c_late': 0, 'c_unr': 0, 'c_oth': 0, 'ch_ok': 0, 'ch_short': 0, 'ch_max': 0}


def mean(a, p):
    return '%5.1f' % (a[p + '_sum'] / a[p + '_n']) if a[p + '_n'] else '   -1'


def main():
    before, after = agg(sys.argv[1]), agg(sys.argv[2])
    print('variant      runs | escort order 4 after the launch: n mean max s | ringed late: n mean max s | 0x15 approach frames after '
          'the launch   (before -> after B.8)')
    for tag, a in after.items():
        b = before[tag]
        print('%-12s %4d | %3d->%-3d %s->%s %5.1f->%-5.1f | %3d->%-3d %s->%s %5.1f->%-5.1f | %6d->%-6d' % (
            tag, a['runs'], b['all_n'], a['all_n'], mean(b, 'all'), mean(a, 'all'), b['all_max'], a['all_max'], b['rl_n'], a['rl_n'],
            mean(b, 'rl'), mean(a, 'rl'), b['rl_max'], a['rl_max'], b['appr'], a['appr']))
    print('variant      | escorts at the launch: in hold, on track (d_join <= 300 m), max d_join m, inbound | join assertion: tracked ok '
          'order4 gap (max short m) miss unreach; max d_join m at the first frame / at T   (before -> after)')
    for tag, a in after.items():
        b = before[tag]
        print('%-12s | %2d->%-2d %2d->%-2d %5d->%-5d %2d->%-2d | %2d %2d->%-2d %d->%d %d->%d (%d->%d) %2d->%-2d %d->%d; %d->%d / %d->%d' % (
            tag, b['hold'], a['hold'], b['on'], a['on'], b['dj_launch'], a['dj_launch'], b['inbound'], a['inbound'], a['tracked'], b['ok'],
            a['ok'], b['o4j'], a['o4j'], b['gap'], a['gap'], b['gap_short'], a['gap_short'], b['miss'], a['miss'], b['unreach'],
            a['unreach'], b['dj0'], a['dj0'], b['djT'], a['djT']))
    print('variant      | off track or not in hold at the launch, by cause: inbound / hold after T / unreachable from the hold entry / other '
          '(before -> after)')
    for tag, a in after.items():
        b = before[tag]
        print('%-12s | %d->%d / %d->%d / %d->%d / %d->%d' % (tag, b['c_inb'], a['c_inb'], b['c_late'], a['c_late'], b['c_unr'], a['c_unr'],
                                                         b['c_oth'], a['c_oth']))
    if len(sys.argv) > 3:
        nolead = agg(sys.argv[3])
        print('the target lead (kStageLeadMs + the step), no lead -> lead: tracked escorts at T + step: ok, gap (max short m), missed | at the '
              'first order-4 chance: ok, short (max m) | escort order 4 after the launch: mean max s')
        for tag, a in after.items():
            n = nolead[tag]
            print('%-12s | %2d->%-2d %d->%d (%d->%d) %d->%d | %2d->%-2d %d->%d (%d->%d) | %s->%s %5.1f->%-5.1f' % (
                tag, n['ok'], a['ok'], n['gap'], a['gap'], n['gap_short'], a['gap_short'], n['miss'], a['miss'], n['ch_ok'], a['ch_ok'],
                n['ch_short'], a['ch_short'], n['ch_max'], a['ch_max'], mean(n, 'all'), mean(a, 'all'), n['all_max'], a['all_max']))


if __name__ == '__main__':
    main()
