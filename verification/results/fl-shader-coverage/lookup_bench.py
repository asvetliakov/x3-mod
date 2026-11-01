#!/usr/bin/env python3
"""Shader-setter lookup cost before and after part 4b (host x86-64 micro-benchmark).

Builds lookup_bench.cpp (-O2 -msse2) against the base commit's src/renderer
(git archive into the scratch directory) and the working tree, runs both and
writes lookup-bench.json beside this script: ns per (pair contract + hull
program) lookup, best of 5 runs of 2e6 calls. Host timing, not game FPS.

usage: lookup_bench.py <scratch dir> [--base 531d0f70]
"""
import argparse
import json
import platform
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def run(renderer, executable):
    subprocess.run(['g++', '-std=c++17', '-O2', '-msse2', '-I', str(renderer), str(HERE / 'lookup_bench.cpp'),
                    *(str(renderer / name) for name in ('linear_material.cpp', 'material_motion.cpp',
                                                        'linear_emission.cpp')), '-o', str(executable)], check=True)
    out = subprocess.run([str(executable)], capture_output=True, text=True, check=True).stdout
    return {name: float(value) for name, value in (line.split() for line in out.splitlines())}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('scratch', type=Path)
    parser.add_argument('--base', default='531d0f70')
    arguments = parser.parse_args()
    scratch = arguments.scratch.resolve()
    if ROOT in scratch.parents or scratch == ROOT:
        raise SystemExit('keep the scratch directory outside the repository')
    base = scratch / 'bench-base'
    subprocess.run(['rm', '-rf', str(base)], check=True)
    base.mkdir(parents=True)
    archive = subprocess.run(['git', '-C', str(ROOT), 'archive', arguments.base, 'src/renderer'],
                             capture_output=True, check=True).stdout
    subprocess.run(['tar', '-x', '-C', str(base)], input=archive, check=True)
    result = {'producer': str(Path(__file__).relative_to(ROOT)), 'base_commit': arguments.base,
              'host': platform.machine(), 'unit': 'ns per pair-contract + hull-program lookup, best of 5 x 2e6',
              'base': run(base / 'src/renderer', scratch / 'bench_base'),
              'tree': run(ROOT / 'src/renderer', scratch / 'bench_tree')}
    (HERE / 'lookup-bench.json').write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps(result))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
