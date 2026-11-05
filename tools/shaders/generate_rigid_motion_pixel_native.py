#!/usr/bin/env python3
"""Run tools/shaders/generate_rigid_motion_pixel.py on the native Windows host (WSL2, no wine).

The base tool's compile_one runs unchanged: the same compile.exe (compile_rigid_motion_pixel.cpp), the game directory's
d3dx9_37.dll, D3DXSHADER_OPTIMIZATION_LEVEL3, the same header text and provenance record byte for byte (its Reproduce
line and tool sources name the base tool, so a bottle --check of the base tool accepts what this writes). Only the
compiler's wine invocation is replaced by a direct WSL-interop run of compile.exe with Windows paths, after
`tasklist.exe` shows no X3AP.exe / X3FL.exe (failing closed when the list cannot be read). The base tool's digest, and
with it every record it checks, stays unchanged. --check recompiles and compares without changing anything.
--engine-shimmer runs tools/shaders/generate_engine_shimmer_program.py's compile_program the same way (its own header
and record, its Reproduce line naming the shimmer tool).
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import types

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_rigid_motion_pixel as base  # noqa: E402


def windows(path):
    return subprocess.run(['wslpath', '-w', str(path)], check=True, capture_output=True, text=True).stdout.strip()


def native_run(command, **kwargs):
    """subprocess.run for the base tool: its wine call [WINE, *wine_args, '--dll', 'd3dx9_37=n', exe, 'Z:'compiler,
    'Z:'source, 'Z:'binary, target] becomes [exe, compiler, source, binary] in Windows paths plus target."""
    if command and command[0] == base.bottle.WINE:
        exe, compiler, source, binary, target = command[-5:]
        paths = [p[2:] if p.startswith('Z:') else p for p in (compiler, source, binary)]
        kwargs.pop('env', None)
        return subprocess.run([exe, *map(windows, paths), target], **kwargs)
    return subprocess.run(command, **kwargs)


def game_running():
    listing = subprocess.run(['tasklist.exe'], capture_output=True, text=True, timeout=30)
    if listing.returncode != 0:
        raise RuntimeError('tasklist.exe failed')
    return [line for line in listing.stdout.splitlines() if line.lower().startswith(('x3ap.exe', 'x3fl.exe'))]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--shader', choices=sorted(base.SHADERS), action='append', default=[],
                        help='fragment to (re)compile (this or --engine-shimmer at least once)')
    parser.add_argument('--engine-shimmer', action='store_true',
                        help='also the engine shimmer program (generate_engine_shimmer_program.py)')
    parser.add_argument('--d3dx', type=Path, default=base.bottle.game_dir() / 'd3dx9_37.dll')
    args = parser.parse_args()
    if not args.shader and not args.engine_shimmer:
        parser.error('nothing to compile: give --shader or --engine-shimmer')
    if Path(base.bottle.WINE).exists():
        raise RuntimeError('a CrossOver wine exists: run generate_rigid_motion_pixel.py through wine_lock.py instead')
    if game_running():
        raise RuntimeError('X3AP / X3FL running or process inventory failed; postpone compilation')
    base.subprocess = types.SimpleNamespace(run=native_run)
    results = [base.compile_one(name, args) for name in args.shader]
    if args.engine_shimmer:
        import generate_engine_shimmer_program as shimmer
        shimmer.subprocess = types.SimpleNamespace(run=native_run)
        results.append(shimmer.compile_program(args))
    print(json.dumps(dict(result='PASS', check=args.check, shaders=results)))


if __name__ == '__main__':
    main()
