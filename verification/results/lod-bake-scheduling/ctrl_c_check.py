#!/usr/bin/env python3
"""Ctrl+C during the bake (macOS/Linux: SIGINT to the batch process, as the terminal sends it): the batch
exits within the bounded poll and leaves no worker process. Scratch --out only; reads the game read-only.

  python3 ctrl_c_check.py SCRATCH_DIR
"""
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
out = Path(sys.argv[1])
only = out / 'only.txt'
out.mkdir(parents=True, exist_ok=True)
only.write_text('\n'.join((ROOT / 'verification/results/lod-bake-scheduling/ab_subset.txt').read_text().split()[:12]))
proc = subprocess.Popen([sys.executable, str(ROOT / 'tools/analysis/lod_overlay.py'), '--batch', '--mod', 'none',
                         '--only', str(only), '--out', str(out / 'out')],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
for line in proc.stdout:
    if line.startswith('bake workers:'):
        print(line.strip())
        break
time.sleep(6)


def children():
    ps = subprocess.run(['ps', '-axo', 'pid=,ppid='], capture_output=True, text=True).stdout.split('\n')
    return [int(l.split()[0]) for l in ps if l.split() and int(l.split()[1]) == proc.pid]


before = children()
t0 = time.time()
proc.send_signal(signal.SIGINT)
proc.stdout.read()
code = proc.wait(60)
print(f'workers before SIGINT {len(before)}; exit {code} after {time.time() - t0:.1f} s;'
      f' workers still alive {sum(1 for p in before if subprocess.run(["kill", "-0", str(p)], capture_output=True).returncode == 0)}')
