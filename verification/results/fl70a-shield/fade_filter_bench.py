#!/usr/bin/env python3
"""FL 70 A: same-session CPU A/B of fill_region before (84a2ccaaa) and after the per-cell fade filter.

  python3 verification/results/fl70a-shield/fade_filter_bench.py build
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/results/fl70a-shield/fade_filter_bench.py run

Work files under build-fl70a-shield/ (untracked); the base headers come from `git archive 84a2ccaaa src/proxy src/renderer`.
"""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
WORK = ROOT / 'build-fl70a-shield'
SRC = ROOT / 'verification/results/fl70a-shield/fade_filter_bench.cpp'
FLAGS = ['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-msse2', '-mfpmath=sse', '-mstackrealign',
         '-mincoming-stack-boundary=2', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX']


def build() -> None:
    base = WORK / 'base'
    base.mkdir(parents=True, exist_ok=True)
    archive = subprocess.run(['git', '-C', str(ROOT), 'archive', '84a2ccaaa', 'src/proxy', 'src/renderer'],
                             check=True, capture_output=True).stdout
    subprocess.run(['tar', '-x', '-C', str(base)], input=archive, check=True)
    objs = []
    for name, inc, extra in (('bench_base', base / 'src', ['-Dx3m=x3m_base']), ('bench_new', ROOT / 'src', [])):
        obj = WORK / f'{name}.o'
        subprocess.run([*FLAGS, *extra, f'-DBENCH_NAME={name}', '-I', str(inc), '-c', str(SRC), '-o', str(obj)], check=True)
        objs.append(str(obj))
    subprocess.run([*FLAGS, '-DBENCH_MAIN', str(SRC), *objs, '-static', '-o', str(WORK / 'fade_filter_bench.exe')],
                   check=True)
    print('built', WORK / 'fade_filter_bench.exe')


def run() -> None:
    print(subprocess.run([str(WORK / 'fade_filter_bench.exe')], capture_output=True, text=True, check=True).stdout, end='')


if __name__ == '__main__':
    build() if sys.argv[1] == 'build' else run()
