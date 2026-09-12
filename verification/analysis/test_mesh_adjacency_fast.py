"""Host tests of the D3DX-rule adjacency algorithm (pure module + Python port).

The Python port (tools/analysis/mesh_adjacency_reference.py) is tested on the
edge cases directly; the C++ module (src/proxy/mesh_adjacency_fast.cpp) is built
with the host compiler through verification/probe/mesh_adjacency_fast_host.cpp
and cross-checked against the port on the same cases and on random meshes.
Byte-for-byte equality with d3dx9_37 is established separately by the Wine
fixture (verification/probe/mesh_adjacency_fast_fixture.cpp); the expectations
marked D3DX below are that fixture's native outputs.
"""
import math
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
POLICIES = ('head_insertion', 'normal_selection', 'weld_refusal', 'heap_order', 'retire_own_entry', 'unlink_refused', 'later_slot_check')
DEFAULTS = dict(head_insertion=True, normal_selection=True, weld_refusal=True, heap_order=True, retire_own_entry=False, unlink_refused=True, later_slot_check=False)


def P(x, y, z):
    return (Q(x), Q(y), Q(z))


def quad():
    # Two triangles over four grid vertices, consistent winding: 0-1-2 and 0-2-3.
    return [P(0, 0, 0), P(16, 0, 0), P(16, 16, 0), P(0, 16, 0)], [(0, 1, 2), (0, 2, 3)]


def fan(third_normal_tilt, order=(0, 1, 2)):
    """Three faces on one edge 0-1: face A uses 0->1, faces B and C use 1->0.

    B lies in the plane of A folded back (normal parallel to A's when tilt 0);
    C is tilted by third_normal_tilt grid units. `order` places A, B, C in the
    index buffer so that chain order and normal preference can be separated.
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
        v, f = quad()
        self.assertEqual(ref.generate(v, f, 1e-6, head_bits=[(ZERO,) * 3, (NAN, ZERO, ZERO), (ZERO,) * 3, (ZERO,) * 3])[0], 'non_finite')

    def test_bad_epsilon_and_index(self):
        v, f = quad()
        self.assertEqual(ref.generate(v, f, -1.0)[0], 'input')
        self.assertEqual(ref.generate(v, f, float('nan'))[0], 'input')
        self.assertEqual(ref.generate(v, [(0, 1, 9)], 1e-6)[0], 'index_range')
        self.assertEqual(ref.generate(v[:2], [(0, 1, 1)], 1e-6)[0], 'input')  # D3DX sizes its edge table V/3

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

    def test_heapsort_order(self):
        # D3DX's heap sort: descending keys, the heap's permutation among equal keys.
        self.assertEqual(ref.heapsort_order([0.0, 1.0, 1.0, 0.0, 0.0, 0.0]), [2, 1, 3, 4, 0, 5])
        self.assertEqual(ref.heapsort_order([]), [])
        self.assertEqual(ref.heapsort_order([5.0]), [0])
        self.assertEqual(ref.heapsort_order([1.0, 2.0]), [1, 0])
        for n in range(2, 40):
            keys = [random.Random(n).choice((0.0, 1.0, -2.5, 3.0)) for _ in range(n)]
            order = ref.heapsort_order(keys)
            self.assertEqual(sorted(order), list(range(n)))
            self.assertEqual([keys[v] for v in order], sorted(keys, reverse=True))

    def test_three_faces_on_one_edge(self):
        # Coplanar B (index 1) and tilted C (index 2): A pairs the parallel one
        # (B) although C precedes it in the chain (head insertion); C is unmatched.
        v, f = fan(16)
        status, report, adjacency = ref.generate(v, f, 1e-6)
        self.assertEqual(status, 'ok')
        self.assertEqual(report['multi_candidates'], 1)
        self.assertEqual(adjacency, [1, U, U, 0, U, U, U, U, U])
        # Without normal selection the head of the chain (the later face C) wins.
        _, _, adjacency = ref.generate(v, f, 1e-6, normal_selection=False)
        self.assertEqual(adjacency, [2, U, U, U, U, U, 0, U, U])
        # Tail insertion pairs the earlier face B regardless of normals.
        _, _, adjacency = ref.generate(v, f, 1e-6, head_insertion=False, normal_selection=False)
        self.assertEqual(adjacency, [1, U, U, 0, U, U, U, U, U])
        # Equal normals: the tie keeps the first candidate in chain order (C, index 2).
        v, f = fan(0)
        _, report, adjacency = ref.generate(v, f, 1e-6)
        self.assertEqual(report['normal_selected'], 0)
        self.assertEqual(adjacency, [2, U, U, U, U, U, 0, U, U])

    def test_degenerate_faces(self):
        # D3DX: a face with a repeated index contributes no edge and pairs with nothing.
        v, _ = quad()
        f = [(0, 1, 0), (2, 2, 2), (0, 1, 2)]
        status, report, adjacency = ref.generate(v, f, 1e-6)
        self.assertEqual(status, 'ok')
        self.assertEqual(report['degenerate_faces'], 2)
        self.assertEqual(adjacency, [U] * 9)
        f = [(0, 1, 0), (1, 0, 2)]
        self.assertEqual(ref.generate(v, f, 1e-6)[2], [U] * 6)

    def test_weld_refusal(self):
        # D3DX never welds two vertices that a face references together, so a
        # duplicate corner keeps its own representative and the face stays
        # non-degenerate: (0,1,4) with 4 at the position of 0 inserts 0->1, 1->4
        # and 4->0, pairs face 1 on 0->1 and leaves face 2's 0->1 without a
        # partner (D3DX: [1,U,U, 0,U,U, U,U,U]).
        v, _ = quad()
        v += [P(0, 0, 0)]
        f = [(0, 1, 4), (1, 0, 2), (0, 1, 3)]
        status, report, adjacency = ref.generate(v, f, 1e-6)
        self.assertEqual((report['refused_welds'], report['welded'], report['welded_degenerate_faces']), (1, 0, 0))
        self.assertEqual(adjacency, [1, U, U, 0, U, U, U, U, U])
        # Welding 4 onto 0 instead makes the face degenerate: nothing is inserted,
        # and faces 1 and 2 pair each other on 1->0 / 0->1.
        _, report, adjacency = ref.generate(v, f, 1e-6, weld_refusal=False)
        self.assertEqual((report['welded'], report['welded_degenerate_faces']), (1, 1))
        self.assertEqual(adjacency, [U, U, U, 2, U, U, 1, U, U])
        # More D3DX outputs of the same rule.
        self.assertEqual(ref.generate(v, [(0, 4, 1), (1, 0, 2)], 1e-6)[2], [U] * 6)
        self.assertEqual(ref.generate(v, [(0, 4, 1), (0, 1, 2)], 1e-6)[2], [U, U, 1, 0, U, U])
        self.assertEqual(ref.generate(v, [(1, 0, 4), (0, 1, 2)], 1e-6)[2], [1, U, U, 0, U, U])
        self.assertEqual(ref.generate(v, [(1, 0, 4), (1, 0, 2)], 1e-6)[2], [U] * 6)
        self.assertEqual(ref.generate(v, [(4, 0, 1), (0, 1, 2)], 1e-6)[2], [U] * 6)
        self.assertEqual(ref.generate(v, [(4, 0, 1), (1, 0, 2)], 1e-6)[2], [U, 1, U, 0, U, U])
        self.assertEqual(ref.generate(v, [(4, 1, 0), (1, 0, 2), (0, 1, 3)], 1e-6)[2], [U, 2, U, U, U, U, 0, U, U])
        # Three vertices at one position, two of them in one face (4,5,1): the sweep
        # order (heap permutation [.., 4, 0, 5] of the equal keys) makes 4 the
        # representative, 0 welds onto it and 5 is refused (D3DX: (4,5,1) pairs
        # (0,1,3) on 1->4 / 4->1).
        v += [P(0, 0, 0)]
        status, report, adjacency = ref.generate(v, [(4, 5, 1), (1, 0, 2), (0, 1, 3)], 1e-6)
        self.assertEqual((report['welded'], report['refused_welds']), (1, 1))
        self.assertEqual(adjacency, [U, U, 2, U, U, U, 0, U, U])
        self.assertEqual(ref.generate(v, [(5, 4, 1), (1, 0, 2)], 1e-6)[2], [U, 1, U, 0, U, U])
        self.assertEqual(ref.generate(v, [(5, 4, 1), (0, 1, 3)], 1e-6)[2], [U] * 6)
        self.assertEqual(ref.generate(v, [(1, 0, 2), (0, 1, 4), (0, 1, 3)], 1e-6)[2], [1, U, U, 0, U, U, U, U, U])
        # A stable index-order sweep welds 4 and 5 onto 0: (4,5,1) degenerates and
        # the other two faces pair each other instead.
        self.assertEqual(ref.generate(v, [(4, 5, 1), (1, 0, 2), (0, 1, 3)], 1e-6, heap_order=False)[2], [U, U, U, 2, U, U, 1, U, U])

    def test_key_window(self):
        # The sweep key is the float at byte 0 of the vertex, whatever element it
        # is: equal positions with keys further apart than epsilon do not weld.
        v, _ = quad()
        v += [P(0, 0, 0), P(16, 16, 0)]
        f = [(0, 1, 2), (4, 5, 3)]
        same = [(ZERO,) * 3] * 6
        self.assertEqual(ref.generate(v, f, 1e-6, head_bits=same)[2], [U, U, 1, 0, U, U])
        apart = [(ZERO,) * 3] * 4 + [(Q(1), ZERO, ZERO)] * 2
        _, report, adjacency = ref.generate(v, f, 1e-6, head_bits=apart)
        self.assertEqual(report['welded'], 0)
        self.assertEqual(adjacency, [U] * 6)

    def test_head_normals(self):
        # The candidate selection's normals also come from the three floats at
        # byte 0 (D3DX passes the vertex stride but no offset to the score): with
        # TEXCOORD first and identical byte-0 triples the normals are zero, the
        # scores tie, and the chain head (the later face C) wins over the
        # geometrically parallel B.
        v, f = fan(16)
        self.assertEqual(ref.generate(v, f, 1e-6)[2], [1, U, U, 0, U, U, U, U, U])
        self.assertEqual(ref.generate(v, f, 1e-6, head_bits=[(ZERO,) * 3] * 5)[2], [2, U, U, U, U, U, 0, U, U])

    def test_epsilon_zero(self):
        v, _ = quad()
        v += [P(0, 0, 0), P(16, 16, 0)]
        self.assertEqual(ref.generate(v, [(0, 1, 2), (4, 5, 3)], 0.0)[2], [U, U, 1, 0, U, U])
        # Exact matching with the same face refusal; the second duplicate finds the
        # older representative that shares no face with it.
        v += [P(0, 0, 0)]  # vertex 6
        # Vertex 4 is refused by 0 (face 0 holds both) and becomes a representative;
        # vertex 6 takes the most recent one, 4, so (6,1,3) reads (4,1,3) and pairs
        # (0,1,4) on 1->4 / 4->1.
        _, report, adjacency = ref.generate(v, [(0, 1, 4), (6, 1, 3), (1, 0, 2)], 0.0)
        self.assertEqual(report['representatives'], 5)
        self.assertEqual(adjacency, [2, 1, U, 0, U, U, 0, U, U])

    def test_single_adjacency(self):
        # D3DX: the reversed duplicate f2 pairs f0 on 0->1 and f1 on 1->2 (the
        # reverse of f2's 2->1); every other shared edge stays unmatched.
        v, _ = quad()
        f = [(0, 1, 2), (0, 1, 2), (2, 1, 0)]
        status, report, adjacency = ref.generate(v, f, 1e-6)
        self.assertEqual(adjacency, [2, U, U, U, 2, U, 1, 0, U])
        self.assertGreater(report['repeated_neighbours'], 0)
        # The refusal comes after selection: f0's 1->2 selects f2 (chain head,
        # equal normals), is refused, and both entries leave the table, so f1
        # finds nothing later (D3DX: [2,U,U, U,U,U, U,0,U]).
        f = [(0, 1, 2), (2, 1, 3), (2, 1, 0)]
        self.assertEqual(ref.generate(v, f, 1e-6)[2], [2, U, U, U, U, U, U, 0, U])
        # Keeping the refused entry changes nothing here (f0's own 1->2 entry is
        # removed either way); the variant is exercised by the random cross-check.
        self.assertEqual(ref.generate(v, f, 1e-6, unlink_refused=False)[2], [2, U, U, U, U, U, U, 0, U])

    def test_own_entry_survives_failed_lookup(self):
        # D3DX removes the querying edge only after a successful lookup. Face 0
        # (0,1,2) pairs face 2 (2,1,0) on 0->1, then its 1->2 and 2->0 lookups
        # select face 2 again and are refused, consuming face 2's 2->1 and 0->2
        # entries. Face 1 (1,2,4) then finds no 2->1, so its own 1->2 entry
        # stays, and face 2's 2->1 lookup pairs it: [2,U,U, 2,U,U, 1,0,U]. The
        # pre-review-26 module retired face 1's entry and left both unmatched.
        v, _ = quad()
        v += [P(8, 8, 16)]
        f = [(0, 1, 2), (1, 2, 4), (2, 1, 0)]
        status, report, adjacency = ref.generate(v, f, 1e-6)
        self.assertEqual(status, 'ok')
        self.assertEqual(adjacency, [2, U, U, 2, U, U, 1, 0, U])
        self.assertEqual(report['repeated_neighbours'], 2)
        self.assertEqual(ref.generate(v, f, 1e-6, retire_own_entry=True)[2], [2, U, U, U, U, U, U, 0, U])


class HostModule(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('c++') or shutil.which('clang++') or shutil.which('g++')
        if compiler is None:
            raise unittest.SkipTest('no host C++ compiler')
        cls.directory = tempfile.mkdtemp(prefix='x3-mesh-adjacency-')
        cls.exe = Path(cls.directory) / 'driver'
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                        str(ROOT / 'verification/probe/mesh_adjacency_fast_host.cpp'),
                        str(ROOT / 'src/proxy/mesh_adjacency_fast.cpp'), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.directory, ignore_errors=True)

    def run_driver(self, vertices, faces, epsilon, bits=16, stride=20, offset=0, heads=None, normalize='sse2', **policy):
        flags = dict(DEFAULTS, **policy)
        heads = heads if heads is not None else vertices
        text = (f'{len(vertices)} {len(faces)} {bits} {epsilon!r} {stride} {offset} ' + ' '.join(str(int(flags[p])) for p in POLICIES)
                + f' {int(normalize == "generic")}\n')
        text += ''.join(f'{x:08x} {y:08x} {z:08x} {hx:08x} {hy:08x} {hz:08x}\n' for (x, y, z), (hx, hy, hz) in zip(vertices, heads))
        text += ''.join(f'{a} {b} {c}\n' for a, b, c in faces)
        out = subprocess.run([str(self.exe)], input=text, capture_output=True, text=True, check=True).stdout.splitlines()
        head_fields = out[0].split()
        report = dict(representatives=int(head_fields[1]), welded=int(head_fields[2]), quantized=bool(int(head_fields[3])),
                      degenerate_faces=int(head_fields[4]), welded_degenerate_faces=int(head_fields[5]), refused_welds=int(head_fields[6]),
                      multi_candidates=int(head_fields[7]), normal_selected=int(head_fields[8]), repeated_neighbours=int(head_fields[9]), unmatched=int(head_fields[10]))
        self.assertEqual(head_fields[11], 'portable')  # the host build has no rsqrtss; the Python port uses the same 1/sqrt
        self.assertEqual(head_fields[12], normalize)
        adjacency = [U if v == '-1' else int(v) for v in out[1].split()] if head_fields[0] == 'ok' else None
        return head_fields[0], report, adjacency

    def check(self, vertices, faces, epsilon, offset=0, heads=None, **policy):
        head_bits = heads if heads is not None else ([(ZERO,) * 3] * len(vertices) if offset else None)
        # The reference sees what the driver's vertex holds at bytes 0-11: the head
        # triple, overwritten by the position where the two overlap (offset < 12).
        effective = None if head_bits is None else [tuple(h[i] if i * 4 < offset else p[(i * 4 - offset) // 4] for i in range(3)) for h, p in zip(head_bits, vertices)]
        expected = ref.generate(vertices, faces, epsilon, head_bits=effective, **policy)
        for bits in (16, 32):
            actual = self.run_driver(vertices, faces, epsilon, bits=bits, offset=offset, heads=head_bits, **policy)
            self.assertEqual(actual, expected, (bits, offset, policy))
        return expected

    def test_edge_cases_match_reference(self):
        v, f = quad()
        self.assertEqual(self.check(v, f, 1e-6)[2], [U, U, 1, 0, U, U])
        self.assertEqual(self.check(v, f, 1e-6, offset=4)[2], [U, U, 1, 0, U, U])
        self.check(v + [P(0, 0, 0), P(16, 16, 0)], [(0, 1, 2), (4, 5, 3)], 1e-6)
        self.check(v + [P(0, 0, 0), P(16, 16, 0)], [(0, 1, 2), (4, 5, 3)], 1e-6, offset=4, heads=[(ZERO,) * 3] * 4 + [(Q(1), ZERO, ZERO)] * 2)
        self.check(v + [P(1, 0, 0), P(16, 16, 1)], [(0, 1, 2), (4, 5, 3)], 1e-6)
        self.check(v + [(NEG_ZERO, ZERO, NEG_ZERO), (Q(16), Q(16), NEG_ZERO)], [(0, 1, 2), (4, 5, 3)], 1e-6)
        self.assertEqual(self.check(v[:3] + [(NAN, ZERO, ZERO)], f, 1e-6)[0], 'non_finite')
        self.assertEqual(self.check(v, f, 1e-6, offset=4, heads=[(ZERO,) * 3, (NAN, ZERO, ZERO), (ZERO,) * 3, (ZERO,) * 3])[0], 'non_finite')
        for tilt in (0, 16):
            vv, ff = fan(tilt)
            self.check(vv, ff, 1e-6, offset=8, heads=[(Q(k), Q(k * 2), ZERO) for k in (0, 1, 1, 0, 3)])
        self.assertEqual(self.check(v, [(0, 1, 9)], 1e-6)[0], 'index_range')
        self.assertEqual(self.check(v, f, -1.0)[0], 'input')
        for tilt in (0, 16):
            for order in ((0, 1, 2), (1, 0, 2), (2, 1, 0)):
                vv, ff = fan(tilt, order)
                for head_insertion in (True, False):
                    for normal_selection in (True, False):
                        self.check(vv, ff, 1e-6, head_insertion=head_insertion, normal_selection=normal_selection)
        welded = v + [P(0, 0, 0)]
        two = welded + [P(0, 0, 0)]
        for policy in ({}, dict(weld_refusal=False), dict(heap_order=False), dict(retire_own_entry=True), dict(unlink_refused=False), dict(later_slot_check=True)):
            for epsilon in (1e-6, 0.0):
                self.check(v, [(0, 1, 2), (0, 1, 2), (2, 1, 0)], epsilon, **policy)
                self.check(v, [(0, 1, 2), (2, 1, 3), (2, 1, 0)], epsilon, **policy)
                self.check(v, [(0, 1, 0), (2, 2, 2), (0, 1, 2)], epsilon, **policy)
                self.check(v, [(0, 1, 0), (1, 0, 2)], epsilon, **policy)
                self.check(welded, [(0, 1, 4), (1, 0, 2), (0, 1, 3)], epsilon, **policy)
                self.check(welded, [(0, 4, 1), (1, 0, 2)], epsilon, **policy)
                self.check(welded, [(0, 4, 1), (0, 1, 2)], epsilon, **policy)
                self.check(welded, [(1, 0, 4), (0, 1, 2)], epsilon, **policy)
                self.check(welded, [(1, 0, 4), (1, 0, 2)], epsilon, **policy)
                self.check(welded, [(4, 0, 1), (0, 1, 2)], epsilon, **policy)
                self.check(welded, [(4, 1, 0), (1, 0, 2), (0, 1, 3)], epsilon, **policy)
                self.check(two, [(4, 5, 1), (1, 0, 2), (0, 1, 3)], epsilon, **policy)
                self.check(two, [(5, 4, 1), (1, 0, 2)], epsilon, **policy)
                self.check(two, [(5, 4, 1), (0, 1, 3)], epsilon, **policy)
                self.check(two + [P(0, 0, 0)], [(0, 1, 4), (6, 1, 3), (1, 0, 2)], epsilon, **policy)
                self.check(welded, [(1, 0, 2), (0, 1, 4), (0, 1, 3)], epsilon, **policy)
                self.check(v + [P(8, 8, 16)], [(0, 1, 2), (1, 2, 4), (2, 1, 0)], epsilon, **policy)
                self.check(v + [P(8, 8, 16)], [(0, 1, 2), (2, 1, 3), (2, 1, 0), (1, 2, 4)], epsilon, **policy)
        unq = list(v)
        unq[1] = tuple(ref.float_to_bits(c) for c in (0.3, 0.0, 0.0))
        self.assertFalse(self.check(unq, f, 1e-6)[1]['quantized'])
        unq[3] = tuple(ref.float_to_bits(c) for c in (0.3 + 1.5e-6, 0.0, 0.0))
        self.assertEqual(self.check(unq, f, 1e-6)[0], 'epsilon_neighbour')
        unq[3] = tuple(ref.float_to_bits(c) for c in (0.3 + 4e-6, 0.0, 0.0))
        self.assertEqual(self.check(unq, f, 1e-6)[0], 'ok')

    def test_order_shortcut_matches_reference(self):
        # The module skips D3DX's heapsort when the position is the key and no face
        # references two distinct vertices of one position class (the engine's split
        # layout): the port always sorts, so equal output on a class-heavy mesh pins
        # the invariance argument; one face touching two copies of a position, or a
        # key that is not the position, must send the module back to the full sweep.
        n = 6
        vertices, faces = [], []

        def corner(x, y):
            vertices.append(P(x - 3, y - 3, (x * 5 + y * 11) % 7))
            return len(vertices) - 1
        for y in range(n - 1):
            for x in range(n - 1):
                faces.append((corner(x, y), corner(x + 1, y), corner(x + 1, y + 1)))
                faces.append((corner(x, y), corner(x + 1, y + 1), corner(x, y + 1)))
        for policy in ({}, dict(heap_order=False), dict(weld_refusal=False)):
            status, report, _ = self.check(vertices, faces, 1e-6, **policy)
            self.assertEqual((status, report['representatives'], report['refused_welds']), ('ok', n * n, 0), policy)
        forced = faces + [(0, 3, 1)]  # vertices 0 and 3 are two copies of corner (0, 0)
        status, report, _ = self.check(vertices, forced, 1e-6)
        self.assertEqual((status, report['refused_welds'] > 0 or report['welded_degenerate_faces'] > 0), ('ok', True))
        self.check(vertices, forced, 1e-6, heap_order=False)
        heads = [(Q(v % 3), ZERO, ZERO) for v in range(len(vertices))]  # key at byte 0 differs from the position (offset 8: the driver's 20-byte vertex)
        status, report, _ = self.check(vertices, faces, 1e-6, offset=8, heads=heads)
        self.assertEqual(status, 'ok')

    def test_random_meshes_match_reference(self):
        rng = random.Random(20260912)
        for trial in range(80):
            count = rng.randint(3, 14)
            span = rng.choice((2, 3, 8))
            vertices = [P(rng.randint(-span, span), rng.randint(-span, span), rng.randint(-span, span)) for _ in range(count)]
            if trial % 4 == 0:
                vertices += [vertices[rng.randrange(count)] for _ in range(3)]  # exact duplicates
            if trial % 5 == 0:
                vertices += [(NEG_ZERO, ZERO, ZERO), (ZERO, NEG_ZERO, ZERO)]
            faces = [tuple(rng.randrange(len(vertices)) for _ in range(3)) for _ in range(rng.randint(1, 24))]
            offset = 4 if trial % 3 == 2 else 0
            heads = [(rng.choice((ZERO, Q(1), Q(-3))), rng.choice((ZERO, Q(2))), rng.choice((ZERO, Q(-1)))) for _ in vertices] if offset else None
            epsilon = 0.0 if trial % 7 == 6 else 1e-6
            for policy in ({}, dict(normal_selection=False), dict(head_insertion=False), dict(weld_refusal=False), dict(heap_order=False),
                           dict(retire_own_entry=True), dict(unlink_refused=False), dict(later_slot_check=True)):
                expected = self.check(vertices, faces, epsilon, offset=offset, heads=heads, **policy)
                self.assertEqual(expected[0], 'ok')
                # Mutual pairing: every neighbour lists the face back.
                adjacency = expected[2]
                for face_index, face_adjacency in enumerate(zip(*[iter(adjacency)] * 3)):
                    for neighbour in face_adjacency:
                        if neighbour != U:
                            self.assertIn(face_index, adjacency[neighbour * 3:neighbour * 3 + 3])

    def test_normalize_paths_match_reference(self):
        # The normalize-dispatch mesh: SSE2 pairs the query with face 1, the generic
        # table (unnormalized copies, exact tie) with the chain head, face 2.
        v, f = near_unit_normals()
        self.assertEqual(self.check(v, f, 1e-6, normalize='sse2')[2][0], 1)
        self.assertEqual(self.check(v, f, 1e-6, normalize='generic')[2][0], 2)
        rng = random.Random(0x5eed)
        for trial in range(60):
            span = 8 if trial % 2 else 20000
            vertices = [P(rng.randint(-span, span), rng.randint(-span, span), rng.randint(-span, span)) for _ in range(rng.randint(3, 9))]
            faces = [tuple(rng.randrange(len(vertices)) for _ in range(3)) for _ in range(rng.randint(1, 16))]
            expected = self.check(vertices, faces, 1e-6, normalize='generic')
            self.assertEqual(expected[0], 'ok')


def near_unit_normals():
    """Query (A, B, C) and candidates (B, A, D1), (B, A, D2) whose cross products
    have |n|^2 within 1e-5 of 1 (generic: copied unnormalized) and tilts of 9 and 29
    grid units (SSE2: normalized, face 1 wins by 1.4e-6)."""
    return [P(0, 0, 0), P(16384, 0, 0), P(-4, 16384, 0), P(-1, -16384, -9), P(-7, -16384, -29)], [(0, 1, 2), (1, 0, 3), (1, 0, 4)]


class GenericNormalize(unittest.TestCase):
    def test_table_generation(self):
        table = ref.RSQRT_TABLE
        self.assertEqual(len(table), 512)
        # Segments 255 and 256 meet at m = 1 where 1/sqrt is exactly 1: a + b == 1.
        for k in (255, 256):
            self.assertEqual(table[k][0] + table[k][1], 1.0)
        self.assertTrue(all(a < 0 for a, _ in table))
        self.assertTrue(all(table[k][0] < table[k + 1][0] for k in range(511)))  # slopes flatten towards m = 2
        for k in (0, 100, 300, 511):
            s = 1.0 if k >> 8 else 0.5
            for m in (s * (1 + (k & 255) / 256), s * (1 + ((k & 255) + 0.5) / 256)):
                self.assertAlmostEqual((table[k][0] * m + table[k][1]) * math.sqrt(m), 1.0, delta=2e-6)

    def test_samples(self):
        g = ref._normalize_generic
        self.assertEqual(g((0.0, 0.0, 0.0)), (0.0, 0.0, 0.0))
        self.assertEqual(g((1.0, 0.0, 0.0)), (1.0, 0.0, 0.0))
        near = (ref.f32(0.999996), ref.f32(0.002), 0.0)  # |v|^2 - 1 within 1e-5: copied unnormalized
        self.assertEqual(g(near), near)
        for v in ((3.0, 4.0, 0.0), (1.0, 1.0, 1.0), (-2.5e-4, 7.75e-3, -0.9991), (100.0, 200.0, 300.0), (1e-10, 1e-10, 1e-10)):
            n = g(v)
            length = math.sqrt(sum(c * c for c in n))
            self.assertAlmostEqual(length, 1.0, delta=1e-5)
            self.assertAlmostEqual(sum(a * b for a, b in zip(n, v)) / math.sqrt(sum(c * c for c in v)), length, delta=1e-6)
        self.assertNotEqual(g((3.0, 4.0, 0.0)), ref._normalize_sse2((3.0, 4.0, 0.0)))

    def test_dispatch_mesh(self):
        v, f = near_unit_normals()
        self.assertEqual(ref.generate(v, f, 1e-6, normalize='sse2')[2], [1, U, U, 0, U, U, U, U, U])
        self.assertEqual(ref.generate(v, f, 1e-6, normalize='generic')[2], [2, U, U, U, U, U, 0, U, U])


if __name__ == '__main__':
    unittest.main()
