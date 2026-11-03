#!/usr/bin/env python3
"""Builds and runs the FL pause key and run in background native fixtures (verification/probe/fl_pause_key_only.cpp,
fl_run_in_background.cpp) exactly as verification/analysis/test_fl_pause_key_only.py and test_fl_run_in_background.py
do, and writes fixture_out.txt beside this script: each fixture's summary and log rows, normalised so a rerun is
byte-stable (run-dependent addresses become 0x<addr>, image bases <base>). The summary lines go to stdout. Needs MinGW
i686 and WSL interop (a native Windows run); no game, no Wine.

usage: python3 -I verification/results/fl-pause-background/run_fixtures.py
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/analysis'))
import test_fl_pause_key_only as pause  # noqa: E402
import test_fl_run_in_background as background  # noqa: E402


def normalise(text):
    text = re.sub(r'site=0x[0-9a-f]{8}', lambda m: m.group(0) if m.group(0) == 'site=0x00000000' else 'site=0x<addr>',
                  text)
    return re.sub(r'base=[0-9a-f]{8}', 'base=<base>', text)


parts, codes = [], []
for name, module in (('fl_pause_key_only', pause), ('fl_run_in_background', background)):
    with tempfile.TemporaryDirectory(prefix='x3-fl-pb-') as tmp:
        exe = module.build(tmp)
        if exe is None:
            print(f'{name}: SKIP (needs i686-w64-mingw32-g++ and WSL interop)')
            sys.exit(2)
        run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=300)
    codes.append(run.returncode)
    parts.append(f'# {name} exit={run.returncode}\n' + normalise(run.stdout))
    for line in run.stdout.splitlines():
        if line.startswith(('SWEEP', 'FLIPS', 'PARITY', 'DECIDE', 'GATE', 'UNFOCUSED', 'RESULT', 'FAIL')):
            print(f'{name}: {line}')
(Path(__file__).resolve().parent / 'fixture_out.txt').write_text(''.join(parts))
sys.exit(0 if not any(codes) else 1)
