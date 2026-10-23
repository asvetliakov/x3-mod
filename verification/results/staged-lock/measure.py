"""Host cost of the staged Lock (docs/architecture/bolt-footprint.md, "Staged Lock").

Builds the test_bolt_footprint harness, runs its `stage` mode RUNS times and
writes the per-run cost rows and their medians to staged-lock.json beside this
script. Host memory is cacheable for both paths: the old direct scan here does
not reproduce the native write-combined read; it is the floor of the old path.

    PYTHONPATH=verification/probe:verification/analysis /usr/bin/python3 verification/results/staged-lock/measure.py
"""
import json
import platform
import statistics
import subprocess
import sys
from pathlib import Path

import test_bolt_footprint as t

RUNS = 9
rows = {}
for _ in range(RUNS):
    for line in t.Harness.run('stage').splitlines():
        parts = line.split()
        if parts[1] != 'cost':
            continue
        f = dict(p.split('=', 1) for p in parts[2:])
        rows.setdefault(f['vertices'], []).append({k: float(v) for k, v in f.items()})
compiler = subprocess.run([t.shutil.which('clang++') or t.shutil.which('c++'), '--version'], capture_output=True, text=True).stdout.splitlines()[0]
out = {'runs': RUNS, 'host': platform.platform(), 'compiler': compiler, 'flags': '-std=c++17 -O2', 'lock_bytes': 147456,
       'median_ns': {n: {k: statistics.median(r[k] for r in rs) for k in ('direct_scan_ns', 'staged_copy_ns', 'staged_scan_ns', 'staged_total_ns')}
                     for n, rs in rows.items()}}
Path(__file__).with_name('staged-lock.json').write_text(json.dumps(out, indent=1) + '\n')
json.dump(out['median_ns'], sys.stdout, indent=1)
print()
