#!/usr/bin/env python3
"""Builds and runs the FL UI scale native fixture (verification/probe/fl_ui_scale.cpp) exactly as
verification/analysis/test_fl_ui_scale.py does (full run, then the late-window run), and writes fixture_out.txt beside
this script: the fixture's log rows and summaries, normalised so a rerun is byte-stable (the private image base and the
fake camera pointer become <addr>). Then the `timing` mode, whose raw TIMING line (diagnostic cost, not game FPS) is
printed to stdout only. Needs MinGW i686 and WSL interop (a native Windows run); no game, no Wine.

usage: python3 -I verification/results/fl-ui-scale/run_fixture.py
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/analysis'))
import test_fl_ui_scale as t  # noqa: E402

with tempfile.TemporaryDirectory(prefix='x3-fl-ui-') as tmp:
    exe = t.build(tmp)
    runs = [subprocess.run([str(exe), *mode], text=True, capture_output=True, timeout=300) for mode in ([], ['late'])]
    timing = subprocess.run([str(exe), 'timing'], text=True, capture_output=True, timeout=300)


def normalise(text):
    text = re.sub(r'0x[0-9a-f]{8}', '0x<addr>', text)
    return re.sub(r'(excluded_camera|previous)=(?!00000000)[0-9a-f]{8}', r'\1=<addr>', text)


out = Path(__file__).resolve().parent / 'fixture_out.txt'
out.write_text(''.join(f'# run {name}\n' + normalise(r.stdout) + f'# exit={r.returncode}\n'
                       for name, r in zip(('full', 'late'), runs)))
for r in runs:
    for line in r.stdout.splitlines():
        if line.startswith(('RESULT', 'FAIL')):
            print(line)
    if not r.stdout:
        print(r.stderr[-500:])
print(next((l for l in timing.stdout.splitlines() if l.startswith('TIMING')), timing.stderr[-300:]))
sys.exit(max(r.returncode for r in runs + [timing]))
