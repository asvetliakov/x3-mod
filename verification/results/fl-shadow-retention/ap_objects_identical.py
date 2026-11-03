#!/usr/bin/env python3
"""AP behaviour check for the FL shadow-caster-retention port (the retirement journal in
src/engine/fl/object_lifetime_fl.cpp): compiles each named AP source twice with build-ap's own command
(compile_commands.json, CMAKE_EXPORT_COMPILE_COMMANDS=ON), once from the base commit and once from --head (a commit,
default the working tree), and compares the objdump disassembly of the two objects (debug sections and line numbers
excluded); then checks that no AP build input changed: every file under src/ that differs from the base lies under
src/engine/fl/, and build-ap compiles nothing there. The default base is main; the comparison uses its merge base with
--head (or HEAD), so commits that landed on main after this change branched are not attributed to it. Writes ap_objects.txt beside this
script. (Copied from verification/results/fl-music/.)

usage: python3 -I verification/results/fl-shadow-retention/ap_objects_identical.py [--base REV] [--head REV] FILE...
       (this port: object_lifetime.cpp motion_output.cpp loader.cpp capture.cpp)
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
parser.add_argument('--base', default='main')
parser.add_argument('--head', default=None, help='a commit; default the working tree')
parser.add_argument('files', nargs='+')
args = parser.parse_args()
# Compare against the point this change branched from, so later commits on --base are not attributed to it.
requested_base = args.base
args.base = subprocess.run(['git', '-C', str(ROOT), 'merge-base', args.base, args.head or 'HEAD'], capture_output=True,
                           text=True, check=True).stdout.strip()[:9]
cc =json.loads((ROOT / 'build-ap/compile_commands.json').read_text())


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
                     f'(base {args.base} = merge-base of {requested_base}, head {head})')
changed = subprocess.run(['git', '-C', str(ROOT), 'diff', '--name-only', args.base, *([args.head] if args.head else []),
                          '--', 'src'], capture_output=True, text=True, check=True).stdout.split()
ap_inputs = [name for name in changed if not name.startswith('src/engine/fl/')]
fl_in_ap = [e['file'] for e in cc if '/src/engine/fl/' in e['file']]
lines.append(f'src changes {len(changed)}, AP inputs changed {len(ap_inputs)} {" ".join(ap_inputs)}, build-ap compiles '
             f'{len(fl_in_ap)} file(s) under src/engine/fl: '
             f'{"identical" if not ap_inputs and not fl_in_ap else "DIFFERENT"} inputs')
(Path(__file__).resolve().parent / 'ap_objects.txt').write_text('\n'.join(lines) + '\n')
print('\n'.join(lines))
sys.exit(0 if all(' identical ' in line or line.endswith(': identical inputs') for line in lines) else 1)
