#!/usr/bin/env python3
"""Builds and runs the FL texture-hook native fixture (verification/probe/d3dx_texture_hook_fixture.cpp) the way
verification/analysis/test_fl_material_strength_hook.py does and writes its stdout to fixture_out.txt beside this file.
Needs i686-w64-mingw32-g++ and WSL interop. Usage: python3 -I run_fixture.py
"""
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'verification/analysis'))
import test_fl_material_strength_hook as test  # noqa: E402

with tempfile.TemporaryDirectory(prefix='x3-fl-texture-hook-') as tmp:
    result = test.build_and_run(tmp)
if result is None:
    sys.exit('needs i686-w64-mingw32-g++ and WSL interop')
(HERE / 'fixture_out.txt').write_text(result.stdout)
print(result.stdout.splitlines()[-1] if result.stdout else 'no output', f'rc={result.returncode}')
sys.exit(result.returncode)
