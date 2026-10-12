"""Before/after table of amendment 14 (the slip-aware law) on the calibrated fixture, 60 fps seed 1: the vanilla leaf, the old
law with the slip term (the model-calibration output of af5ec4cf) and the slip-aware law, per scenario and class, with the
hard threshold each figure is now asserted against (over all nine fps x seed runs; ship_steering_host.cpp).
Usage (repository root): python3 -I verification/results/ship-steering/slip_law_table.py
  old  = git show af5ec4cf:verification/results/ship-steering/ship_steering_host.txt
  new  = verification/results/ship-steering/ship_steering_host.txt
  van  = verification/results/ship-steering/ship_steering_host_vanilla_rows.txt (ship_steering_host --vanilla-rows)"""
import re
import subprocess

R = 'verification/results/ship-steering/'
CLS = ['M5', 'M3', 'M6', 'TS', 'M2']


def num(line, key):
    m = re.search(r'\b' + re.escape(key) + r'=\s*(-?[\d.]+)', line)
    return float(m.group(1)) if m else None


def vanilla_of(line, key):
    m = re.search(re.escape(key) + r'=\s*-?[\d.]+(?: deg| m/s)? \(vanilla (-?[\d.]+)\)', line)
    return float(m.group(1)) if m else None


def parse(lines):
    t = {}
    for l in lines:
        w = l.split()
        if not w:
            continue
        if w[0] == 'formation':
            t[(w[1], 'slot')] = num(l, 'slot_rms')
            t[(w[1], 'winc')] = num(l, 'window_increases')
            t[(w[1], '2r')] = num(l, '2r')
        elif w[0] in ('flyby@150', 'flyby@600'):
            t[(w[1], w[0])] = num(l, 'min_clear')
        elif w[0] == 'crossing':
            t[(w[1], 'cross')] = num(l, 'min_dist')
            u = re.search(r'unresolved=(\d+)/(\d+)', l)
            t[(w[1], 'cross_unres')] = int(u.group(1)) + int(u.group(2))
        elif l.startswith('gate theta'):
            k = 'g' + w[2]
            t[(w[3], k)] = num(l, 'heading_err')
            t[(w[3], k + '_van')] = vanilla_of(l, 'heading_err')
            t[(w[3], k + '_vline')] = num(l, 'vel_line')
            m = re.search(r'vel_line=\s*[\d.]+ deg \(vanilla ([\d.]+)\)', l)
            t[(w[3], k + '_vline_van')] = float(m.group(1)) if m else None
        elif l.startswith('gate spread'):
            t[(w[2], 'spread')] = num(l, 'max_heading_err')
            t[(w[2], 'spread_vline')] = num(l, 'max_vel_line')
        elif l.startswith('gate far/rot'):
            t[(w[2], 'far')] = num(l, 'heading_err')
            t[(w[2], 'far_vline')] = num(l, 'vel_line')
        elif l.startswith('gate lateral'):
            t[(w[2], 'lat')] = num(l, 'heading_err')
            t[(w[2], 'lat_van')] = vanilla_of(l, 'heading_err')
            t[(w[2], 'lat_vline')] = num(l, 'vel_line')
    return t


def f(x, nd=1):
    return '-' if x is None else ('%.*f' % (nd, x))


def main():
    old = parse(subprocess.run(['git', 'show', 'af5ec4cf:' + R + 'ship_steering_host.txt'], capture_output=True, text=True, check=True).stdout.splitlines())
    new = parse(open(R + 'ship_steering_host.txt').read().splitlines())
    van = parse(open(R + 'ship_steering_host_vanilla_rows.txt').read().splitlines())
    C = {'M5': (234.2, 43.4), 'M3': (236.9, 48.8), 'M6': (296.6, 161.0), 'TS': (263.5, 102.0), 'M2': (1258.1, 1763.5)}
    print('| Scenario | Class | Vanilla | Old law + slip | Slip-aware law | Hard threshold (9 runs) |')
    print('| --- | --- | --- | --- | --- | --- |')
    for c in CLS:
        thr = '<= 30 m (floor x 1.2)' if c == 'M5' else '<= 2r = %.1f m' % new[(c, '2r')]
        print('| formation slot RMS (m) | %s | %s | %s | %s | %s |' % (c, f(van.get((c, 'slot'))), f(old.get((c, 'slot'))), f(new.get((c, 'slot'))), thr))
    for c in CLS:
        print('| formation window increases | %s | %s | %s | %s | 0 |' % (c, f(van.get((c, 'winc')), 0), f(old.get((c, 'winc')), 0), f(new.get((c, 'winc')), 0)))
    for c in CLS:
        print('| flyby@150 min clearance (m) | %s | %s | %s | %s | >= C = %.1f |' % (c, f(van.get((c, 'flyby@150'))), f(old.get((c, 'flyby@150'))),
                                                                                 f(new.get((c, 'flyby@150'))), C[c][0]))
    print('| flyby@600 min clearance (m) | M2 | %s | %s | %s | >= C = %.1f |' % (f(van.get(('M2', 'flyby@600'))), f(old.get(('M2', 'flyby@600'))),
                                                                              f(new.get(('M2', 'flyby@600'))), C['M2'][0]))
    for c in CLS:
        print('| crossing min distance (m) | %s | %s | %s | %s | >= C = %.1f |' % (c, f(van.get((c, 'cross'))), f(old.get((c, 'cross'))), f(new.get((c, 'cross'))), C[c][1]))
    for c in CLS:
        print('| crossing unresolved ticks (a+b) | %s | - | %s | %s | side flips 0 |' % (c, f(old.get((c, 'cross_unres')), 0), f(new.get((c, 'cross_unres')), 0)))
    for th, lim in (('-45', None), ('+0', None), ('+45', {'M5': 27, 'M3': 24, 'M6': 24, 'TS': 22})):
        for c in CLS[:4]:
            lm = 22 if lim is None else lim[c]
            print('| gate theta %s nose to B at the crossing (deg) | %s | %s | %s | %s | <= %d |' % (th, c, f(new.get((c, 'g' + th + '_van'))), f(old.get((c, 'g' + th))),
                                                                                              f(new.get((c, 'g' + th))), lm))
    for c in CLS[:4]:
        print('| gate (theta any) velocity to the line at the crossing (deg) | %s | %s | not measured | %s | <= %s |' % (
            c, f(new.get((c, 'g+0_vline_van'))), f(new.get((c, 'g+0_vline'))), '22' if c == 'M5' else '13'))
    for c in CLS[:4]:
        print('| gate spread max nose to pass point (deg) | %s | - | %s | %s | <= 24 |' % (c, f(old.get((c, 'spread'))), f(new.get((c, 'spread')))))
    for c in CLS[:4]:
        print('| gate spread max velocity to the line (deg) | %s | - | not measured | %s | <= %s |' % (c, f(new.get((c, 'spread_vline'))), '33' if c == 'M5' else '20.7'))
    for c in CLS[:4]:
        print('| gate far/rotated nose (deg) | %s | - | %s | %s (velocity %s) | <= 22 (velocity <= 13) |' % (c, f(old.get((c, 'far'))), f(new.get((c, 'far'))),
                                                                                                     f(new.get((c, 'far_vline')))))
    for c in CLS[:4]:
        print('| gate lateral 3 r nose (deg) | %s | %s | %s | %s (velocity %s) | <= 22 (velocity <= 13) |' % (c, f(new.get((c, 'lat_van'))), f(old.get((c, 'lat'))),
                                                                                                       f(new.get((c, 'lat'))), f(new.get((c, 'lat_vline')))))


if __name__ == '__main__':
    main()
