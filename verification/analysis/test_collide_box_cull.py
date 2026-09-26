"""Host checks of the sector-collide box early-out (src/proxy/collide_box_cull_core.h).

The site verifier on the installed executable and its refusals on a patched
copy, the source constants, the stub encoders against the C++ ones, the
install/frame/window line parsers, the margin proof on the host models
(box reject implies engine reject over boundary and random inputs; a single
counter-example fails), the counter window, the production wiring and the
--collide-box-cull launcher gate (--dry-run only, never a launch). No Wine.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
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
from source_text import source_text

HARNESS = r'''
#include "collide_box_cull_core.h"
#include <cstdio>
#include <cstdlib>
using namespace x3m::collide_box_cull::core;
static std::uint64_t state = 0x9e3779b97f4a7c15ull;
static std::uint32_t rnd() { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return std::uint32_t(state >> 16); }
static std::int32_t sgn(std::uint32_t m) { return (rnd() & 1) ? std::int32_t(m) : std::int32_t(0u - m); }
int main(int argc, char** argv) {
    unsigned failures = 0; unsigned long long rejects = 0, engine_only = 0, total = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    auto one = [&](std::int32_t dx, std::int32_t dy, std::int32_t dz, std::int32_t r, std::int32_t sweep) {
        ++total;
        const bool box1 = p1_box_reject(1, 2, dx, dy, dz, r), eng1 = p1_engine_reject(dx, dy, dz, r);
        const bool box2 = p2_box_reject(dx, dy, dz, r, sweep), eng2 = p2_engine_reject(dx, dy, dz, r, sweep);
        if ((box1 && !eng1) || (box2 && !eng2)) { if (++failures < 5) std::printf("COUNTEREXAMPLE d=%d,%d,%d r=%d sweep=%d\n", dx, dy, dz, r, sweep); }
        rejects += box1; engine_only += eng1 && !box1;
    };
    // Boundary band: |d| in [R-2, T+2] on one axis, the others small, zero, or at the cap.
    const std::int32_t radii[] = {0, 1, 31, 32, 63, 64, 1000, 4097, 250000, 1000000, 99999999, 1000000000, 0x3e000000, 0x7c000000, 0x7fffffff, -1, -4000};
    for (std::int32_t r : radii) {
        const std::int32_t R = p1_engine_threshold(r); std::int32_t T = 0; if (!p1_threshold(r, &T)) T = R;
        for (std::int64_t m = std::int64_t(R) - 2; m <= std::int64_t(T) + 2; m += (T - R > 4096 ? 1 + (rnd() % ((T - R) / 2048)) : 1)) {
            if (m < 0 || m > 0x7fffffff) continue;
            const std::uint32_t others[] = {0, 1, std::uint32_t(m), std::uint32_t(m / 2), 0x3fffffffu, 0x40000000u, 0x7fffffffu, 0x80000000u};
            for (std::uint32_t o1 : others) for (std::uint32_t o2 : others) { one(sgn(std::uint32_t(m)), sgn(o1), sgn(o2), r, 0); one(sgn(o1), sgn(std::uint32_t(m)), sgn(o2), r, 64); one(sgn(o1), sgn(o2), sgn(std::uint32_t(m)), r, -3); }
        }
    }
    const unsigned long long random = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1000000ull;
    for (unsigned long long i = 0; i < random; ++i) {
        const std::uint32_t mask = (rnd() & 3) == 0 ? 0xffffffffu : (rnd() & 1) ? 0x3fffffffu : 0x00ffffffu;
        const std::int32_t r = std::int32_t(rnd() % ((rnd() & 7) ? 4000000u : 0x7fffffffu));
        one(std::int32_t(rnd() & mask), std::int32_t(rnd() & mask), std::int32_t(rnd() & mask), r, std::int32_t(rnd() % 4000000u));
        // and on the band of this radius
        std::int32_t T = 0; if (p1_threshold(r, &T)) one(sgn(std::uint32_t(T) + (rnd() % 5) - 2), std::int32_t(rnd() & 0xffff), std::int32_t(rnd() & mask & 0x3fffffff), r, 0);
    }
    // Decisions the stub makes by construction.
    check(!p1_box_reject(7, 1, 1 << 29, 0, 0, 10) && !p1_box_reject(1, 7, 1 << 29, 0, 0, 10) && p1_box_reject(1, 1, 1 << 29, 0, 0, 10), "class 7 on either side is never rejected");
    check(!p1_box_reject(1, 1, 0x40000000, 0, 0, 10) && !p1_box_reject(1, 1, 5000, 0, std::int32_t(0x80000000u), 10) && p1_box_reject(1, 1, 0x3fffffff, 0, 0, 10), "axis cap: 2^30 and INT_MIN leave the engine path");
    check(!p1_box_reject(1, 1, 1 << 20, 0, 0, -1) && !p1_box_reject(1, 1, 0x3fffffff, 0, 0, 0x7c000000), "negative radius sum and overflowing threshold leave the engine path");
    check(engine_distance(0x7fffffff, 0x7fffffff, 0x7fffffff) == std::int32_t(0x80000000u) && !p1_engine_reject(0x7fffffff, 0x7fffffff, 0x7fffffff, 10), "engine: a saturated distance is not rejected (CVTTSD2SI indefinite)");
    check(p1_engine_threshold(1000) == 1010 && p1_engine_threshold(0) == 0 && p1_engine_threshold(250000) == 252499, "engine R = (r*0x1028f + 0x8000) >> 16");
    std::int32_t t = 0;
    check(p1_threshold(1000, &t) && t == 1000 + 31 + 64 && p2_threshold(500, 100, &t) && t == 664 && !p2_threshold(0x7fffffff, 1, &t) && !p2_threshold(0x7fffffc0, 0, &t), "thresholds");
    // Encoders: lengths and the fixed fields.
    unsigned char a[stub_capacity], b[stub_capacity];
    const unsigned n1 = encode_p1_stub(0x10000000, 0x20000000, Counters{0x20000010, 0x20000014}, 0x1000008c, 0x0045df90, a);
    const unsigned n2 = encode_p2_stub(0x10000100, 0x20000000, Counters{0, 0}, 0x1000016c, 0x0045ce07, b);
    check(n1 == p1_stub_length_counted && n2 == p2_stub_length_plain, "stub lengths");
    check(encode_p1_stub(0, 0, Counters{0, 0}, 0, 0, a) == p1_stub_length_plain && encode_p2_stub(0, 0, Counters{1, 2}, 0, 0, b) == p2_stub_length_counted, "stub lengths, other variants");
    for (unsigned i = 0; i < n1; ++i) std::printf("%02x", (encode_p1_stub(0x10000000, 0x20000000, Counters{0x20000010, 0x20000014}, 0x1000008c, 0x0045df90, a), a[i]));
    std::printf("\n");
    encode_p2_stub(0x10000100, 0x20000000, Counters{0, 0}, 0x1000016c, 0x0045ce07, b);
    for (unsigned i = 0; i < n2; ++i) std::printf("%02x", b[i]);
    std::printf("\n");
    // Window: p50 / max / sum over 300 frames, then empty again.
    static Window w; WindowSummary s;
    for (unsigned f = 0; f < 300; ++f) { const std::uint32_t v[4] = {f, f / 2, 7, f == 299 ? 9000u : 0u}; w.add(1000 + f, v); }
    check(w.full() && w.close(s) && s.frame == 1299 && s.frames == 300 && s.p50[0] == 150 && s.max[0] == 299 && s.sum[0] == 44850 && s.p50[2] == 7 && s.p50[3] == 0 && s.max[3] == 9000 && !w.count() && !w.close(s), "counter window");
    check(p1_site_va + p1_compare_offset == p1_compare_va && p1_site_va + p1_reject_offset == p1_reject_va && p1_site_va + p1_continue_offset == p1_continue_va
          && p2_site_va + p2_compare_offset == p2_compare_va && p2_site_va + p2_continue_offset == p2_continue_va && p1_site_va + site_length == p1_next_va && p2_site_va + site_length == p2_next_va, "address relations");
    check(rejects > 1000 && engine_only > 1000, "both the box-rejected and the engine-only-rejected sets are exercised");
    std::printf("collide_box_cull_core pairs=%llu box_rejects=%llu checks_failed=%u\n", total, rejects, failures);
    return failures ? 1 : 0;
}
'''


def load_manage():
    spec = importlib.util.spec_from_file_location('collide_box_cull_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def patched_report(data, changes):
    """The verifier on a copy of the installed image with bytes changed at the given VAs."""
    image = bytearray(data)
    for va, raw in changes:
        offset = va - 0x401000 + 0x400
        image[offset:offset + len(raw)] = raw
    return probe.inspect(bytes(image), probe.decode(bytes(image)), source_text(probe.CORE), probe.other_claims())


class CollideSites(unittest.TestCase):
    @unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed executable not present')
    def test_installed_executable(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report)
        self.assertEqual(len(report['checks']), 72)   # 29 box-cull checks + 26 of the narrow census (test_collide_narrow_census.py) + 17 of the SSE2 SAT (test_collide_sat_sse2.py)
        self.assertGreaterEqual(report['other_claims_checked'], 100)

    @unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed executable not present')
    def test_changed_bytes_and_branches_refused(self):
        data = probe.DEFAULT_EXE.read_bytes()
        rel = lambda at, target: b'\xe9' + struct.pack('<i', target - (at + 5)) + b'\x90'   # six bytes over whole instructions
        cases = {
            'p1_windows': (probe.P1_COMPARE + 12, b'\x8e'),                      # 0x1028f -> 0x1028e
            'p1_site_two_whole_movs': (probe.P1_SITE + 1, b'\x4e'),             # mov ecx,[esi+0x70]
            'p1_no_interior_branch': (0x45d304, rel(0x45d304, probe.P1_SITE + 3)),
            'p1_sources': (0x45d304, rel(0x45d304, probe.P1_SITE)),
            'p1_compare_jg_reject': (0x45d606, b'\x0f\x8c'),                     # jl
            'p1_reject_falls_to_continue': (0x45d6de + 2, b'\xad'),              # another target
            'p2_windows': (probe.P2_COMPARE + 9, b'\x24'),                       # add ecx,[esp+0x24]
            'p2_no_interior_branch': (0x45cb0a, rel(0x45cb0a, probe.P2_SITE + 3)),
            'p2_sources': (0x45cc5e, b'\x90\x90'),
            'p2_compare_jg_continue': (0x45ccf2, b'\x0f\x8d'),
            'p2_esi_written_after_site': (probe.P2_NEXT, b'\x8b\x53\x70'),
            'helper_bytes': (probe.FTOL_HELPER_VA + 21, b'\xf2\x0f\x2d'),        # cvtsd2si: rounding, not truncation
            'calls_to_helpers': (0x45d5da + 1, b'\x00'),
            'single_ret_4': (probe.RET_VA, b'\xc2\x08\x00'),
            'jump_tables': (0x45e0dc, struct.pack('<I', 0x45d3c0)),
        }
        for failed, change in cases.items():
            with self.subTest(check=failed):
                report = patched_report(data, [change])
                self.assertFalse(report['checks'][failed], failed)
                self.assertEqual(report['result'], 'FAIL')

    def test_claims_and_overlap_detection(self):
        claims = probe.other_claims()
        addresses = {address for _, address, _ in claims}
        for expected in (0x47d258, 0x47d528, 0x498140, 0x4c27af, 0x43a38e, 0x445a41, 0x4a3ffd):
            self.assertIn(expected, addresses)
        self.assertEqual(probe.overlaps(claims), [])
        self.assertEqual(probe.overlaps([('x', probe.P1_SITE + 5, 5)]), [('x', hex(probe.P1_SITE + 5))])
        self.assertEqual(probe.overlaps([('y', probe.P2_CONTINUE - 4, 5)]), [('y', hex(probe.P2_CONTINUE - 4))])
        self.assertEqual(probe.overlaps([('z', probe.P1_SITE - 5, 5), ('w', 0x47d2a2, 8)]), [])

    def test_source_constants(self):
        self.assertEqual(probe.source_constants(source_text(probe.CORE)), probe.EXPECTED_CONSTANTS)

    def test_line_parsers(self):
        row = probe.parse_install_line('00:01 collide_box_cull requested=1 patched=1 reason=ok p1_site=0x0045d58e p2_site=0x0045cc7c write_p1=plain write_p2=atomic stub_p1=0x0a100000 stub_p2=0x0a100090 counters=1 enabled=1')
        self.assertEqual(row, {'requested': True, 'patched': True, 'reason': 'ok', 'p1_site': 0x45d58e, 'p2_site': 0x45cc7c, 'write_p1': 'plain', 'write_p2': 'atomic',
                               'stub_p1': 0x0a100000, 'stub_p2': 0x0a100090, 'counters': True, 'enabled': True})
        self.assertIsNone(probe.parse_install_line('collide_box_cull requested=1 patched=0 reason=bytes_mismatch'))
        frame = probe.parse_frame_line('collide_census_frame device=1 frame=4991 p1_pairs=213456 p1_rejected=209001 p2_cands=5120 p2_rejected=5003 counters=1 enabled=1')
        self.assertEqual((frame['frame'], frame['p1_pairs'], frame['p1_rejected'], frame['p2_cands'], frame['p2_rejected'], frame['bounded']), (4991, 213456, 209001, 5120, 5003, True))
        self.assertFalse(probe.parse_frame_line('collide_census_frame device=1 frame=1 p1_pairs=1 p1_rejected=2 p2_cands=0 p2_rejected=0 counters=1 enabled=1')['bounded'])
        self.assertIsNone(probe.parse_frame_line('collide_census frame=1 frames=300'))
        line = ('collide_census frame=5399 frames=300 p1_pairs_p50=213000 p1_pairs_max=214000 p1_pairs_sum=63900000 p1_rejected_p50=209000 p1_rejected_max=210000 '
                'p1_rejected_sum=62700000 p2_cands_p50=5000 p2_cands_max=5200 p2_cands_sum=1500000 p2_rejected_p50=4900 p2_rejected_max=5100 p2_rejected_sum=1470000 counters=1 enabled=1')
        window = probe.parse_window_line(line)
        self.assertEqual((window['frame'], window['frames'], window['p1_pairs_p50'], window['p2_rejected_sum'], window['bounded']), (5399, 300, 213000, 1470000, True))
        self.assertIsNone(probe.parse_window_line(line.replace('p2_cands_max=5200 ', '')))
        self.assertIsNone(probe.parse_window_line('collide_census_frame device=1 frame=1 p1_pairs=1 p1_rejected=0 p2_cands=0 p2_rejected=0 counters=1 enabled=1'))

    def test_core_compiled_margin_proof_and_encoder_parity(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-collide-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'collide_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable), '3000000'], capture_output=True, text=True, timeout=600)
            self.assertEqual(run.returncode, 0, run.stdout[-2000:] + run.stderr)
            lines = run.stdout.splitlines()
            self.assertRegex(lines[-1], r'^collide_box_cull_core pairs=\d{7,} box_rejects=\d+ checks_failed=0$')
            self.assertEqual(bytes.fromhex(lines[0]), probe.encode_p1_stub(0x10000000, 0x20000000, 0x20000010, 0x20000014, 0x1000008c, probe.P1_CONTINUE))
            self.assertEqual(bytes.fromhex(lines[1]), probe.encode_p2_stub(0x10000100, 0x20000000, 0, 0, 0x1000016c, probe.P2_CONTINUE))

    def test_encoders(self):
        stub = probe.encode_p1_stub(0x10000000, 0x20000000, 0x20000010, 0x20000014, 0x1000008c, probe.P1_CONTINUE)
        self.assertEqual(len(stub), 139)
        self.assertEqual(stub[:9], b'\x80\x3d' + struct.pack('<I', 0x20000000) + b'\x00\x74' + bytes([117 - 9]))
        self.assertEqual(stub[117:123], b'\xff\x25' + struct.pack('<I', 0x1000008c))
        self.assertEqual(stub[129:135], b'\xbf\x07\x00\x00\x00\xe9')
        self.assertEqual((0x10000000 + 139 + struct.unpack('<i', stub[135:139])[0]) & 0xffffffff, probe.P1_CONTINUE)
        plain = probe.encode_p2_stub(0x10000100, 0x20000000, 0, 0, 0x1000016c, probe.P2_CONTINUE)
        self.assertEqual(len(plain), 105)
        self.assertNotIn(b'\xff\x05', plain)
        self.assertEqual(plain[-7:-4], b'\x8b\xf2\xe9')
        self.assertEqual((0x10000100 + 105 + struct.unpack('<i', plain[-4:])[0]) & 0xffffffff, probe.P2_CONTINUE)
        with self.assertRaises(ValueError):
            probe.encode_p1_stub(1 << 32, 0, 0, 0, 0, 0)

    def test_production_wiring(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertEqual(capture.count('collide_box_cull::initialize();'), 1)
        self.assertEqual(capture.count('collide_box_cull::present(ctx.id,ctx.frame,ctx.capture);'), 1)
        self.assertLess(capture.index('game_phases::initialize();'), capture.index('collide_box_cull::initialize();'))
        self.assertIn('x3m::collide_box_cull::shutdown();', source_text(ROOT / 'src/proxy/loader.cpp'))
        self.assertIn('src/proxy/collide_box_cull.cpp', source_text(ROOT / 'CMakeLists.txt'))
        module = source_text(ROOT / 'src/proxy/collide_box_cull.cpp')
        self.assertIn('L"X3M_COLLIDE_BOX_CULL"', module)
        self.assertIn('length == 1 && setting[0] == L\'1\'', module)
        for needle in ('install_window_open()', 'executable_verified()', 'helper_mismatch', 'pin_self()', 'engine_patch::restore(p1_site_)'):
            self.assertIn(needle, module)


class CollideLaunchOption(unittest.TestCase):
    def launch(self, directory, *args, inherited=None, vanilla=True):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        if not vanilla:   # a modded launch wants an installed proxy that matches its manifest
            (game / 'd3d9.dll').write_bytes(b'proxy')
            (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def test_modded_launch_default_and_its_off_switch(self):
        # Launcher default on a modded launch since 2026-09-23 (collide_default, like the memo and the SSE2 SAT).
        with tempfile.TemporaryDirectory() as directory:
            env = lambda *args, **kw: json.loads(self.launch(directory, *args, **kw)[1])['env'].get('X3M_COLLIDE_BOX_CULL')
            self.assertEqual(env(vanilla=False), '1')
            self.assertEqual(env('--collide-box-cull', vanilla=False), '1')
            self.assertIsNone(env('--no-collide-box-cull', vanilla=False, inherited={'X3M_COLLIDE_BOX_CULL': '1'}))
            self.assertIsNone(env(inherited={'X3M_COLLIDE_BOX_CULL': '1'}))   # --vanilla forwards nothing unless asked

    def test_absent_option_drops_the_variable_even_when_inherited(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, _ = self.launch(directory, inherited={'X3M_COLLIDE_BOX_CULL': '1'})
            self.assertEqual(code, 0)
            self.assertNotIn('X3M_COLLIDE_BOX_CULL', json.loads(output)['env'])

    def test_dry_run_carries_the_switch(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = json.loads(self.launch(directory)[1])
            code, output, error = self.launch(directory, '--collide-box-cull')
            self.assertEqual(code, 0, error)
            delivered = json.loads(output)
            self.assertEqual(delivered['command'], baseline['command'])
            self.assertEqual({k: v for k, v in delivered['env'].items() if k not in baseline['env']}, {'X3M_COLLIDE_BOX_CULL': '1'})


if __name__ == '__main__':
    unittest.main()
