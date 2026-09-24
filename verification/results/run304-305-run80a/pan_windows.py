"""Run 79 A Q2: per 300-frame window (frame//300, matching gpu_sync_timing windows), camera_rotation_deg per frame from
motion_output_frame: median, p90, fraction of frames > 0.05 deg, and camera_cut count; classifies window as rest (p90 < 0.02
deg), pan (frac>0.05deg >= 0.5) or mixed. usage: pan_windows.py RUN..."""
import glob, re, sys, collections, statistics as st
for run in sys.argv[1:]:
    log = sorted(glob.glob(f'/tmp/x3-bottleX3-run{run}/session-*.log'))[0]
    w = collections.defaultdict(list); cut = collections.Counter()
    for l in open(log, errors='replace'):
        if l.startswith('motion_output_frame'):
            m = re.search(r' frame=(\d+)', l); r = re.search(r' camera_rotation_deg=(\S+)', l); c = re.search(r' camera_cut=(\d)', l)
            if not (m and r): continue
            k = int(m.group(1)) // 300 + 1; w[k].append(float(r.group(1))); cut[k] += c and c.group(1) == '1'
    print(f'run{run}')
    for k in sorted(w):
        v = sorted(w[k]); p90 = v[int(0.9 * (len(v) - 1))]; fr = sum(x > 0.05 for x in v) / len(v)
        cls = 'rest' if p90 < 0.02 else 'pan' if fr >= 0.5 else 'mixed'
        print(f'  w{k:<3} n={len(v)} rot_med={st.median(v):.4f} rot_p90={p90:.4f} frac>0.05={fr:.2f} cuts={cut[k]} {cls}')
