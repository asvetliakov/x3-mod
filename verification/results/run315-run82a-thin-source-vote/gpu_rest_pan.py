"""Run 79 A Q2 aggregate: joins gpu_taa.py per-window taa stage timings with pan_windows.py classes; per session and class
(rest/pan/mixed, windows with TAA resolved n>=100 only) prints the median over windows of each stage's window median and p90 (us).
usage: python3 gpu_rest_pan.py (reads gpu_taa_out.txt, pan_windows_out.txt)"""
import re, collections, statistics as st
cls = {}; run = None
for l in open('pan_windows_out.txt'):
    if l.startswith('run'): run = l.split()[0]; continue
    m = re.match(r'\s+w(\d+).* (rest|pan|mixed)$', l); cls[(run, int(m.group(1)))] = m.group(2)
agg = collections.defaultdict(lambda: collections.defaultdict(list))
for l in open('gpu_taa_out.txt'):
    if l.startswith('run'): run = l.split()[0]; continue
    k = int(re.match(r'\s+w(\d+)', l).group(1))
    for p, a, b, n in re.findall(r'(taa\w*)=(\d+)/(\d+)/(\d+)', l):
        if int(n) >= 100: agg[(run, cls[(run, k)])][p].append((int(a), int(b)))
for (r, c), d in sorted(agg.items()):
    print(f'{r} {c:5} windows={len(d["taa"])} ' + ' '.join(f'{p}={st.median(x for x, y in v):.0f}/{st.median(y for x, y in v):.0f}' for p, v in sorted(d.items())))
