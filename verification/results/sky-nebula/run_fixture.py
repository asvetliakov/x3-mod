#!/usr/bin/env python3
"""Builds and runs the nebula part deband native D3D9 fixture (verification/probe/sky_nebula_fixture.cpp) with the
production pass (src/renderer/sky_nebula_pass.cpp, X3M_SKY_NEBULA_FIXTURE) and writes its stdout to fixture_out.txt beside
this file. Needs i686-w64-mingw32-g++ and WSL interop (a native Windows run, no game). Usage:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 -I verification/results/sky-nebula/run_fixture.py
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
with tempfile.TemporaryDirectory(prefix='x3-sky-nebula-fixture-') as tmp:
    exe = Path(tmp) / 'sky_nebula_fixture.exe'
    subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-mstackrealign',
                    '-mincoming-stack-boundary=2', '-msse2', '-mfpmath=sse', '-DX3M_SKY_NEBULA_FIXTURE', '-static',
                    str(ROOT / 'verification/probe/sky_nebula_fixture.cpp'), str(ROOT / 'src/renderer/sky_nebula_pass.cpp'),
                    '-ld3d9', '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], text=True, capture_output=True, timeout=900)
(HERE / 'fixture_out.txt').write_text(result.stdout)
print(result.stdout.splitlines()[-1] if result.stdout else 'no output', f'rc={result.returncode}')
sys.exit(result.returncode)
