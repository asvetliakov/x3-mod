"""Scenario T, B.6 (gate-traffic-controller.md B.6, Run166 span tuning): per variant over its 9 runs (3 fps x 3 seeds) the head's release
after the funnel frees, the escorts waiting after their leader's launch and the holders' lap speed, from the host fixture's "B.6:" rows.
Usage: python3 -I scenario_t_b6.py <ship_steering_host stdout> [<label>]
The before-B.6 figures (scenario_t_b6.txt) come from the B.6 fixture source adapted by scenario_t_b6_pre_build.py and built against
the pre-B.6 core (git show 73cea4aa:src/proxy/ship_gate_ctl_core.h beside this commit's other proxy headers), run with --traffic; its
"B.6:" rows are ship_steering_host_b6_pre_rows.txt. The adaptations, exactly: sb_b6_checks() and sb_b6_review_checks() stubbed empty
(their units need lap_law, release_window, petal_near, escort_settled, esc_in10); the fixture's 4-argument lap_speed call shimmed to the
old 5-argument one with kEscortStageS (the before "above the lap law" column is against the old law); m.esc_in10 read as false (no
first-pass phase in the old core: every post-launch frame counts as before the pass, and the before "within 9.7 km without the pass"
column is not meaningful). Everything else, scenario T and its B.6 rows included, is the after build's source."""
import re
import sys
from collections import OrderedDict

ROW = re.compile(r'traffic T (.{12}) fps=\s*(\d+) seed=(\d+) B\.6: (.*)$')
KV = re.compile(r'([a-z_0-9]+)=(-?[0-9.]+)')


def runs(path):
    for line in open(path, errors='replace'):
        m = ROW.search(line)
        if not m:
            continue
        rest = m.group(4)
        # the row has repeated keys (released, n, mean, at25, at60) per section: split on '|'
        secs = [dict(KV.findall(p)) for p in rest.split('|')]
        free, win, esc = secs[0], secs[1], secs[2]
        h = re.search(r'escorts n=(\d+) mean=(-?[0-9.]+) at25=(\d+) at60=(\d+) committed n=(\d+) mean=(-?[0-9.]+) at25=(\d+) at60=(\d+)', rest)
        pre = re.search(r'leader released frames=(\d+) v_max=(-?[0-9.]+) over25=(\d+), leader holding frames=(\d+) v_max=(-?[0-9.]+) '
                        r'over25=(\d+), above_law=(\d+) approach=(\d+)', rest)
        yield m.group(1).strip(), int(m.group(2)), int(m.group(3)), free, win, esc, h, pre


def main():
    path = sys.argv[1]
    label = sys.argv[2] if len(sys.argv) > 2 else path
    agg = OrderedDict()
    for tag, fps, seed, free, win, esc, h, pre in runs(path):
        a = agg.setdefault(tag, {'runs': 0, 'free_eps': 0, 'free_rel': 0, 'wait_sum': 0.0, 'wait_max': 0.0, 'win_eps': 0, 'win_rel': 0,
                                 'lat_max': 0.0, 'late': 0, 'missed': 0, 'deferred': 0, 'esc_frames': 0, 'esc_over': 0, 'esc_vmax': -1.0,
                                 'appr': 0, 'hold': [0, 0.0, 0, 0, 0, 0.0, 0, 0], 'pre': None, 'post': None})
        a['runs'] += 1
        a['free_eps'] += int(free['free_eps'])
        rel = int(free['released'])
        a['free_rel'] += rel
        a['wait_sum'] += float(free['mean']) * rel if rel else 0.0
        a['wait_max'] = max(a['wait_max'], float(free['max']))
        a['win_eps'] += int(win['in_window_eps'])
        a['win_rel'] += int(win['released'])
        a['lat_max'] = max(a['lat_max'], float(win['max']))
        a['late'] += int(win['late'])
        a['missed'] += int(win['missed'])
        a['deferred'] += int(win['deferred'])
        a['esc_frames'] += int(esc['escort_window_frames'])
        a['esc_over'] += int(esc['over25'])
        a['esc_vmax'] = max(a['esc_vmax'], float(esc['v_max']))
        a['appr'] += int(esc['approach_frames'])
        if 'post_pass_frames' in esc:
            q = a['post'] or [0, 0, 0, 0, -1.0, 0, 0]
            a['post'] = [q[0] + int(esc['approach_frames']), q[1] + int(esc['approach_below60']), q[2] + int(esc['pre_pass_lap_frames']),
                         q[3] + int(esc['post_pass_frames']), max(q[4], float(esc['post_pass_v_max'])), q[5] + int(esc['post_pass_over25']),
                         q[6] + int(esc['flag_late'])]
        if pre:
            g = pre.groups()
            pa = a['pre'] or [0, -1.0, 0, 0, -1.0, 0, 0, 0]
            a['pre'] = [pa[0] + int(g[0]), max(pa[1], float(g[1])), pa[2] + int(g[2]), pa[3] + int(g[3]), max(pa[4], float(g[4])),
                        pa[5] + int(g[5]), pa[6] + int(g[6]), pa[7] + int(g[7])]
        if h:
            en, em, e25, e60, cn, cm, c25, c60 = h.groups()
            hh = a['hold']
            hh[0] += int(en)
            hh[1] += float(em) * int(en) if int(en) else 0.0
            hh[2] += int(e25)
            hh[3] += int(e60)
            hh[4] += int(cn)
            hh[5] += float(cm) * int(cn) if int(cn) else 0.0
            hh[6] += int(c25)
            hh[7] += int(c60)
    print('== %s' % label)
    print('variant      runs | funnel free->release: eps released mean max s | free in window: eps released latency_max s late unreleased '
          'deferred | escorts waiting in W (lap): frames >25 v_max; 0x15 approach frames | holders v_sb: escorts mean %@25 %@60; '
          'committed mean %@25 %@60')
    for tag, a in agg.items():
        hh = a['hold']
        em = hh[1] / hh[0] if hh[0] else -1
        cm = hh[5] / hh[4] if hh[4] else -1
        print('%-12s %4d | %3d %3d %6.1f %6.1f | %3d %3d %5.2f %d %d %d | %5d %d %5.1f; %6d | %5.1f %3.0f %3.0f; %5.1f %3.0f %3.0f' % (
            tag, a['runs'], a['free_eps'], a['free_rel'], a['wait_sum'] / a['free_rel'] if a['free_rel'] else -1, a['wait_max'], a['win_eps'],
            a['win_rel'], a['lat_max'], a['late'], a['missed'], a['deferred'], a['esc_frames'], a['esc_over'], a['esc_vmax'], a['appr'], em,
            100.0 * hh[2] / hh[0] if hh[0] else -1, 100.0 * hh[3] / hh[0] if hh[0] else -1, cm, 100.0 * hh[6] / hh[4] if hh[4] else -1,
            100.0 * hh[7] / hh[4] if hh[4] else -1))
    if any(a['post'] for a in agg.values()):
        print('escorts after the launch (fresh writes): 0x15 approach before the first pass inside 10 km: frames below-60 | lap before '
              'it: frames | after it: frames v_max >25 | within 9.7 km without the pass noted')
        for tag, a in agg.items():
            if a['post']:
                print('%-12s %6d %d | %6d | %6d %5.1f %d | %d' % ((tag,) + tuple(a['post'])))
    if any(a['pre'] for a in agg.values()):
        print('pre-launch escorts in their window: leader released: frames v_max >25 | leader holding (no target): frames v_max >25 | '
              'above the lap law | 0x15 approach')
        for tag, a in agg.items():
            if a['pre']:
                print('%-12s %6d %5.1f %d | %6d %5.1f %6d | %d | %d' % ((tag,) + tuple(a['pre'])))


if __name__ == '__main__':
    main()
