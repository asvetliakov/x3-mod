#!/usr/bin/env python3
"""Builds and runs the FL chase camera native fixture (verification/probe/fl_chase_camera.cpp) exactly as
verification/analysis/test_fl_chase_camera.py does, and writes fixture_out.txt beside this script: the fixture's
summary and log rows (the bit-flip sweep's refusal rows are counted by the fixture, not printed). Needs MinGW i686
and WSL interop (a native Windows run); no game, no Wine.

usage: python3 -I verification/results/fl-chase-camera/run_fixture.py
"""
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/analysis'))
import test_fl_chase_camera as t  # noqa: E402

with tempfile.TemporaryDirectory(prefix='x3-fl-cc-') as tmp:
    exe, _ = t.build(tmp)
    start = time.perf_counter()
    run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=120)
    seconds = time.perf_counter() - start
out = Path(__file__).resolve().parent / 'fixture_out.txt'
out.write_text(run.stdout + f'# exit={run.returncode} run_seconds={seconds:.2f}\n')
print(run.stdout.splitlines()[-1] if run.stdout else run.stderr[-500:])
sys.exit(run.returncode)
