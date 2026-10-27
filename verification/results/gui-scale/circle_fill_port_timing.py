#!/usr/bin/env python3
"""Host timing of the oversized-disc port's span walk (src/proxy/circle_fill_sites.h fill()), without the engine leaves.

  python3 -I verification/results/gui-scale/circle_fill_port_timing.py > verification/results/gui-scale/circle_fill_port_timing.txt

Compiles the header with the host C++ compiler (-O2; x86-64, so 64-bit arithmetic in one register: the i686 DLL
splits it into register pairs, inferred slower by a small factor) and times fill() with a leaf that only sums its
arguments, per call, over 2000 calls. The engine leaves' pixel stores are the same work the stock routine does for a
correct disc and are not included. No Wine, no game.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HARNESS = r'''
#include "circle_fill_sites.h"
#include <chrono>
#include <cstdio>
using namespace x3m::circle_fill::sites;
int main() {
    struct Case { const char* name; int cx, cy, r, w, h; };
    const Case cases[] = {{"r=1633 centred in the 1664x1664 map texture (clipped)", 832, 832, 1633, 1664, 1664},
                          {"r=1633 inside a 6000x6000 texture (unclipped)", 3000, 3000, 1633, 6000, 6000},
                          {"r=2400 centred in the 1664x1664 map texture (clipped)", 832, 832, 2400, 1664, 1664},
                          {"r=826 inside a 6000x6000 texture (unclipped)", 3000, 3000, 826, 6000, 6000}};
    for (const Case& c : cases) {
        std::int64_t B = 0;
        if (!semi_axis(std::int64_t(c.r) * c.r, 65536, &B)) return 1;
        volatile long sink = 0;
        unsigned long calls = 0;
        const int repeats = 2000;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < repeats; ++i)
            fill(c.cx, c.cy, c.r, B, c.w, c.h, [&](int x0, int y, int x1) { ++calls; sink = sink + x1 - x0 + y; });
        const auto t1 = std::chrono::steady_clock::now();
        const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / repeats;
        std::printf("%s: %.2f us per call, %lu leaf calls per call\n", c.name, us, calls / repeats);
    }
    return 0;
}
'''

compiler = shutil.which('clang++') or shutil.which('c++')
with tempfile.TemporaryDirectory(prefix='x3-circle-fill-timing-') as directory:
    source, executable = Path(directory) / 'timing.cpp', Path(directory) / 'timing'
    source.write_text(HARNESS)
    subprocess.run([compiler, '-std=c++17', '-O2', '-I', str(ROOT / 'src/proxy'), str(source), '-o', str(executable)], check=True)
    version = subprocess.run([compiler, '--version'], capture_output=True, text=True).stdout.splitlines()[0]
    cpu = next((line.split(':', 1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name')), '?')
    print(f'compiler: {version}; cpu: {cpu}; -O2 x86-64 host build')
    sys.stdout.flush()
    subprocess.run([str(executable)], check=True)
