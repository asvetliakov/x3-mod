#!/usr/bin/env python3
"""Native Windows benchmark of the mesh adjacency module against D3DX GenerateAdjacency.

Builds verification/probe/mesh_adjacency_native_bench.cpp with MinGW i686 (the project's flags), runs it
natively through WSL interop on the DLLs AP (d3dx9_37) and FL (d3dx9_43) import, and writes bench.txt beside
this script: the command, machine, DLL provenance and one table row per DLL and mesh.

    python3 verification/results/mesh-adjacency-native-windows/bench.py [--reps N] [--meshes a,b] [--out PATH]
"""
import argparse
import hashlib
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'verification/probe/mesh_adjacency_native_bench.cpp'
MODULE = ROOT / 'src/proxy/mesh_adjacency_fast.cpp'
FLAGS = ['-std=gnu++20', '-O2', '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
         '-static', '-Wall', '-Wextra', '-Werror']
COLUMNS = ['dll', 'mesh', 'faces', 'vertices', 'native_min_us', 'native_mean_us', 'fast_min_us', 'fast_mean_us',
           'speedup_min', 'equal', 'served_equal', 'production_status', 'reps', 'mismatches', 'welded_pct',
           'locked_min_us']


def toolchain():
    """The compiler path, or None when MinGW i686 or WSL interop is unavailable."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    return compiler


def build(compiler, out_dir):
    exe = Path(out_dir) / 'mesh_adjacency_native_bench.exe'
    subprocess.run([compiler, *FLAGS, str(SOURCE), str(MODULE), '-o', str(exe)], check=True)
    return exe


def run(exe, args, timeout=1800):
    return subprocess.run([str(exe), *args], capture_output=True, text=True, timeout=timeout)


def fields(line):
    return dict(token.split('=', 1) for token in line.split()[1:] if '=' in token)


def rows(stdout):
    """BENCH_ROW lines as dicts in table column order."""
    out = []
    for line in stdout.splitlines():
        if not line.startswith('BENCH_ROW '):
            continue
        f = fields(line)
        f['welded_pct'] = f'{100.0 * int(f["welded"]) / int(f["vertices"]):.1f}'
        out.append({c: f[c] for c in COLUMNS})
    return out


def table(rows_):
    lines = ['| ' + ' | '.join(COLUMNS) + ' |', '|' + '---|' * len(COLUMNS)]
    lines += ['| ' + ' | '.join(r[c] for c in COLUMNS) + ' |' for r in rows_]
    return '\n'.join(lines)


def parse_table(text):
    """Rows of a table written by table(), keyed by COLUMNS."""
    lines = [l for l in text.splitlines() if l.startswith('| ')]
    if not lines or [c.strip() for c in lines[0].strip('|').split('|')] != COLUMNS:
        raise ValueError('no benchmark table')
    out = []
    for l in lines[1:]:
        cells = [c.strip() for c in l.strip('|').split('|')]
        if len(cells) != len(COLUMNS):
            raise ValueError(f'row width {len(cells)}')
        r = dict(zip(COLUMNS, cells))
        for c in ('faces', 'vertices', 'equal', 'served_equal', 'reps', 'mismatches'):
            int(r[c])
        for c in ('native_min_us', 'native_mean_us', 'fast_min_us', 'fast_mean_us', 'speedup_min', 'welded_pct',
                  'locked_min_us'):
            float(r[c])
        out.append(r)
    return out


def machine():
    cpu = next((l.split(':', 1)[1].strip() for l in Path('/proc/cpuinfo').read_text().splitlines()
                if l.startswith('model name')), 'unknown')
    ver = subprocess.run(['cmd.exe', '/c', 'ver'], capture_output=True, text=True, cwd='/mnt/c').stdout.strip()
    return cpu, ver


def dll_provenance(stdout):
    out = []
    for line in stdout.splitlines():
        if line.startswith('BENCH_DLL '):
            f = fields(line)
            path = f.get('path', '')
            # A 32-bit process sees SysWOW64 under the name System32 (file system redirection).
            if path.lower().startswith('c:\\windows\\system32\\'):
                path = path[:11] + 'SysWOW64' + path[19:]
            host = Path('/mnt/' + path[0].lower() + path[2:].replace('\\', '/')) if len(path) > 2 else None
            digest = hashlib.sha256(host.read_bytes()).hexdigest() if host and host.exists() else 'unknown'
            out.append(f'# dll {f["name"]} loaded={f["loaded"]} path={path} sha256={digest}')
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--reps', type=int, default=20)
    p.add_argument('--meshes')
    p.add_argument('--out', type=Path, default=Path(__file__).with_name('bench.txt'))
    a = p.parse_args()
    compiler = toolchain()
    if not compiler:
        sys.exit('needs i686-w64-mingw32-g++ and WSL interop')
    args = ['--reps', str(a.reps)] + (['--meshes', a.meshes] if a.meshes else [])
    with tempfile.TemporaryDirectory(prefix='x3-adjacency-bench-') as tmp:
        exe = build(compiler, tmp)
        result = run(exe, args)
    if result.returncode not in (0, 2) or 'BENCH_DONE' not in result.stdout:
        sys.exit(f'bench failed rc={result.returncode}\n{result.stdout[-2000:]}{result.stderr[-2000:]}')
    cpu, ver = machine()
    gcc = subprocess.run([compiler, '--version'], capture_output=True, text=True).stdout.splitlines()[0]
    info = [l for l in result.stdout.splitlines() if l.startswith(('BENCH_INFO', 'BENCH_DONE'))]
    header = [
        f'# Native Windows mesh adjacency benchmark, {date.today().isoformat()}',
        f'# command: python3 {Path(__file__).relative_to(ROOT)} ' + ' '.join(args),
        f'# exe args: {" ".join(args)}; build: {gcc} {" ".join(FLAGS)}',
        f'# cpu: {cpu}; windows: {ver}',
        *dll_provenance(result.stdout),
        *('# ' + l for l in info),
        '# times in microseconds over `reps` timed calls after one untimed warm-up each; fast = module generate()',
        '# on locked buffers (locked_min_us adds LockVertexBuffer/LockIndexBuffer/unlock); epsilon 1e-6;',
        '# equal = verify-policy module output byte-equal to native; served_equal = what fast mode returns',
        '# (module output if production admission accepts, else native); exit rc=%d' % result.returncode,
    ]
    a.out.write_text('\n'.join(header) + '\n\n' + table(rows(result.stdout)) + '\n')
    print(a.out)
    return 0 if result.returncode == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
