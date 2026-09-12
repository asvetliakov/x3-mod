"""Host tests of the exact-equality adjacency algorithm (pure module + Python port).

The Python port (tools/analysis/mesh_adjacency_reference.py) is tested on the
edge cases directly; the C++ module (src/proxy/mesh_adjacency_fast.cpp) is built
with the host compiler through verification/probe/mesh_adjacency_fast_host.cpp
and cross-checked against the port on the same cases and on random meshes.
Byte-for-byte equality with d3dx9_37 is established separately by the Wine
fixture (verification/probe/mesh_adjacency_fast_fixture.cpp).
"""
import random
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import mesh_adjacency_reference as ref  # noqa: E402

U = ref.UNUSED
Q = ref.quantized_bits
ZERO, NEG_ZERO, NAN = 0x00000000, 0x80000000, 0x7FC00000


def P(x, y, z):
    return (Q(x), Q(y), Q(z))


def quad():
    # Two triangles over four grid vertices, consistent winding: 0-1-2 and 0-2-3.
    return [P(0, 0, 0), P(16, 0, 0), P(16, 16, 0), P(0, 16, 0)], [(0, 1, 2), (0, 2, 3)]


def fan(third_normal_tilt, order=(0, 1, 2)):
    """Three faces on one edge 0-1: face A uses 0->1, faces B and C use 1->0.

    B lies in the plane of A folded back (normal parallel to A's when tilt 0);
    C is tilted by third_normal_tilt grid units. `order` places A, B, C in the
    index buffer so that bucket order and normal preference can be separated.
    """
    vertices = [P(0, 0, 0), P(16, 0, 0), P(8, 16, 0), P(8, -16, 0), P(8, -16, third_normal_tilt)]
    faces = {0: (0, 1, 2), 1: (1, 0, 3), 2: (1, 0, 4)}
    return vertices, [faces[i] for i in order]


class ReferenceCases(unittest.TestCase):
    def test_shared_edge(self):
        v, f = quad()
        status, report, adjacency = ref.generate(v, f, 1e-6)
        self.assertEqual(status, 'ok')
        self.assertEqual(adjacency, [U, U, 1, 0, U, U])
        self.assertTrue(report['quantized'])
        self.assertEqual(report['welded'], 0)

    def test_exact_duplicates_weld(self):
        v, f = quad()
        v += [P(0, 0, 0), P(16, 16, 0)]           # duplicates of 0 and 2
        f = [(0, 1, 2), (4, 5, 3)]                 # second face through the duplicates
        status, report, adjacency = ref.generate(v, f, 1e-6)
        self.assertEqual(status, 'ok')
        self.assertEqual(report['welded'], 2)
        self.assertEqual(adjacency, [U, U, 1, 0, U, U])

    def test_grid_neighbours_do_not_weld(self):
        v, f = quad()
        v += [P(1, 0, 0), P(16, 16, 1)]            # one quantum (6.1e-5) away, 61x the epsilon
        f = [(0, 1, 2), (4, 5, 3)]
        status, report, adjacency = ref.generate(v, f, 1e-6)
        self.assertEqual(status, 'ok')
        self.assertTrue(report['quantized'])
        self.assertEqual(report['welded'], 0)
        self.assertEqual(adjacency, [U] * 6)

    def test_signed_zero_welds(self):
        v, f = quad()
        v += [(NEG_ZERO, ZERO, NEG_ZERO), (Q(16), Q(16), NEG_ZERO)]
        f = [(0, 1, 2), (4, 5, 3)]
        status, report, adjacency = ref.generate(v, f, 1e-6)
        self.assertEqual(status, 'ok')
        self.assertEqual(report['welded'], 2)
        self.assertEqual(adjacency, [U, U, 1, 0, U, U])

    def test_nan_and_infinity_bypass(self):
        v, f = quad()
        v[3] = (NAN, ZERO, ZERO)
        self.assertEqual(ref.generate(v, f, 1e-6)[0], 'non_finite')
        v[3] = (0x7F800000, ZERO, ZERO)
        self.assertEqual(ref.generate(v, f, 1e-6)[0], 'non_finite')

    def test_bad_epsilon_and_index(self):
        v, f = quad()
        self.assertEqual(ref.generate(v, f, -1.0)[0], 'input')
        self.assertEqual(ref.generate(v, f, float('nan'))[0], 'input')
        self.assertEqual(ref.generate(v, [(0, 1, 9)], 1e-6)[0], 'index_range')

    def test_unquantized_gate(self):
        v, f = quad()
        v[1] = tuple(ref.float_to_bits(c) for c in (0.3, 0.0, 0.0))  # not on the 2^-18 grid
        status, report, _ = ref.generate(v, f, 1e-6)
        self.assertEqual(status, 'ok')
        self.assertFalse(report['quantized'])
        v[3] = tuple(ref.float_to_bits(c) for c in (0.3 + 1.5e-6, 0.0, 0.0))  # within 2*eps of vertex 1
        self.assertEqual(ref.generate(v, f, 1e-6)[0], 'epsilon_neighbour')
        v[3] = tuple(ref.float_to_bits(c) for c in (0.3 + 4e-6, 0.0, 0.0))    # outside the 2*eps margin
        self.assertEqual(ref.generate(v, f, 1e-6)[0], 'ok')
        self.assertEqual(ref.generate(v, f, 0.0)[0], 'ok')                    # epsilon 0 needs no gate

    def test_three_faces_on_one_edge(self):
        # Coplanar B (index 1) and tilted C (index 2): A pairs the parallel one
        # (B) although C precedes it in the bucket (head insertion); C is unmatched.
        v, f = fan(16)
        status, report, adjacency = ref.generate(v, f, 1e-6)
        self.assertEqual(status, 'ok')
        self.assertEqual(report['multi_candidates'], 1)
        self.assertEqual(adjacency, [1, U, U, 0, U, U, U, U, U])
        # Without normal selection the head of the bucket (the later face C) wins.
        _, _, adjacency = ref.generate(v, f, 1e-6, normal_selection=False)
        self.assertEqual(adjacency, [2, U, U, U, U, U, 0, U, U])
        # Tail insertion pairs the earlier face B regardless of normals.
        _, _, adjacency = ref.generate(v, f, 1e-6, head_insertion=False, normal_selection=False)
        self.assertEqual(adjacency, [1, U, U, 0, U, U, U, U, U])
        # Equal normals: the tie keeps the first candidate in bucket order (C, index 2).
        v, f = fan(0)
        _, report, adjacency = ref.generate(v, f, 1e-6)
        self.assertEqual(report['normal_selected'], 0)
        self.assertEqual(adjacency, [2, U, U, U, U, U, 0, U, U])

    def test_degenerate_faces(self):
        # D3DX (fixture evidence): a face with a repeated index contributes no
        # edge and pairs with nothing.
        v, _ = quad()
        f = [(0, 1, 0), (2, 2, 2), (0, 1, 2)]
        status, report, adjacency = ref.generate(v, f, 1e-6)
        self.assertEqual(status, 'ok')
        self.assertEqual(report['degenerate_faces'], 2)
        self.assertEqual(adjacency, [U] * 9)
        f = [(0, 1, 0), (1, 0, 2)]
        self.assertEqual(ref.generate(v, f, 1e-6)[2], [U] * 6)
        # Without the raw skip face 0's 0->1 pairs face 1's 1->0.
        _, _, adjacency = ref.generate(v, f, 1e-6, skip_raw_degenerate=False)
        self.assertEqual(adjacency, [1, U, U, 0, U, U])
        # A face degenerate only through welding (4 == 0): of two corners sharing
        # a representative the larger raw index is invalid and both edges touching
        # it are skipped. (0,1,4) keeps 0->1 only: it pairs face 1 and face 2's
        # 0->1 finds no 1->0 (D3DX: [1,U,U, 0,U,U, U,U,U]).
        v += [P(0, 0, 0)]
        f = [(0, 1, 4), (1, 0, 2), (0, 1, 3)]
        status, report, adjacency = ref.generate(v, f, 1e-6)
        self.assertEqual((report['welded_degenerate_faces'], report['dropped_edges']), (1, 2))
        self.assertEqual(adjacency, [1, U, U, 0, U, U, U, U, U])
        _, _, adjacency = ref.generate(v, f, 1e-6, skip_rep_degenerate=True)
        self.assertEqual(adjacency, [U, U, U, 2, U, U, 1, U, U])
        _, _, adjacency = ref.generate(v, f, 1e-6, drop_welded_corners=False)
        self.assertEqual(adjacency, [1, 2, U, 0, U, U, 0, U, U])
        # (0,4,1) keeps only 1->0 (point 2): D3DX pairs it with (0,1,2), not (1,0,2);
        # (1,0,4) keeps only 1->0 (point 0); (4,0,1) keeps only 0->1 (point 1) and
        # (4,1,0) only 1->0 (point 1): the larger index is the invalid corner, not
        # the later one.
        self.assertEqual(ref.generate(v, [(0, 4, 1), (1, 0, 2)], 1e-6)[2], [U] * 6)
        self.assertEqual(ref.generate(v, [(0, 4, 1), (0, 1, 2)], 1e-6)[2], [U, U, 1, 0, U, U])
        self.assertEqual(ref.generate(v, [(1, 0, 4), (0, 1, 2)], 1e-6)[2], [1, U, U, 0, U, U])
        self.assertEqual(ref.generate(v, [(1, 0, 4), (1, 0, 2)], 1e-6)[2], [U] * 6)
        self.assertEqual(ref.generate(v, [(4, 0, 1), (0, 1, 2)], 1e-6)[2], [U] * 6)
        self.assertEqual(ref.generate(v, [(4, 0, 1), (1, 0, 2)], 1e-6)[2], [U, 1, U, 0, U, U])
        self.assertEqual(ref.generate(v, [(4, 1, 0), (1, 0, 2), (0, 1, 3)], 1e-6)[2], [U, 2, U, U, U, U, 0, U, U])
        # Two welded duplicates of a corner outside the face: the larger index (5)
        # is invalid whatever its position (D3DX: (4,5,1) keeps 1->4 only).
        v += [P(0, 0, 0)]  # vertex 5, also welded to 0
        self.assertEqual(ref.generate(v, [(4, 5, 1), (1, 0, 2), (0, 1, 3)], 1e-6)[2], [U, U, 2, U, U, U, 0, U, U])
        self.assertEqual(ref.generate(v, [(5, 4, 1), (1, 0, 2)], 1e-6)[2], [U, 1, U, 0, U, U])
        self.assertEqual(ref.generate(v, [(5, 4, 1), (0, 1, 3)], 1e-6)[2], [U] * 6)
        # Processed second, the welded face is still preferred by normal selection
        # (its zero normal scores 0 against the anti-parallel face's -1).
        self.assertEqual(ref.generate(v, [(1, 0, 2), (0, 1, 4), (0, 1, 3)], 1e-6)[2], [1, U, U, 0, U, U, U, U, U])


    def test_single_adjacency(self):
        # D3DX: two faces never become adjacent across a second edge. The
        # reversed duplicate f2 pairs f0 on 0->1 and f1 on 1->2; every other
        # shared edge stays unmatched.
        v, _ = quad()
        f = [(0, 1, 2), (0, 1, 2), (2, 1, 0)]
        status, report, adjacency = ref.generate(v, f, 1e-6)
        self.assertEqual(adjacency, [2, U, U, U, 2, U, 1, 0, U])
        self.assertGreater(report['repeated_neighbours'], 0)
        _, _, adjacency = ref.generate(v, f, 1e-6, single_adjacency=False)
        self.assertEqual(adjacency, [2, 2, 2, U, U, U, 0, 0, 0])
        # The refusal comes after selection: f0's 1->2 selects f2 (bucket head,
        # equal normals), is refused, and its own entry is retired, so f1 finds
        # nothing later (D3DX: [2,U,U, U,U,U, U,0,U]).
        f = [(0, 1, 2), (2, 1, 3), (2, 1, 0)]
        self.assertEqual(ref.generate(v, f, 1e-6)[2], [2, U, U, U, U, U, U, 0, U])


class HostModule(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('c++') or shutil.which('clang++') or shutil.which('g++')
        if compiler is None:
            raise unittest.SkipTest('no host C++ compiler')
        cls.directory = tempfile.mkdtemp(prefix='x3-mesh-adjacency-')
        cls.exe = Path(cls.directory) / 'driver'
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'verification/probe/mesh_adjacency_fast_host.cpp'),
                        str(ROOT / 'src/proxy/mesh_adjacency_fast.cpp'), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.directory, ignore_errors=True)

    def run_driver(self, vertices, faces, epsilon, bits=16, stride=20, offset=4, head=True, normal=True, skip_raw=True, skip_rep=False, drop=True, single=True):
        text = f'{len(vertices)} {len(faces)} {bits} {epsilon!r} {stride} {offset} {int(head)} {int(normal)} {int(skip_raw)} {int(skip_rep)} {int(drop)} {int(single)}\n'
        text += ''.join(f'{x:08x} {y:08x} {z:08x}\n' for x, y, z in vertices)
        text += ''.join(f'{a} {b} {c}\n' for a, b, c in faces)
        out = subprocess.run([str(self.exe)], input=text, capture_output=True, text=True, check=True).stdout.splitlines()
        head_fields = out[0].split()
        report = dict(representatives=int(head_fields[1]), welded=int(head_fields[2]), quantized=bool(int(head_fields[3])),
                      degenerate_faces=int(head_fields[4]), welded_degenerate_faces=int(head_fields[5]), dropped_edges=int(head_fields[6]),
                      multi_candidates=int(head_fields[7]), normal_selected=int(head_fields[8]), repeated_neighbours=int(head_fields[9]), unmatched=int(head_fields[10]))
        adjacency = [U if v == '-1' else int(v) for v in out[1].split()] if head_fields[0] == 'ok' else None
        return head_fields[0], report, adjacency

    def check(self, vertices, faces, epsilon, **kw):
        expected = ref.generate(vertices, faces, epsilon, head_insertion=kw.get('head', True),
                                normal_selection=kw.get('normal', True), skip_raw_degenerate=kw.get('skip_raw', True),
                                skip_rep_degenerate=kw.get('skip_rep', False), drop_welded_corners=kw.get('drop', True), single_adjacency=kw.get('single', True))
        for bits in (16, 32):
            actual = self.run_driver(vertices, faces, epsilon, bits=bits, **kw)
            self.assertEqual(actual, expected, (bits, kw))
        return expected

    def test_edge_cases_match_reference(self):
        v, f = quad()
        self.assertEqual(self.check(v, f, 1e-6)[2], [U, U, 1, 0, U, U])
        self.check(v + [P(0, 0, 0), P(16, 16, 0)], [(0, 1, 2), (4, 5, 3)], 1e-6)
        self.check(v + [P(1, 0, 0), P(16, 16, 1)], [(0, 1, 2), (4, 5, 3)], 1e-6)
        self.check(v + [(NEG_ZERO, ZERO, NEG_ZERO), (Q(16), Q(16), NEG_ZERO)], [(0, 1, 2), (4, 5, 3)], 1e-6)
        self.assertEqual(self.check(v[:3] + [(NAN, ZERO, ZERO)], f, 1e-6)[0], 'non_finite')
        self.assertEqual(self.check(v, [(0, 1, 9)], 1e-6)[0], 'index_range')
        self.assertEqual(self.check(v, f, -1.0)[0], 'input')
        for tilt in (0, 16):
            for order in ((0, 1, 2), (1, 0, 2), (2, 1, 0)):
                vv, ff = fan(tilt, order)
                for head in (True, False):
                    for normal in (True, False):
                        self.check(vv, ff, 1e-6, head=head, normal=normal)
        welded = v + [P(0, 0, 0)]
        for kw in ({}, dict(skip_raw=False), dict(skip_rep=True), dict(drop=False), dict(single=False)):
            self.check(v, [(0, 1, 2), (0, 1, 2), (2, 1, 0)], 1e-6, **kw)
            self.check(v, [(0, 1, 2), (2, 1, 3), (2, 1, 0)], 1e-6, **kw)
            self.check(v, [(0, 1, 0), (2, 2, 2), (0, 1, 2)], 1e-6, **kw)
            self.check(v, [(0, 1, 0), (1, 0, 2)], 1e-6, **kw)
            self.check(welded, [(0, 1, 4), (1, 0, 2), (0, 1, 3)], 1e-6, **kw)
            self.check(welded, [(0, 4, 1), (1, 0, 2)], 1e-6, **kw)
            self.check(welded, [(0, 4, 1), (0, 1, 2)], 1e-6, **kw)
            self.check(welded, [(1, 0, 4), (0, 1, 2)], 1e-6, **kw)
            self.check(welded, [(1, 0, 4), (1, 0, 2)], 1e-6, **kw)
            self.check(welded, [(4, 0, 1), (0, 1, 2)], 1e-6, **kw)
            self.check(welded, [(4, 1, 0), (1, 0, 2), (0, 1, 3)], 1e-6, **kw)
            self.check(welded + [P(0, 0, 0)], [(4, 5, 1), (1, 0, 2), (0, 1, 3)], 1e-6, **kw)
            self.check(welded + [P(0, 0, 0)], [(5, 4, 1), (1, 0, 2)], 1e-6, **kw)
            self.check(welded + [P(0, 0, 0)], [(5, 4, 1), (0, 1, 3)], 1e-6, **kw)
            self.check(welded, [(1, 0, 2), (0, 1, 4), (0, 1, 3)], 1e-6, **kw)
        unq = list(v)
        unq[1] = tuple(ref.float_to_bits(c) for c in (0.3, 0.0, 0.0))
        self.assertFalse(self.check(unq, f, 1e-6)[1]['quantized'])
        unq[3] = tuple(ref.float_to_bits(c) for c in (0.3 + 1.5e-6, 0.0, 0.0))
        self.assertEqual(self.check(unq, f, 1e-6)[0], 'epsilon_neighbour')
        unq[3] = tuple(ref.float_to_bits(c) for c in (0.3 + 4e-6, 0.0, 0.0))
        self.assertEqual(self.check(unq, f, 1e-6)[0], 'ok')

    def test_random_meshes_match_reference(self):
        rng = random.Random(20260912)
        for trial in range(60):
            count = rng.randint(3, 14)
            span = rng.choice((2, 3, 8))
            vertices = [P(rng.randint(-span, span), rng.randint(-span, span), rng.randint(-span, span)) for _ in range(count)]
            if trial % 4 == 0:
                vertices += [vertices[rng.randrange(count)] for _ in range(3)]  # exact duplicates
            if trial % 5 == 0:
                vertices += [(NEG_ZERO, ZERO, ZERO), (ZERO, NEG_ZERO, ZERO)]
            faces = [tuple(rng.randrange(len(vertices)) for _ in range(3)) for _ in range(rng.randint(1, 24))]
            for kw in ({}, dict(normal=False), dict(head=False), dict(skip_raw=False), dict(skip_rep=True), dict(drop=False), dict(single=False)):
                expected = self.check(vertices, faces, 1e-6, **kw)
                self.assertEqual(expected[0], 'ok')
                # Mutual pairing: every neighbour lists the face back.
                adjacency = expected[2]
                for face_index, face_adjacency in enumerate(zip(*[iter(adjacency)] * 3)):
                    for neighbour in face_adjacency:
                        if neighbour != U:
                            self.assertIn(face_index, adjacency[neighbour * 3:neighbour * 3 + 3])


if __name__ == '__main__':
    unittest.main()
