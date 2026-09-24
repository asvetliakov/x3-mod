"""Run 80 A Q3: per session, frame_end dt_ms per frame; per 300-frame window (pan_windows.py class) the median/p95 dt;
aggregate by class (median of window medians / p95s, windows excl first); spikes = frames with dt > 2x the session median
(excluding frames before the first TAA-resolved frame's window, i.e. w1), listed with the nearest lod_switch frame and
camera_cut. usage: frame_rest_pan.py RUN... (reads pan_windows_out.txt)"""
import glob, re, sys, collections, statistics as st
cls = {}; run = None
for l in open('pan_windows_out.txt'):
    if l.startswith('run'): run = l.split()[0]; continue
    m = re.match(r'\s+w(\d+).* (rest|pan|mixed)$', l); cls[(run, int(m.group(1)))] = m.group(2)
for r in sys.argv[1:]:
    log = sorted(glob.glob(f'/tmp/x3-bottleX3-run{r}/session-*.log'))[0]; dt = {}; sw = []; cut = set(); rss = []
    for l in open(log, errors='replace'):
        if l.startswith('frame_end device=1'):
            m = re.search(r' frame=(\d+) .* dt_ms=(\d+)', l); dt[int(m.group(1))] = int(m.group(2))
        elif l.startswith('lod_switch '):
            sw.append((int(re.search(r'frame=(\d+)', l).group(1)), re.search(r'body=(\S+)', l).group(1).split('\\')[-1], re.search(r' D=(\d+)', l).group(1)))
        elif l.startswith('motion_output_frame') and ' camera_cut=1' in l:
            cut.add(int(re.search(r' frame=(\d+)', l).group(1)))
    run = f'run{r}'; win = collections.defaultdict(list)
    for f, d in dt.items(): win[f // 300 + 1].append(d)
    agg = collections.defaultdict(list)
    for k, v in sorted(win.items()):
        if k == 1 or len(v) < 300: continue
        v = sorted(v); agg[cls.get((run, k), '?')].append((st.median(v), v[int(0.95 * (len(v) - 1))]))
    print(run, ' '.join(f'{c}: windows={len(x)} dt p50 {st.median(a for a, b in x):.1f} p95 {st.median(b for a, b in x):.1f} ms' for c, x in sorted(agg.items())))
    body = [d for f, d in dt.items() if f >= 300]; med = st.median(body)
    sp = [(f, d) for f, d in sorted(dt.items()) if f >= 300 and d > 2 * med]
    print(f'  session dt median {med} ms over {len(body)} frames (frame>=300); spikes >2x: {len(sp)}; >100 ms: {sum(d > 100 for f, d in sp)}')
    for f, d in sorted(sp, key=lambda x: -x[1])[:15]:
        near = min(sw, key=lambda s: abs(s[0] - f)) if sw else None
        print(f'   frame {f} dt {d} ms class={cls.get((run, f // 300 + 1))} cut_within5={any(abs(c - f) <= 5 for c in cut)}' + (f' nearest_lod_switch={near[0]} ({near[0]-f:+d}) {near[1]} D={near[2]}' if near else ''))
    if sw:
        first = {}
        for f, b, D in sw: first.setdefault(b, (f, D))
        for b, (f, D) in sorted(first.items(), key=lambda x: x[1][0]):
            around = [dt.get(g, 0) for g in range(f - 3, f + 4)]
            print(f'  first lod_switch {b} frame {f} D={D} dt[-3..+3]={around}')
