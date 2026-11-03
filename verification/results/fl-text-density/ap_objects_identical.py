#!/usr/bin/env python3
"""AP behaviour check for the FL text-density change: compiles each named AP source twice with build-ap's own command
(compile_commands.json, CMAKE_EXPORT_COMPILE_COMMANDS=ON), once from the base commit and once from --head (a commit,
default the working tree), and compares the objdump disassembly of the two objects (debug sections and line numbers
excluded). The default base is 93658e53c, the parent of the FL text-density commit. Writes ap_objects.txt beside this
script; with --head it records the commit and each source's blob hash (the blob stays valid when the commit is later
amended with evidence files only).

usage: python3 -I verification/results/fl-text-density/ap_objects_identical.py [--base REV] [--head REV] FILE...
       (FILE = a name under src/proxy, e.g. chase_camera.cpp loader.cpp capture.cpp chase_lead.cpp)
"""
import argparse
import json
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser()
parser.add_argument('--base', default='93658e53c')
parser.add_argument('--head', default=None, help='a commit; default the working tree')
parser.add_argument('files', nargs='+')
args = parser.parse_args()
cc = json.loads((ROOT / 'build-ap/compile_commands.json').read_text())


def show(rev, name):
    return subprocess.run(['git', '-C', str(ROOT), 'show', f'{rev}:src/proxy/{name}'], capture_output=True, text=True,
                          check=True).stdout


lines = []
with tempfile.TemporaryDirectory(prefix='x3-ap-same-') as tmp:
    for name in args.files:
        entry = next(e for e in cc if e['file'].endswith('/src/proxy/' + name))
        command = shlex.split(entry['command'])
        out_i = command.index('-o')

        def disassemble(src, obj):
            a = list(command)
            a[out_i + 1] = obj
            a = [src if x == entry['file'] else x for x in a]
            a.insert(1, f'-I{ROOT}/src/proxy')
            subprocess.run(a, check=True, cwd=entry['directory'])
            text = subprocess.run(['i686-w64-mingw32-objdump', '-d', '--no-show-raw-insn', obj], capture_output=True,
                                  text=True, check=True).stdout
            return text.splitlines()[2:]
        sources = []
        for tag, rev in (('base', args.base), ('head', args.head)):
            if rev is None:
                sources.append(entry['file'])
                continue
            path = Path(tmp) / tag / name
            path.parent.mkdir(exist_ok=True)
            path.write_text(show(rev, name))
            sources.append(str(path))
        old = disassemble(sources[0], str(Path(tmp) / f'base_{name}.o'))
        new = disassemble(sources[1], str(Path(tmp) / f'head_{name}.o'))
        head = 'working tree'
        if args.head:
            commit = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', '--short=9', args.head], capture_output=True,
                                    text=True, check=True).stdout.strip()
            blob = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', '--short=12', f'{args.head}:src/proxy/{name}'],
                                  capture_output=True, text=True, check=True).stdout.strip()
            head = f'{commit}, blob {blob}'
        lines.append(f'{name} {"identical" if old == new else "DIFFERENT"} {len(old)} disassembly lines '
                     f'(base {args.base}, head {head})')
(Path(__file__).resolve().parent / 'ap_objects.txt').write_text('\n'.join(lines) + '\n')
print('\n'.join(lines))
sys.exit(0 if all(' identical ' in line for line in lines) else 1)
