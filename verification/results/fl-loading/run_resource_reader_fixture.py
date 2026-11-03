#!/usr/bin/env python3
"""Producer of the FL resource reader / DAT pool fixture figures in docs/verification/farnhams-legacy.md ("resource
reader and DAT handles on FL (host evidence, 2026-11-03)") and docs/reverse-engineering/fl/resource-reader.md ("Port
(2026-11-03)"): builds verification/probe/fl_resource_reader.cpp exactly as test_fl_loading.FlResourceReaderNative does
(MinGW i686, the project's SSE2 / four-byte incoming stack flags), runs it natively through WSL interop on the installed
X3FL.exe with a temporary copy of its zlib1.dll (no Wine, no game), and prints the summary lines (the LOG rows are
dropped). Runs it `--runs` times for the timing rows.

usage: python3 verification/results/fl-loading/run_resource_reader_fixture.py [--runs 3]
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
sys.path.insert(0, str(ROOT / 'verification/analysis'))
import test_fl_loading as t  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--runs', type=int, default=1)
    args = parser.parse_args()
    if not t.available() or not t.EXE.exists():
        print('needs i686-w64-mingw32-g++, WSL interop and the installed X3FL.exe')
        return 2
    status = 0
    with tempfile.TemporaryDirectory(prefix='x3-fl-reader-') as tmp:
        exe = t.build_reader(tmp)
        for run in range(args.runs):
            r = subprocess.run([str(exe), t.windows_path(t.EXE), t.windows_path(Path(tmp) / 'zlib1.dll')],
                               capture_output=True, text=True, timeout=300)
            lines = [l for l in r.stdout.splitlines() if not l.startswith(('LOG', 'IMAGE'))]
            if run:
                lines = [l for l in lines if l.startswith(('PERF', 'RESULT'))]
            print(f'# run {run + 1} exit={r.returncode}')
            print('\n'.join(lines))
            status |= r.returncode
    return status


if __name__ == '__main__':
    sys.exit(main())
