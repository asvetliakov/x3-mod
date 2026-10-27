"""Host checks of the oversized-disc fix (src/proxy/circle_fill_sites.h, src/proxy/circle_fill.cpp).

The core header compiled on the host: the 64-bit port of the filled-ellipse rasteriser 0x004ef620 against the
unbounded-integer emulation in verification/results/gui-scale/sector_map_scanner_range.py span for span (centred
radii 100..2400 and 32767, discs clipped at the texture edges, two other aspects), identical to the 32-bit emulation up
to r = 825 and different from it from 826; the B quotient and its refusals; the class -> leaf map against the
verifier's table. The site verifier on the installed executable and its refusal on a copy with a site byte changed; the
log-row parsers; the production wiring. No Wine, no game launch.
"""
import ast
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_circle_fill_site as verifier  # noqa: E402
from source_text import source_text  # noqa: E402

EXE = Path(os.environ.get('X3M_EXE', verifier.DEFAULT_EXE))  # X3M_EXE: the native install from WSL2 (/mnt/c/X3/X3AP.exe)
EMULATION = ROOT / 'verification/results/gui-scale/sector_map_scanner_range.py'

HARNESS = r'''
#include "circle_fill_sites.h"
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>
using namespace x3m::circle_fill::sites;
int main() {
    long cx, cy, r, w, h, aspect;
    while (std::scanf("%ld %ld %ld %ld %ld %ld", &cx, &cy, &r, &w, &h, &aspect) == 6) {
        const std::int64_t rr = s16(r);
        std::int64_t B = 0;
        if (!semi_axis(rr * rr, int(aspect), &B)) { std::printf("decline\nend\n"); continue; }
        std::map<int, std::pair<int, int>> spans;
        unsigned long calls = 0;
        fill(int(cx), int(cy), int(r), B, int(w), int(h), [&](int x0, int y, int x1) {
            ++calls;
            auto it = spans.find(y);
            if (it == spans.end()) spans[y] = {x0, x1};
            else { if (x0 < it->second.first) it->second.first = x0; if (x1 > it->second.second) it->second.second = x1; }
        });
        std::printf("B %" PRId64 " calls %lu\n", B, calls);
        for (const auto& s : spans) std::printf("%d %d %d\n", s.first, s.second.first, s.second.second);
        std::printf("end\n");
    }
    std::printf("modes");
    const char* texts[] = {"on", "off", "On", "OFF", "", "onn", "of", "on "};
    for (const char* text : texts) { Mode m = Mode::off; const bool ok = parse_mode(text, &m); std::printf(" %d%d", ok ? 1 : 0, int(m)); }
    Mode wide = Mode::off;
    std::printf(" %d%d %d %s\n", parse_mode(L"on", &wide) ? 1 : 0, int(wide), parse_mode(static_cast<const char*>(nullptr), &wide) ? 1 : 0,
                mode_name(default_mode));
    std::printf("classes");
    for (int c = -2; c <= 30; ++c) std::printf(" %d:%lx", c, (unsigned long)leaf_for_class(c));
    std::printf("\n");
    return 0;
}
'''


def emulation():
    """circlefill() of the evidence script, without running the script (it reads the game files at import)."""
    tree = ast.parse(EMULATION.read_text())
    keep = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in ('i32', 's16', 'u16', 'circlefill')]
    namespace = {}
    exec(compile(ast.Module(body=keep, type_ignores=[]), str(EMULATION), 'exec'), namespace)  # noqa: S102 (repository source)
    return namespace['circlefill']


def clipped(spans, width, height):
    """The emulation's spans through the engine's per-span clip (the port also clips when B > A, aspect < 65536)."""
    out = {}
    for y, (x0, x1) in spans.items():
        x0, x1 = max(x0, 0), min(x1, width - 1)
        if 0 <= y < height and x0 <= x1:
            out[y] = (x0, x1)
    return out


def expected_spans(circlefill, cx, cy, r, width, height, aspect):
    """What the port must draw: the unbounded emulation, clipped when B > A."""
    spans = circlefill(cx, cy, r, width, height, wrap=False, asp=aspect)
    q = ((aspect * aspect + 0x8000) >> 16) & 0xffffffff
    return clipped(spans, width, height) if q and (r * r << 16) // q > r * r else spans


C = 3000  # centre of the unclipped cases (texture 2C x 2C)


class CircleFillCore(unittest.TestCase):
    C = C
    CENTRED = [(C, C, r, 2 * C, 2 * C, 65536) for r in (100, 813, 825, 826, 1291, 1633, 2400)]
    CLIPPED = [(200, 1500, 1633, 1664, 1664, 65536),    # the 2x map window texture, centre near the left edge
               (1600, 40, 2400, 1664, 1664, 65536),     # near the top-right corner
               (-300, 800, 1000, 1664, 1664, 65536),    # centre outside the texture
               (832, 1700, 1290, 1664, 1664, 65536)]    # centre below the bottom edge
    EXTREME = [(0, 0, 32767, 32000, 32000, 65536)]  # the largest int16 radius: a quarter disc inside the 16-bit coordinates
    ASPECTS = [(C, C, 1633, 2 * C, 2 * C, 131072), (C, C, 1633, 2 * C, 2 * C, 32768), (C, C, 1633, 2 * C, 2 * C, 50000),
               (840, 832, 820, 1664, 1664, 40000)]  # B > A with the disc near the right edge: inside cx +- r, wider in x
    DECLINES = [(C, C, 1633, 2 * C, 2 * C, 0), (C, C, 1633, 2 * C, 2 * C, 181), (C, C, 1633, 2 * C, 2 * C, 2048),
                (C, C, 32767, 2 * C, 2 * C, 32768)]

    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise unittest.SkipTest('no host C++ compiler')
        cls.cases = cls.CENTRED + cls.CLIPPED + cls.EXTREME + cls.ASPECTS + cls.DECLINES
        with tempfile.TemporaryDirectory(prefix='x3-circle-fill-') as directory:
            source, executable = Path(directory) / 'harness.cpp', Path(directory) / 'harness'
            source.write_text(HARNESS)
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(source), '-o', str(executable)], capture_output=True, text=True, timeout=180)
            if build.returncode:
                raise AssertionError(build.stderr)
            run = subprocess.run([str(executable)], input=''.join(' '.join(map(str, c)) + '\n' for c in cls.cases),
                                 capture_output=True, text=True, timeout=120)
        lines = run.stdout.splitlines()
        cls.results, current = [], None
        for line in lines:
            if line.startswith('modes'):
                cls.modes = line.split()[1:]
            elif line.startswith('classes'):
                cls.classes = {int(c): int(v, 16) for c, v in (item.split(':') for item in line.split()[1:])}
            elif line == 'decline':
                current = None
            elif line.startswith('B '):
                parts = line.split()
                current = {'B': int(parts[1]), 'calls': int(parts[3]), 'spans': {}}
            elif line == 'end':
                cls.results.append(current)
            else:
                y, x0, x1 = map(int, line.split())
                current['spans'][y] = (x0, x1)
        cls.circlefill = staticmethod(emulation())

    def result(self, case):
        return self.results[self.cases.index(case)]

    def test_port_matches_unbounded_emulation(self):
        for case in self.CENTRED + self.CLIPPED + self.EXTREME + self.ASPECTS:
            cx, cy, r, w, h, aspect = case
            with self.subTest(case=case):
                port = self.result(case)
                self.assertIsNotNone(port)
                expected = expected_spans(self.circlefill, cx, cy, r, w, h, aspect)
                self.assertTrue(expected)
                self.assertEqual(port['spans'], expected)
                self.assertLessEqual(port['calls'], 2 * len(expected) + 2 * r)  # at most two leaf calls per row step

    def test_identical_to_the_engine_up_to_825_and_not_from_826(self):
        for r in (100, 813, 825):
            case = (self.C, self.C, r, 2 * self.C, 2 * self.C, 65536)
            self.assertEqual(self.result(case)['spans'], self.circlefill(*case[:5], wrap=True), r)
        for r in (826, 1291, 1633, 2400):
            case = (self.C, self.C, r, 2 * self.C, 2 * self.C, 65536)
            self.assertNotEqual(self.result(case)['spans'], self.circlefill(*case[:5], wrap=True), r)
        clipped = self.CLIPPED[0]
        self.assertNotEqual(self.result(clipped)['spans'], self.circlefill(*clipped[:5], wrap=True))

    def test_round_disc_extent(self):
        port = self.result((self.C, self.C, 1633, 2 * self.C, 2 * self.C, 65536))
        self.assertEqual(port['B'], 1633 * 1633)
        self.assertEqual(min(port['spans']), self.C - 1633)
        self.assertEqual(max(port['spans']), self.C + 1633)
        x0, x1 = port['spans'][self.C]
        self.assertTrue(self.C - x0 == x1 - self.C and abs(x1 - self.C - 1633) <= 1, (x0, x1))
        clipped = self.result(self.CLIPPED[0])['spans']
        self.assertTrue(all(0 <= x0 <= x1 <= 1663 and 0 <= y < 1664 for y, (x0, x1) in clipped.items()))
        self.assertEqual(len(clipped), 1664)

    def test_wide_aspect_never_runs_past_the_row(self):
        case = self.ASPECTS[3]
        cx, cy, r, w, h, aspect = case
        port = self.result(case)
        self.assertGreater(port['B'], r * r)
        self.assertTrue(cx + r < w and cx - r >= 0 and cy + r < h and cy - r >= 0)  # the engine's own test would not clip
        self.assertEqual(max(x1 for _, x1 in port['spans'].values()), w - 1)
        self.assertTrue(all(0 <= x0 <= x1 <= w - 1 for x0, x1 in port['spans'].values()))
        unclipped = self.circlefill(cx, cy, r, w, h, wrap=False, asp=aspect)
        self.assertGreater(max(x1 for _, x1 in unclipped.values()), w - 1)  # what the stock clip rule would have drawn

    def test_quotient_and_declines(self):
        self.assertEqual(self.result(self.ASPECTS[0])['B'], (1633 * 1633) // 4)
        self.assertEqual(self.result(self.ASPECTS[1])['B'], 1633 * 1633 * 4)
        for case in self.DECLINES:
            self.assertIsNone(self.result(case), case)

    def test_setting_parser(self):
        # on, off accepted; case, empty, prefixes and trailing blanks refused; the wide form; null refused; default on
        self.assertEqual(self.modes, ['11', '10', '00', '00', '00', '00', '00', '00', '11', '0', 'on'])

    def test_class_map(self):
        self.assertEqual({c: v for c, v in self.classes.items() if v}, verifier.LEAVES)


class CircleFillSite(unittest.TestCase):
    def test_installed_executable(self):
        if not EXE.is_file():
            self.skipTest(f'{EXE} not present')
        report = verifier.verify(EXE)
        self.assertEqual(report['result'], 'PASS', [k for k, v in report['checks'].items() if not v] or report.get('error'))
        self.assertEqual(report['callers'], ['0x48c076'])
        self.assertEqual(report['dword_refs'], 0)
        self.assertEqual(report['overlapping_claims'], [])

    def test_changed_site_fails(self):
        if not EXE.is_file():
            self.skipTest(f'{EXE} not present')
        data = bytearray(EXE.read_bytes())
        image = verifier.common.Image(bytes(data))
        offset = next(raw_pointer + verifier.SITE_VA + 3 - (image.image_base + virtual_address)
                      for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in verifier.exe_identity.section_table(bytes(data))
                      if image.image_base + virtual_address <= verifier.SITE_VA < image.image_base + virtual_address + raw_size)
        data[offset] ^= 0x01  # mov eax,[esp+0x58] -> a different instruction
        report = verifier.verify(bytes(data))
        self.assertEqual(report['result'], 'FAIL')
        self.assertFalse(report['checks'].get('window_bytes', False))
        self.assertFalse(report['checks'].get('regions_hash', False))

    def test_log_parsers(self):
        row = verifier.parse_log_line('[x3m] circle_fill site=004ef620 status=patched reason=ok mode=on setting=on write=atomic threshold=813 '
                                      'continue=0a3c0010')
        self.assertEqual((row['site'], row['patched'], row['threshold'], row['continue'], row['mode']), (0x4ef620, True, 813, 0x0a3c0010, 'on'))
        row = verifier.parse_log_line('circle_fill site=004ef620 status=refused reason=helper_mismatch mode=on setting=- write=none threshold=813 '
                                      'continue=00000000')
        self.assertEqual((row['patched'], row['reason'], row['setting']), (False, 'helper_mismatch', '-'))
        row = verifier.parse_log_line('circle_fill site=004ef620 status=off reason=off mode=off setting=off write=none threshold=813 continue=00000000')
        self.assertEqual((row['status'], row['mode']), ('off', 'off'))
        row = verifier.parse_log_line('circle_fill site=004ef620 status=refused reason=invalid_setting mode=- setting=yes write=none threshold=813 '
                                      'continue=00000000')
        self.assertEqual((row['reason'], row['mode']), ('invalid_setting', '-'))
        wide = verifier.parse_wide_line('circle_fill_wide r=1633 cx=700 cy=640 aspect=65536 surface=1664x1664 class=24 leaf=004b3620')
        self.assertEqual((wide['r'], wide['w'], wide['cls'], wide['leaf']), (1633, 1664, 24, 0x4b3620))
        restore = verifier.parse_restore_line('circle_fill_restore site=004ef620 status=restored registered=0')
        self.assertEqual((restore['status'], restore['registered']), ('restored', False))
        self.assertIsNone(verifier.parse_log_line('circle_fill site=004ef620 status=maybe'))

    def test_production_wiring(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        loader = source_text(ROOT / 'src/proxy/loader.cpp')
        module = source_text(ROOT / 'src/proxy/circle_fill.cpp')
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn('circle_fill::initialize();', capture)
        self.assertLess(capture.index('dust_leak_fix::initialize();'), capture.index('circle_fill::initialize();'))
        self.assertIn('x3m::circle_fill::shutdown();', loader)
        self.assertIn('src/proxy/circle_fill.cpp ', cmake)
        self.assertIn('set_source_files_properties(src/proxy/circle_fill.cpp\n  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")',
                      cmake)
        self.assertIn("'_x3m_circle_fill_wide'", (ROOT / 'verification/probe/check_no_x87.py').read_text())
        # The thunk's fast path reads the radius as 0x004ef62a does and compares it with the header's threshold.
        self.assertIn('movsx eax, word ptr [esp+12]\n    cmp eax, 813\n    jg 1f\n    jmp dword ptr [_x3m_circle_fill_continue]', module)
        self.assertIn('if (r <= sites::stock_radius_max) return 0;', module)
        self.assertIn('install_window_open()', module)
        self.assertIn('x3m::config::get(L"X3M_CIRCLE_FILL_FIX", text, sites::setting_capacity)', module)
        self.assertIn('} else if (mode == sites::Mode::off)\n        reason = "off";', module)
        self.assertIn('executable_verified()', module)


if __name__ == '__main__':
    unittest.main()
