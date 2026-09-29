"""Engine-side lens-flare cull (X3M_LENS_FLARE_GAIN=0): the core header, its Python twin, the site verifier, the wiring.

src/proxy/lens_flare_cull_core.h is compiled on the host (no Windows dependency): the stub encoder must agree with the
verifier's twin, the body-name set must resolve through a synthetic body table by id and by scan (case folded, a
dynamic literal name, an absent name), the bitmap must refuse ids outside its span. The verifier must pass a synthetic
image carrying the small-parts window, the lens block, the walker's call, the pass's model read and the two callers of
the pass, and refuse each of them changed. capture.cpp, loader.cpp, CMakeLists.txt and the fixture build must carry
the module, the shared claim must be exposed by cull_small_parts.h, and the docs must name the new meaning of gain 0.
The installed executable, when present, must pass verify_lens_flare_cull_site.py.
"""
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_lens_flare_cull_site as verifier  # noqa: E402
import verify_cull_small_parts_site as small  # noqa: E402
from source_text import source_text  # noqa: E402
from verification.analysis.test_cull_small_parts import image as small_image, inspect_image as small_inspect  # noqa: E402

HARNESS = r'''
#include "lens_flare_cull_core.h"
#include <cstdio>
#include <cstring>
using namespace x3m::lens_flare_cull::core;
static unsigned failed = 0;
static void expect(bool ok, const char* what) { if (!ok) { ++failed; std::printf("FAIL %s\n", what); } }
// A synthetic 32-bit image: the manager at 0x1000, the slot array at 0x2000 (11004 slots, 0x4b490 bytes), names at 0x50000.
static unsigned char memory[0x60000];
static bool reader(std::uintptr_t address, void* out, std::size_t size) {
    if (address < 0x800 || address + size > sizeof memory) return false;
    std::memcpy(out, memory + address, size);
    return true;
}
static void put32(std::uint32_t at, std::uint32_t v) { std::memcpy(memory + at, &v, 4); }
static std::uint32_t name_at = 0x50000;
static std::uint32_t name(const char* text) { const std::uint32_t at = name_at; std::strcpy(reinterpret_cast<char*>(memory + at), text); name_at += 32; return at; }
int main() {
    unsigned char out[stub_length];
    encode_stub(0x10000000u, 0x10002000u, 0x10003000u, 0x10002004u, 0x0047d2c3u, 0x10000054u, out);
    for (unsigned i = 0; i < stub_length; ++i) std::printf("%02x", out[i]);
    std::printf("\n");
    std::int32_t id = 0;
    expect(default_name_id("v\\00752", &id) && id == 752 && default_name_id("v\\11011", &id) && id == 11011, "default names parse");
    expect(!default_name_id("v\\0752", &id) && !default_name_id("ships\\x", &id) && !default_name_id("V\\00752", &id), "other forms refused");
    expect(name_equal("v\\01006", "V\\01006") && !name_equal("v\\01006", "v/01006"), "case fold, separators distinct");
    expect(slot_id(752, 11000) == 752 && slot_id(2000, 11000) == 11000 && slot_id(11002, 11000) == 20002, "slot -> id");
    Bitmap map; map.clear();
    expect(map.set(752) && map.test(752) && !map.test(753) && !map.set(0x8000) && !map.set(-1) && map.set(0x7fff), "bitmap span");
    const std::uint32_t fixed = 11000, dynamic = 4, slots = 0x2000;
    put32(0x1000 + 0xb4, fixed); put32(0x1000 + 0xb8, dynamic); put32(0x1000 + 0xbc, slots);
    put32(0x0ffc, 0x1000); // the global at 0xffc -> manager
    put32(slots + 753 * 0x1c + 0x0c, name("v\\00753"));
    put32(slots + 754 * 0x1c + 0x0c, name("effects\\ray"));
    put32(slots + (fixed + 0) * 0x1c + 0x0c, name("v\\01006"));
    put32(slots + (fixed + 1) * 0x1c + 0x0c, name("V\\00754"));
    put32(slots + (fixed + 2) * 0x1c + 0x0c, name("v\\01019abcdefghijk"));
    const Table t = read_table(&reader, 0x0ffc);
    expect(t.valid && t.fixed == 11000 && t.dynamic == 4 && t.slots == slots, "table header");
    map.clear();
    bool found[body_name_count] = {};
    const Resolution r = resolve(&reader, t, &map, found, 0, fixed + dynamic);
    expect(r.resolved == 39 && r.mapped == 39 && r.scanned == 3, "37 by id, 2 by scan, 3 absent");
    Mappings m; map.clear(); std::memset(found, 0, sizeof found);
    expect(resolve(&reader, t, &map, found, 0, fixed + dynamic, &m).resolved == 39 && m.count == 2 && m.entries[0].slot == fixed && m.entries[1].slot == fixed + 1, "the two dynamic mappings recorded");
    expect(mappings_hold(&reader, t, m), "mappings hold");
    put32(slots + (fixed + 0) * 0x1c + 0x0c, name("ships\\y"));
    expect(!mappings_hold(&reader, t, m), "a re-bound dynamic slot no longer holds");
    expect(map.test(752) && map.test(753) && !map.test(754) && map.test(20000) && map.test(20001) && !map.test(20002) && map.test(11000) && !map.test(1006), "bitmap contents");
    expect(resolve(&reader, t, &map, found, 0, fixed + dynamic).resolved == 0, "nothing new on a second pass");
    expect(!read_table(&reader, 0x0ff8).valid && !read_table(&reader, 0x5ffff).valid, "no manager or unreadable: invalid");
    std::printf("lens_flare_cull_core checks_failed=%u\n", failed);
    return failed ? 1 : 0;
}
'''


def image(*changes):
    """The small-parts synthetic image plus the lens block, the walker's call, the model read and the lens draw call."""
    recursion = b'\xe8' + struct.pack('<i', verifier.PASS_VA - (verifier.PASS_RECURSION_VA + 5))
    extra = [(verifier.LENS_WALK_VA, verifier.LENS_WALK), (verifier.WALKER_CALL_VA, verifier.WALKER_CALL),
             (verifier.MODEL_READ_VA, verifier.MODEL_READ), (verifier.LENS_DRAW_VA - 1, verifier.LENS_DRAW),
             (verifier.PASS_RECURSION_VA, recursion), *changes]
    return small_image(*extra)


def inspect_image(data):
    return verifier.inspect(data, small_inspect(data), source_text(verifier.CORE))


class LensFlareCullCore(unittest.TestCase):
    def test_core_compiled_and_twin(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-lens-flare-cull-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'lens_flare_cull_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            lines = run.stdout.splitlines()
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(lines[-1], 'lens_flare_cull_core checks_failed=0')
            twin = verifier.encode_stub(0x10000000, 0x10002000, 0x10003000, 0x10002004, 0x0047d2c3, 0x10000054)
            self.assertEqual(bytes.fromhex(lines[0]), twin)
            self.assertEqual(twin[verifier.STUB_REPLAY:verifier.STUB_REPLAY + 25], small.WINDOW[14:39])  # 0x0047d2a2..0x0047d2b9 replayed
            self.assertEqual(twin[verifier.STUB_CULL + 6], 0xe9)
            self.assertEqual(struct.unpack('<i', twin[verifier.STUB_CULL + 7:verifier.STUB_CULL + 11])[0], 0x0047d2c3 - (0x10000000 + verifier.STUB_CONTINUE))

    def test_source_constants_and_names(self):
        text = source_text(verifier.CORE)
        self.assertEqual(verifier.source_constants(text), verifier.EXPECTED_CONSTANTS)
        names = verifier.body_names(text)
        self.assertEqual(len(names), 42)
        self.assertEqual(len(set(names)), 42)
        for stock in (61, 548, 549, 550, 719, 720, 721, 722, 735, 739, 740, 741, 744, 745, 752, 753, 754, 760, 761, 762, 763, 764, 765, 766, 778, 781, *range(11000, 11012)):
            self.assertIn('v\\%05d' % stock, names)
        for observed in (1006, 1011, 1016, 1019):  # run385: dynamic literal names on Mayhem 3
            self.assertIn('v\\%05d' % observed, names)

    def test_parsers(self):
        row = verifier.parse_status_line('00:01 lens_flare_cull status=patched reason=ok bodies=40 mapped=40 site=0x0047d2a2 cull=0x0047d2c3 stub=0x00a300d0 write=atomic')
        self.assertEqual((row['status'], row['reason'], row['bodies'], row['site'], row['cull'], row['write']), ('patched', 'ok', 40, 0x47d2a2, 0x47d2c3, 'atomic'))
        self.assertIsNone(verifier.parse_status_line('lens_flare_cull status=maybe reason=ok bodies=0 mapped=0 site=0x0 cull=0x0 stub=0x0 write=none'))
        window = verifier.parse_window_line('lens_flare_cull culled=1234 total=5678 enabled=1 bodies=40 mapped=40 frame=300')
        self.assertEqual((window['culled'], window['total'], window['enabled'], window['frame']), (1234, 5678, 1, 300))
        bodies = verifier.parse_bodies_line('lens_flare_cull_bodies bodies=41 mapped=41 scanned=6 fixed=11000 dynamic=7 enabled=1 restarts=2 frame=12')
        self.assertEqual((bodies['bodies'], bodies['scanned'], bodies['dynamic'], bodies['enabled'], bodies['restarts']), (41, 6, 7, 1, 2))


class LensFlareCullSite(unittest.TestCase):
    def test_synthetic_image_passes_all_but_identity(self):
        report = inspect_image(image())
        checks = {k: v for k, v in report['checks'].items() if k != 'exe_identity'}
        self.assertTrue(all(checks.values()), report)
        self.assertFalse(report['checks']['exe_identity'])
        self.assertEqual(report['pass_callers'], ['0x47d53c', '0x47e7a5'])
        self.assertEqual(len(report['checks']), 13)

    def test_changed_bytes_refused(self):
        cases = {
            'lens_walk_bytes': (verifier.LENS_WALK_VA + 7, b'\x68'),                                   # mov ecx,[eax+0x68]: not the lens scene
            'walker_call_bytes': (verifier.WALKER_CALL_VA + 1, b'\x01'),                               # push 1
            'model_read_bytes': (verifier.MODEL_READ_VA + 2, b'\x44'),                                 # mov eax,[edi+0x144]
            'pass_callers': (0x47e000, b'\xe8' + struct.pack('<i', verifier.PASS_VA - (0x47e000 + 5))),  # a third caller
            'lens_draw_follows_walk': (verifier.LENS_DRAW_VA, b'\xe9'),                                # jmp, not call
            'small_parts_site': (small.CULL_VA + 6, b'\xfb'),                                          # the shared window changed
        }
        for failed, change in cases.items():
            with self.subTest(check=failed):
                report = inspect_image(image(change))
                self.assertFalse(report['checks'][failed], report)

    def test_installed_executable(self):
        report = verifier.verify()
        self.assertEqual(report['result'], 'PASS', report)


class LensFlareCullWiring(unittest.TestCase):
    def test_production_wiring(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertIn('lens_flare_cull::initialize(lens_flare_gain_zero);', capture)
        self.assertLess(capture.index('cull_small_parts::initialize();'), capture.index('lens_flare_cull::initialize('))
        self.assertIn('lens_flare_gain_zero = v == 0.f;', capture)
        self.assertIn('lens_flare_cull::begin_frame(ctx.frame);', capture)
        self.assertIn('if ((log_tier::cached_perf || log_tier::cached_debug) && ctx.frame % 300 == 0) lens_flare_cull::report(ctx.frame);', capture)
        loader = source_text(ROOT / 'src/proxy/loader.cpp')
        self.assertLess(loader.index('x3m::lens_flare_cull::shutdown();'), loader.index('x3m::cull_small_parts::shutdown();'))
        self.assertIn('src/proxy/lens_flare_cull.cpp', source_text(ROOT / 'CMakeLists.txt'))
        self.assertIn("('src/proxy/lens_flare_cull.cpp', 'lens_flare_cull', [FIXTURE_DEFINE])", source_text(ROOT / 'verification/probe/build_cull_small_parts.py'))
        header = source_text(ROOT / 'src/proxy/cull_small_parts.h')
        self.assertIn('bool chain_stub(std::uintptr_t site, std::uintptr_t cull_target, void* stub, void** next_slot, const char** status);', header)
        module = source_text(ROOT / 'src/proxy/lens_flare_cull.cpp')
        self.assertIn('cull_small_parts::chain_stub(site, cull_target, reinterpret_cast<void*>(stub), slot, &reason)', module)
        self.assertNotIn('X3M_LENS_FLARE_CULL', module)  # no key of its own: gain 0 alone
        self.assertNotIn('x3m::config::get', module)

    def test_docs_name_the_gain_zero_meaning(self):
        self.assertIn('lens_flare_cull', (ROOT / 'docs/user/config.md').read_text())
        self.assertIn('lens_flare_cull', (ROOT / 'docs/verification/launcher-options-inventory.md').read_text())
        tiers = (ROOT / 'docs/architecture/logging-tiers.md').read_text()
        row = next(line for line in tiers.splitlines() if '`lens_flare_cull culled=`' in line)
        self.assertEqual(row.split(' | ')[3], 'perf')
        self.assertIn('Lens-flare cull on the small-parts site', (ROOT / 'docs/reverse-engineering/lod-selection.md').read_text())


if __name__ == '__main__':
    unittest.main()
