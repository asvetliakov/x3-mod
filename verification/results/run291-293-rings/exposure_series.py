"""run291-293 (and run289/290 for the burst frames): per-frame auto-exposure from the hdr_frame rows.
Fields: ev (applied), ev_adapted, ev_target, ev_fresh, avg_log_l, luma_p99, stepped. Reports, over frames with a lit meter
(tiles>0, frame >= 300), the ev range, per-frame |d ev| quantiles, the share of frames where ev moves (|d ev| > 1e-4), the
longest still run, and the mean |d ev| while moving. Also the ev of any capture frames (frame_end capture=1).
usage: exposure_series.py <session.log> [...]"""
import re, sys
K = ('ev', 'ev_adapted', 'ev_target', 'ev_fresh', 'avg_log_l', 'luma_p99', 'stepped', 'tiles')
for path in sys.argv[1:]:
    rows, cap = {}, set()
    for line in open(path, errors='replace'):
        if line.startswith('hdr_frame device=1 '):
            d = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
            try: rows[int(d['frame'])] = {k: float(d[k]) for k in K}
            except (KeyError, ValueError): pass
        elif line.startswith('frame_end device=1 ') and ' capture=1 ' in line:
            cap.add(int(re.search(r' frame=(\d+)', line).group(1)))
    fr = [f for f in sorted(rows) if f >= 300 and rows[f]['tiles'] > 0]
    ev = [rows[f]['ev'] for f in fr]
    dv = [abs(b - a) for a, b in zip(ev, ev[1:])]
    q = lambda a, p: sorted(a)[min(len(a) - 1, int(p * len(a)))] if a else float('nan')
    moving = [x for x in dv if x > 1e-4]
    run, best = 0, 0
    for x in dv:
        run = run + 1 if x <= 1e-4 else 0; best = max(best, run)
    tg = [rows[f]['ev_target'] for f in fr]
    print(f"{path.split('/')[-2]} frames={len(fr)} ev min/p50/max {min(ev):.3f}/{q(ev,.5):.3f}/{max(ev):.3f} "
          f"target min/max {min(tg):.3f}/{max(tg):.3f} |dev| p50/p90/p99/max {q(dv,.5):.5f}/{q(dv,.9):.5f}/{q(dv,.99):.5f}/{max(dv):.5f} "
          f"moving {100*len(moving)/max(1,len(dv)):.1f}% mean|dev|moving {sum(moving)/max(1,len(moving)):.5f} longest_still {best} "
          f"avg_log_l p10/p90 {q([rows[f]['avg_log_l'] for f in fr],.1):.2f}/{q([rows[f]['avg_log_l'] for f in fr],.9):.2f}")
    # 10 windows of the flight: ev range per window
    n = len(fr); w = max(1, n // 10)
    print('  per-tenth ev min..max:', ' '.join(f"{min(ev[i:i+w]):.2f}..{max(ev[i:i+w]):.2f}" for i in range(0, n, w)))
    if cap:
        print('  capture frames ev:', ' '.join(f"{f}:{rows[f]['ev']:.4f}" for f in sorted(cap) if f in rows))
