"""Native Windows mesh adjacency benchmark (verification/probe/mesh_adjacency_native_bench.cpp).

Builds the benchmark with MinGW i686 and runs a short configuration natively through WSL interop against the
D3DX DLLs the editions import (d3dx9_37 for AP, d3dx9_43 for FL): the module's output must be byte-equal to
native GenerateAdjacency and the table must round-trip through the parser. The recorded bench.txt must parse
and show no served mismatch.
"""
import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / 'verification/results/mesh-adjacency-native-windows'
_spec = importlib.util.spec_from_file_location('mesh_adjacency_native_bench', RESULTS / 'bench.py')
bench = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bench)


class RecordedTable(unittest.TestCase):
    def test_recorded_table_parses_and_serves_native_equal_output(self):
        rows = bench.parse_table((RESULTS / 'bench.txt').read_text())
        self.assertEqual({r['dll'] for r in rows}, {'d3dx9_37.dll', 'd3dx9_43.dll'})
        for r in rows:
            self.assertEqual(r['served_equal'], '1', r['mesh'])
            self.assertGreaterEqual(int(r['reps']), 20, r['mesh'])
            if r['production_status'] == 'ok':
                self.assertEqual((r['equal'], r['mismatches']), ('1', '0'), r['mesh'])


class NativeBench(unittest.TestCase):
    def test_short_run_is_byte_equal_and_parses(self):
        compiler = bench.toolchain()
        if not compiler:
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        with tempfile.TemporaryDirectory(prefix='x3-adjacency-bench-') as tmp:
            exe = bench.build(compiler, tmp)
            result = bench.run(exe, ['--reps', '3', '--meshes', 'realistic-630,split-16'], timeout=300)
        self.assertEqual(result.returncode, 0, result.stdout[-2000:] + result.stderr[-2000:])
        self.assertIn('BENCH_DONE failures=0', result.stdout)
        rows = bench.rows(result.stdout)
        self.assertGreaterEqual(len(rows), 2)
        self.assertEqual({r['mesh'] for r in rows}, {'realistic-630', 'split-16'})
        for r in rows:
            self.assertEqual((r['equal'], r['served_equal'], r['mismatches']), ('1', '1', '0'), r)
            self.assertEqual(r['production_status'], 'ok')
            self.assertGreaterEqual(int(r['reps']), 3)
        self.assertEqual(bench.parse_table(bench.table(rows)), rows)
        realistic = next(r for r in rows if r['mesh'] == 'realistic-630')
        self.assertEqual((realistic['faces'], realistic['vertices']), ('630', '948'))
        self.assertAlmostEqual(float(realistic['welded_pct']), 44.1, delta=0.1)


if __name__ == '__main__':
    unittest.main()
