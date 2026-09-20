#!/usr/bin/env python3
"""Build only: actual media gate x86 CPU/default-pool fixture. Never invokes Wine."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/media-presentation-gate'
FLAGS = ('-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-fno-exceptions',
         '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
         '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX')

def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / 'media_presentation_gate_fixture.exe'
    sources = ('verification/probe/media_presentation_gate_cpu_fixture.cpp',
               'src/proxy/media_presentation_gate.cpp',
               'src/proxy/media_presentation_gate_win32.cpp')
    subprocess.run(['i686-w64-mingw32-g++', *FLAGS,
                    *(str(ROOT / source) for source in sources),
                    '-static', '-static-libgcc', '-static-libstdc++', '-ld3d9',
                    '-o', str(exe)], check=True, cwd=ROOT)
    return {'binary': str(exe), 'runtime': 'not run',
            'scope': 'actual gate CPU/state + fault injection + synthetic helper/native DEFAULT-pool Reset'}

if __name__ == '__main__':
    print(json.dumps(build(), indent=2))
