#!/usr/bin/env python3
"""AP behaviour check for the FL dust leak fix port: compiles src/proxy/capture.cpp and loader.cpp twice with build-ap's
flags (build-ap/CMakeFiles/d3d9.dir/flags.make and includes_CXX.rsp), once from --base (default HEAD) and once from
the working tree, and compares the objdump disassembly (debug sections and line numbers excluded). Prints one line per
source and the sha256 of build-ap's dust_leak_fix.cpp.obj (no source or header it includes is changed by the port).

usage: python3 -I verification/results/fl-dust-leak-fix/ap_objects_identical.py --base main
"""
import argparse
import hashlib
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / 'build-ap'
parser = argparse.ArgumentParser()
parser.add_argument('--base', default='HEAD')
args = parser.parse_args()
flags = (BUILD / 'CMakeFiles/d3d9.dir/flags.make').read_text()
defines = shlex.split(re.search(r'^CXX_DEFINES = (.*)$', flags, re.M).group(1))
cxxflags = shlex.split(re.search(r'^CXX_FLAGS = (.*)$', flags, re.M).group(1))
includes = shlex.split((BUILD / 'CMakeFiles/d3d9.dir/includes_CXX.rsp').read_text())
lines = []
with tempfile.TemporaryDirectory(prefix='x3-ap-dust-') as tmp:
    for name in ('capture.cpp', 'loader.cpp'):
        def disassemble(src, obj):
            subprocess.run(['i686-w64-mingw32-g++', *defines, f'-I{ROOT}/src/proxy', *includes, *cxxflags,
                            '-std=gnu++17', '-c', src, '-o', obj], check=True, cwd=BUILD)
            text = subprocess.run(['i686-w64-mingw32-objdump', '-d', '--no-show-raw-insn', obj], capture_output=True,
                                  text=True, check=True).stdout
            return text.splitlines()[2:]
        base_src = Path(tmp) / 'base' / name
        base_src.parent.mkdir(exist_ok=True)
        base_src.write_text(subprocess.run(['git', '-C', str(ROOT), 'show', f'{args.base}:src/proxy/{name}'],
                                           capture_output=True, text=True, check=True).stdout)
        old = disassemble(str(base_src), str(Path(tmp) / f'base_{name}.o'))
        new = disassemble(str(ROOT / 'src/proxy' / name), str(Path(tmp) / f'head_{name}.o'))
        lines.append(f'{name} {"identical" if old == new else "DIFFERENT"} {len(old)} disassembly lines '
                     f'(base {args.base}, head working tree)')
obj = BUILD / 'CMakeFiles/d3d9.dir/src/proxy/dust_leak_fix.cpp.obj'
lines.append('dust_leak_fix.cpp.obj sha256 ' + (hashlib.sha256(obj.read_bytes()).hexdigest() if obj.exists() else 'absent'))
print('\n'.join(lines))
sys.exit(0 if all(' identical ' in line for line in lines[:2]) else 1)
