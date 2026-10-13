"""Amendment 17 (capital-gate-transit.md section 17.1): scenario M before (HEAD's tracked fixture_capital.txt, amendments 16 + 16b)
and after (the working tree's), from the fixture's own printed rows: the staggered variants (60 fps seed 1 rows), the
simultaneous aggregates over 9 runs, M-c / M-d / M-b / un-latch rows, K and the check total.

Usage (from the repository root):
    git show <before>:verification/results/ship-steering/capital-gate-transit/fixture_capital.txt > /tmp/before.txt
    python3 -I verification/results/ship-steering/capital-crowd/a17_m_ab.py /tmp/before.txt \\
        verification/results/ship-steering/capital-gate-transit/fixture_capital.txt
"""
import re
import sys

FIELDS = ('arrived', 'kept', 'slow_cross', 'fb_cross', 'fb_frames', 'rot_slow_max', 'min_box_sep', 'detours a/b/c/none', 'lane_low',
          'detours_c_apart', 'arrive')
KV = re.compile(r'([A-Za-z_/<>.-]+(?: [a-z/]+)?)=(-?[0-9][0-9./-]*)')


def rows(path):
    with open(path) as f:
        for line in f:
            yield line.rstrip('\n')


def first_row(lines, tag):
    for line in lines:
        if line.startswith('crowd M ' + tag) and 'fps= 60 seed=1:' in line:
            return dict(KV.findall(line.split(':', 1)[1]))
    return {}


def main(before, after):
    out = []
    for label, path in (('before', before), ('after', after)):
        lines = list(rows(path))
        out.append(f'== {label}: {path}')
        for tag in ('w 0.45 r ', 'w 0.25 r ', 'w = r (unreadable)', 'M-c abreast', 'M-d aT>aL', 'M-d aT<aL', 'unlatch ', 'same lane (b)'):
            d = first_row(lines, tag)
            out.append(f'  staggered/sub {tag.strip():20s} 60 fps s1: ' + ' '.join(f'{k}={d[k]}' for k in FIELDS if k in d))
        for line in lines:
            if line.startswith('crowd M16b over') or line.startswith('crowd M17 over'):
                out.append('  ' + line)
            elif line.startswith('crowd M unlatch:') or line.startswith('dock-leg pair K') or line.startswith('ship_steering_host checks='):
                out.append('  ' + line)
            elif line.startswith('crowd M:'):
                out.append('  ' + line)
    print('\n'.join(out))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
