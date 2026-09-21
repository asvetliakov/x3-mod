#!/usr/bin/env python3
"""Parent-owned checkpoint A fixture build; no Wine/game execution."""
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
    p = argparse.ArgumentParser()
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--compiler', default='i686-w64-mingw32-g++')
    p.add_argument('--objdump', default='i686-w64-mingw32-objdump')
    p.add_argument('--objcopy', default='i686-w64-mingw32-objcopy')
    args = p.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    start = time.monotonic()
    commands, objects = [], []
    sources = [('src/ownership/clone_upload_abi.cpp', 'shell', ['-fno-exceptions']),
               ('src/ownership/clone_upload_abi.cpp', 'eh', ['-DX3M_CLONE_UPLOAD_ABI_EH']),
               ('src/proxy/lattice_upload_hook.cpp', 'hook', ['-fno-exceptions', '-DX3M_LATTICE_UPLOAD_HOOK_FIXTURE']),
               ('src/proxy/engine_patch.cpp', 'engine_patch', ['-fno-exceptions']),
               ('verification/probe/lattice_upload_hook_fixture.cpp', 'fixture', ['-DX3M_LATTICE_UPLOAD_HOOK_FIXTURE'])]
    dependencies = set()
    for source, name, extra in sources:
        obj, dep = out/f'{name}.o', out/f'{name}.d'
        cmd = [args.compiler, *FLAGS, *extra, '-MMD', '-MF', str(dep),
               '-c', str(ROOT/source), '-o', str(obj)]
        subprocess.run(cmd, check=True, cwd=ROOT)
        commands.append(cmd)
        objects.append(str(obj))
        dependencies.update(Path(token).resolve() for token in dep.read_text().replace('\\\n', ' ').split(':', 1)[1].split())
        (out/f'{name}.asm').write_bytes(subprocess.check_output([args.objdump, '-dr', str(obj)]))
    # Redirect only these two fixture-object IAT slots, not production source or
    # the shell's imports. The fixture shims forward to real Windows APIs unless
    # the selected synthetic site's fault-injection control is enabled.
    cmd = [args.objcopy,
           '--redefine-sym', '__imp__VirtualProtect@16=_lattice_hook_protect_iat',
           '--redefine-sym', '__imp__FlushInstructionCache@12=_lattice_hook_flush_iat',
           str(out/'engine_patch.o')]
    subprocess.run(cmd, check=True, cwd=ROOT)
    commands.append(cmd)
    (out/'engine_patch-injected.asm').write_bytes(subprocess.check_output([args.objdump, '-dr', str(out/'engine_patch.o')]))
    exe = out/'lattice_upload_hook_fixture.exe'
    cmd = [args.compiler, '-static', *objects, '-o', str(exe)]
    subprocess.run(cmd, check=True, cwd=ROOT)
    commands.append(cmd)
    dependencies.update((Path(__file__).resolve(), ROOT/'verification/probe/lattice_upload_hook_check.py'))
    report = {'commands': commands,
              'sources': {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in sorted(dependencies) if path.is_relative_to(ROOT)},
              'compiler': subprocess.check_output([args.compiler, '--version'], text=True).splitlines()[0],
              'exe': str(exe), 'exe_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
              'seconds': time.monotonic()-start, 'executed': False,
              'native_windows_verified': False, 'game_launched': False}
    (out/'build.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: report[k] for k in ('exe', 'exe_sha256', 'seconds', 'executed')}))


if __name__ == '__main__':
    main()
