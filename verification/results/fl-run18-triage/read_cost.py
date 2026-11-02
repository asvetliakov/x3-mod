#!/usr/bin/env python3
"""FL run 18 mitigation read costs measured natively: verification/probe/fl_run18_read_bench.cpp with the production
src/proxy/engine_memory.cpp (engine_memory built -mno-sse -mno-mmx -mfpmath=387 as in CMakeLists.txt, the bench with the
DLL's code flags), MinGW i686, run on the Windows host through WSL interop; five runs of 2,000,000 calls, medians printed.
Items 2 (pointer cache) and 3 (spin backoff, VirtualQuery outside the lock) were evaluated and not taken; their
numbers in perf_mitigations.txt came from the evaluation branch (8306b2d18, 0d1a9616c, unmerged).
Usage: python3 read_cost.py <scratch dir> [engine_memory.cpp to link, default the checkout's]
"""
import re
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CXX = 'i686-w64-mingw32-g++'
COMMON = ['-std=c++17', '-O2', '-mstackrealign', '-mincoming-stack-boundary=2', '-I', str(ROOT / 'src/proxy')]


def main():
    scratch = Path(sys.argv[1])
    source = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / 'src/proxy/engine_memory.cpp'
    scratch.mkdir(parents=True, exist_ok=True)
    memory = scratch / 'engine_memory.o'
    subprocess.run([CXX, *COMMON, '-mno-sse', '-mno-mmx', '-mfpmath=387', '-c', str(source), '-o', str(memory)], check=True)
    exe = scratch / 'fl_run18_read_bench.exe'
    subprocess.run([CXX, *COMMON, '-msse2', '-mfpmath=sse', '-static', str(ROOT / 'verification/probe/fl_run18_read_bench.cpp'),
                    str(memory), '-o', str(exe)], check=True)
    runs = {}
    for _ in range(5):
        out = subprocess.run([str(exe), '2000000'], check=True, capture_output=True, text=True).stdout
        for key, value in re.findall(r'(\w+_ns)=([0-9.]+)', out):
            runs.setdefault(key, []).append(float(value))
    print('engine_memory source: %s' % source)
    for key, values in runs.items():
        print('%s median=%.1f runs=%s' % (key, statistics.median(values), ','.join('%.1f' % v for v in values)))


if __name__ == '__main__':
    main()
