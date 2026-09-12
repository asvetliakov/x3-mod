#!/usr/bin/env python3
"""Replay mesh-adjacency-<n>.bin dumps (X3M_MESH_ADJACENCY_DUMP=1) offline.

A dump holds one mesh the game's verify mode found mismatching: declaration,
vertex and index bytes, epsilon, the computational state, D3DX's adjacency and
the module's (src/proxy/loading_trace.h, AdjacencyDumpHeader). This tool feeds
such dumps through

* the host build of the production module (verification/probe/
  mesh_adjacency_fast_host.cpp, compiled with the host compiler; the portable
  1/sqrt stands in for rsqrtss),
* optionally the Python reference port (--reference; slow on large meshes),
* optionally the Wine fixture's `replay` mode (--wine), which runs the real
  d3dx9_37 and the x86 module on the same bytes under the Wine runner lock
  (build the fixtures first: sh verification/probe/build_loading_trace.sh),

and compares every result with the dump's native array. For a mismatch it
prints the first entries with the faces' raw indices and positions and which
policy variant of the module, if any, reproduces D3DX, so the rule at fault can
be named before the next game run.

usage: replay_mesh_adjacency.py [--reference] [--wine] [--limit N] DUMP_OR_DIR...
Exit status 0 when every replayed result equals the dump's native array.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
sys.path.insert(0, str(ROOT / 'verification' / 'probe'))
import mesh_adjacency_reference as ref  # noqa: E402

UNUSED = 0xFFFFFFFF
MAGIC = b'X3MADJ01'
HEADER = struct.Struct('<8s14I')
POLICIES = ('head_insertion', 'normal_selection', 'weld_refusal', 'heap_order', 'retire_own_entry', 'unlink_refused', 'later_slot_check')
DEFAULTS = dict(head_insertion=True, normal_selection=True, weld_refusal=True, heap_order=True, retire_own_entry=False, unlink_refused=True, later_slot_check=False)
VARIANTS = {'tail_insertion': dict(head_insertion=False), 'no_normal_selection': dict(normal_selection=False), 'no_weld_refusal': dict(weld_refusal=False),
            'index_order_sweep': dict(heap_order=False), 'retire_own_entry': dict(retire_own_entry=True), 'keep_refused_entry': dict(unlink_refused=False),
            'later_slot_check': dict(later_slot_check=True)}


class Dump:
    def __init__(self, path):
        self.path = Path(path)
        data = self.path.read_bytes()
        fields = HEADER.unpack_from(data, 0)
        if fields[0] != MAGIC or fields[1] != HEADER.size:
            raise ValueError(f'{path}: not a mesh adjacency dump')
        (self.faces, self.vertices, self.stride, self.position_offset, self.options, self.declaration_count,
         self.epsilon_bits, self.x87_control, self.mxcsr, self.mismatches, self.first) = fields[2:13]
        self.bits32 = bool(self.options & 1)
        offset = HEADER.size
        self.declaration = [struct.unpack_from('<HHBBBB', data, offset + 8 * i) for i in range(self.declaration_count)]
        offset += 8 * self.declaration_count
        self.vertex_bytes = data[offset:offset + self.vertices * self.stride]
        offset += self.vertices * self.stride
        index_size = 4 if self.bits32 else 2
        self.index_bytes = data[offset:offset + self.faces * 3 * index_size]
        offset += self.faces * 3 * index_size
        self.native = list(struct.unpack_from(f'<{self.faces * 3}I', data, offset))
        offset += self.faces * 12
        self.module = list(struct.unpack_from(f'<{self.faces * 3}I', data, offset))
        offset += self.faces * 12
        if offset != len(data):
            raise ValueError(f'{path}: {len(data) - offset} trailing bytes')
        self.epsilon = struct.unpack('<f', struct.pack('<I', self.epsilon_bits))[0]
        self.positions = [struct.unpack_from('<3I', self.vertex_bytes, v * self.stride + self.position_offset) for v in range(self.vertices)]
        self.heads = [struct.unpack_from('<3I', self.vertex_bytes, v * self.stride) for v in range(self.vertices)]  # D3DX's key and normal source: byte 0
        self.indices = list(struct.unpack_from(f'<{self.faces * 3}{"I" if self.bits32 else "H"}', self.index_bytes))
        self.face_list = [tuple(self.indices[f * 3:f * 3 + 3]) for f in range(self.faces)]

    def describe(self):
        return (f'faces={self.faces} vertices={self.vertices} bits={32 if self.bits32 else 16} stride={self.stride} position_offset={self.position_offset} '
                f'epsilon={self.epsilon:g} options={self.options:08x} x87_control={self.x87_control:04x} mxcsr={self.mxcsr:08x} '
                f'dump_mismatches={self.mismatches} dump_first={self.first} declaration={",".join("%d:%d:%d:%d" % (e[1], e[2], e[4], e[5]) for e in self.declaration)}')

    def position(self, v):
        return tuple(ref.bits_to_float(c) for c in self.positions[v])


def build_host_driver():
    compiler = shutil.which('c++') or shutil.which('clang++') or shutil.which('g++')
    if compiler is None:
        raise SystemExit('no host C++ compiler')
    directory = Path(tempfile.mkdtemp(prefix='x3-mesh-adjacency-replay-'))
    exe = directory / 'driver'
    subprocess.run([compiler, '-std=c++17', '-O2', '-ffp-contract=off', str(ROOT / 'verification/probe/mesh_adjacency_fast_host.cpp'),
                    str(ROOT / 'src/proxy/mesh_adjacency_fast.cpp'), '-o', str(exe)], check=True)
    return directory, exe


def run_host(exe, dump, **policy):
    flags = dict(DEFAULTS, **policy)
    text = f'{dump.vertices} {dump.faces} {32 if dump.bits32 else 16} {dump.epsilon!r} {dump.stride} {dump.position_offset} ' + ' '.join(str(int(flags[p])) for p in POLICIES) + '\n'
    text += ''.join(f'{x:08x} {y:08x} {z:08x} {hx:08x} {hy:08x} {hz:08x}\n' for (x, y, z), (hx, hy, hz) in zip(dump.positions, dump.heads))
    text += ''.join(f'{a} {b} {c}\n' for a, b, c in dump.face_list)
    out = subprocess.run([str(exe)], input=text, capture_output=True, text=True, check=True).stdout.splitlines()
    head = out[0].split()
    report = dict(status=head[0], representatives=int(head[1]), welded=int(head[2]), quantized=int(head[3]), degenerate_faces=int(head[4]),
                  welded_degenerate_faces=int(head[5]), refused_welds=int(head[6]), multi_candidates=int(head[7]), normal_selected=int(head[8]),
                  repeated_neighbours=int(head[9]), unmatched=int(head[10]), rsqrt=head[11])
    adjacency = [UNUSED if v == '-1' else int(v) for v in out[1].split()] if head[0] == 'ok' else None
    return report, adjacency


def mismatches(a, b):
    return [i for i, (x, y) in enumerate(zip(a, b)) if x != y]


def entry(value):
    return -1 if value == UNUSED else value


def explain(dump, actual, label, limit):
    diff = mismatches(dump.native, actual)
    for i in diff[:limit]:
        face, slot = divmod(i, 3)
        native, ours = dump.native[i], actual[i]
        own = dump.face_list[face]
        other = native if native != UNUSED else ours
        other_indices = dump.face_list[other] if other != UNUSED else ()
        print(f'  {label} index={i} face={face} slot={slot} native={entry(native)} ours={entry(ours)} face_indices={own} '
              f'positions={[dump.position(v) for v in own]} other={entry(other)} other_indices={other_indices}')


def replay_wine(paths):
    import bottle  # noqa: E402  (verification/probe)
    build = ROOT / 'verification/probe/build/mesh_adjacency_fast'
    exe = build / 'mesh_adjacency_fast_fixture.exe'
    if not exe.exists() or not (build / 'd3dx9_37.dll').exists():
        raise SystemExit('fixture not built: sh verification/probe/build_loading_trace.sh (and run_loading_trace.py copies d3dx9_37.dll next to it)')
    override = 'd3dx9_37=n,b;d3d9=b'
    command = [sys.executable, str(ROOT / 'verification/probe/wine_lock.py'), '--holder', 'replay_mesh_adjacency', bottle.WINE, '--bottle', bottle.BOTTLE, '--no-update',
               '--dll', override, '--workdir', str(build), str(exe), 'replay'] + ['Z:' + str(Path(p).resolve()) for p in paths]
    run = subprocess.run(command, env=dict(os.environ, WINEDLLOVERRIDES=override), capture_output=True, text=True)
    for line in run.stdout.splitlines():
        if line.startswith(('REPLAY_', 'MESH ADJACENCY REPLAY', 'FAIL')):
            print(line)
    return run.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('paths', nargs='+', help='dump files or directories holding mesh-adjacency-*.bin')
    parser.add_argument('--reference', action='store_true', help='also run the Python reference port (slow on large meshes)')
    parser.add_argument('--wine', action='store_true', help='also replay through the Wine fixture against the real d3dx9_37 (Wine lock)')
    parser.add_argument('--limit', type=int, default=8, help='mismatching entries to print per dump (default 8)')
    args = parser.parse_args()
    files = []
    for p in args.paths:
        path = Path(p)
        files += sorted(path.glob('mesh-adjacency-*.bin')) if path.is_dir() else [path]
    if not files:
        raise SystemExit('no dumps')
    directory, exe = build_host_driver()
    failures = 0
    try:
        for path in files:
            dump = Dump(path)
            report, adjacency = run_host(exe, dump)
            host_equal = adjacency is not None and adjacency == dump.native
            module_equal = adjacency is not None and adjacency == dump.module
            print(f'REPLAY file={path.name} {dump.describe()} host_status={report["status"]} host_equal_native={int(host_equal)} '
                  f'host_mismatches={len(mismatches(dump.native, adjacency)) if adjacency else "-"} host_equal_dump_module={int(module_equal)} '
                  f'representatives={report["representatives"]} welded={report["welded"]} refused_welds={report["refused_welds"]} '
                  f'multi_candidates={report["multi_candidates"]} normal_selected={report["normal_selected"]} degenerate_faces={report["degenerate_faces"]} '
                  f'welded_degenerate_faces={report["welded_degenerate_faces"]} repeated_neighbours={report["repeated_neighbours"]} rsqrt={report["rsqrt"]}')
            if not host_equal:
                failures += 1
                if adjacency is not None:
                    explain(dump, adjacency, 'HOST_MISMATCH', args.limit)
                    for name, policy in VARIANTS.items():
                        _, alt = run_host(exe, dump, **policy)
                        print(f'  HOST_POLICY variant={name} equal_native={int(alt == dump.native)} mismatches={len(mismatches(dump.native, alt)) if alt else "-"}')
            if args.reference:
                status, rep, reference = ref.generate(dump.positions, dump.face_list, dump.epsilon, head_bits=dump.heads)
                reference_equal = reference is not None and reference == dump.native
                print(f'  REFERENCE file={path.name} status={status} equal_native={int(reference_equal)} equal_host={int(reference == adjacency)} '
                      f'refused_welds={rep["refused_welds"]} multi_candidates={rep["multi_candidates"]} normal_selected={rep["normal_selected"]}')
                if reference is not None and not reference_equal:
                    explain(dump, reference, 'REFERENCE_MISMATCH', args.limit)
        if args.wine:
            code = replay_wine(files)
            if code:
                failures += 1
                print(f'WINE_REPLAY exit={code}')
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    print(f'REPLAY_SUMMARY dumps={len(files)} failures={failures}')
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
