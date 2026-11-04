#!/usr/bin/env python3
"""Builds and runs the sky-star native D3D9 fixture (verification/probe/sky_stars_fixture.cpp) with the production pass
(src/renderer/sky_stars_pass.cpp, X3M_SKY_STARS_FIXTURE; src/proxy/engine_memory.cpp for the sector key cost row) and writes its stdout to fixture_out.txt beside this file.
Needs i686-w64-mingw32-g++ and WSL interop (a native Windows run, no game). Usage:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 -I verification/results/sky-stars/run_fixture.py
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
compiler = shutil.which('i686-w64-mingw32-g++')
if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
    sys.exit('needs i686-w64-mingw32-g++ and WSL interop')
with tempfile.TemporaryDirectory(prefix='x3-sky-stars-fixture-') as tmp:
    exe = Path(tmp) / 'sky_stars_fixture.exe'
    memory = Path(tmp) / 'engine_memory.o'  # the sector key cost row's reader, with its production flags (CMakeLists.txt)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-mstackrealign', '-mincoming-stack-boundary=2']
    subprocess.run([compiler, *common, '-mno-sse', '-mno-mmx', '-mfpmath=387', '-c',
                    str(ROOT / 'src/proxy/engine_memory.cpp'), '-o', str(memory)], check=True)
    subprocess.run([compiler, *common, '-msse2', '-mfpmath=sse', '-DX3M_SKY_STARS_FIXTURE', '-static',
                    str(ROOT / 'verification/probe/sky_stars_fixture.cpp'), str(ROOT / 'src/renderer/sky_stars_pass.cpp'),
                    str(memory), '-ld3d9', '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], text=True, capture_output=True, timeout=600)
(HERE / 'fixture_out.txt').write_text(result.stdout)
print(result.stdout.splitlines()[-1] if result.stdout else 'no output', f'rc={result.returncode}')
sys.exit(result.returncode)
