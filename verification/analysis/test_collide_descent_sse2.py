"""Host checks of the SSE2 OBB-tree descent replacement (src/proxy/collide_descent_sse2_core.h).

The iterative core is compiled with the host compiler and compared, on random, deep (more than 64 pending entries, so
nested frames) and contact-bearing tree pairs, with a plain recursive transcription of the engine's control flow
written here: same visit sequence, same leaf calls, same counters, same early termination. The site verifier is run
against the installed executable when it is present and against corrupted copies of it; the install-line parser, the
runner's acceptance rule, the production wiring, the x87 audit roots and the --collide-descent-sse2 launcher gate are
checked from the sources. The bit-exactness against the engine's own bytes is the Wine fixture's job
(verification/probe/run_collide_descent_sse2.py), not this module's.
"""
import contextlib
import importlib.util
import io
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_collide_descent_site as probe  # noqa: E402
import run_collide_descent_sse2 as runner  # noqa: E402

HARNESS = r'''
#include "collide_descent_sse2_core.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace x3m::collide_descent_sse2::core;
static unsigned long long rng = 0x9e3779b97f4a7c15ull;
static unsigned rnd() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return unsigned(rng >> 16); }
static double uniform(double lo, double hi) { return lo + (hi - lo) * ((rnd() + 0.5) / 4294967296.0); }
struct Tree { std::vector<Node> nodes; };
static unsigned grow(Tree& t, const float* d, unsigned leaves, bool chain, double shrink, double spread) {
    const unsigned index = unsigned(t.nodes.size());
    t.nodes.push_back(Node{});
    Node n{};
    n.R[0] = n.R[4] = n.R[8] = 1.0f;
    const float tilt = float(uniform(-0.2, 0.2));
    n.R[1] = tilt; n.R[3] = -tilt;
    for (unsigned i = 0; i < 3; ++i) { n.c[i] = index ? float(uniform(-spread, spread) * d[i]) : 0.0f; n.d[i] = d[i]; }
    if (leaves > 1) {
        const unsigned left = chain ? 1 : leaves / 2;
        for (unsigned side = 0; side < 2; ++side) {
            float child[3];
            for (unsigned i = 0; i < 3; ++i) child[i] = float(d[i] * shrink);
            const unsigned at = grow(t, child, side == 0 ? left : leaves - left, chain, shrink, spread);
            (side == 0 ? n.first : n.second) = &t.nodes[0] + at;
        }
    }
    t.nodes[index] = n;
    return index;
}
static Tree* make(unsigned leaves, bool chain, float extent, double shrink, double spread) {
    Tree* t = new Tree; t->nodes.reserve(2 * leaves);
    const float d[3] = {extent, extent * 0.7f, extent * 0.5f};
    grow(*t, d, leaves, chain, shrink, spread);
    return t;
}
// The engine's state and a log of everything observable.
static int contacts, first_contact, flags, cap;
static unsigned visits, entries, contact_every, leaf_calls;
static std::vector<unsigned long long> trail;
static void note(unsigned tag, const Node* a, const Node* b) { trail.push_back((unsigned long long)tag << 60 ^ (unsigned long long)reinterpret_cast<std::uintptr_t>(a) * 1315423911ull ^ (unsigned long long)reinterpret_cast<std::uintptr_t>(b)); }
static int leaf_test(const Node* a, const Node* b) {
    ++leaf_calls; note(2, a, b); trail.push_back(visits); trail.push_back(unsigned(contacts));
    if (contact_every && trail.size() % contact_every == 0) ++contacts;
    return 0;
}
struct Env {
    int contacts() const { return ::contacts; }
    int first_contact() const { return ::first_contact; }
    unsigned flags() const { return unsigned(::flags); }
    int cap() const { return ::cap; }
    void add_visits(unsigned n) const { ::visits += n; }
    void add_entries(unsigned n) const { ::entries += n; }
    void visit(const Pair& p, const float*) const { note(1, p.a, p.b); for (unsigned i = 0; i < 12; ++i) { std::uint32_t bits; std::memcpy(&bits, i < 9 ? &p.R[i] : &p.T[i - 9], 4); trail.push_back(bits); } }
    int leaf(const Node* a, const Node* b) const { return leaf_test(a, b); }
};
// 0x004e2530 as it is written: recursion, the first child's result tested, the transforms composed per child.
static int reference(const Node* a, const Node* b, const float* R, const float* T, float s) {
    ++entries;
    if (first_contact && contacts > 0) return 0;
    if ((flags & 4) && contacts >= cap) return 0;
    ++visits;
    const float bs[3] = {float(double(b->d[0]) * s), float(double(b->d[1]) * s), float(double(b->d[2]) * s)};
    Pair here; here.a = a; here.b = b;
    for (unsigned i = 0; i < 9; ++i) here.R[i] = R[i];
    for (unsigned i = 0; i < 3; ++i) here.T[i] = T[i];
    Env().visit(here, bs);
    if (x3m::collide_sat_sse2::core::obb_disjoint(R, bs, T, a->d)) return 0;
    const bool a_leaf = !a->first && !a->second, b_leaf = !b->first && !b->second;
    if (a_leaf && b_leaf) return leaf_test(a, b);
    double P[9], Td[3];
    for (unsigned i = 0; i < 9; ++i) P[i] = R[i];
    for (unsigned i = 0; i < 3; ++i) Td[i] = T[i];
    float R1[9], T1[3];
    const bool split_b = !b_leaf && (a_leaf || !(b->d[0] < a->d[0]));
    if (split_b) {
        mul_rr<double>(P, b->second->R, R1); mul_rc_scaled<double>(P, b->second->c, Td, double(s), T1);
        if (const int hit = reference(a, b->second, R1, T1, s)) return hit;
        mul_rr<double>(P, b->first->R, R1); mul_rc_scaled<double>(P, b->first->c, Td, double(s), T1);
        return reference(a, b->first, R1, T1, s);
    }
    mul_trr<double>(a->second->R, P, R1); mul_trv<double>(a->second->R, Td, a->second->c, T1);
    if (const int hit = reference(a->second, b, R1, T1, s)) return hit;
    mul_trr<double>(a->first->R, P, R1); mul_trv<double>(a->first->R, Td, a->first->c, T1);
    return reference(a->first, b, R1, T1, s);
}
int main() {
    Tree* trees[6] = {make(256, false, 20, 0.8, 0.3), make(64, false, 3, 0.8, 0.3), make(150, true, 5, 0.99, 0.02), make(130, true, 4, 0.99, 0.02), make(1, false, 2, 1, 0), make(33, false, 8, 0.7, 0.5)};
    unsigned failures = 0, deep = 0;
    unsigned long long total_visits = 0, total_leafs = 0;
    for (unsigned serial = 0; serial < 600; ++serial) {
        const Tree* a = trees[serial % 6]; const Tree* b = trees[(serial / 6) % 6];
        float R[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, T[3];
        const float tilt = float(uniform(-0.5, 0.5));
        R[2] = tilt; R[6] = -tilt;
        for (float& v : T) v = float(uniform(-1, 1) * (serial % 5));
        const float s = float(uniform(0.5, 2.0));
        const int modes[4][3] = {{1, 2, 1}, {0, 0xc, 8}, {0, 4, 1}, {0, 0, 0}};
        first_contact = modes[serial % 4][0]; flags = modes[serial % 4][1]; cap = modes[serial % 4][2];
        contact_every = serial % 3 ? 7 : 0;
        contacts = 0; visits = entries = 0; trail.clear();
        const int want = reference(&a->nodes[0], &b->nodes[0], R, T, s);
        const std::vector<unsigned long long> want_trail = trail;
        const unsigned want_visits = visits, want_entries = entries; const int want_contacts = contacts;
        contacts = 0; visits = entries = 0; trail.clear();
        Env env;
        const int got = descend<double>(&a->nodes[0], &b->nodes[0], R, T, s, env);
        if (got != want || trail != want_trail || visits != want_visits || entries != want_entries || contacts != want_contacts) ++failures;
        total_visits += visits;
        if (serial % 6 == 2 && (serial / 6) % 6 == 3 && visits > 2 * stack_entries) ++deep;
    }
    total_leafs = leaf_calls / 2;   // every pair runs twice
    std::printf("failures=%u visits=%llu leafs=%llu deep_pairs=%u\n", failures, total_visits, total_leafs, deep);
    return failures ? 1 : 0;
}
'''

SAMPLE = '''collide_descent_sse2 requested=1 patched=1 reason=ok site=0x004e2956 target=0x004e2530 write=plain handler=0x00401230
PASS engine_sse2_sat pairs=126150 seconds=1.7 stack_span_bytes=74672
MICRO sat_scalar_full ns=21.16 sink=0
COMPARE core_vs_engine category=realistic pairs=40000 visits=2436584 leaf_calls=24320 contacts=452 transform_sequence_differs=0 node_sequence_differs=0 leaf_sequence_differs=0 outputs_differ=0 entries_differ=0
MIX visits=212707 descended=106353 leaf=14906 pruned_by_sat=91448
SUMMARY pairs=126150 visits=25495277 leaf_calls=6892149 contacts=350959 core_transform_differs=0 core_nodes_differs=0 core_leafs_differs=0 core_outputs_differ=0 core_entries_differ=0 thunk_leafs_differs=0 thunk_outputs_differ=0 thunk_entries_differ=0 vanilla_sat_nodes_differs=15 vanilla_sat_leafs_differs=7 float_nodes_differs=6 float_leafs_differs=2 float_outputs_differ=6
COLLIDE DESCENT SSE2 BENCH visits_per_query=212707 leaf_calls_per_query=14906 engine_vanilla_ns=118.03 engine_sse2_sat_ns=32.45 descent_sse2_ns=31.19
COLLIDE DESCENT SSE2 CPU checks=49 failures=0
'''


def load_manage():
    spec = importlib.util.spec_from_file_location('collide_descent_sse2_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class DescentCore(unittest.TestCase):
    def test_iterative_core_matches_the_recursive_transcription(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-collide-descent-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'descent_host'
            # -ffp-contract=off: no fused multiply-add (the i686 target has none; an arm64 host would fuse by default).
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'), str(directory / 'harness.cpp'),
                                    '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=300)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        fields = {k: int(v) for k, v in (item.split('=') for item in run.stdout.split())}
        self.assertEqual(fields['failures'], 0)
        self.assertGreater(fields['visits'], 100000)
        self.assertGreater(fields['leafs'], 1000)
        self.assertGreater(fields['deep_pairs'], 0, 'the chain pairs must exceed one frame of pending entries')


class DescentSite(unittest.TestCase):
    def test_source_constants_and_parser(self):
        self.assertEqual(probe.source_constants(probe.CORE.read_text()), {**probe.EXPECTED_CONSTANTS, 'helper_ranges': probe.HELPERS})
        row = probe.parse_install_line('00:01 collide_descent_sse2 requested=1 patched=1 reason=ok site=0x004e2956 target=0x004e2530 write=plain handler=0x6f123450')
        self.assertEqual((row['requested'], row['patched'], row['reason'], row['site'], row['target'], row['write']), (True, True, 'ok', 0x4e2956, 0x4e2530, 'plain'))
        self.assertIsNone(probe.parse_install_line('collide_descent_sse2 requested=1 patched=0 reason=body_mismatch'))
        self.assertEqual(probe.x87_depth([]), 0)
        self.assertTrue(probe._is_store(bytes.fromhex('830544856000'), 2) and probe._is_store(bytes.fromhex('90a344856000'), 2) and probe._is_store(bytes.fromhex('893544856000'), 2))
        self.assertFalse(probe._is_store(bytes.fromhex('90a144856000'), 2) or probe._is_store(bytes.fromhex('8b0544856000'), 2) or probe._is_store(bytes.fromhex('ff0544856000'), 2))

    @unittest.skipUnless(Path(probe.sites.DEFAULT_EXE).is_file() and shutil.which(probe.common.OBJDUMP), 'installed X3AP.exe and objdump required')
    def test_installed_executable_and_corrupted_copies(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', [k for k, v in report['checks'].items() if not v])
        self.assertGreaterEqual(len(report['checks']), 25)
        data = Path(probe.sites.DEFAULT_EXE).read_bytes()
        for va, raw, failing in ((0x4e2956, b'\xe9', 'site_whole_call'), (0x4e2957, b'\xd6', 'site_whole_call'), (0x4e2940, b'\xd8', 'windows'), (0x4e2600, b'\x90', 'descent_body_hash'),
                                 (0x4e2300, b'\x90', 'leaf_body_hash'), (0x4dfd90, b'\x90', 'helpers_hash'), (0x4e2578, b'\x75', 'descent_body_hash')):
            image = bytearray(data)
            offset = va - 0x401000 + 0x400
            image[offset:offset + len(raw)] = raw
            with tempfile.NamedTemporaryFile(suffix='.exe') as f:
                f.write(image)
                f.flush()
                changed = probe.verify(f.name)
            self.assertEqual(changed['result'], 'FAIL', hex(va))
            self.assertTrue(changed['checks'].get(failing) is False or changed['checks'].get('decode') is False, (hex(va), failing))
        # The census's entry claim and the SAT module's rel32 sit in the hashed body's holes: neither changes the hashes.
        image = probe.common.Image(data)
        self.assertEqual(probe.sites.fnv1a(probe.masked_body(image)), probe.DESCENT_FNV1A)
        patched = bytearray(data)
        for va, raw in ((0x4e2530, b'\xe9\x11\x22\x33\x44'), (0x4e25a4, b'\x55\x66\x77\x08'), (0x4e2190, b'\xe9\x01\x02\x03\x04')):
            offset = va - 0x401000 + 0x400
            patched[offset:offset + len(raw)] = raw
        other = probe.common.Image(bytes(patched))
        self.assertEqual((probe.sites.fnv1a(probe.masked_body(other)), probe.sites.fnv1a(probe.masked_leaf(other))), (probe.DESCENT_FNV1A, probe.LEAF_FNV1A))


class DescentWiring(unittest.TestCase):
    def test_production_wiring_and_audit_roots(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertEqual(capture.count('collide_descent_sse2::initialize();'), 1)
        self.assertLess(capture.index('collide_sat_sse2::initialize();'), capture.index('collide_descent_sse2::initialize();'))
        self.assertLess(capture.index('collide_narrow_census::initialize();'), capture.index('collide_descent_sse2::initialize();'))
        self.assertIn('x3m::collide_descent_sse2::shutdown();', (ROOT / 'src/proxy/loader.cpp').read_text())
        self.assertIn('src/proxy/collide_descent_sse2.cpp', (ROOT / 'CMakeLists.txt').read_text())
        module = (ROOT / 'src/proxy/collide_descent_sse2.cpp').read_text()
        self.assertIn('L"X3M_COLLIDE_DESCENT_SSE2"', module)
        self.assertIn('mov edx, 0x004e2190', module)
        self.assertEqual(module.count('_mm_setcsr('), 4)
        audit = (ROOT / 'verification/probe/check_no_x87.py').read_text()
        self.assertIn("'_x3m_collide_descent_thunk', '_x3m_collide_descent_sse2', '_x3m_collide_descent_leaf'", audit)
        self.assertNotIn('0xd9,', (ROOT / 'verification/probe/collide_descent_sse2_fixture.cpp').read_text())   # no engine bytes in the tracked fixture

    def test_runner_accepts_only_a_clean_record(self):
        record = {**runner.parse(SAMPLE), 'exit_status': 0}
        self.assertTrue(runner.accepted(record))
        self.assertEqual(record['summary']['pairs'], 126150)
        self.assertEqual(record['bench_ratios'], {'vs_vanilla': 3.78, 'vs_sse2_sat': 1.04})
        self.assertEqual(record['visit_mix']['descended'], 106353)
        for change in (('core_nodes_differs=0', 'core_nodes_differs=1'), ('thunk_leafs_differs=0', 'thunk_leafs_differs=2'), ('pairs=126150 visits', 'pairs=99999 visits'),
                       ('checks=49 failures=0', 'checks=49 failures=1')):
            self.assertFalse(runner.accepted({**runner.parse(SAMPLE.replace(*change)), 'exit_status': 0}), change)
        self.assertFalse(runner.accepted({**runner.parse(SAMPLE), 'exit_status': 1}))
        self.assertFalse(runner.accepted({**runner.parse(''), 'exit_status': 0}))


class DescentLaunchOption(unittest.TestCase):
    def launch(self, directory, *args, inherited=None):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def test_absent_option_drops_the_variable_even_when_inherited(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, _ = self.launch(directory, inherited={'X3M_COLLIDE_DESCENT_SSE2': '1'})
            self.assertEqual(code, 0)
            self.assertNotIn('X3M_COLLIDE_DESCENT_SSE2', json.loads(output)['env'])

    def test_dry_run_carries_the_switch_alone_and_with_its_neighbours(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = json.loads(self.launch(directory)[1])
            code, output, error = self.launch(directory, '--collide-descent-sse2')
            self.assertEqual(code, 0, error)
            delivered = json.loads(output)
            self.assertEqual(delivered['command'], baseline['command'])
            self.assertEqual({k: v for k, v in delivered['env'].items() if k not in baseline['env']}, {'X3M_COLLIDE_DESCENT_SSE2': '1'})
            all_three = json.loads(self.launch(directory, '--collide-descent-sse2', '--collide-sat-sse2', '--collide-narrow-census')[1])
            self.assertEqual({k: v for k, v in all_three['env'].items() if k not in baseline['env']},
                             {'X3M_COLLIDE_DESCENT_SSE2': '1', 'X3M_COLLIDE_SAT_SSE2': '1', 'X3M_COLLIDE_NARROW_CENSUS': '1'})


if __name__ == '__main__':
    unittest.main()
