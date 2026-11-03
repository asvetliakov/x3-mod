#!/usr/bin/env python3
"""AP behaviour check for the FL circle_fill_fix port: compiles src/proxy/capture.cpp and loader.cpp twice with
build-ap's flags (build-ap/CMakeFiles/d3d9.dir/flags.make and includes_CXX.rsp), once from --base (default HEAD) and
once from the working tree, and compares the objdump disassembly. Also compiles src/proxy/circle_fill.cpp (AP's
module, with its per-file options) from both and compares, and checks that the AP source list in CMakeLists.txt is the
same at --base and in the working tree. Same shape as verification/results/fl-dust-leak-fix/ap_objects_identical.py.

usage: python3 -I verification/results/fl-circle-fill/ap_objects_identical.py --base main
"""
import argparse
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
per_file = {'circle_fill.cpp': ['-fno-exceptions', '-mno-sse', '-mno-mmx', '-mfpmath=387']}


def show(path):
    return subprocess.run(['git', '-C', str(ROOT), 'show', f'{args.base}:{path}'], capture_output=True, text=True,
                          check=True).stdout


lines = []
with tempfile.TemporaryDirectory(prefix='x3-ap-circle-') as tmp:
    for name in ('capture.cpp', 'loader.cpp', 'circle_fill.cpp'):
        def disassemble(src, obj):
            subprocess.run(['i686-w64-mingw32-g++', *defines, f'-I{ROOT}/src/proxy', *includes, *cxxflags,
                            *per_file.get(name, []), '-std=gnu++17', '-c', src, '-o', obj], check=True, cwd=BUILD)
            text = subprocess.run(['i686-w64-mingw32-objdump', '-d', '--no-show-raw-insn', obj], capture_output=True,
                                  text=True, check=True).stdout
            return text.splitlines()[2:]
        base_src = Path(tmp) / 'base' / name
        base_src.parent.mkdir(exist_ok=True)
        base_src.write_text(show(f'src/proxy/{name}'))
        old = disassemble(str(base_src), str(Path(tmp) / f'base_{name}.o'))
        new = disassemble(str(ROOT / 'src/proxy' / name), str(Path(tmp) / f'head_{name}.o'))
        lines.append(f'{name} {"identical" if old == new else "DIFFERENT"} {len(old)} disassembly lines '
                     f'(base {args.base}, head working tree)')
ap_list = lambda text: re.search(r'set\(X3M_AP_SOURCES\n(.*?)\)\n', text, re.S).group(1)  # noqa: E731
same_list = ap_list(show('CMakeLists.txt')) == ap_list((ROOT / 'CMakeLists.txt').read_text())
lines.append(f'X3M_AP_SOURCES {"identical" if same_list else "DIFFERENT"}')
print('\n'.join(lines))
sys.exit(0 if all(' DIFFERENT' not in line for line in lines) else 1)
