"""Run158: the 45 engine evasions (ship_avoid_engine_warn rows) with the subject's class and its order before the stop, and the
warn_pre entry of the frame before (other class/code/order, d, r, E's verdict).
Usage: LC_ALL=C grep -aE '^(ship_trace frame=|ship_avoid_engine_warn)' /mnt/c/X3/x3m.log > w.txt; python3 -I warn_subjects.py w.txt"""
import re, sys, collections as C
cls = {}; last = {}; prev_order = {}; W = []; P = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.search(r'frame=(\d+) t_ms=\d+ obj=(\S+) class=(\S+) order=(\S+) tgt=(\S+)', l)
        o = m.group(2); cls[o] = m.group(3)
        if m.group(4) not in ('0x0', '0x12'): last[o] = (m.group(4), m.group(5))
    elif l.startswith('ship_avoid_engine_warn_pre'):
        d = dict(re.findall(r"(\w+)=([^ \n]+)", l.split(' entries_')[0]))
        P[(int(d['frame']), d['obj'])] = [x.split(':') for x in l.rstrip().split('=')[-1].split('|')]
        prev_order[(int(d['frame']), d['obj'])] = last.get(d['obj'], ('?', '?'))
    elif l.startswith('ship_avoid_engine_warn '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); W.append(d)
rows = C.Counter(); D = []
for d in W:
    f = int(d['frame']); o = d['obj']; k = (f - 1, o)
    if k not in P: rows[(cls.get(o, '?'), '?', '-', '-', '-')] += 1; continue
    po = prev_order[k]
    for e in P[k]:
        rows[(cls.get(o, '?'), 'subj_prev_order=%s' % po[0], 'other=%s/%s' % (e[1], e[2]), 'other_order=%s' % e[3], 'other_is_prev_target=%d' % (e[0] == po[1]), e[8])] += 1
        if e[1] in ('18', '5'): D.append((cls.get(o, '?'), e[1], int(e[0] == po[1]), float(e[6]), float(e[7])))
for k, n in sorted(rows.items(), key=lambda x: -x[1]): print(n, *k)
print('gate/station warns: subject class, other class, other is its target, d_m, r_other_m')
for x in sorted(D): print('  %s %s tgt=%d d=%.0f r=%.0f' % x)
