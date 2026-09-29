"""Dust-scene leak fix: the site header, its Python twin, the log parsers, the production wiring and the launcher option.

The header src/proxy/dust_leak_fix_sites.h is compiled on the host (no Windows dependency) and must print the window,
the site, the stub template and the VAs the verifier pins; the stub encoder must agree with the verifier's twin; the
parsers must accept the rows the fixture and the DLL write; capture.cpp, loader.cpp and CMakeLists.txt must carry the
module; the launcher must send X3M_DUST_LEAK_FIX=on by default, the explicit value otherwise, nothing under --vanilla.
The installed executable, when present, must pass verify_dust_leak_fix_site.py and a patched copy must fail it.
"""
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_dust_leak_fix_site as verifier  # noqa: E402
from source_text import source_text  # noqa: E402
from test_config_schema import hermetic_launcher, launch_env  # noqa: E402

EXE = Path(verifier.DEFAULT_EXE)
HARNESS = r'''
#include "dust_leak_fix_sites.h"
#include <cstdio>
using namespace x3m::dust_leak_fix::sites;
static unsigned failed = 0;
static void expect(bool ok, const char* what) { if (!ok) { ++failed; std::printf("FAIL %s\n", what); } }
static void hex(const unsigned char* p, unsigned n) { for (unsigned i = 0; i < n; ++i) std::printf("%02x", p[i]); std::printf("\n"); }
int main() {
    hex(expected_window, window_length); hex(expected_site, site_length); hex(stub_code, stub_code_length);
    std::printf("%08x %08x %08x %08x %08x %08x\n", unsigned(window_va), unsigned(site_va), unsigned(jne_va), unsigned(exit_va), unsigned(loop_va), unsigned(release_va));
    unsigned char out[stub_length];
    encode_stub(0x00a30000u, 0x00487be0u, 0x00a30034u, 0x00a30030u, out);
    hex(out, stub_length);
    expect(plan(expected_window) == nullptr, "plan accepts the engine window");
    for (unsigned i = 0; i < window_length; ++i) {
        unsigned char changed[window_length]; for (unsigned j = 0; j < window_length; ++j) changed[j] = expected_window[j];
        changed[i] ^= 0x01;
        const char* r = plan(changed);
        expect(r && r[0] == 'b', "a changed window byte is refused");
    }
    Mode m = Mode::off;
    expect(parse_mode("on", &m) && m == Mode::on, "on"); expect(parse_mode("off", &m) && m == Mode::off, "off");
    expect(!parse_mode("On", &m) && !parse_mode("", &m) && !parse_mode("on ", &m) && !parse_mode((const char*)nullptr, &m), "strict parse");
    expect(default_mode == Mode::on, "default on");
    expect(detours(0x1000, 0) && !detours(0, 0) && !detours(0x1000, 0x2000), "detour model");
    std::printf("dust_leak_fix_core checks_failed=%u\n", failed);
    return failed ? 1 : 0;
}
'''


class DustLeakFixCore(unittest.TestCase):
    def test_core_compiled_bytes_and_parser(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-dust-leak-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'dust_leak_fix_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            lines = run.stdout.splitlines()
            self.assertEqual(lines[-1], 'dust_leak_fix_core checks_failed=0')
            self.assertEqual([bytes.fromhex(line) for line in lines[0:3]], [verifier.WINDOW, verifier.SITE, verifier.STUB_CODE])
            self.assertEqual([int(v, 16) for v in lines[3].split()],
                             [verifier.WINDOW_VA, verifier.SITE_VA, verifier.JNE_VA, verifier.EXIT_VA, verifier.LOOP_VA, verifier.RELEASE_VA])
            self.assertEqual(bytes.fromhex(lines[4]), verifier.encode_stub(0x00a30000, 0x00487be0, 0x00a30034, 0x00a30030))

    def test_python_twin(self):
        self.assertEqual(verifier.source_constants(source_text(ROOT / 'src/proxy/dust_leak_fix_sites.h')), verifier.EXPECTED_CONSTANTS)
        self.assertEqual(verifier.WINDOW[verifier.SITE_VA - verifier.WINDOW_VA:][:5], verifier.SITE)
        self.assertEqual(verifier.SITE_VA // 8, (verifier.SITE_VA + 4) // 8)   # the five patch bytes: one lock cmpxchg8b
        stub = verifier.encode_stub(0x1000, verifier.RELEASE_VA, 0x2004, 0x2000)
        self.assertEqual(int.from_bytes(stub[0x0f:0x13], 'little', signed=True), verifier.RELEASE_VA - (0x1000 + 0x13))
        self.assertEqual((stub[0x23:0x27], stub[0x29:0x2d]), ((0x2004).to_bytes(4, 'little'), (0x2000).to_bytes(4, 'little')))
        self.assertEqual(len(stub), 45)

    def test_line_parsers(self):
        row = verifier.parse_log_line('00:01 dust_leak_fix site=0041f4d1 status=patched reason=ok mode=on setting=on write=atomic stub=00a300d0')
        self.assertEqual(row, {'site': 0x41f4d1, 'status': 'patched', 'reason': 'ok', 'mode': 'on', 'setting': 'on', 'write': 'atomic', 'stub': 0xa300d0, 'patched': True})
        off = verifier.parse_log_line('dust_leak_fix site=0041f4d1 status=off reason=off mode=off setting=off write=none stub=00000000')
        self.assertEqual((off['patched'], off['status']), (False, 'off'))
        self.assertIsNone(verifier.parse_log_line('dust_leak_fix site=0041f4d1 status=patched reason=ok mode=on setting=on write=atomic'))
        restore = verifier.parse_restore_line('dust_leak_fix_restore site=0041f4d1 status=restored found=e9321d5b00 registered=0')
        self.assertEqual(restore, {'site': 0x41f4d1, 'status': 'restored', 'found': bytes.fromhex('e9321d5b00'), 'registered': False})
        self.assertIsNone(verifier.parse_restore_line('dust_leak_fix_restore site=0041f4d1 status=restore_not_owned found=-- registered=1')['found'])
        self.assertEqual(verifier.parse_hits_line('12:34 dust_leak_fix hits=27 total=81 frame=900'), {'hits': 27, 'total': 81, 'frame': 900})
        self.assertIsNone(verifier.parse_hits_line('dust_leak_fix site=0041f4d1 status=off reason=off mode=off setting=off write=none stub=00000000'))

    def test_production_wiring(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertEqual(capture.count('dust_leak_fix::initialize();'), 1)
        self.assertLess(capture.index('game_phases::initialize();'), capture.index('dust_leak_fix::initialize();'))
        self.assertIn('if ((log_tier::cached_perf || log_tier::cached_debug) && ctx.frame % 300 == 0) dust_leak_fix::report(ctx.frame);', capture)
        self.assertEqual(capture.count('dust_leak_fix::report('), 1)
        self.assertIn('x3m::dust_leak_fix::shutdown();', source_text(ROOT / 'src/proxy/loader.cpp'))
        self.assertIn('src/proxy/dust_leak_fix.cpp', source_text(ROOT / 'CMakeLists.txt'))
        module = source_text(ROOT / 'src/proxy/dust_leak_fix.cpp')
        for needle in ('L"X3M_DUST_LEAK_FIX"', '"too_long"', '"invalid_setting"', 'install_window_open()', 'executable_verified()', 'sites::plan(current)',
                       'engine_patch::claim(site_, spec)', 'engine_patch::store_pointer(slot, *site_.entry)', 'engine_patch::push_front(site_',
                       'take_back("chain_failed")', 'take_back("readback_mismatch")', '"rollback_failed"', '"patched_unverified"', '"restore_not_owned"',
                       'log_handle()', 'WriteFile(handle', 'SetLastError(error);',
                       'log("dust_leak_fix site=%08lx status=%s reason=%s mode=%s setting=%s write=%s stub=%08lx"',
                       'log("dust_leak_fix hits=%lu total=%lu frame=%u"',
                       '"dust_leak_fix_restore site=%08lx status=%s found=%s registered=%u\\n"'):
            self.assertIn(needle, module)
        self.assertNotIn('windows.h', source_text(ROOT / 'src/proxy/dust_leak_fix_sites.h'))
        for forbidden in ('float ', 'double ', 'GetModuleHandleEx'):   # no FP, and no pin needed: the stub holds no pointer into the DLL
            self.assertNotIn(forbidden, module)


@unittest.skipUnless(EXE.is_file(), 'installed executable not present')
class DustLeakFixSite(unittest.TestCase):
    def run_verifier(self, exe):
        run = subprocess.run([sys.executable, str(ROOT / 'verification/probe/verify_dust_leak_fix_site.py'), '--exe', str(exe)],
                             capture_output=True, text=True, timeout=600)
        return run.returncode, json.loads(run.stdout)

    def test_installed_executable(self):
        code, report = self.run_verifier(EXE)
        self.assertEqual((code, report['result']), (0, 'PASS'), json.dumps(report.get('checks'), indent=1))
        self.assertEqual(report['site_sources'], [hex(v) for v in verifier.FAILURE_SOURCES])
        self.assertEqual(report['exe_sha256'], hashlib.sha256(EXE.read_bytes()).hexdigest())

    def test_patched_copy_fails(self):
        with tempfile.TemporaryDirectory(prefix='x3-dust-leak-') as temporary:
            copy = Path(temporary) / 'patched.exe'
            copy.write_bytes(verifier.patched_image(EXE.read_bytes()))
            code, report = self.run_verifier(copy)
            self.assertEqual((code, report['result']), (1, 'FAIL'))
            self.assertFalse(report['checks']['window_bytes'])


class DustLeakFixLaunchOption(unittest.TestCase):
    def test_default_explicit_and_vanilla(self):
        module, game, wine, directory = hermetic_launcher()
        try:
            self.assertEqual(launch_env(module, game, wine)['X3M_DUST_LEAK_FIX'], 'on')
            self.assertEqual(launch_env(module, game, wine, '--dust-leak-fix', 'off')['X3M_DUST_LEAK_FIX'], 'off')
            self.assertEqual(launch_env(module, game, wine, '--dust-leak-fix', 'on')['X3M_DUST_LEAK_FIX'], 'on')
            self.assertNotIn('X3M_DUST_LEAK_FIX', launch_env(module, game, wine, '--vanilla'))
            with self.assertRaises(SystemExit):
                launch_env(module, game, wine, '--vanilla', '--dust-leak-fix', 'off')
        finally:
            directory.cleanup()


if __name__ == '__main__':
    unittest.main()
