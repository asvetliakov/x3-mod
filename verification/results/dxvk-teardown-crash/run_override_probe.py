#!/usr/bin/env python3
"""One hand-started state_hook_benchmark `device proxy` case (the runner's proxy-timing-off
environment) with `--dll "d3d9=n,b;winedbg.exe=d"`: does a teardown crash still start winedbg?
Run as: X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 <this> <case-dir>
<case-dir> holds state_hook_benchmark.exe and the proxy d3d9.dll (a runner case directory).
Prints the exit code, the proxy's exception row and whether winedbg text appeared."""
import os, subprocess, sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import bottle  # noqa: E402
import run_state_hook_benchmark as r  # noqa: E402
d = Path(sys.argv[1]).resolve()
env = dict(os.environ, **r.BASE_ENV, **r.ROUTE_ENV, X3M_FRAME_TIMING='0')
env.pop('X3M_STATE_SHADOW', None)
log = d / 'x3m.log'
if log.exists(): log.rename(d / f'x3m.log.prev-{int(time.time())}')
cmd = [bottle.WINE] + bottle.wine_args() + ['--workdir', str(d), '--dll', 'd3d9=n,b;winedbg.exe=d',
                                            str(d / 'state_hook_benchmark.exe'), 'device', 'proxy']
t = time.time()
p = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=600)
print(f'exit={p.returncode} elapsed_s={time.time()-t:.1f}')
out = p.stdout + p.stderr
for key in ('WineDbg attached', 'Unhandled exception', 'Unhandled page fault', 'winedbg', 'RESULT ', 'disabled'):
    rows = [l for l in out.splitlines() if key in l]
    print(f'{key!r}: {len(rows)}', *(rows[:2]), sep='\n  ' if rows else ' ')
if log.exists():
    for l in log.read_text(errors='replace').splitlines():
        if l.startswith(('exception ', 'device_destroy', 'motion_output_release', 'device_hooked')): print('x3m:', l[:200])
