#!/usr/bin/env python3
"""AP behaviour check for the FL base ship steering and avoidance port (after verification/results/fl-seta/ap_objects_identical.py):
compiles every AP source the port can reach twice with build-ap's flags (build-ap/CMakeFiles/d3d9.dir/flags.make and
includes_CXX.rsp; a configured build-ap with its generated x3m_source_commit_inc.h is enough), once inside a copy of --base's
src/ tree (default HEAD; `git archive`, so a changed shared header is compiled from --base too) and once from the working tree,
and compares the objdump disassembly (debug sections and line numbers excluded). Sources: capture.cpp (the FL call sites) and
every AP translation unit that includes ship_avoidance_walk_core.h (the reader-bound TShips slot). The AP steering glue sources
must be byte-identical to --base as text. Prints one line per source.

usage: python3 -I verification/results/fl-steering/ap_objects_identical.py --base main
"""
import argparse
import re
import shlex
import subprocess
import sys
import tarfile
import tempfile
from io import BytesIO
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / 'build-ap'
COMPILED = ('src/proxy/capture.cpp', 'src/proxy/ship_steering_install.cpp', 'src/proxy/ship_steering_decide.cpp',
            'src/proxy/ship_steering_clamp.cpp', 'src/proxy/ship_steering_warn.cpp', 'src/proxy/ship_steering_rows.cpp',
            'src/proxy/ship_gate_ctl_glue.cpp', 'src/proxy/ship_dock_glue.cpp')
UNCHANGED = ('src/proxy/ship_steering_install.cpp', 'src/proxy/ship_steering_decide.cpp', 'src/proxy/ship_steering_clamp.cpp',
             'src/proxy/ship_steering_warn.cpp', 'src/proxy/ship_steering_rows.cpp', 'src/proxy/ship_steering_internal.h',
             'src/proxy/ship_steering_sites.h', 'src/proxy/ship_steering_core.h', 'src/proxy/ship_steering.h',
             'src/proxy/ship_avoidance_core.h', 'src/proxy/ship_gate_ctl_glue.cpp', 'src/proxy/ship_dock_glue.cpp')
parser = argparse.ArgumentParser()
parser.add_argument('--base', default='HEAD')
args = parser.parse_args()
flags = (BUILD / 'CMakeFiles/d3d9.dir/flags.make').read_text()
defines = shlex.split(re.search(r'^CXX_DEFINES = (.*)$', flags, re.M).group(1))
cxxflags = shlex.split(re.search(r'^CXX_FLAGS = (.*)$', flags, re.M).group(1))
includes = shlex.split((BUILD / 'CMakeFiles/d3d9.dir/includes_CXX.rsp').read_text())
lines = []
with tempfile.TemporaryDirectory(prefix='x3-ap-steer-') as tmp:
    base_root = Path(tmp) / 'base'
    tar = subprocess.run(['git', '-C', str(ROOT), 'archive', args.base, 'src'], capture_output=True, check=True).stdout
    with tarfile.open(fileobj=BytesIO(tar)) as t:
        t.extractall(base_root, filter='data')

    def disassemble(tree, name, obj):
        # the per-file options of CMakeLists.txt (-fno-exceptions for the steering glue) as the d3d9 target compiles them; no
        # forced -std: the compiler's default standard, as the d3d9 target builds
        extra = ['-fno-exceptions'] if 'ship_' in name else []
        subprocess.run(['i686-w64-mingw32-g++', *defines, f'-I{tree}/src/proxy', *[i.replace(str(ROOT), str(tree)) if
                        '/src/' in i else i for i in includes], *cxxflags, *extra, '-c', str(tree / name),
                        '-o', obj], check=True, cwd=BUILD)
        text = subprocess.run(['i686-w64-mingw32-objdump', '-d', '--no-show-raw-insn', obj], capture_output=True, text=True,
                              check=True).stdout
        return text.splitlines()[2:]
    for name in COMPILED:
        stem = Path(name).name
        old = disassemble(base_root, name, str(Path(tmp) / f'base_{stem}.o'))
        new = disassemble(ROOT, name, str(Path(tmp) / f'head_{stem}.o'))
        lines.append(f'{stem} {"identical" if old == new else "DIFFERENT"} {len(old)} disassembly lines '
                     f'(base {args.base}, head working tree)')
diff = subprocess.run(['git', '-C', str(ROOT), 'diff', '--quiet', args.base, '--', *UNCHANGED]).returncode
lines.append(f'AP steering sources ({len(UNCHANGED)} files) {"identical" if diff == 0 else "DIFFERENT"} to {args.base}')
print('\n'.join(lines))
sys.exit(0 if all(' identical ' in line or line.endswith(f'identical to {args.base}') for line in lines) else 1)
