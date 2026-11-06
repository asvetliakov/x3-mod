#!/usr/bin/env python3
"""Builds and runs cockpit_half.cpp (host model, production stage headers) over a set of cockpit eyes.
Usage: python3 run_cockpit_half.py [eye_x eye_y eye_z pitch lane roof_h near ...]"""
import pathlib, re, shutil, subprocess, sys, tempfile
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
src = (ROOT / 'verification/analysis/test_weapon_effects_core.py').read_text()
stub = re.search(r"D3D9_STUB = r'''\n(.*?)'''", src, re.S).group(1)
tmp = pathlib.Path(tempfile.mkdtemp())
(tmp / 'd3d9.h').write_text(stub)
cxx = shutil.which('clang++') or shutil.which('g++')
exe = tmp / 'cockpit_half'
subprocess.run([cxx, '-std=c++17', '-O2', '-I', str(tmp), '-I', str(ROOT / 'src/proxy'), str(HERE / 'cockpit_half.cpp'), '-o', str(exe)], check=True)
cases = [sys.argv[1:]] if len(sys.argv) > 1 else [
    ['0', '20', '300', '0', '0', '0', '1'], ['0', '40', '450', '0', '0', '0', '1'], ['0', '0', '0', '0', '0', '0', '1'],
    ['0', '20', '300', '10', '0', '0', '1'], ['0', '20', '300', '-10', '0', '0', '1'], ['30', '-20', '200', '5', '0', '0', '1']]
for c in cases:
    out = subprocess.run([str(exe)] + c, capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        if line.startswith(('RESULT', 'DRAWS', 'DRAW ', 'BOXCUT', 'ROW', 'CUTWHY', 'FACEROWS')): print(line)
