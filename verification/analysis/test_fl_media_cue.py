"""Host tests of the FL media-cue gate (src/engine/fl/media_cue_fl.cpp, media_cue_sites_fl.h, code_window_fl.h).

docs/reverse-engineering/fl/media-cue.md, "Port (2026-11-03)". (a) The native fixture verification/probe/fl_media_cue.cpp
(MinGW i686 with the project flags, run natively through WSL interop) maps the installed X3FL.exe at a private base,
installs the production module over the real relocated allocator and runs it: the ID2 refusal (EAX 0, the failed
allocator's flags, every other register, DF, XMM7, x87 CW, MXCSR and LastError kept, no callee reached), pass-through
paths, the negative cache in the selector scope only, observed returns whose outgoing flags equal the native
allocator's (the flag-neutral return-slot reservation), the trace's caller classes, every window change refused,
read-back / chain rollback, late-window refusal. (b) The read-only site verifier on the exe and on mutated copies.
(c) The generated windows and the edition wiring.
"""
from pathlib import Path
import re
import tempfile
import unittest

import fl_music_fixture_build as fb
import fl_music_sites_common as c
import verify_media_cue_sites_fl as verifier

ROOT = Path(__file__).resolve().parents[2]
EXE = c.DEFAULT_EXE


def strip(text):
    return re.sub(r'//.*', '', text)


class FlMediaCueNative(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-fl-media-cue-')
        cls.run_ = None
        if fb.available() and EXE.exists():
            exe = fb.build('media_cue', cls.tmp.name)
            cls.run_ = fb.run(exe, EXE)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        if self.run_ is None:
            self.skipTest('needs i686-w64-mingw32-g++, WSL interop and the installed X3FL.exe')

    def test_fixture_passes(self):
        out = self.run_.stdout
        self.assertEqual(self.run_.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:3000])
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+) failures=0', out).group(1)), 20)
        cases, refused = map(int, re.search(r'FLIPS windows=7 cases=(\d+) refused=(\d+)', out).groups())
        self.assertEqual(cases, refused)
        self.assertIn('CACHE first_fail=1 refused=1 script=1 cleared=1 flags=1', out)
        self.assertIn('TRACE entries=6 selector=4 script=2 refused=1', out)
        self.assertIn('FAULTS taken=2', out)
        rows = re.findall(r'^LOG (media_cue\S* .*)$', out, re.M)
        self.assertTrue(rows and all(r.endswith('edition=fl') for r in rows), rows[:3])
        self.assertTrue(any('status=active' in r and 'write=atomic' in r for r in rows))


class FlMediaCueSites(unittest.TestCase):
    def setUp(self):
        if not EXE.exists():
            self.skipTest('needs the installed X3FL.exe')

    def test_verifier_passes(self):
        report = verifier.verify(EXE)
        self.assertEqual(report['result'], 'PASS', report['failed'])
        self.assertEqual(report['allocator_instructions'], 91)
        self.assertEqual(report['media_window_count'], 7)

    def test_mutations_refused(self):
        data = bytearray(EXE.read_bytes())
        im = c.Image(EXE)
        cases = {'entry_span_bytes': 0x0b4083, 'wrapper_frame': 0x0b46b6, 'selector_kind_0x5a': 0x066306}
        for check, rva in cases.items():
            mutated = bytearray(data)
            mutated[im.pe.offset(rva)] ^= 0x01
            with tempfile.NamedTemporaryFile(suffix='.exe') as f:
                f.write(mutated)
                f.flush()
                report = verifier.verify(Path(f.name), sha256=None)
            self.assertEqual(report['result'], 'FAIL', check)
            self.assertIn('media_windows_match_exe', report['failed'], check)
            self.assertIn(check, report['failed'])

    def test_generated_windows_current(self):
        import generate_windows
        text, _ = generate_windows.generate(EXE)
        self.assertEqual(text, (ROOT / 'src/engine/fl/music_windows_fl_inc.h').read_text())


class FlMediaCueWiring(unittest.TestCase):
    def test_edition_lists_and_capture(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        ap = re.search(r'set\(X3M_AP_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('_fl.cpp', ap)
        self.assertIn('src/proxy/media_cue.cpp', ap)
        self.assertRegex(cmake, r'target_sources\(d3d9 PRIVATE src/engine/fl/media_cue_fl\.cpp src/engine/fl/music_keep_fl\.cpp\n'
                                r'\s+src/engine/fl/music_alert_fl\.cpp\)')
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        fl_init = capture.rindex('media_cue::initialize();')
        self.assertLess(capture.rindex('fov::initialize();'), fl_init)
        self.assertIn('#if X3M_EDITION_FL', capture[capture.rindex('#if', 0, fl_init) - 1:fl_init])
        frame = capture.rindex('media_cue::frame(ctx.frame);')
        self.assertIn('#else', capture[frame - 40:frame])

    def test_provider_contract(self):
        source = (ROOT / 'src/engine/fl/media_cue_fl.cpp').read_text()
        sites = (ROOT / 'src/engine/fl/media_cue_sites_fl.h').read_text()
        for needle in ('detail::refuse_id2_video(f->ecx, f->edx)', 'engine_patch::install_window_open()',
                       'W::first_mismatch(read_code, base, W::media_windows, W::media_fixups)', 'callers_mismatch',
                       'engine_patch::restore(site_)', 'rollback_failed', 'x3m::fl::module_base()', 'edition=fl'):
            self.assertIn(needle, source)
        self.assertIn('put(0x8d), put(0x64), put(0x24), put(0xfc); // lea esp,[esp-4]', sites)  # flag-neutral slot
        self.assertNotIn('0x00498140', source)


if __name__ == '__main__':
    unittest.main()
