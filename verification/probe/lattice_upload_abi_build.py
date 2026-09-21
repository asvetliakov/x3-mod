#!/usr/bin/env python3
"""Parent-owned two-object ABI build and disassembly; never executes Wine."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-msse2',
         '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--compiler', default='i686-w64-mingw32-g++')
    parser.add_argument('--objdump', default='i686-w64-mingw32-objdump')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    start = time.monotonic()
    commands, objects = [], []
    sources = [('src/ownership/clone_upload_abi.cpp', 'shell',
                ['-fno-exceptions', '-DX3M_LATTICE_UPLOAD_ABI_FIXTURE']),
               ('src/ownership/clone_upload_abi.cpp', 'eh',
                ['-DX3M_CLONE_UPLOAD_ABI_EH']),
               ('verification/probe/lattice_upload_abi_fixture.cpp', 'fixture', [])]
    for source, name, extra in sources:
        obj = out / f'{name}.o'
        cmd = [args.compiler, *FLAGS, *extra, '-c', str(ROOT / source), '-o', str(obj)]
        subprocess.run(cmd, check=True, cwd=ROOT)
        commands.append(cmd)
        objects.append(str(obj))
        (out / f'{name}.asm').write_bytes(subprocess.check_output([args.objdump, '-dr', str(obj)]))
    # Also inspect the real public call; the fixture build alone cannot prove it.
    obj = out / 'production-shell.o'
    cmd = [args.compiler, *FLAGS, '-fno-exceptions', '-c',
           str(ROOT / sources[0][0]), '-o', str(obj)]
    subprocess.run(cmd, check=True, cwd=ROOT)
    commands.append(cmd)
    (out / 'production-shell.asm').write_bytes(subprocess.check_output([args.objdump, '-dr', str(obj)]))
    exe = out / 'lattice_upload_abi_fixture.exe'
    cmd = [args.compiler, '-static', *objects, '-o', str(exe)]
    subprocess.run(cmd, check=True, cwd=ROOT)
    commands.append(cmd)
    tracked = {source for source, _, _ in sources} | {
        'src/ownership/clone_upload_abi.h', 'verification/probe/lattice_upload_abi_build.py',
        'verification/probe/lattice_upload_abi_check.py'}
    report = {'commands': commands, 'sources': {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest()
              for p in sorted(tracked)}, 'seconds': time.monotonic()-start,
              'compiler': subprocess.check_output([args.compiler, '--version'], text=True).splitlines()[0],
              'exe': str(exe), 'exe_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
              'executed': False, 'native_windows_verified': False}
    (out/'build.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: report[k] for k in ('exe', 'exe_sha256', 'seconds', 'executed')}))


if __name__ == '__main__':
    main()
