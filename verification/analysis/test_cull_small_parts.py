"""Host checks of the small-parts cull trampoline (src/proxy/cull_small_parts_core.h).

The site verifier on a synthetic image (the 56-byte window, whole
instructions, the flag writer inside the displaced span, the cull target,
interior-branch, extra-source and changed-byte refusal, claim disjointness),
the source constants, the stub encoder, the threshold rule against the
tracked run131 rows, the install/value/frame line parsers, the core compiled
with the host compiler, the census classification with a threshold, and the
--cull-small-parts launcher gate (--dry-run only, never a launch). The
installed executable is only read when present. No Wine, no game.
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
from unittest import mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_cull_small_parts_site as probe  # noqa: E402
import verify_cull_census_sites as census_probe  # noqa: E402
from verification.analysis.test_chase_aim_sites import synthetic_image  # noqa: E402

HARNESS = r'''
#include "cull_small_parts_core.h"
#include "cull_census_core.h"
#include <cstdio>
#include <cstring>
using namespace x3m::cull_small_parts::core;
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    float m00; std::uint32_t bits = 0x3f4ccccc; std::memcpy(&m00, &bits, 4);
    check(threshold_for(2.0, m00, 1280) == 3 && threshold_for(4.0, m00, 1280) == 6 && threshold_for(8.0, m00, 1280) == 11, "run131 thresholds 3/6/11");
    check(threshold_for(2.0, 0.8f, 1280) == 3 && threshold_for(2.0, 0.8f, 1920) == 2 && threshold_for(0.5, 1.0f, 1280) == 1, "other scales");
    check(threshold_for(0.0, m00, 1280) == 0 && threshold_for(-1.0, m00, 1280) == 0 && threshold_for(65.0, m00, 1280) == 0 && threshold_for(2.0, 0.0f, 1280) == 0 && threshold_for(2.0, m00, 32) == 0, "unusable inputs give 0");
    check(threshold_for(64.0, 0.06f, 64) == 21334 && threshold_for(64.0, 0.06f, 16384) == 84 && threshold_max == 0x1000000, "band extremes (the cap is beyond the band)");
    double px = 0;
    check(parse_px("2", &px) && px == 2.0 && parse_px("+2.5", &px) && px == 2.5 && parse_px(".5", &px) && px == 0.5 && !parse_px("2,5", &px) && !parse_px("1e1", &px) && !parse_px("", &px) && !parse_px(nullptr, &px), "parser");
    check(valid_px(64.0) && !valid_px(64.01) && !valid_px(0.0), "band");
    unsigned char s[stub_length]; encode_stub(0x10000000, 0x20000000, 0x20000004, 0x0047d2c3, 0x10000044, s, Scope::all);
    std::uint32_t v = 0;
    std::memcpy(&v, s + 2, 4); check(s[0] == 0x83 && s[1] == 0x3d && v == 0x20000000 && s[6] == 0 && s[7] == 0x7e && s[8] == stub_continue - 9, "cmp dword [threshold],0; jle continue");
    std::memcpy(&v, s + 11, 4); check(s[9] == 0x50 && s[10] == 0xa1 && v == 0x20000000 && !std::memcmp(s + 15, "\x39\x44\x24\x30\x58\x7d", 6) && s[21] == stub_continue - 22, "push eax; mov eax,[threshold]; cmp [esp+0x30],eax; pop eax; jge continue");
    check(!std::memcmp(s + 22, "\x8b\x4f\x18\x85\xc9\x8b\x87\xd8\x01\x00\x00\x74", 12) && s[34] == stub_cull - 35 && !std::memcmp(s + 35, "\x8b\x89\xd8\x01\x00\x00\x3b\xc8\x7e", 9) && s[44] == stub_cull - 45 && s[45] == 0x8b && s[46] == 0xc1, "the engine's limit computation replayed");
    std::memcpy(&v, s + 49, 4); check(s[47] == 0xff && s[48] == 0x05 && v == 0x20000004, "inc dword [culled]");
    std::memcpy(&v, s + 54, 4); check(s[53] == 0xe9 && 0x1000003a + v == 0x0047d2c3, "jmp cull target");
    std::memcpy(&v, s + 60, 4); check(s[58] == 0xff && s[59] == 0x25 && v == 0x10000044, "jmp [next]");
    unsigned char b[stub_length]; encode_stub(0x10000000, 0x20000000, 0x20000004, 0x0047d2c3, 0x10000044, b, Scope::bodies);
    check(!std::memcmp(b, s, stub_scope_branch) && !std::memcmp(b + stub_cull, s + stub_cull, stub_length - stub_cull), "bodies stub: only bytes 27..46 differ");
    check(!std::memcmp(b + 22, site, site_length) && b[27] == 0x75 && 29 + b[28] == stub_continue && !std::memcmp(b + 29, "\x8b\x87\xd8\x01\x00\x00\xeb", 7) && 37 + b[36] == stub_cull && b[37] == 0xcc && b[46] == 0xcc, "bodies stub: displaced test; jne continue; mov eax,[edi+0x1d8]; jmp cull");
    Scope sc = Scope::bodies;
    check(parse_scope(nullptr, &sc) && sc == Scope::all && parse_scope("bodies", &sc) && sc == Scope::bodies && !parse_scope("All", &sc) && !parse_scope("parts", &sc) && parse_scope("all", &sc) && sc == Scope::all, "scope parser");
    check(!std::strcmp(scope_name(Scope::bodies), "bodies") && !std::strcmp(scope_name(Scope::all), "all"), "scope names");
    check(std::memcmp(window + site_offset, site, site_length) == 0 && window[cull_offset] == 0x83 && window[cull_offset + 1] == 0xa7 && window[window_length - 2] == 0xeb && window[window_length - 1] == 0x05, "site and cull bytes inside the window");
    check(window_va + site_offset == site_va && site_va + site_length == next_va && window_va + cull_offset == cull_va && window_va + window_length + 5 == after_cull_va, "address relations");
    using namespace x3m::cull_census::core;
    Entry e{}; e.exited = 1; e.flags_in = 0x1002; e.flags_out = 0x1000; e.s = 2; e.measure = 4; e.limit = 0;
    check(classify(e) == Verdict::culled_other && classify(e, 3) == Verdict::culled_small && classify(e, 2) == Verdict::culled_other, "census: culled_small below the threshold only");
    e.limit = 8; check(classify(e, 3) == Verdict::culled_size, "census: the engine's size cull named first");
    e.limit = 0; e.measure = 0; e.s = 1; check(classify(e, 3) == Verdict::culled_min, "census: the engine's degenerate cull named first");
    e.flags_in = 0x4001002; check(classify(e, 3) == Verdict::culled_small, "census: a 0x4000000 node below the threshold is the stub's");
    check(classify(e, 3, true) == Verdict::culled_small, "census, scope bodies: a parentless node below the threshold is the stub's");
    e.parent = 0x1000; check(classify(e, 3, true) == Verdict::culled_other && classify(e, 3, false) == Verdict::culled_small, "census, scope bodies: a parented node is never the stub's");
    e.parent = 0;
    e.flags_out = 0x1002; check(classify(e, 3) == Verdict::kept, "census: kept stays kept");
    check(!std::strcmp(verdict_name(Verdict::culled_small), "culled_small") && verdict_count == 6, "verdict name");
    std::printf("cull_small_parts_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


def load_manage():
    spec = importlib.util.spec_from_file_location('cull_small_parts_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def image(*changes):
    """A synthetic PE with the window, its documented incoming branches, the after-cull target and the final ret 8."""
    extra = [(probe.FUNCTION[0], probe.PROLOGUE), *probe.S_STORES.items(), (probe.WINDOW_VA, probe.WINDOW), (0x47d28c, b'\x74\x14'),                     # je 0x47d2a2, the engine's other incoming branch
             (probe.AFTER_CULL_VA, b'\x8b\x87\x2c\x01\x00\x00'), (probe.RET_VA, b'\xc2\x08\x00'), *changes]
    return synthetic_image(extra=extra, text_size=0x100000)


def inspect_image(data):
    with tempfile.NamedTemporaryFile(suffix='.exe') as f:
        f.write(data)
        f.flush()
        return probe.inspect(data, probe.decode(f.name), probe.CORE.read_text())


class CullSmallPartsSite(unittest.TestCase):
    def test_synthetic_image_passes_all_but_identity(self):
        report = inspect_image(image())
        checks = {k: v for k, v in report['checks'].items() if k != 'exe_identity'}
        self.assertTrue(all(checks.values()), report)
        self.assertFalse(report['checks']['exe_identity'])
        self.assertEqual(report['site_sources'], ['0x47d28c', '0x47d297'])
        self.assertEqual(report['interior_branches'], [])
        self.assertEqual(len(report['checks']), 19)

    def test_changed_bytes_and_branches_refused(self):
        cases = {
            'window_bytes': (probe.WINDOW_VA + 2, b'\x15'),                                  # cmp esi,0x15
            'site_whole_instructions': (probe.SITE_VA + 2, b'\x1c'),                         # mov ecx,[edi+0x1c]
            'next_instruction': (probe.NEXT_VA + 2, b'\xdc'),                                # mov eax,[edi+0x1dc]
            'je_consumes_flags': (probe.JE_VA, b'\xeb\x0c'),                                 # jmp instead of je
            'cull_instruction': (probe.CULL_VA + 6, b'\xfb'),                                # and ..,0xfffffffb
            'no_interior_branch': (0x47d300, b'\xe9' + struct.pack('<i', probe.SITE_VA + 3 - (0x47d300 + 5))),
            'site_sources': (0x47d400, b'\xe9' + struct.pack('<i', probe.SITE_VA - (0x47d400 + 5))),
            'window_branches_contained': (probe.WINDOW_VA + 4, b'\x40'),                     # jge far outside the window
            'prologue_frame': (probe.FUNCTION[0] + 2, b'\x18'),                             # sub esp,0x18
            's_slot_stores': (0x47d24a + 3, b'\x28'),                                       # mov [esp+0x28],eax
            'function_ret': (probe.RET_VA, b'\xc2\x04\x00'),
        }
        for failed, change in cases.items():
            with self.subTest(check=failed):
                report = inspect_image(image(change))
                self.assertFalse(report['checks'][failed], report)

    def test_claims_disjoint_and_constants(self):
        self.assertEqual(probe.source_constants(probe.CORE.read_text()), probe.EXPECTED_CONSTANTS)
        self.assertEqual(probe.SITE, bytes.fromhex('8b4f1885c9'))
        self.assertEqual(len(probe.WINDOW), 56)
        self.assertEqual(probe.WINDOW[probe.CULL_VA - probe.WINDOW_VA:probe.CULL_VA - probe.WINDOW_VA + 7], probe.CULL)
        claim = (probe.SITE_VA, probe.SITE_VA + 5)
        for name, (address, length) in probe.OTHER_CLAIMS.items():
            with self.subTest(claim=name):
                self.assertTrue(claim[1] <= address or address + length <= claim[0])
        self.assertEqual(probe.OTHER_CLAIMS['cull_census_measure'][0], census_probe.MEASURE_SITE_VA)
        self.assertEqual(probe.OTHER_CLAIMS['cull_census_exit'][0], census_probe.EXIT_SITE_VA)
        self.assertIn('culled_small', census_probe.VERDICTS)

    def test_encoder_and_threshold(self):
        stub = probe.encode_stub(0x10000000, 0x20000000, 0x20000004, probe.CULL_VA, 0x10000044)
        self.assertEqual(len(stub), 64)
        self.assertEqual(stub[:2], b'\x83\x3d')
        self.assertEqual(stub[6:9], b'\x00\x7e\x31')
        self.assertEqual(stub[9:15], b'\x50\xa1' + struct.pack('<I', 0x20000000))
        self.assertEqual(stub[15:22], b'\x39\x44\x24\x30\x58\x7d\x24')
        self.assertEqual(stub[22:47], bytes.fromhex('8b4f18 85c9 8b87d8010000 740c 8b89d8010000 3bc8 7e02 8bc1'.replace(' ', '')))
        self.assertEqual(stub[47:53], b'\xff\x05' + struct.pack('<I', 0x20000004))
        self.assertEqual(struct.unpack('<i', stub[54:58])[0], probe.CULL_VA - (0x10000000 + 58))
        self.assertEqual(stub[58:64], b'\xff\x25' + struct.pack('<I', 0x10000044))
        bodies = probe.encode_stub(0x10000000, 0x20000000, 0x20000004, probe.CULL_VA, 0x10000044, scope='bodies')
        self.assertEqual((bodies[:27], bodies[47:]), (stub[:27], stub[47:]))
        self.assertEqual(bodies[27:47], bytes.fromhex('751d 8b87d8010000 eb0a'.replace(' ', '')) + b'\xcc' * 10)
        self.assertTrue(probe.scope_stub_ok())
        with self.assertRaises(ValueError):
            probe.encode_stub(1 << 32, 0, 0, 0, 0)
        with self.assertRaises(ValueError):
            probe.encode_stub(0, 0, 0, 0, 0, scope='parts')
        m00 = struct.unpack('<f', struct.pack('<I', 0x3f4ccccc))[0]
        self.assertEqual((probe.threshold_for(2, m00, 1280), probe.threshold_for(4, m00, 1280), probe.threshold_for(8, m00, 1280)), (3, 6, 11))
        self.assertEqual(probe.threshold_for(2, 0.8, 1920), 2)
        self.assertEqual(probe.threshold_for(0, m00, 1280), 0)

    def test_tracked_rows_reproduce_the_census_classes(self):
        document = json.loads((ROOT / 'verification/fixtures/run131-cull-census-rows.json').read_text())
        rows = document['rows']
        self.assertEqual(len(rows), 1214)
        m00 = struct.unpack('<f', struct.pack('<I', int(document['projection_m00_bits'], 16)))[0]
        for px, expected in document['expected'].items():
            with self.subTest(px=px):
                threshold = probe.threshold_for(float(px), m00, document['width'])
                self.assertEqual(threshold, expected['threshold_s'])
                flipped = [r for r in rows if r[8] == 0 and r[0] < threshold]
                self.assertEqual((len(flipped), sum(r[9] for r in flipped)), (expected['nodes'], expected['draws']))
        # The rows carry no parent link: `bodies` is pinned as the fixture assigns parents (proven by limit > thr_1d8, else no body flag).
        mask = int(document['body_flags_mask'], 16)
        self.assertEqual(mask, 0x09000000)
        for px, expected in document['expected'].items():
            with self.subTest(px=px, scope='bodies'):
                flipped = [r for r in rows if r[8] == 0 and r[0] < expected['threshold_s'] and r[7] & mask and not r[6] > r[5]]
                self.assertEqual((len(flipped), sum(r[9] for r in flipped)), (expected['bodies_nodes'], expected['bodies_draws']))
        self.assertEqual([(document['expected'][px]['bodies_nodes'], document['expected'][px]['bodies_draws']) for px in ('2', '4', '8')], [(89, 395), (120, 450), (131, 471)])
        self.assertEqual(sum(1 for r in rows if r[6] > r[5] and r[8] == 0 and r[0] < 11), 0)
        self.assertEqual((document['expected']['2']['draws'], document['expected']['4']['draws'], document['expected']['8']['draws']), (403, 458, 479))
        self.assertTrue(all(r[7] & 2 for r in rows))

    def test_line_parsers(self):
        row = probe.parse_log_line('00:00:01.234 cull_small_parts requested=2 px=2 patched=1 reason=ok site=0x0047d2a2 cull=0x0047d2c3 write=atomic stub=0x0a100000 camera=active')
        self.assertEqual(row, {'requested': '2', 'px': 2.0, 'patched': True, 'reason': 'ok', 'site': 0x47d2a2, 'cull': 0x47d2c3, 'write': 'atomic', 'stub': 0x0a100000, 'camera': 'active', 'scope': None})
        self.assertEqual(probe.parse_log_line(' cull_small_parts requested=2 px=2 patched=1 reason=ok site=0x0047d2a2 cull=0x0047d2c3 write=atomic stub=0x0a100000 camera=active scope=bodies')['scope'], 'bodies')
        self.assertIsNone(probe.parse_log_line('cull_small_parts requested=2 px=0 patched=0 reason=bytes_mismatch'))
        self.assertEqual(probe.parse_value_line('cull_small_parts_value px=2 m00=0.799999952 width=1280 threshold=3'), {'px': 2.0, 'm00': 0.799999952, 'width': 1280, 'threshold': 3})
        frame = probe.parse_frame_line('cull_small_parts_frame device=1 frame=4991 px=2 threshold=3 culled=1147 m00=0.799999952 width=1280')
        self.assertEqual((frame['frame'], frame['threshold'], frame['culled'], frame['width']), (4991, 3, 1147, 1280))
        self.assertIsNone(frame['scope'])
        self.assertEqual(probe.parse_frame_line('cull_small_parts_frame device=1 frame=4991 px=2 threshold=3 culled=806 m00=0.799999952 width=1280 scope=bodies')['scope'], 'bodies')
        self.assertIsNone(probe.parse_frame_line('cull_small_parts_value px=2 m00=0.8 width=1280 threshold=3'))

    def test_core_compiled(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-cull-small-parts-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'cull_small_parts_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'cull_small_parts_core checks_failed=0\n')

    @unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed executable not present')
    def test_installed_executable(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report)


class CullSmallPartsLaunchOption(unittest.TestCase):
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

    def test_absent_or_zero_drops_the_variable_even_when_inherited(self):
        with tempfile.TemporaryDirectory() as directory:
            for args in ((), ('--cull-small-parts', '0')):
                code, output, _ = self.launch(directory, *args, inherited={'X3M_CULL_SMALL_PARTS_PX': '2', 'X3M_CULL_SMALL_PARTS_SCOPE': 'all'})
                self.assertEqual(code, 0)
                self.assertNotIn('X3M_CULL_SMALL_PARTS_PX', json.loads(output)['env'])
                self.assertNotIn('X3M_CULL_SMALL_PARTS_SCOPE', json.loads(output)['env'])

    def test_dry_run_carries_the_value(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = json.loads(self.launch(directory)[1])
            code, output, error = self.launch(directory, '--cull-small-parts', '2')
            self.assertEqual(code, 0, error)
            delivered = json.loads(output)
            self.assertEqual(delivered['command'], baseline['command'])
            self.assertEqual({k: v for k, v in delivered['env'].items() if k not in baseline['env']}, {'X3M_CULL_SMALL_PARTS_PX': '2.0000', 'X3M_CULL_SMALL_PARTS_SCOPE': 'all'})

    def test_scope_forwarded_and_default_overrides_inherited(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, expected in ((('--cull-small-parts-scope', 'all'), 'all'), (('--cull-small-parts-scope', 'bodies'), 'bodies'), ((), 'all')):
                code, output, error = self.launch(directory, '--cull-small-parts', '2', *args, inherited={'X3M_CULL_SMALL_PARTS_SCOPE': 'all'})
                self.assertEqual(code, 0, error)
                self.assertEqual(json.loads(output)['env']['X3M_CULL_SMALL_PARTS_SCOPE'], expected)

    def test_scope_refused_without_the_cull_or_with_an_unknown_value(self):
        with tempfile.TemporaryDirectory() as directory:
            for args in (('--cull-small-parts-scope', 'all'), ('--cull-small-parts', '0', '--cull-small-parts-scope', 'bodies')):
                code, _, error = self.launch(directory, *args)
                self.assertEqual(code, 2, args)
                self.assertIn('--cull-small-parts-scope requires a non-zero --cull-small-parts', error)
            code, _, error = self.launch(directory, '--cull-small-parts', '2', '--cull-small-parts-scope', 'parts')
            self.assertEqual(code, 2)
            self.assertIn('invalid choice', error)

    def modded_launch(self, directory, *args, inherited=None):
        """A dry-run launch against a fake installed proxy, so the launcher
        default applies (no --vanilla)."""
        module = load_manage()
        game = Path(directory) / 'modded'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        (game / 'd3d9.dll').write_bytes(b'proxy')
        (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--game-dir', str(game), *args]
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

    def test_modded_launch_defaults_to_two_px_scope_all(self):
        """Run 43 B default: every modded launch culls at 2 px over all nodes;
        an explicit 0 is the off switch and --vanilla forwards nothing."""
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = self.modded_launch(directory)
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_CULL_SMALL_PARTS_PX'], env['X3M_CULL_SMALL_PARTS_SCOPE']), ('2.0000', 'all'))
            # An explicit value and an explicit scope still win.
            env = json.loads(self.modded_launch(directory, '--cull-small-parts', '4', '--cull-small-parts-scope', 'bodies')[1])['env']
            self.assertEqual((env['X3M_CULL_SMALL_PARTS_PX'], env['X3M_CULL_SMALL_PARTS_SCOPE']), ('4.0000', 'bodies'))
            # The scope alone is enough on a modded launch: the cull is on by default.
            code, output, error = self.modded_launch(directory, '--cull-small-parts-scope', 'bodies')
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_CULL_SMALL_PARTS_SCOPE'], 'bodies')
            # Explicit off, even with the variables inherited.
            env = json.loads(self.modded_launch(directory, '--cull-small-parts', '0',
                                                inherited={'X3M_CULL_SMALL_PARTS_PX': '8', 'X3M_CULL_SMALL_PARTS_SCOPE': 'bodies'})[1])['env']
            self.assertNotIn('X3M_CULL_SMALL_PARTS_PX', env)
            self.assertNotIn('X3M_CULL_SMALL_PARTS_SCOPE', env)
            # --vanilla sets nothing and still refuses a bare scope.
            self.assertNotIn('X3M_CULL_SMALL_PARTS_PX', json.loads(self.launch(directory)[1])['env'])

    def test_out_of_range_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('65', '-1', 'nan', '1e-7'):
                code, _, error = self.launch(directory, '--cull-small-parts', value)
                self.assertEqual(code, 2, value)
                self.assertIn('--cull-small-parts out of range', error)


if __name__ == '__main__':
    unittest.main()
