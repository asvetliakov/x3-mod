"""Run 80 A Q3: dt spikes (> 2x session median, frame>=300) excluding F8 burst frames (frames with a shadow_map0 dump file),
with the loading_metric / loading rows within +-5 frames. usage: spikes_nonburst.py RUN..."""
import glob, re, sys, os, statistics as st
for r in sys.argv[1:]:
    d = f'/tmp/x3-bottleX3-run{r}'; log = sorted(glob.glob(f'{d}/session-*.log'))[0]
    burst = {int(re.search(r'_(\d+)\.r32f', f).group(1)) for f in os.listdir(d) if f.startswith('shadow_map0_')}
    dt = {}; kinds = {}
    for l in open(log, errors='replace'):
        if l.startswith('frame_end device=1'):
            m = re.search(r' frame=(\d+) .* dt_ms=(\d+)', l); dt[int(m.group(1))] = int(m.group(2)); cur = int(m.group(1))
        elif l.startswith(('loading', 'chase_native_timing_slow', 'mesh_cache', 'save_', 'game_phase')) and dt:
            kinds.setdefault(cur, set()).add(l.split()[0])
    med = st.median(v for f, v in dt.items() if f >= 300)
    sp = [(f, v) for f, v in sorted(dt.items()) if f >= 300 and v > 2 * med and f not in burst]
    print(f'run{r} burst frames {sorted(burst)} | non-burst spikes {len(sp)} (>100 ms {sum(v > 100 for f, v in sp)})')
    for f, v in sp: print(f'   frame {f} dt {v} ms rows_near={sorted(set().union(*[kinds.get(g, set()) for g in range(f-5, f+6)]))}')
