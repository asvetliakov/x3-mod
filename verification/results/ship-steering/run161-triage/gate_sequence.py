"""Run159 triage: the capital crossings and launches per gate (ship_gate_approach capital=1: the first row of a pair is the
crossing of X, the second the launch with cross_to_launch_ms), with game time (ship_trace t_ms of that frame) and real time
(frame_end elapsed_ms), and the interval to the previous launch at the same gate.
Usage: python3 -I gate_sequence.py <extracted rows>"""
import re, sys
from collections import defaultdict
KV = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
tms = {}; fe = {}; ap = []; gate_of = {}
with open(sys.argv[1], errors='replace') as f:
    for l in f:
        if l.startswith('ship_trace frame='):
            m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) .*? tgt=(\S+) tgt_class=18 ', l)
            if m:
                tms.setdefault(int(m.group(1)), int(m.group(2)))
            else:
                m2 = re.match(r'ship_trace frame=(\d+) t_ms=(\d+) ', l); tms.setdefault(int(m2.group(1)), int(m2.group(2)))
        elif l.startswith('frame_end '):
            d = KV(l); fe[int(d['frame'])] = int(d['elapsed_ms'])
        elif l.startswith('ship_gate_approach ') and ' capital=1 ' in l:
            ap.append(KV(l))
        elif l.startswith('ship_avoid_e_verdict ') and 'A_order=0x5 ' in l and 'B_class=18 ' in l:
            d = KV(l); gate_of.setdefault(d['A'], d['B'])
keys = sorted(tms)
def tg(fr):
    import bisect
    i = bisect.bisect_left(keys, fr); i = min(i, len(keys) - 1); return tms[keys[i]] / 1e3
by = defaultdict(list)
for d in ap: by[d['obj']].append(d)
prev = None
print('obj class | cross frame t_game real_s heading_err cross_v | launch frame t_game cross_to_launch_s | game s since previous launch')
for o, ds in sorted(by.items(), key=lambda kv: int(kv[1][0]['frame'])):
    c, l = ds[0], ds[-1]
    fl = int(l['frame']); t = tg(fl)
    print('%s %s | %s %.1f %.1f %s %s | %d %.1f %.1f | %s' % (o, c['class'], c['frame'], tg(int(c['frame'])), fe.get(int(c['frame']), 0) / 1e3,
          c['cross_heading_err_deg'], c['cross_v_ms'], fl, t, float(l['cross_to_launch_ms']) / 1e3, '%.1f' % (t - prev) if prev else '-'))
    prev = t
