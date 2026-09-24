"""Host checks of the Terran-station LOD patch (src/proxy/terran_lod_sites.h).

The site header compiled on the host (window, original and patched bytes,
refusal of a changed or already patched window, the X3M_TERRAN_STATION_LOD
parser), the census's flag31 ancestor stack (src/proxy/cull_census_core.h),
the bytes against the site verifier's Python twin, the site verifier on the
installed executable and its refusal on a patched copy, the install-line and
census-row parsers, the production wiring and the --terran-station-lod
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
import verify_cull_census_sites as census  # noqa: E402
import verify_terran_lod_site as verifier  # noqa: E402

EXE = Path(verifier.DEFAULT_EXE)

HARNESS = r'''
#include "terran_lod_sites.h"
#include "cull_census_core.h"
#include <cstdio>
#include <cstring>
using namespace x3m::terran_station_lod::sites;
using x3m::cull_census::core::AncestorStack;
using x3m::cull_census::core::flag31_suffix;
using x3m::cull_census::core::flag31_unknown;
using x3m::cull_census::core::ancestor_cap;
// install()/restore()'s Ops over a buffer: reads and stores only inside it, stores only while "writable",
// counters for the no-write claims, and three failure injections.
struct BufferOps {
    static constexpr std::uint32_t writable = 0x40, initial = 0x20;   // PAGE_EXECUTE_READWRITE / PAGE_EXECUTE_READ
    unsigned char* base; unsigned size;
    std::uint32_t protection = initial;
    unsigned writes = 0, protects = 0, stuck_after = 0;   // stuck_after: stores numbered above it do not land (0 = never)
    bool fail_protect = false, drop_writes = false, corrupt_first = false;
    bool in(std::uintptr_t at, unsigned n) const { const auto b = reinterpret_cast<std::uintptr_t>(base); return at >= b && at + n <= b + size; }
    bool read(std::uintptr_t at, unsigned char* out, unsigned n) { if (!in(at, n)) return false; std::memcpy(out, reinterpret_cast<const void*>(at), n); return true; }
    bool protect(std::uintptr_t at, unsigned n, std::uint32_t p, std::uint32_t* previous) {
        ++protects; if (fail_protect || !in(at, n)) return false; *previous = protection; protection = p; return true;
    }
    bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic) {
        *atomic = (at & 7u) + n <= 8u;
        if (!in(at, n) || protection != writable) return false;
        ++writes;
        if (drop_writes || (stuck_after && writes > stuck_after)) return true;
        std::memcpy(reinterpret_cast<void*>(at), bytes, n);
        if (corrupt_first) { reinterpret_cast<unsigned char*>(at)[1] ^= 0x03; corrupt_first = false; }
        return true;
    }
};
static void hex(const unsigned char* p, unsigned n) { for (unsigned i = 0; i < n; ++i) std::printf("%02x", p[i]); std::printf("\n"); }
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    hex(expected_window, window_length); hex(expected_site, site_length); hex(patched_site, site_length);
    std::printf("%08lx %08lx %08lx\n", (unsigned long)window_va, (unsigned long)site_va, (unsigned long)target_va);
    unsigned char copy[window_length];
    check(plan(expected_window) == nullptr, "engine window accepted");
    const unsigned offsets[] = {0, 5, 9, site_offset, site_offset + 1, 12, window_length - 1};
    for (unsigned at : offsets) {
        std::memcpy(copy, expected_window, window_length); copy[at] ^= 0x01;
        const char* r = plan(copy); check(r && !std::strcmp(r, "bytes_mismatch"), "a changed window byte is refused");
    }
    std::memcpy(copy, expected_window, window_length); std::memcpy(copy + site_offset, patched_site, site_length);
    { const char* r = plan(copy); check(r && !std::strcmp(r, "bytes_mismatch"), "an already patched window is refused"); }
    Mode m = Mode::distance;
    check(parse_mode("size", &m) && m == Mode::size, "size");
    check(parse_mode("distance", &m) && m == Mode::distance, "distance");
    check(parse_mode(L"size", &m) && m == Mode::size && parse_mode(L"distance", &m) && m == Mode::distance, "wide");
    const char* rejected[] = {"", "Size", "SIZE", " size", "size ", "sizes", "siz", "dist", "distance\n", "distances", "0", "1", "on", "off", "size,distance"};
    for (const char* t : rejected) { m = Mode::distance; check(!parse_mode(t, &m) && m == Mode::distance, t); }
    check(!parse_mode(static_cast<const char*>(nullptr), &m), "null");
    check(default_mode == Mode::size && !std::strcmp(mode_name(Mode::size), "size") && !std::strcmp(mode_name(Mode::distance), "distance"), "default and names");
    check(setting_capacity == 32, "1..31 characters");
    // flag31: the census order is measure(node) then, when the pass reaches the child loop, exit(node) pushes it.
    AncestorStack s;
    auto measure = [&](std::uint32_t parent, std::uint32_t flags) { return s.resolve(parent, flags); };
    auto exit_ = [&](std::uint32_t node, std::uint32_t parent, std::uint32_t flags) { s.push(node, s.resolve(parent, flags)); };
    check(measure(0, 0x80001002u) == 1, "Terran root: own bit");  exit_(0x100, 0, 0x80001002u);
    check(measure(0x100, 0x1002u) == 1, "child of a Terran root");  exit_(0x110, 0x100, 0x1002u);
    check(measure(0x110, 0x1002u) == 1, "grandchild");               exit_(0x111, 0x110, 0x1002u);
    check(measure(0x111, 0x0u) == 1, "great-grandchild");
    check(measure(0x100, 0x1002u) == 1 && s.depth == 1, "sibling after a finished subtree pops back to the root");
    check(measure(0, 0x1002u) == 0 && s.depth == 0, "next root without the bit restarts the stack"); exit_(0x200, 0, 0x1002u);
    check(measure(0x200, 0x80000000u) == 0, "a child's own bit 31 does not count, the root's does");
    check(measure(0x999, 0x80000000u) == flag31_unknown && s.depth == 1, "parent not on the stack: unknown, stack kept");
    s.clear();
    std::uint32_t parent = 0;
    for (unsigned level = 0; level < ancestor_cap + 2; ++level) { exit_(0x1000 + level, parent, level == 0 ? 0x80000000u : 0u); parent = 0x1000 + level; }
    check(s.depth == ancestor_cap && measure(0x1000 + ancestor_cap - 1, 0) == 1 && measure(0x1000 + ancestor_cap + 1, 0) == flag31_unknown, "depth cap: deeper nodes unknown");
    check(!std::strcmp(flag31_suffix(0), " flag31=0") && !std::strcmp(flag31_suffix(1), " flag31=1") && !std::strcmp(flag31_suffix(flag31_unknown), " flag31=-"), "suffix");
    // install()/restore() against a copied window in a buffer, the site at the engine's offset 4 of its 8-byte word.
    alignas(8) static unsigned char code[64];
    unsigned char original[64];
    std::memset(code, 0xcc, sizeof code); std::memcpy(code + 2, expected_window, window_length); std::memcpy(original, code, sizeof code);
    const std::uintptr_t window = reinterpret_cast<std::uintptr_t>(code + 2), site = window + site_offset;
    BufferOps ops{code, sizeof code};
    bool live = true, atomic = false; std::uint32_t protection = 0;
    auto is = [](const char* r, const char* want) { return r && !std::strcmp(r, want); };
    // A mismatched window: refused before any protection change or write.
    code[2 + 3] ^= 0x01;
    check(is(install(ops, window, true, &live, &atomic, &protection), "bytes_mismatch") && !live && ops.writes == 0 && ops.protects == 0 && code[2 + site_offset] == 0x74, "mismatched window refused, nothing written");
    code[2 + 3] ^= 0x01;
    check(is(install(ops, window, false, &live, &atomic, &protection), "late_claim") && !live && ops.writes == 0 && ops.protects == 0, "closed install window refused, nothing written");
    check(is(install(ops, 0, true, &live, &atomic, &protection), "invalid_site") && ops.writes == 0, "null window refused");
    check(!std::memcmp(code, original, sizeof code), "buffer untouched by the refusals");
    // The write: eb 05 read back, the atomic path, the protection put back, nothing else changed.
    check(is(install(ops, window, true, &live, &atomic, &protection), "ok") && live && atomic && ops.writes == 1, "install ok, one atomic write");
    check(code[2 + site_offset] == 0xeb && code[2 + site_offset + 1] == 0x05 && protection == BufferOps::initial && ops.protection == BufferOps::initial, "patched bytes read back as eb 05, protection restored");
    { unsigned char expect[64]; std::memcpy(expect, original, 64); std::memcpy(expect + 2 + site_offset, patched_site, site_length); check(!std::memcmp(code, expect, 64), "only the two site bytes changed"); }
    check(is(install(ops, window, true, &live, &atomic, &protection), "bytes_mismatch") && ops.writes == 1, "already patched window refused, no second write");
    // The restore: 74 05 back, the whole buffer as before, what it found reported; a second restore finds 74 05 and writes nothing.
    unsigned char found[site_length]{}; bool found_read = false;
    auto found_is = [&](unsigned char a, unsigned char b) { return found_read && found[0] == a && found[1] == b; };
    check(is(restore(ops, site, protection, found, &found_read), "restored") && found_is(0xeb, 0x05) && ops.writes == 2 && !std::memcmp(code, original, sizeof code) && ops.protection == BufferOps::initial, "restore returns 74 05 over eb 05");
    check(is(restore(ops, site, protection, found, &found_read), "restored") && found_is(0x74, 0x05) && ops.writes == 2, "restore of a site already holding 74 05: nothing written");
    // Failure paths: a store that does not land rolls back; a failed protect writes nothing; a store that sticks stays registered.
    ops.drop_writes = true;
    check(is(install(ops, window, true, &live, &atomic, &protection), "patch_rolled_back") && !live && !std::memcmp(code, original, sizeof code), "unverified write rolled back, judged by read-back");
    ops.drop_writes = false; ops.fail_protect = true; const unsigned before = ops.writes;
    check(is(install(ops, window, true, &live, &atomic, &protection), "protect_failed") && !live && ops.writes == before, "protect failure: nothing written");
    ops.fail_protect = false; ops.stuck_after = ops.writes + 1; ops.corrupt_first = true;
    check(is(install(ops, window, true, &live, &atomic, &protection), "rollback_failed") && live && code[2 + site_offset] == 0xeb && code[2 + site_offset + 1] == 0x06, "wrong bytes that cannot be undone stay registered (live)");
    check(is(restore(ops, site, protection, found, &found_read), "restore_failed") && found_is(0xeb, 0x06) && code[2 + site_offset + 1] == 0x06, "restore store that does not land: restore_failed, found reported");
    ops.stuck_after = 0;
    check(is(restore(ops, site, protection, found, &found_read), "restore_not_owned") && found_is(0xeb, 0x06) && !std::memcmp(code, original, sizeof code) && ops.protection == BufferOps::initial,
          "foreign bytes on a registered site: 74 05 written back anyway, reported as not owned");
    ops.fail_protect = true; std::memcpy(code + 2 + site_offset, patched_site, site_length);
    check(is(restore(ops, site, protection, found, &found_read), "restore_failed") && found_is(0xeb, 0x05) && code[2 + site_offset] == 0xeb, "restore protect failure: restore_failed, nothing written");
    ops.fail_protect = false;
    check(is(restore(ops, reinterpret_cast<std::uintptr_t>(code) + sizeof code, protection, found, &found_read), "restore_failed") && !found_read, "unreadable site: restore_failed, found unread");
    std::printf("terran_lod_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


def load_manage():
    spec = importlib.util.spec_from_file_location('terran_lod_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TerranLodCore(unittest.TestCase):
    def test_core_compiled_bytes_refusals_parser_and_flag31(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-terran-lod-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'terran_lod_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            lines = run.stdout.splitlines()
            self.assertEqual(lines[-1], 'terran_lod_core checks_failed=0')
            self.assertEqual(bytes.fromhex(lines[0]), verifier.WINDOW)
            self.assertEqual(bytes.fromhex(lines[0]), bytes.fromhex('f7 87 2c 01 00 00 00 00 00 80 74 05 c6 44 24 18 01'))
            self.assertEqual((bytes.fromhex(lines[1]), bytes.fromhex(lines[2])), (verifier.SITE, verifier.PATCHED))
            self.assertEqual([int(v, 16) for v in lines[3].split()], [verifier.WINDOW_VA, verifier.SITE_VA, verifier.TARGET_VA])

    def test_python_twin(self):
        self.assertEqual(verifier.source_constants((ROOT / 'src/proxy/terran_lod_sites.h').read_text()), verifier.EXPECTED_CONSTANTS)
        # jmp rel8 +5 from the end of the two bytes lands where je did.
        self.assertEqual(verifier.SITE_VA + 2 + verifier.PATCHED[1], verifier.TARGET_VA)
        self.assertEqual(verifier.WINDOW[verifier.SITE_VA - verifier.WINDOW_VA:][:2], verifier.SITE)

    def test_install_line_parser(self):
        row = verifier.parse_log_line
        ok = row('00:01 terran_station_lod site=0047d01c status=patched reason=ok mode=size setting=- write=atomic')
        self.assertEqual(ok, {'site': 0x47d01c, 'status': 'patched', 'reason': 'ok', 'mode': 'size', 'setting': '-', 'write': 'atomic', 'patched': True})
        off = row('terran_station_lod site=0047d01c status=off reason=distance mode=distance setting=distance write=none')
        self.assertEqual((off['patched'], off['status'], off['mode']), (False, 'off', 'distance'))
        refused = row('terran_station_lod site=0047d01c status=refused reason=bytes_mismatch mode=size setting=size write=none')
        self.assertEqual((refused['patched'], refused['reason']), (False, 'bytes_mismatch'))
        self.assertEqual(row('terran_station_lod site=0047d01c status=refused reason=too_long mode=- setting=? write=none')['mode'], '-')
        unverified = row('terran_station_lod site=0047d01c status=patched_unverified reason=rollback_failed mode=size setting=- write=atomic')
        self.assertEqual((unverified['patched'], unverified['status'], unverified['reason']), (False, 'patched_unverified', 'rollback_failed'))
        self.assertIsNone(row('terran_station_lod site=0047d01c status=patched reason=ok'))
        self.assertIsNone(row('terran_station_lod site=0047d01c status=maybe reason=ok mode=size setting=- write=atomic'))

    def test_restore_line_parser(self):
        row = verifier.parse_restore_line('terran_station_lod_restore site=0047d01c status=restored found=eb05 registered=0')
        self.assertEqual(row, {'site': 0x47d01c, 'status': 'restored', 'found': b'\xeb\x05', 'registered': False})
        failed = verifier.parse_restore_line('terran_station_lod_restore site=0047d01c status=restore_failed found=-- registered=1')
        self.assertEqual((failed['status'], failed['found'], failed['registered']), ('restore_failed', None, True))
        self.assertEqual(verifier.parse_restore_line('terran_station_lod_restore site=0047d01c status=restore_not_owned found=eb06 registered=0')['found'], b'\xeb\x06')
        self.assertIsNone(verifier.parse_restore_line('terran_station_lod_restore site=0047d01c status=restored'))

    def test_census_row_flag31(self):
        prefix = ('cull_census device=1 frame=9 view=34766bf8 node=34763100 model=000050eb s=40 measure=40 d=100000 radius=100 '
                  'thr_1dc=0 thr_1d8=4 limit=4 flags_in=00001002 flags_out=00001002 lod=1 verdict=kept lods=3 thr=30,15,0 body=stations\\usc_small_station_d')
        self.assertEqual(census.parse_row(prefix + ' flag31=1')['flag31'], 1)
        self.assertEqual(census.parse_row(prefix + ' flag31=0')['flag31'], 0)
        self.assertIsNone(census.parse_row(prefix + ' flag31=-')['flag31'])
        older = census.parse_row(prefix)
        self.assertNotIn('flag31', older)
        self.assertEqual(older['body'], 'stations\\usc_small_station_d')

    def test_production_wiring(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertEqual(capture.count('terran_station_lod::initialize();'), 1)
        self.assertLess(capture.index('game_phases::initialize();'), capture.index('terran_station_lod::initialize();'))
        self.assertLess(capture.index('lod_scale::initialize();'), capture.index('terran_station_lod::initialize();'))
        self.assertIn('if (reserved == nullptr) x3m::terran_station_lod::shutdown();', (ROOT / 'src/proxy/loader.cpp').read_text())
        self.assertIn('src/proxy/terran_station_lod.cpp', (ROOT / 'CMakeLists.txt').read_text())
        module = (ROOT / 'src/proxy/terran_station_lod.cpp').read_text()
        for needle in ('L"X3M_TERRAN_STATION_LOD"', 'setting_capacity', '"too_long"', '"invalid_setting"', 'install_window_open()', 'executable_verified()',
                       'sites::install(ops, window_address, engine_patch::install_window_open()', 'sites::restore(ops, site_, site_protection, found, &found_read)',
                       'engine_patch::write_code', 'FlushInstructionCache', 'VirtualProtect', 'PAGE_EXECUTE_READWRITE',
                       '"patched_unverified"', 'if (std::strcmp(reason, "restore_failed")) patched_ = false;', 'log_handle()', 'WriteFile(handle',
                       '"terran_station_lod_restore site=%08lx status=%s found=%s registered=%u\\n"', 'SetLastError(error);',
                       'log("terran_station_lod site=%08lx status=%s reason=%s mode=%s setting=%s write=%s"'):
            self.assertIn(needle, module)
        header = (ROOT / 'src/proxy/terran_lod_sites.h').read_text()
        for needle in ('plan(current)', 'memcmp(back, expected_site, site_length)', '"patch_rolled_back"', '"rollback_unprotected"', '"rollback_failed"',
                       '"restore_not_owned"', '"restore_failed"', '*live = true;'):
            self.assertIn(needle, header)
        self.assertNotIn('windows.h', header)
        for forbidden in ('float ', 'double ', 'push_front', 'Emitter'):
            self.assertNotIn(forbidden, module)
        census_source = (ROOT / 'src/proxy/cull_census.cpp').read_text()
        self.assertIn('flag31_suffix(e.flag31)', census_source)
        self.assertIn('lod=%ld verdict=%s%s%s%s%s"', census_source)


@unittest.skipUnless(EXE.is_file(), 'installed executable not present')
class TerranLodSite(unittest.TestCase):
    def run_verifier(self, exe):
        return subprocess.run([sys.executable, str(ROOT / 'verification/probe/verify_terran_lod_site.py'), '--exe', str(exe)],
                              capture_output=True, text=True, timeout=300, cwd=ROOT / 'verification/probe')

    def test_installed_executable(self):
        run = self.run_verifier(EXE)
        self.assertEqual(run.returncode, 0, run.stdout[-3000:] + run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(report['result'], 'PASS')
        self.assertEqual(report['site_bytes'], '7405')
        self.assertEqual(report['target_sources'], ['0x47d00b', '0x47d010', '0x47d01c'])
        self.assertEqual((report['raw_branch_hits'], report['dword_refs'], report['overlapping_claims']), ([], 0, []))

    def test_patched_copy_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            copy = Path(directory) / 'X3AP.exe'
            copy.write_bytes(verifier.patched_image(EXE.read_bytes()))
            report = json.loads(self.run_verifier(copy).stdout)
            self.assertEqual(report['result'], 'FAIL')
            self.assertTrue(report['checks']['exe_identity'])
            self.assertFalse(report['checks']['window_bytes'])
            self.assertEqual(report['site_bytes'], 'eb05')


class TerranLodLaunchOption(unittest.TestCase):
    NAME = 'X3M_TERRAN_STATION_LOD'

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

    def test_default_explicit_modes_and_vanilla(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.value(directory), 'size')
            self.assertEqual(self.value(directory, inherited={self.NAME: 'distance'}), 'size')   # a stale value never travels
            self.assertEqual(self.value(directory, '--terran-station-lod', 'size'), 'size')
            self.assertEqual(self.value(directory, '--terran-station-lod', 'distance', inherited={self.NAME: 'size'}), 'distance')
            self.assertIsNone(self.value(directory, vanilla=True, inherited={self.NAME: 'size'}))

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, vanilla, message in ((('--terran-station-lod', 'size'), True, 'cannot be combined with --vanilla'),
                                           (('--terran-station-lod', 'distance'), True, 'cannot be combined with --vanilla'),
                                           (('--terran-station-lod', 'Size'), False, 'invalid choice'),
                                           (('--terran-station-lod', 'off'), False, 'invalid choice')):
                with self.subTest(args=args, vanilla=vanilla):
                    code, _, error = self.launch(directory, *args, vanilla=vanilla)
                    self.assertNotEqual(code, 0)
                    self.assertIn(message, error)

    def test_launch_command_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            size = json.loads(self.launch(directory)[1])
            distance = json.loads(self.launch(directory, '--terran-station-lod', 'distance')[1])
            self.assertEqual(size['command'], distance['command'])
            self.assertEqual({k: v for k, v in size['env'].items() if distance['env'].get(k) != v}, {self.NAME: 'size'})


if __name__ == '__main__':
    unittest.main()
