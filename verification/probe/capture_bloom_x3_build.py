#!/usr/bin/env python3
"""Cross-compile only the native-D3D capture integration EXE; no DLL/Wine."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'verification/probe'
spec = importlib.util.spec_from_file_location('seh_builder', ROOT / 'tools/build/build_compositor_bridge.py')
seh = importlib.util.module_from_spec(spec)
spec.loader.exec_module(seh)

def build(directory):
    directory = directory.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    def run(args):
        subprocess.run([str(a) for a in args], cwd=ROOT, check=True)
    raw = directory / 'capture_bloom_x3_outer.obj'
    linked = directory / 'capture_bloom_x3_outer_gnu.obj'
    run([os.environ.get('X3M_SEH_CLANG', '/usr/bin/clang'), *seh.FLAGS,
         '-c', PROBE / 'capture_bloom_x3_outer_seh.c', '-o', raw])
    # GNU PE ld cannot consume SafeSEH metadata. Preserve original compiler
    # object and prove removing only that section leaves all code/relocs exact.
    run(['i686-w64-mingw32-objcopy', '--remove-section=.sxdata', raw, linked])
    before, after = seh.sections(raw), seh.sections(linked)
    if set(after) != set(before) - {'.sxdata'}:
        raise RuntimeError('Unexpected SEH object section change')
    for name, value in after.items():
        old = before[name]
        if value[:len(old)] != old or any(value[len(old):]):
            raise RuntimeError('Unexpected SEH object byte change: ' + name)
    if seh.relocations(raw) != seh.relocations(linked):
        raise RuntimeError('Unexpected SEH relocation change')
    definition = directory / 'capture_bloom_x3_seh_runtime.def'
    definition.write_text('LIBRARY msvcrt.dll\nEXPORTS\n_except_handler3\n')
    library = directory / 'libcapture_bloom_x3_seh.a'
    run(['i686-w64-mingw32-dlltool', '--input-def', definition, '--output-lib', library])
    exe = directory / 'capture_bloom_x3_fixture.exe'
    command = ['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
               '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2', '-static',
               PROBE / 'capture_bloom_x3_fixture.cpp', linked, library, '-o', exe, '-luser32']
    run(command)
    return exe, [str(a) for a in command]

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, default=PROBE / 'build/capture-bloom-x3')
    print(build(parser.parse_args().output_dir)[0])
