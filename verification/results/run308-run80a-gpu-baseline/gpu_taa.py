"""Run 79 A Q2: per 300-frame window, gpu_sync_timing median/p90 (us) of every taa* pass (+ n) and the frame dt median/p90,
for the --gpu-sync-timing sessions. usage: gpu_taa.py RUN..."""
import glob, re, sys, collections
for run in sys.argv[1:]:
    log = sorted(glob.glob(f'/tmp/x3-bottleX3-run{run}/session-*.log'))[0]
    w = collections.OrderedDict(); passes = set()
    for l in open(log, errors='replace'):
        if not l.startswith('gpu_sync_timing window='): continue
        d = dict(t.split('=', 1) for t in l.split()[1:] if '=' in t)
        row = w.setdefault(int(d['window']), {'frames': d['frames'], 'dt': (int(d['dt_median_us']), int(d['dt_p90_us']))})
        if d['pass'].startswith('taa') or d['pass'] in ('scene', 'present'):
            row[d['pass']] = (int(d['median_us']), int(d['p90_us']), int(d['n'])); passes.add(d['pass'])
    order = sorted(passes)
    print(f'run{run} columns: window frames dt_med/p90 | ' + ' '.join(order) + '  (each median/p90/n us)')
    for k, r in w.items():
        print(f"  w{k:<3} {r['frames']:>11} {r['dt'][0]/1e3:6.2f}/{r['dt'][1]/1e3:6.2f} | " + ' '.join(
            f"{p}={'/'.join(map(str, r[p]))}" if p in r else f'{p}=-' for p in order))
