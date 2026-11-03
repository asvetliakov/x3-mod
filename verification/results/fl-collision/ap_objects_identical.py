#!/usr/bin/env python3
"""AP behaviour check for the FL collision-trio port: compiles the shared AP source the port touches
(src/proxy/capture.cpp; loader.cpp alongside as a control) twice with build-ap's flags
(build-ap/CMakeFiles/d3d9.dir/flags.make and includes_CXX.rsp; a configured build-ap is enough), once from --base
(default HEAD) and once from the working tree, and compares the objdump disassembly (debug sections and line numbers
excluded). The nine AP collision sources (src/proxy/collide_{box_cull,sat_sse2,memo}.cpp, .h and _core.h) are not
compiled but diffed: they must be byte-identical to --base. Prints one line per source.

usage: python3 -I verification/results/fl-collision/ap_objects_identical.py --base main
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
COMPILED = ('capture.cpp', 'loader.cpp')
UNCHANGED = ('src/proxy/collide_box_cull.cpp', 'src/proxy/collide_box_cull.h', 'src/proxy/collide_box_cull_core.h',
             'src/proxy/collide_sat_sse2.cpp', 'src/proxy/collide_sat_sse2.h', 'src/proxy/collide_sat_sse2_core.h',
             'src/proxy/collide_memo.cpp', 'src/proxy/collide_memo.h', 'src/proxy/collide_memo_core.h')
parser = argparse.ArgumentParser()
parser.add_argument('--base', default='HEAD')
args = parser.parse_args()
flags = (BUILD / 'CMakeFiles/d3d9.dir/flags.make').read_text()
defines = shlex.split(re.search(r'^CXX_DEFINES = (.*)$', flags, re.M).group(1))
cxxflags = shlex.split(re.search(r'^CXX_FLAGS = (.*)$', flags, re.M).group(1))
includes = shlex.split((BUILD / 'CMakeFiles/d3d9.dir/includes_CXX.rsp').read_text())
lines = []
with tempfile.TemporaryDirectory(prefix='x3-ap-collide-') as tmp:
    for name in COMPILED:
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
diff = subprocess.run(['git', '-C', str(ROOT), 'diff', '--quiet', args.base, '--', *UNCHANGED]).returncode
lines.append(f'AP collision sources ({len(UNCHANGED)} files) {"identical" if diff == 0 else "DIFFERENT"} to {args.base}')
print('\n'.join(lines))
sys.exit(0 if all(' identical ' in line or line.endswith(f'identical to {args.base}') for line in lines) else 1)
