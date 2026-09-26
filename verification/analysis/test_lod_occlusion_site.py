"""Host checks of the LOD occlusion patch (src/proxy/lod_occlusion_sites.h).

The site header compiled on the host (window, original and patched rel32,
refusal of a changed or already patched window, the X3M_LOD_OCCLUSION parser,
install/read-back/rollback/restore against a copied window at the engine's
qword offset), the bytes against the site verifier's Python twin, the site
verifier on the installed executable and its refusal on a patched copy, the
install/restore line parsers, the production wiring and the --lod-occlusion
launcher option (--dry-run only, never a launch). No Wine.
"""
import contextlib
import hashlib
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
import verify_lod_occlusion_site as verifier  # noqa: E402

EXE = Path(verifier.DEFAULT_EXE)

HARNESS = r'''
#include "lod_occlusion_sites.h"
#include <cstdio>
#include <cstring>
using namespace x3m::lod_occlusion::sites;
// install()/restore()'s Ops over a buffer: reads and stores only inside it, stores only while "writable",
// counters for the no-write claims, and three failure injections.
struct BufferOps {
    static constexpr std::uint32_t writable = 0x40, initial = 0x20;   // PAGE_EXECUTE_READWRITE / PAGE_EXECUTE_READ
    unsigned char* base; unsigned size;
    std::uint32_t protection = initial;
    unsigned writes = 0, protects = 0, stuck_after = 0;   // stuck_after: stores numbered above it do not land (0 = never)
    bool fail_protect = false, drop_writes = false, corrupt_first = false, last_atomic = false;
    bool in(std::uintptr_t at, unsigned n) const { const auto b = reinterpret_cast<std::uintptr_t>(base); return at >= b && at + n <= b + size; }
    bool read(std::uintptr_t at, unsigned char* out, unsigned n) { if (!in(at, n)) return false; std::memcpy(out, reinterpret_cast<const void*>(at), n); return true; }
    bool protect(std::uintptr_t at, unsigned n, std::uint32_t p, std::uint32_t* previous) {
        ++protects; if (fail_protect || !in(at, n)) return false; *previous = protection; protection = p; return true;
    }
    bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic) {
        *atomic = last_atomic = (at & 7u) + n <= 8u;
        if (!in(at, n) || protection != writable) return false;
        ++writes;
        if (drop_writes || (stuck_after && writes > stuck_after)) return true;
        std::memcpy(reinterpret_cast<void*>(at), bytes, n);
        if (corrupt_first) { reinterpret_cast<unsigned char*>(at)[0] = 0xc8; corrupt_first = false; }
        return true;
    }
};
static void hex(const unsigned char* p, unsigned n) { for (unsigned i = 0; i < n; ++i) std::printf("%02x", p[i]); std::printf("\n"); }
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    hex(expected_window, window_length); hex(expected_site, site_length); hex(expected_write, write_length); hex(patched_write, write_length);
    std::printf("%08lx %08lx %08lx %08lx %08lx\n", (unsigned long)window_va, (unsigned long)site_va, (unsigned long)write_va, (unsigned long)lod0_va, (unsigned long)placeholder_va);
    unsigned char copy[window_length];
    check(plan(expected_window) == nullptr, "engine window accepted");
    const unsigned offsets[] = {0, 2, 5, 12, site_offset, site_offset + 1, write_offset, write_offset + 3, 25, window_length - 1};
    for (unsigned at : offsets) {
        std::memcpy(copy, expected_window, window_length); copy[at] ^= 0x01;
        const char* r = plan(copy); check(r && !std::strcmp(r, "bytes_mismatch"), "a changed window byte is refused");
    }
    std::memcpy(copy, expected_window, window_length); std::memcpy(copy + write_offset, patched_write, write_length);
    { const char* r = plan(copy); check(r && !std::strcmp(r, "bytes_mismatch"), "an already patched window is refused"); }
    Mode m = Mode::all;
    check(parse_mode("record0", &m) && m == Mode::record0, "record0");
    check(parse_mode("all", &m) && m == Mode::all, "all");
    check(parse_mode(L"record0", &m) && m == Mode::record0 && parse_mode(L"all", &m) && m == Mode::all, "wide");
    const char* rejected[] = {"", "Record0", "ALL", " all", "all ", "alls", "al", "record", "record1", "record0\n", "0", "1", "on", "off", "none", "record0,all"};
    for (const char* t : rejected) { m = Mode::all; check(!parse_mode(t, &m) && m == Mode::all, t); }
    check(!parse_mode(static_cast<const char*>(nullptr), &m), "null");
    check(default_mode == Mode::record0 && !std::strcmp(mode_name(Mode::record0), "record0") && !std::strcmp(mode_name(Mode::all), "all"), "default and names");
    check(setting_capacity == 32, "1..31 characters");
    // install()/restore() against a copied window in a buffer, the rel32 at the engine's offset 1 of its 8-byte word
    // (window at +7: 0x004c34e7 & 7 == 7, so window + 18 & 7 == 1).
    alignas(8) static unsigned char code[64];
    unsigned char original[64];
    std::memset(code, 0xcc, sizeof code); std::memcpy(code + 7, expected_window, window_length); std::memcpy(original, code, sizeof code);
    const std::uintptr_t window = reinterpret_cast<std::uintptr_t>(code + 7), at = window + write_offset;
    check((at & 7u) == (write_va & 7u), "buffer write span at the engine's qword offset");
    BufferOps ops{code, sizeof code};
    bool live = true, atomic = false; std::uint32_t protection = 0;
    auto is = [](const char* r, const char* want) { return r && !std::strcmp(r, want); };
    auto span_is = [&](const unsigned char* want) { return !std::memcmp(code + 7 + write_offset, want, write_length); };
    // A mismatched window: refused before any protection change or write.
    code[7 + 4] ^= 0x01;
    check(is(install(ops, window, true, &live, &atomic, &protection), "bytes_mismatch") && !live && ops.writes == 0 && ops.protects == 0 && span_is(expected_write), "mismatched window refused, nothing written");
    code[7 + 4] ^= 0x01;
    check(is(install(ops, window, false, &live, &atomic, &protection), "late_claim") && !live && ops.writes == 0 && ops.protects == 0, "closed install window refused, nothing written");
    check(is(install(ops, 0, true, &live, &atomic, &protection), "invalid_site") && ops.writes == 0, "null window refused");
    check(is(install(ops, reinterpret_cast<std::uintptr_t>(code) + 40, true, &live, &atomic, &protection), "unreadable") && ops.writes == 0, "window past the readable range refused");
    check(!std::memcmp(code, original, sizeof code), "buffer untouched by the refusals");
    // The write: 00 00 00 00 read back, the atomic path, the protection put back, nothing else changed.
    check(is(install(ops, window, true, &live, &atomic, &protection), "ok") && live && atomic && ops.writes == 1, "install ok, one atomic write");
    check(span_is(patched_write) && code[7 + site_offset] == 0x0f && code[7 + site_offset + 1] == 0x85 && protection == BufferOps::initial && ops.protection == BufferOps::initial,
          "rel32 read back as 00 00 00 00, opcode 0f 85 kept, protection restored");
    { unsigned char expect[64]; std::memcpy(expect, original, 64); std::memcpy(expect + 7 + write_offset, patched_write, write_length); check(!std::memcmp(code, expect, 64), "only the four rel32 bytes changed"); }
    check(is(install(ops, window, true, &live, &atomic, &protection), "bytes_mismatch") && ops.writes == 1, "already patched window refused, no second write");
    // The restore: c9 00 00 00 back, the whole buffer as before, what it found reported; a second restore writes nothing.
    unsigned char found[write_length]{}; bool found_read = false;
    auto found_is = [&](const unsigned char* want) { return found_read && !std::memcmp(found, want, write_length); };
    const unsigned char corrupt[write_length] = {0xc8, 0x00, 0x00, 0x00};
    check(is(restore(ops, at, protection, found, &found_read), "restored") && found_is(patched_write) && ops.writes == 2 && ops.last_atomic &&
          !std::memcmp(code, original, sizeof code) && ops.protection == BufferOps::initial, "restore returns c9 00 00 00 over 00 00 00 00, atomic");
    check(is(restore(ops, at, protection, found, &found_read), "restored") && found_is(expected_write) && ops.writes == 2, "restore of a span already holding c9 00 00 00: nothing written");
    // Failure paths: a store that does not land rolls back; a failed protect writes nothing; a store that sticks stays registered.
    ops.drop_writes = true;
    check(is(install(ops, window, true, &live, &atomic, &protection), "patch_rolled_back") && !live && !std::memcmp(code, original, sizeof code), "unverified write rolled back, judged by read-back");
    ops.drop_writes = false; ops.fail_protect = true; const unsigned before = ops.writes;
    check(is(install(ops, window, true, &live, &atomic, &protection), "protect_failed") && !live && ops.writes == before, "protect failure: nothing written");
    ops.fail_protect = false; ops.stuck_after = ops.writes + 1; ops.corrupt_first = true;
    check(is(install(ops, window, true, &live, &atomic, &protection), "rollback_failed") && live && span_is(corrupt), "wrong bytes that cannot be undone stay registered (live)");
    // Restore only over the patched bytes: foreign bytes (here the failed rollback's) are refused, nothing written.
    { const unsigned w = ops.writes, pr = ops.protects;
      check(is(restore(ops, at, protection, found, &found_read), "restore_not_owned") && found_is(corrupt) && span_is(corrupt) && ops.writes == w && ops.protects == pr,
            "foreign bytes on a registered span: restore_not_owned, nothing written"); }
    ops.stuck_after = 0;
    std::memcpy(code + 7 + write_offset, patched_write, write_length);
    ops.drop_writes = true;
    check(is(restore(ops, at, protection, found, &found_read), "restore_failed") && found_is(patched_write) && span_is(patched_write) && ops.protection == BufferOps::initial,
          "restore store that does not land: restore_failed, found reported, protection put back");
    ops.drop_writes = false; ops.fail_protect = true;
    { const unsigned w = ops.writes;
      check(is(restore(ops, at, protection, found, &found_read), "restore_failed") && found_is(patched_write) && span_is(patched_write) && ops.writes == w,
            "restore protect failure: restore_failed, nothing written"); }
    ops.fail_protect = false;
    check(is(restore(ops, at, protection, found, &found_read), "restored") && found_is(patched_write) && !std::memcmp(code, original, sizeof code) &&
          ops.protection == BufferOps::initial, "restore after the failures: c9 00 00 00, whole buffer as before");
    { const unsigned w = ops.writes, pr = ops.protects;
      check(is(restore(ops, reinterpret_cast<std::uintptr_t>(code) + sizeof code, protection, found, &found_read), "restore_not_owned") && !found_read &&
            ops.writes == w && ops.protects == pr, "unreadable span: restore_not_owned, found unread, nothing written"); }
    std::printf("lod_occlusion_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


def load_manage():
    spec = importlib.util.spec_from_file_location('lod_occlusion_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class LodOcclusionCore(unittest.TestCase):
    def test_core_compiled_bytes_refusals_parser_and_sequence(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-lod-occlusion-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'lod_occlusion_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            lines = run.stdout.splitlines()
            self.assertEqual(lines[-1], 'lod_occlusion_core checks_failed=0')
            self.assertEqual(bytes.fromhex(lines[0]), verifier.WINDOW)
            self.assertEqual(bytes.fromhex(lines[0]), bytes.fromhex('8b4d0c 83b94c01000000 8b15746f6000 0f85c9000000 837c247400 89542418'))
            self.assertEqual([bytes.fromhex(line) for line in lines[1:4]], [verifier.SITE, verifier.WRITE, verifier.PATCHED])
            self.assertEqual([int(v, 16) for v in lines[4].split()],
                             [verifier.WINDOW_VA, verifier.SITE_VA, verifier.WRITE_VA, verifier.LOD0_VA, verifier.PLACEHOLDER_VA])

    def test_python_twin(self):
        self.assertEqual(verifier.source_constants((ROOT / 'src/proxy/lod_occlusion_sites.h').read_text()), verifier.EXPECTED_CONSTANTS)
        # jne rel32 +0xc9 reaches the placeholder bind; rel32 0 reaches the next instruction.
        self.assertEqual(verifier.SITE_VA + 6 + int.from_bytes(verifier.WRITE, 'little', signed=True), verifier.PLACEHOLDER_VA)
        self.assertEqual(verifier.SITE_VA + 6 + int.from_bytes(verifier.PATCHED, 'little', signed=True), verifier.LOD0_VA)
        self.assertEqual(verifier.WINDOW[verifier.SITE_VA - verifier.WINDOW_VA:][:6], verifier.SITE)
        self.assertEqual(verifier.SITE[2:], verifier.WRITE)
        # The four written bytes share one aligned qword (one lock cmpxchg8b); the whole six-byte jne does not.
        self.assertEqual(verifier.WRITE_VA // 8, (verifier.WRITE_VA + 3) // 8)
        self.assertNotEqual(verifier.SITE_VA // 8, (verifier.SITE_VA + 5) // 8)

    def test_install_line_parser(self):
        row = verifier.parse_log_line
        ok = row('00:01 lod_occlusion site=004c34f7 status=patched reason=ok mode=all setting=all write=atomic')
        self.assertEqual(ok, {'site': 0x4c34f7, 'status': 'patched', 'reason': 'ok', 'mode': 'all', 'setting': 'all', 'write': 'atomic', 'patched': True, 'default': None})
        # Since Run 81 the row names the value's source: default=1 when the launcher filled in its default.
        self.assertTrue(row('lod_occlusion site=004c34f7 status=patched reason=ok mode=all setting=all write=atomic default=1')['default'])
        self.assertIs(row('lod_occlusion site=004c34f7 status=off reason=record0 mode=record0 setting=record0 write=none default=0')['default'], False)
        off = row('lod_occlusion site=004c34f7 status=off reason=record0 mode=record0 setting=- write=none')
        self.assertEqual((off['patched'], off['status'], off['mode']), (False, 'off', 'record0'))
        refused = row('lod_occlusion site=004c34f7 status=refused reason=bytes_mismatch mode=all setting=all write=none')
        self.assertEqual((refused['patched'], refused['reason']), (False, 'bytes_mismatch'))
        self.assertEqual(row('lod_occlusion site=004c34f7 status=refused reason=too_long mode=- setting=? write=none')['mode'], '-')
        unverified = row('lod_occlusion site=004c34f7 status=patched_unverified reason=rollback_failed mode=all setting=all write=atomic')
        self.assertEqual((unverified['patched'], unverified['status'], unverified['reason']), (False, 'patched_unverified', 'rollback_failed'))
        self.assertIsNone(row('lod_occlusion site=004c34f7 status=patched reason=ok'))
        self.assertIsNone(row('lod_occlusion site=004c34f7 status=maybe reason=ok mode=all setting=- write=atomic'))
        self.assertIsNone(row('lod_occlusion site=004c34f7 status=patched reason=ok mode=size setting=- write=atomic'))

    def test_restore_line_parser(self):
        row = verifier.parse_restore_line('lod_occlusion_restore site=004c34f7 status=restored found=00000000 registered=0')
        self.assertEqual(row, {'site': 0x4c34f7, 'status': 'restored', 'found': b'\0\0\0\0', 'registered': False})
        failed = verifier.parse_restore_line('lod_occlusion_restore site=004c34f7 status=restore_failed found=-- registered=1')
        self.assertEqual((failed['status'], failed['found'], failed['registered']), ('restore_failed', None, True))
        self.assertEqual(verifier.parse_restore_line('lod_occlusion_restore site=004c34f7 status=restore_not_owned found=c8000000 registered=0')['found'],
                         bytes.fromhex('c8000000'))
        self.assertIsNone(verifier.parse_restore_line('lod_occlusion_restore site=004c34f7 status=restored found=eb05 registered=0'))
        self.assertIsNone(verifier.parse_restore_line('lod_occlusion_restore site=004c34f7 status=restored'))

    def test_production_wiring(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertEqual(capture.count('lod_occlusion::initialize();'), 1)
        self.assertLess(capture.index('game_phases::initialize();'), capture.index('lod_occlusion::initialize();'))
        self.assertLess(capture.index('terran_station_lod::initialize();'), capture.index('lod_occlusion::initialize();'))
        self.assertIn('if (reserved == nullptr) x3m::lod_occlusion::shutdown();', (ROOT / 'src/proxy/loader.cpp').read_text())
        self.assertIn('src/proxy/lod_occlusion.cpp', (ROOT / 'CMakeLists.txt').read_text())
        module = (ROOT / 'src/proxy/lod_occlusion.cpp').read_text()
        for needle in ('L"X3M_LOD_OCCLUSION"', 'setting_capacity', '"too_long"', '"invalid_setting"', 'install_window_open()', 'executable_verified()',
                       'sites::install(ops, window_address, engine_patch::install_window_open()', 'sites::restore(ops, write_at_, site_protection, found, &found_read)',
                       'engine_patch::write_code', 'FlushInstructionCache', 'VirtualProtect', 'PAGE_EXECUTE_READWRITE',
                       '"patched_unverified"', 'if (!std::strcmp(reason, "restored")) patched_ = false;', 'log_handle()', 'WriteFile(handle',
                       '"lod_occlusion_restore site=%08lx status=%s found=%s registered=%u\\n"', 'SetLastError(error);',
                       'log("lod_occlusion site=%08lx status=%s reason=%s mode=%s setting=%s write=%s default=%u"',
                       'x3m::config::get(L"X3M_LOD_OCCLUSION_DEFAULT", marker, 2) == 1 && marker[0] == L\'1\''):
            self.assertIn(needle, module)
        header = (ROOT / 'src/proxy/lod_occlusion_sites.h').read_text()
        for needle in ('plan(current)', 'memcmp(back, expected_write, write_length)', '"patch_rolled_back"', '"rollback_unprotected"', '"rollback_failed"',
                       '"restore_not_owned"', '"restore_failed"', '*live = true;'):
            self.assertIn(needle, header)
        self.assertNotIn('windows.h', header)
        for forbidden in ('float ', 'double ', 'push_front', 'Emitter'):
            self.assertNotIn(forbidden, module)


@unittest.skipUnless(EXE.is_file(), 'installed executable not present')
class LodOcclusionSite(unittest.TestCase):
    def run_verifier(self, exe):
        return subprocess.run([sys.executable, str(ROOT / 'verification/probe/verify_lod_occlusion_site.py'), '--exe', str(exe)],
                              capture_output=True, text=True, timeout=300, cwd=ROOT / 'verification/probe')

    def test_installed_executable(self):
        run = self.run_verifier(EXE)
        self.assertEqual(run.returncode, 0, run.stdout[-3000:] + run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(report['result'], 'PASS')
        self.assertEqual(report['site_bytes'], '0f85c9000000')
        self.assertEqual(report['placeholder_sources'], ['0x4c34f7'])
        self.assertEqual((report['raw_branch_hits_not_interior'], report['incoming_window_branches'], report['dword_refs'], report['overlapping_claims']),
                         ([], [], 0, []))

    def test_patched_copy_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            copy = Path(directory) / 'X3AP.exe'
            copy.write_bytes(verifier.patched_image(EXE.read_bytes()))
            report = json.loads(self.run_verifier(copy).stdout)
            self.assertEqual(report['result'], 'FAIL')
            self.assertTrue(report['checks']['exe_identity'])
            self.assertFalse(report['checks']['window_bytes'])
            self.assertEqual(report['site_bytes'], '0f8500000000')


class LodOcclusionLaunchOption(unittest.TestCase):
    NAME = 'X3M_LOD_OCCLUSION'

    def launch(self, directory, *args, inherited=None, vanilla=False):
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
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def value(self, directory, *args, **kwargs):
        code, output, error = self.launch(directory, *args, **kwargs)
        self.assertEqual(code, 0, error)
        return json.loads(output)['env'].get(self.NAME)

    def pair(self, directory, *args, **kwargs):
        code, output, error = self.launch(directory, *args, **kwargs)
        self.assertEqual(code, 0, error)
        env = json.loads(output)['env']
        return env.get(self.NAME), env.get(self.NAME + '_DEFAULT')

    def test_default_explicit_modes_and_vanilla(self):
        # Default all since Run 81 (user decision 2026-09-24), marked default=1; an explicit value is default=0;
        # off is the no-patch spelling of record0; --vanilla sends neither variable.
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.pair(directory), ('all', '1'))
            self.assertEqual(self.pair(directory, inherited={self.NAME: 'record0', self.NAME + '_DEFAULT': '0'}), ('all', '1'))   # a stale value never travels
            self.assertEqual(self.pair(directory, '--lod-occlusion', 'record0'), ('record0', '0'))
            self.assertEqual(self.pair(directory, '--lod-occlusion', 'off', inherited={self.NAME: 'all'}), ('record0', '0'))
            self.assertEqual(self.pair(directory, '--lod-occlusion', 'all', inherited={self.NAME: 'record0'}), ('all', '0'))
            self.assertEqual(self.pair(directory, vanilla=True, inherited={self.NAME: 'all', self.NAME + '_DEFAULT': '1'}), (None, None))

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, vanilla, message in ((('--lod-occlusion', 'all'), True, 'cannot be combined with --vanilla'),
                                           (('--lod-occlusion', 'record0'), True, 'cannot be combined with --vanilla'),
                                           (('--lod-occlusion', 'off'), True, 'cannot be combined with --vanilla'),
                                           (('--lod-occlusion', 'All'), False, 'invalid choice'),
                                           (('--lod-occlusion', 'on'), False, 'invalid choice')):
                with self.subTest(args=args, vanilla=vanilla):
                    code, _, error = self.launch(directory, *args, vanilla=vanilla)
                    self.assertNotEqual(code, 0)
                    self.assertIn(message, error)

    def test_launch_command_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            record0 = json.loads(self.launch(directory, '--lod-occlusion', 'record0')[1])
            every = json.loads(self.launch(directory, '--lod-occlusion', 'all')[1])
            self.assertEqual(record0['command'], every['command'])
            self.assertEqual({k: v for k, v in every['env'].items() if record0['env'].get(k) != v}, {self.NAME: 'all'})


if __name__ == '__main__':
    unittest.main()
