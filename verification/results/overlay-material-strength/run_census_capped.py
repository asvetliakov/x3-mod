#!/usr/bin/env python3
"""Fleet class census (class_census_k3.py) one capped process at a time, in chunks of 50 bodies, stopping when the
host's available memory is below 3000 MB (the WSL host stalled on an uncapped fleet pass, 2026-10-25). Each chunk runs
as `systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 python3 class_census_k3.py ...`.
Usage (repo root): python3 run_census_capped.py GAME NAMES.txt OUT.tsv [CHUNK]"""
import subprocess
import sys
from pathlib import Path

game, names, out = sys.argv[1:4]
chunk = int(sys.argv[4]) if len(sys.argv) > 4 else 50
here = Path(__file__).resolve().parent
n = sum(1 for line in open(names) if line.strip())


def available_mb():
    for line in open('/proc/meminfo'):
        if line.startswith('MemAvailable:'):
            return int(line.split()[1]) // 1024
    return 0


start = 0
while start < n:
    avail = available_mb()
    if avail < 3000:
        print(f'stop: available {avail} MB < 3000 at body {start}', flush=True)
        sys.exit(2)
    end = min(start + chunk, n)
    cmd = ['systemd-run', '--user', '--scope', '-q', '-p', 'MemoryMax=3G', '-p', 'MemorySwapMax=0', 'python3',
           str(here / 'class_census_k3.py'), game, names, out, str(start), str(end)]
    rc = subprocess.call(cmd)
    if rc:
        print(f'chunk {start}-{end} failed rc={rc}', flush=True)
        sys.exit(3)
    print(f'chunk {start}-{end} done, available {avail} MB before it', flush=True)
    start = end
print('done', n, 'bodies')
