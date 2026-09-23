"""Read-path A/B under Wine (bottle X3): the object-lifetime fixture built from the pre-fix
reader (HEAD 5a9f45f8 engine_memory.{h,cpp} + object_lifetime.cpp) and from the fixed working
tree, run alternately three times each, one Wine command at a time through wine_lock.py.
Prints each run's TIMING (snapshot_us = 12 validated reads) and RESULT rows.

Build the two executables first (as build_object_lifetime.sh, sources from `git show 5a9f45f8:`
for the old one) into DIR as object_lifetime_old.exe / object_lifetime_new.exe, then:

    python3 verification/results/run288-exit-crash/engine_memory_read_path_ab.py DIR
"""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
WINE = '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'
directory = sys.argv[1]
env = dict(os.environ, X3M_FIXTURE_BOTTLE='X3')
for i in range(3):
    for variant in ('old', 'new'):
        run = subprocess.run(['python3', str(ROOT / 'verification/probe/wine_lock.py'), WINE, '--bottle', 'X3', '--no-update',
                              '--workdir', directory, f'{directory}/object_lifetime_{variant}.exe'],
                             cwd=ROOT, env=env, capture_output=True, text=True, timeout=300)
        for line in run.stdout.splitlines():
            if line.startswith(('TIMING', 'RESULT')):
                print(variant, i, line, flush=True)
