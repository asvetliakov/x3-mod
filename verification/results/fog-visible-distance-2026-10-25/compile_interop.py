#!/usr/bin/env python3
"""tools/shaders/generate_rigid_motion_pixel.py on the native Windows host (WSL2 interop, no Wine, no device).

The generator runs its MinGW-built compile.exe under Wine; on the WSL2 host the same exe runs natively through
interop with the same d3dx9_37.dll (C:\\X3, sha256 c2ccb84c... = the recorded compiler). Only the Wine call is
rewritten (exe and Windows paths via wslpath); the generator, its provenance and its --check are unchanged, so
the records' tool_sources hashes stay valid. X3AP.exe must not be running (tasklist.exe).
usage: python3 compile_interop.py [--check] [--shader NAME]...   (the generator's own options)
"""
import importlib.util
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DLL = Path('/mnt/c/X3/d3dx9_37.dll')
spec = importlib.util.spec_from_file_location('generator', ROOT / 'tools/shaders/generate_rigid_motion_pixel.py')
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)
real_run = subprocess.run


def winpath(text):
    path = text[2:] if text.startswith('Z:') else text
    return real_run(['wslpath', '-w', path], check=True, capture_output=True, text=True).stdout.strip()


def run(argv, *args, **kwargs):
    if argv and argv[0] == gen.bottle.WINE:
        rest = argv[argv.index('--dll') + 2:]  # exe, dll, source, binary, target
        argv = [rest[0]] + [winpath(a) for a in rest[1:4]] + rest[4:]
        kwargs.pop('env', None)
    return real_run(argv, *args, **kwargs)


def game_running():
    out = real_run(['tasklist.exe', '/FI', 'IMAGENAME eq X3AP.exe'], capture_output=True, text=True).stdout
    return 'X3AP.exe' in out


gen.subprocess.run = run
gen.game_running = game_running
if __name__ == '__main__':
    sys.argv = [sys.argv[0], '--d3dx', str(DLL)] + sys.argv[1:]
    gen.main()
