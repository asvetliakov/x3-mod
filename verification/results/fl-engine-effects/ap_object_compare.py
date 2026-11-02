#!/usr/bin/env python3
"""AP object identity for the FL engine-effects port, step 1 (docs/verification/farnhams-legacy.md).

Compiles the AP translation units the port touches twice with build-ap's flags (flags.make: CXX_DEFINES, CXX_INCLUDES,
CXX_FLAGS without -g): once from the worktree, once from a scratch copy of src/ whose changed files are replaced by
`git show <base>:<path>`, and compares the objects with debug sections stripped (objcopy --strip-debug). Prints one
JSON line {base, compared, identical, different}.

usage: ap_object_compare.py <build-ap dir> <scratch dir> [base commit, default 4af1e4521]
"""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BUILD = None  # the response files in flags.make are relative to the build tree
SOURCES = ['src/proxy/engine_effects.cpp', 'src/proxy/engine_effects_patch.cpp', 'src/proxy/motion_output.cpp',
           'src/proxy/capture.cpp']


def flags(build):
    text = (build / 'CMakeFiles/d3d9.dir/flags.make').read_text()
    get = lambda key: re.search(rf'^{key} = (.*)$', text, re.M).group(1)
    cxx = [f for f in get('CXX_FLAGS').split() if f != '-g']
    return get('CXX_DEFINES').replace('\\"', '"').split() + get('CXX_INCLUDES').split() + cxx + ['-std=gnu++17']


def compile_strip(tree, source, out, args):
    obj = out.with_suffix('.o')
    subprocess.run(['i686-w64-mingw32-g++', *args, '-c', str(tree / source), '-o', str(obj)], check=True, cwd=BUILD)
    subprocess.run(['i686-w64-mingw32-objcopy', '--strip-debug', str(obj), str(out)], check=True)
    return out.read_bytes()


def main():
    global BUILD
    build, scratch = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    BUILD = build
    base = sys.argv[3] if len(sys.argv) > 3 else '4af1e4521'
    tree = scratch / 'base'
    shutil.rmtree(tree, ignore_errors=True)
    shutil.copytree(ROOT / 'src', tree / 'src')
    changed = subprocess.run(['git', 'diff', '--name-only', base, '--', 'src'], cwd=ROOT, capture_output=True,
                             text=True, check=True).stdout.split()
    for path in changed:
        shown = subprocess.run(['git', 'show', f'{base}:{path}'], cwd=ROOT, capture_output=True)
        target = tree / path
        if shown.returncode == 0:
            target.write_bytes(shown.stdout)
        else:
            target.unlink(missing_ok=True)   # new in the worktree
    args = flags(build)
    result = dict(base=base, changed_sources=changed, compared=[], identical=[], different=[])
    for source in SOURCES:
        stem = Path(source).stem
        a = compile_strip(tree, source, scratch / f'base_{stem}.obj', args)
        b = compile_strip(ROOT, source, scratch / f'new_{stem}.obj', args)
        result['compared'].append(source)
        result['identical' if a == b else 'different'].append(source)
    print(json.dumps(result))
    return 0


if __name__ == '__main__':
    sys.exit(main())
