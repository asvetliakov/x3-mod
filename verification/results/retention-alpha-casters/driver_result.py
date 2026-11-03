#!/usr/bin/env python3
"""Compile and run the retention store driver of test_shadow_retention and print its RESULT line
(failures and executed CHECK count). Run from the repository root; host C++ compiler, no Wine."""
import shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/analysis')); sys.path.insert(0, str(ROOT / 'verification/probe'))
import test_shadow_retention as t  # noqa: E402
compiler = shutil.which('c++') or shutil.which('g++')
with tempfile.TemporaryDirectory() as d:
    (Path(d) / 'driver.cpp').write_text(t.DRIVER)
    subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'), str(Path(d) / 'driver.cpp'), '-o', str(Path(d) / 'driver')], check=True)
    out = subprocess.run([str(Path(d) / 'driver')], capture_output=True, text=True).stdout
print(next(line for line in out.splitlines() if line.startswith('RESULT')))
