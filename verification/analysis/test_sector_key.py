"""Sector key reader (src/proxy/sector_key_core.h, the edition part src/proxy/sector_key.cpp;
docs/reverse-engineering/sector-identity.md section 3; docs/architecture/sector-background-modern.md, "Implementation A,
per-sector seed"): verification/probe/sector_key_check.cpp compiled natively against a fabricated memory image, both
editions' bindings, every refusal of the chain, the 64-link cap, the cache and a stale S, the window comparator at the
preferred and a relocated base, the hold rule. The core's window tables and bindings must equal the verifier's
(verification/results/sector-identity/verify_sector_identity.py, its summary.json relocation offsets). The edition part
and the route's wiring are checked in the sources. No D3D, no game bytes.
"""
import ast
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / 'verification/probe/sector_key_check.cpp'
VERIFIER = ROOT / 'verification/results/sector-identity/verify_sector_identity.py'
SUMMARY = ROOT / 'verification/results/sector-identity/summary.json'


def run_check():
    compiler = shutil.which('g++') or shutil.which('clang++') or shutil.which('c++')
    if not compiler:
        raise RuntimeError('host C++ compiler required')
    with tempfile.TemporaryDirectory(prefix='x3-sector-key-') as folder:
        exe = Path(folder) / 'check'
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', str(DRIVER), '-o', str(exe)],
                       check=True, capture_output=True, text=True)
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
    return run.returncode, json.loads(run.stdout)


def verifier_windows():
    text = VERIFIER.read_text()
    start = text.index('WINDOWS = {') + len('WINDOWS = ')
    return ast.literal_eval(text[start:text.index('\n}\n', start) + 2])


def function(source, head):
    start = source.index(head)
    return source[start:source.index('\n}\n', start)]


class SectorKeyCoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rc, cls.report = run_check()

    def test_all_checks_pass(self):
        r = self.report
        self.assertEqual((self.rc, r['ok'], r['failed']), (0, True, ''), r)
        self.assertGreaterEqual(r['checks'], 50)
        self.assertEqual((r['refusal_cases'], r['refusal_passed']), (30, 30))
        self.assertEqual(r['links_at_cap'], 64)

    def test_windows_equal_the_verifier(self):
        pinned = verifier_windows()
        summary = json.loads(SUMMARY.read_text())
        for edition in ('ap', 'fl'):
            core = [(n, va, hx) for n, va, hx, _ in self.report['windows'][edition]]
            self.assertEqual(core, [(n, va, hx) for n, va, hx in pinned[edition]], edition)
            relocs = {n: r for n, _, _, r in self.report['windows'][edition]}
            self.assertEqual(relocs, {w['name']: w['highlow_reloc_offsets'] for w in summary[edition]['windows']},
                             edition)
            self.assertTrue(all(w['match'] for w in summary[edition]['windows']), edition)
        self.assertEqual(self.report['fl_fixups'], 6)
        self.assertEqual(summary['result'], 'PASS')

    def test_bindings(self):
        summary = json.loads(SUMMARY.read_text())
        b = self.report['bindings']
        self.assertEqual(b['ap'], [0x006085e4, summary['kc_ap_mayhem']['native_id_member']])
        self.assertEqual(summary['kc_ap_stock']['native_id_member'], 34)
        self.assertEqual(b['fl'], [0x005aa174, summary['kc_fl']['native_id_member']])
        # The VM global is pinned by the windows: AP vm_create_store stores it, FL sa_set_event_object loads it (+43).
        windows = verifier_windows()
        self.assertIn('a3e4856000', dict((n, h) for n, _, h in windows['ap'])['vm_create_store'])
        self.assertIn('8b0d74a15a00', dict((n, h) for n, _, h in windows['fl'])['sa_set_event_object'])


class SectorKeySourceTests(unittest.TestCase):
    def test_core_is_portable(self):
        core = (ROOT / 'src/proxy/sector_key_core.h').read_text()
        includes = re.findall(r'#include\s*[<"]([^>"]+)[>"]', core)
        self.assertEqual(includes, ['cstdint'])
        code = re.sub(r'//[^\n]*', '', core)
        hit = re.search(r'\b(float|double|memcpy|malloc|new|delete|asm|VirtualProtect)\b', code)
        self.assertIsNone(hit, hit and hit.group(0))

    def test_edition_part(self):
        part = (ROOT / 'src/proxy/sector_key.cpp').read_text()
        self.assertIn('constexpr const core::Edition& running = core::fl;', part)
        self.assertIn('constexpr const core::Edition& running = core::ap;', part)
        self.assertIn('fl::verified()', part)
        self.assertIn('object_trace::executable_verified()', part)
        self.assertIn('engine_patch::read_code(at, out, n)', part)
        self.assertIn('engine_memory::read(at, out, n)', part)
        sample = function(part, 'core::Result sample(')
        self.assertLess(sample.index('GetLastError()'), sample.index('core::read('))
        self.assertIn('SetLastError(error);', sample)
        hit = re.search(r'VirtualProtect|write_code|claim|install', re.sub(r'//[^\n]*', '', part))
        self.assertIsNone(hit, hit and hit.group(0))
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertEqual(cmake.count('src/renderer/sky_stars_pass.cpp src/proxy/sector_key.cpp'), 2)

    def test_route_wiring(self):
        fragment = (ROOT / 'src/proxy/motion_output_sky_stars_inc.h').read_text()
        classify = function(fragment, 'void MotionOutput::sky_stars_classify_draw(')
        self.assertIn('sky_stars_->prepare(seed, sky_stars_look_, sky_stars_hold_.key, &rebuilt);', classify)
        sample = function(fragment, 'void MotionOutput::sky_stars_sector_sample(')
        self.assertIn('sky_stars_hold_.apply(r);', sample)
        self.assertIn('sector_key::invalidate();', sample)
        pass_source = (ROOT / 'src/renderer/sky_stars_pass.cpp').read_text()
        prepare = function(pass_source, 'unsigned SkyStarsPass::prepare(')
        self.assertIn('same_build(seed, look, sector_key, built_seed_, built_look_, built_key_)', prepare)
        self.assertIn('&stats, sector_key);', prepare)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        context = function(capture, 'void sector_background_context(')
        self.assertIn('ctx.motion_output.sky_stars_sector_sample(value, GetTickCount64());', context)
        self.assertEqual(capture.count('volumetric_fog_requested || sky_stars_requested'), 3)


if __name__ == '__main__':
    unittest.main()
