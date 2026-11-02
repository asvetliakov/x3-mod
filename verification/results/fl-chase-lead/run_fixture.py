#!/usr/bin/env python3
"""Builds and runs the FL chase lead native fixture (verification/probe/fl_chase_lead.cpp) exactly as
verification/analysis/test_fl_chase_lead.py does, and writes fixture_out.txt beside this script: the fixture's
summary and log rows (the bit-flip sweep's refusal rows are counted by the fixture, not printed), normalised so a rerun
is byte-stable: run-dependent addresses become 0x<addr>, thread ids <tid>, microsecond figures <us>. The raw TIMING
line (the ledger's cost row) is printed to stdout. Needs MinGW i686 and WSL interop (a native Windows run); no game,
no Wine.

usage: python3 -I verification/results/fl-chase-lead/run_fixture.py
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/analysis'))
import test_fl_chase_lead as t  # noqa: E402

with tempfile.TemporaryDirectory(prefix='x3-fl-cl-') as tmp:
    exe, _ = t.build(tmp)
    run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=300)


def normalise(text):
    text = re.sub(r'0x[0-9a-f]{8}', '0x<addr>', text)
    text = re.sub(r'thread=\d+', 'thread=<tid>', text)
    return re.sub(r'(_us=)-?[0-9.]+', r'\1<us>', text)


out = Path(__file__).resolve().parent / 'fixture_out.txt'
out.write_text(normalise(run.stdout) + f'# exit={run.returncode}\n')
for line in run.stdout.splitlines():
    if line.startswith(('TIMING', 'RESULT')):
        print(line)
if not run.stdout:
    print(run.stderr[-500:])
sys.exit(run.returncode)
