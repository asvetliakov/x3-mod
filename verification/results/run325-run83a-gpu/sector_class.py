"""Run 83 A launch 2 Q2-Q4: per run, sector (volumetric_fog_sector index active at window start) and pan class (pan_windows_out.txt),
median over windows (n>=100) of each pass's window median_us. usage: python3 sector_class.py 312 315 325"""
import glob, re, sys, collections, statistics as st
P = ['taa','taa_box','taa_resolve','taa_copy','taa_mask','taa_mask_tests','engine','shadow_depth','fog_route','bloom','sun_apply','hdr_writeback','scene']
cls = {}; run = None
for l in open('pan_windows_out.txt'):
    if l.startswith('run'): run = l.split()[0][3:]; continue
    m = re.match(r'\s+w(\d+).* (rest|pan|mixed)$', l)
    if m: cls[(run, int(m.group(1)))] = m.group(2)
for r in sys.argv[1:]:
    log = sorted(glob.glob(f'/tmp/x3-bottleX3-run{r}/session-*.log'))[0]
    sec = []; win = collections.defaultdict(dict)
    for l in open(log, errors='replace'):
        if l.startswith('volumetric_fog_sector '):
            sec.append((int(re.search(r'frame=(\d+)', l).group(1)), int(re.search(r'index=(-?\d+)', l).group(1))))
        elif l.startswith('gpu_sync_timing window='):
            m = re.match(r'gpu_sync_timing window=(\d+) pass=(\w+) median_us=(\d+) p90_us=\d+ n=(\d+)', l)
            if m and int(m.group(4)) >= 100: win[int(m.group(1))][m.group(2)] = int(m.group(3))
    groups = collections.defaultdict(lambda: collections.defaultdict(list))
    for w, d in win.items():
        start = (w - 1) * 300; s = [i for f, i in sec if f <= start]; s = s[-1] if s else None
        if s is None or s < 0: continue
        for p in P:
            if p in d: groups[(s, cls.get((r, w), '?'))][p].append(d[p])
    for (s, c), d in sorted(groups.items()):
        print(f'run{r} sector={s} {c:5} windows={len(d.get("taa", []))} ' + ' '.join(f'{p}={st.median(d[p]):.0f}' for p in P if d.get(p)))
