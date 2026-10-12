"""Host cost of core::steer() and av::resolve() (420 obstacles) per call: the slip-aware law (the working tree's headers) against
the old law (af5ec4cf's ship_steering_core.h / ship_avoidance_core.h), steer_cost_bench.cpp built twice with c++ -O2 (x86-64
host, not the i686 DLL: a relative figure), each run 5 times; prints the medians.
Usage (repository root): python3 -I verification/results/ship-steering/steer_cost_bench.py"""
import re
import statistics
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def run(exe):
    out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout
    return {k: float(v) for k, v in re.findall(r'^(\S+(?:\(\d+\))?) ns/call ([\d.]+)', out, re.M)}


def main():
    with tempfile.TemporaryDirectory(prefix='x3-steer-bench-') as d:
        d = Path(d)
        old = d / 'old'
        old.mkdir()
        for h in ('ship_steering_core.h', 'ship_avoidance_core.h'):
            (old / h).write_text(subprocess.run(['git', 'show', 'af5ec4cf:src/proxy/' + h], capture_output=True, text=True, check=True,
                                                cwd=ROOT).stdout)
        src = HERE / 'steer_cost_bench.cpp'
        builds = {'old (af5ec4cf)': ['-I', str(old)], 'slip-aware': ['-DNEWAPI', '-I', str(ROOT / 'src/proxy')]}
        for name, flags in builds.items():
            exe = d / ('bench_' + name.split()[0].replace('-', '_'))
            subprocess.run(['c++', '-std=c++17', '-O2', *flags, str(src), '-o', str(exe)], check=True)
            runs = [run(exe) for _ in range(5)]
            print('%-16s ' % name + '  '.join('%s %.1f ns' % (k, statistics.median(r[k] for r in runs)) for k in runs[0]))


if __name__ == '__main__':
    main()
