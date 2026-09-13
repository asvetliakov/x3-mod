#!/usr/bin/env python3
"""Cross-compile the real BloomPass fixture; never starts Wine or the game."""
from pathlib import Path
import argparse
import subprocess
ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-msse2', '-mfpmath=sse',
         '-mstackrealign', '-mincoming-stack-boundary=2', '-static', '-DX3M_BLOOM_PASS_FIXTURE']

def build(directory):
    directory.mkdir(parents=True, exist_ok=True)
    exe = directory / 'bloom_pass_fixture.exe'
    command = ['i686-w64-mingw32-g++', *FLAGS, str(ROOT/'verification/probe/bloom_pass_fixture.cpp'),
               str(ROOT/'src/renderer/bloom_pass.cpp'), '-o', str(exe), '-luser32', '-ldxguid']
    subprocess.run(command, cwd=ROOT, check=True)
    return exe, command

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, default=ROOT/'verification/probe/build/bloom-pass')
    print(build(parser.parse_args().output_dir)[0])
