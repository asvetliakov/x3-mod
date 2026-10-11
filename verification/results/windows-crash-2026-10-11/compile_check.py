#!/usr/bin/env python3
"""Compile-only check of the dust-leak counter fix without the installed EXE or Wine (2026-10-11, WSL2 host).

1. Builds the Wine fixture through build_dust_leak_fix_patch.build() with the window bytes taken from the header's
   twin (verify_dust_leak_fix_site.WINDOW) instead of the EXE; -Werror, the fixture's own audit runs. Not executed.
2. Compiles src/proxy/dust_leak_fix.cpp with the DLL's flags and counts warnings and x87 mnemonics in the object.
Usage: python3 verification/results/windows-crash-2026-10-11/compile_check.py
"""
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import build_dust_leak_fix_patch as build  # noqa: E402
import verify_dust_leak_fix_site as verifier  # noqa: E402

build.window_bytes = lambda: verifier.WINDOW
record = build.build()
print(json.dumps({'fixture': record['binary'], 'toolchain': record['toolchain'], 'audit_seam': record['audit']['module_calls_seam']}))
with tempfile.TemporaryDirectory() as directory:
    obj = Path(directory) / 'dust_leak_fix.o'
    run = subprocess.run(['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Wno-cast-function-type', '-msse2',
                          '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX',
                          '-Isrc/proxy', '-Isrc', '-c', 'src/proxy/dust_leak_fix.cpp', '-o', str(obj)],
                         cwd=ROOT, capture_output=True, text=True)
    warnings = len(re.findall(r'warning:', run.stderr))
    dis = subprocess.run(['i686-w64-mingw32-objdump', '-d', str(obj)], capture_output=True, text=True).stdout if run.returncode == 0 else ''
    x87 = len(re.findall(r'\tf(ld|st|stp|ild|istp|add|mul|sub|div|xch|chs|abs)\w*\s', dis))
    print(json.dumps({'module_compile_exit': run.returncode, 'warnings': warnings, 'x87_mnemonics': x87}))
