"""Host checks of the SSE2 separating-axis replacement (src/proxy/collide_sat_sse2_core.h).

The site verifier on the installed executable and its refusals on a patched
copy (changed windows, another callee, a second caller, a branch or pointer
into the rel32, a body that writes ECX, x87 left on the stack at the site, an
XMM user in the descent, a competing claim), coexistence with the narrow
census's claims, the source constants, the portable core against its Python
twin on random, tangent, degenerate and non-finite pairs, the conservative
margin, the install-line parser, the fixture runner's parser, the production
wiring, the x87 audit roots and the --collide-sat-sse2 launcher gate
(--dry-run only, never a launch). No Wine.
"""
import contextlib
import importlib.util
import io
import json
import math
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_collide_sites as probe  # noqa: E402
import run_collide_sat_sse2 as runner  # noqa: E402

HARNESS = r'''
#include "collide_sat_sse2_core.h"
#include <cstdio>
using namespace x3m::collide_sat_sse2::core;
int main() {
    unsigned bits[18];
    for (;;) {
        for (unsigned& b : bits) if (std::scanf("%x", &b) != 1) return 0;
        float v[18]; std::memcpy(v, bits, sizeof v);
        std::printf("%d\n", obb_disjoint(v, v + 9, v + 12, v + 15));   // R[9], b[3], T[3], a[3]
    }
}
'''


def f32(value):
    return probe._f32(value)


def rotation(rng):
    while True:
        q = [rng.uniform(-1, 1) for _ in range(4)]
        n = sum(v * v for v in q)
        if 1e-3 < n <= 1:
            break
    w, x, y, z = (v / math.sqrt(n) for v in q)
    return [f32(v) for v in (1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                             2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y))]


def cases(count, seed=20260919):
    rng = random.Random(seed)
    specials = [float('nan'), float('inf'), float('-inf'), 3.4028234663852886e38, 0.0, -0.0, 1e-45]
    out = []
    for k in range(count):
        R = rotation(rng) if k % 7 else [1.0, 0, 0, 0, 1.0, 0, 0, 0, 1.0]
        a = [f32(math.exp(rng.uniform(math.log(1e-3), math.log(50)))) for _ in range(3)]
        b = [f32(math.exp(rng.uniform(math.log(1e-4), math.log(2)))) for _ in range(3)]
        reach = rng.uniform(0, 2.5) * (math.sqrt(sum(v * v for v in a)) + math.sqrt(sum(v * v for v in b)))
        T = [f32(rng.uniform(-1, 1) * reach) for _ in range(3)]
        if k % 11 == 0:
            T[k % 3] = f32(a[k % 3] + b[k % 3])                      # faces touching
        if k % 13 == 0:
            scale = (1e30, 1e19, 1e-38)[k % 3]
            a, b, T = ([f32(v * scale) for v in vec] for vec in (a, b, T))
        if k % 17 == 0:
            flat = R + b + T + a
            flat[rng.randrange(18)] = specials[k % len(specials)]
            R, b, T, a = flat[:9], flat[9:12], flat[12:15], flat[15:]
        out.append((R, b, T, a))
    return out


def load_manage():
    spec = importlib.util.spec_from_file_location('collide_sat_sse2_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def patched_report(data, changes, sat_claims=None):
    image = bytearray(data)
    for va, raw in changes:
        offset = va - 0x401000 + 0x400
        image[offset:offset + len(raw)] = raw
    with tempfile.NamedTemporaryFile(suffix='.exe') as f:
        f.write(image)
        f.flush()
        sat = probe.sat_inputs()
        if sat_claims is not None:
            sat['claims'] = sat_claims
        return probe.inspect(bytes(image), probe.decode(f.name), probe.CORE.read_text(), probe.other_claims(), probe.narrow_inputs(f.name), sat)


class SatSite(unittest.TestCase):
    @unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed executable not present')
    def test_installed_executable(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report)
        self.assertEqual(report['sat_site'], '0x4e25a3')
        self.assertGreaterEqual(report['sat_other_claims_checked'], 100)
        self.assertEqual(len([k for k in report['checks'] if k.startswith('sat_')]), 17)

    @unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed executable not present')
    def test_changed_bytes_branches_and_pointers_refused(self):
        data = probe.DEFAULT_EXE.read_bytes()
        rel = lambda opcode, at, target: opcode + struct.pack('<i', target - (at + 5))
        scratch = 0x4e2188   # eight int3 bytes before 0x004e2190, outside every decoded range
        cases_ = {
            'sat_windows': [(probe.SAT_RETURN + 2, b'\x0c')],                                            # add esp,0xc
            'sat_site_whole_call': [(probe.SAT_SITE + 1, struct.pack('<i', 0x4e2190 - probe.SAT_RETURN))],   # the call targets another function
            'sat_sole_caller': [(scratch, rel(b'\xe8', scratch, probe.SAT_TARGET))],
            'sat_no_rel32_into_span': [(scratch, rel(b'\xe9', scratch, probe.SAT_SITE + 2))],
            'sat_no_abs32_into_span': [(0x45e0dc, struct.pack('<I', probe.SAT_SITE + 3))],
            'sat_no_short_jump_into_span': [(0x4e259f, b'\xeb\x05\x90\x90')],                            # jmp short 0x4e25a6
            'sat_callee_hash': [(probe.SAT_TARGET + 0x120, b'\x90')],
            'sat_callee_keeps_ecx_edx_esi_edi': [(0x4e328f, b'\x59')],                                   # push ecx -> pop ecx
            'sat_callee_plain_rets': [(0x4e3397, b'\xcc')],
            'sat_arguments': [(0x4e2577, b'\x8b\x7c\x24\x5c')],                                          # R into EDI instead of ESI
            'sat_x87_empty_at_site': [(0x4e259f, b'\xd9\x54\x24\x30')],                                  # fst instead of fstp: one value left
            'sat_flags_dead_at_return': [(probe.SAT_RETURN, b'\x8d\x64\x24')],                           # lea: no flag write
            'sat_no_xmm_in_descent': [(0x4e25da, b'\x0f\x57\xc0\x90')],                                  # xorps xmm0,xmm0 in the descent
        }
        for failed, change in cases_.items():
            with self.subTest(check=failed):
                report = patched_report(data, change)
                self.assertFalse(report['checks'][failed], failed)
                self.assertEqual(report['result'], 'FAIL')

    def test_claims_and_coexistence_with_the_census(self):
        claims = probe.sat_other_claims()
        addresses = {address for _, address, _ in claims}
        for expected in (probe.N5_SITE, probe.N6_SITE, probe.N7_SITE, probe.N8_SITE, probe.P1_SITE, probe.P2_SITE, 0x47d2a2):
            self.assertIn(expected, addresses)
        self.assertNotIn(probe.SAT_SITE, addresses)
        self.assertEqual(probe.sat_overlaps(claims), [])
        for name, address in (('x', probe.SAT_SITE + 4), ('y', probe.SAT_SITE - 20), ('z', probe.SAT_RETURN + 6), ('w', probe.SAT_TARGET + 700), ('v', probe.FABS_HELPER_VA + 3)):
            self.assertEqual(probe.sat_overlaps([(name, address, 5)]), [(name, hex(address))])
        self.assertEqual(probe.sat_overlaps([('census7', probe.N7_SITE, len(probe.N7_WINDOW)), ('census8', probe.N8_SITE, len(probe.N8_WINDOW)), ('leaf_call', probe.N8_CALLER, 5)]), [])
        # The census and the box cull see the SAT site as a foreign claim and stay clear of it: every combination may be on.
        self.assertIn(probe.SAT_SITE, {address for _, address, _ in probe.narrow_other_claims()})
        self.assertIn(probe.SAT_SITE, {address for _, address, _ in probe.other_claims()})
        self.assertEqual(probe.narrow_overlaps(probe.narrow_other_claims()), [])
        self.assertEqual(probe.overlaps(probe.other_claims()), [])

    @unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed executable not present')
    def test_competing_claim_refused(self):
        report = patched_report(probe.DEFAULT_EXE.read_bytes(), [], sat_claims=probe.sat_other_claims() + [('intruder', probe.SAT_SITE - 2, 5)])
        self.assertFalse(report['checks']['sat_claims_disjoint'])

    def test_source_constants_and_parsers(self):
        self.assertEqual(probe.sat_source_constants(probe.SAT_CORE.read_text()), probe.SAT_EXPECTED_CONSTANTS)
        row = probe.parse_sat_install_line('00:01 collide_sat_sse2 requested=1 patched=1 reason=ok site=0x004e25a3 target=0x004e3280 write=plain handler=0x6f123450')
        self.assertEqual((row['patched'], row['reason'], row['site'], row['target'], row['write'], row['handler']), (True, 'ok', 0x4e25a3, 0x4e3280, 'plain', 0x6f123450))
        self.assertIsNone(probe.parse_sat_install_line('collide_sat_sse2 requested=1 patched=0 reason=callee_mismatch'))
        parsed = runner.parse('CATEGORY realistic cases=10 both_keep=4 both_prune=6 sse_keeps=0 violations=0 axis_mismatch=0 axis_earlier=0\n'
                              'CATEGORY hostile cases=5 both_keep=1 both_prune=2 sse_keeps=2 violations=0 axis_mismatch=1 axis_earlier=0\n'
                              'COLLIDE SAT SSE2 BENCH null_ns=3.00 early_axis_mean=1.09 early_replica_ns=63.00 early_sse2_ns=9.00 early_bracketed_ns=11.00 '
                              'full_replica_ns=123.00 full_sse2_ns=23.00 full_bracketed_ns=26.00\nCOLLIDE SAT SSE2 CPU checks=40 failures=0\n')
        self.assertEqual((parsed['checks'], parsed['failures'], parsed['cases'], parsed['sse_keeps'], parsed['violations']), (40, 0, 15, 2, 0))
        self.assertEqual((parsed['bench']['early_ratio'], parsed['bench']['full_ratio'], parsed['bench']['mxcsr_bracket_ns']), (10.0, 6.0, 2.0))

    def test_twin_semantics(self):
        identity = [1.0, 0, 0, 0, 1.0, 0, 0, 0, 1.0]
        unit = [1.0, 1.0, 1.0]
        self.assertEqual(probe.sat_disjoint(identity, unit, [0.0, 0.0, 0.0], unit), 0)
        self.assertEqual(probe.sat_disjoint(identity, unit, [3.0, 0.0, 0.0], unit), 1)
        self.assertEqual(probe.sat_disjoint(identity, unit, [0.0, 3.0, 0.0], unit), 3)
        self.assertEqual(probe.sat_disjoint(identity, unit, [0.0, 0.0, -3.0], unit), 4)
        # Touching faces: reps widens the radius sum, the pair is kept (RAPID's bias toward overlap).
        self.assertEqual(probe.sat_disjoint(identity, unit, [2.0, 0.0, 0.0], unit), 0)
        # Non-finite inputs never separate; the engine's fcompp would report axis 1 on unordered.
        for bad in (float('nan'), float('inf'), float('-inf')):
            self.assertEqual(probe.sat_disjoint(identity, unit, [bad, 0.0, 0.0], unit), 0)
            self.assertEqual(probe.sat_disjoint(identity, [bad, 1.0, 1.0], [9.0, 0.0, 0.0], unit), 0)   # every axis that could separate carries the bad extent
        self.assertEqual(probe.sat_disjoint(identity, [-1.0, -1.0, -1.0], [9.0, 0.0, 0.0], [-1.0, -1.0, -1.0]), 0)   # negative radius sum: kept
        # The margin: separated only beyond (ra + rb) * (1 + 2^-45), i.e. 256 double ulps above the engine's own threshold.
        self.assertEqual(probe.SAT_SLACK, 1.0 + 2.0 ** -45)
        self.assertGreater(probe.SAT_SLACK * 2.0, 2.0)

    def test_core_compiled_against_the_twin(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        population = cases(6000)
        text = '\n'.join(' '.join(f'{struct.unpack("<I", struct.pack("<f", v))[0]:x}' for v in R + b + T + a) for R, b, T, a in population) + '\n'
        with tempfile.TemporaryDirectory(prefix='x3-collide-sat-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'sat_host'
            # -ffp-contract=off: no fused multiply-add (the i686 target has none; an arm64 host would fuse by default).
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'), str(directory / 'harness.cpp'),
                                    '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], input=text, capture_output=True, text=True, timeout=120)
            self.assertEqual(run.returncode, 0, run.stderr)
        got = [int(line) for line in run.stdout.split()]
        want = [probe.sat_disjoint(R, b, T, a) for R, b, T, a in population]
        self.assertEqual(got, want)
        self.assertGreater(want.count(0), 200)
        self.assertGreaterEqual(len(set(want)), 12)   # most of the 15 axes and the overlap verdict occur

    def test_production_wiring(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertEqual(capture.count('collide_sat_sse2::initialize();'), 1)
        self.assertLess(capture.index('collide_narrow_census::initialize();'), capture.index('collide_sat_sse2::initialize();'))
        self.assertIn('x3m::collide_sat_sse2::shutdown();', (ROOT / 'src/proxy/loader.cpp').read_text())
        self.assertIn('src/proxy/collide_sat_sse2.cpp', (ROOT / 'CMakeLists.txt').read_text())
        module = (ROOT / 'src/proxy/collide_sat_sse2.cpp').read_text()
        self.assertIn('L"X3M_COLLIDE_SAT_SSE2"', module)
        self.assertIn('length == 1 && setting[0] == L\'1\'', module)
        for needle in ('install_window_open()', 'executable_verified()', 'callee_mismatch', 'pin_self()', 'engine_patch::claim_call(site_', 'engine_patch::restore_call(site_)',
                       'stmxcsr', 'cmp eax, 0x1f80', 'push ecx', 'push edx'):
            self.assertIn(needle, module)
        for absent in ('X3M_COLLIDE_BOX_CULL', 'X3M_COLLIDE_NARROW_CENSUS', 'LightCallBoundary', 'InterlockedExchange', 'QueryPerformanceCounter'):
            self.assertNotIn(absent, module)   # independent of the other options; nothing per call but the test itself
        core = probe.SAT_CORE.read_text()
        self.assertFalse('std::fabs(' in core)    # GCC emits x87 fld/fabs/fstp for it even under -mfpmath=sse
        self.assertFalse('<cmath>' in core)
        audit = (ROOT / 'verification/probe/check_no_x87.py').read_text()
        self.assertIn("'_x3m_collide_sat_thunk', '_x3m_collide_sat_sse2'", audit)
        build = (ROOT / 'verification/probe/build_collide_sat_sse2.py').read_text()
        self.assertIn('sat_replica_inc.h', build)   # the engine bytes are generated into build/, never tracked
        self.assertNotIn('0xd9,0x06', (ROOT / 'verification/probe/collide_sat_sse2_fixture.cpp').read_text())


class SatLaunchOption(unittest.TestCase):
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
            code, output, _ = self.launch(directory, inherited={'X3M_COLLIDE_SAT_SSE2': '1'})
            self.assertEqual(code, 0)
            self.assertNotIn('X3M_COLLIDE_SAT_SSE2', json.loads(output)['env'])

    def test_dry_run_carries_the_switch_alone_and_with_the_census(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = json.loads(self.launch(directory)[1])
            code, output, error = self.launch(directory, '--collide-sat-sse2')
            self.assertEqual(code, 0, error)
            delivered = json.loads(output)
            self.assertEqual(delivered['command'], baseline['command'])
            self.assertEqual({k: v for k, v in delivered['env'].items() if k not in baseline['env']}, {'X3M_COLLIDE_SAT_SSE2': '1'})
            both = json.loads(self.launch(directory, '--collide-sat-sse2', '--collide-narrow-census')[1])
            self.assertEqual({k: v for k, v in both['env'].items() if k not in baseline['env']}, {'X3M_COLLIDE_SAT_SSE2': '1', 'X3M_COLLIDE_NARROW_CENSUS': '1'})


if __name__ == '__main__':
    unittest.main()
