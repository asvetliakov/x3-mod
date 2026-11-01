#!/usr/bin/env python3
"""The own sun stage's per-frame CPU side measured natively: verification/probe/own_sun_host.cpp's `bench` (projection,
fog scalar, radiance, quad, pixel constants through src/proxy/own_sun_core.h) built with the DLL's code flags
(i686-w64-mingw32-g++ -O2 -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2) and run on the Windows host
through WSL interop; five runs of 1,000,000 frames, the median printed. Not game FPS, not the GPU draw.
Usage: python3 cpu_cost.py <scratch dir>
"""
import re
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def main():
    scratch = Path(sys.argv[1])
    scratch.mkdir(parents=True, exist_ok=True)
    exe = scratch / 'own_sun_bench.exe'
    subprocess.run(['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-msse2', '-mfpmath=sse', '-mstackrealign',
                    '-mincoming-stack-boundary=2', '-static', str(ROOT / 'verification/probe/own_sun_host.cpp'), '-o', str(exe)],
                   check=True)
    runs = []
    for _ in range(5):
        out = subprocess.run([str(exe), 'bench', '1000000'], check=True, capture_output=True, text=True).stdout
        runs.append(float(re.search(r'ns_per_frame=([0-9.]+)', out).group(1)))
    print('i686 native ns_per_frame runs=%s median=%.1f' % (','.join('%.1f' % r for r in runs), statistics.median(runs)))


if __name__ == '__main__':
    main()
