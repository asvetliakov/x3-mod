"""Host tests of the FL combat trigger (src/engine/fl/music_alert_fl.cpp, music_alert_sites_fl.h).

docs/reverse-engineering/fl/music-alert.md, "Port (2026-11-03)". (a) The native fixture verification/probe/fl_music_alert.cpp
(MinGW i686, natively through WSL interop) maps the installed X3FL.exe, installs the production module over the real
seam and enters the seam's own bytes with EBP = a fake registry and EBX = a fake player: the unchanged native
pass-through, the shared engagement policy on the FL bindings (a quiet level lowered, the player-hit and missile
reasons, the hold on the game clock cfg+0x708 with AP's +0x718 holding garbage, expiry, a rewound clock), EAX/ECX as
the replayed moves leave them, EDX/EBX/ESI/EDI/EBP/ESP, EFLAGS, DF, XMM7, x87 CW, MXCSR and LastError kept, every
window change refused, the main loop's call checked, read-back / chain rollback, restore and the late window.
(b) The read-only site verifier on the exe and on mutated copies. (c) Wiring and the shared-policy binding.
"""
from pathlib import Path
import re
import tempfile
import unittest

import fl_music_fixture_build as fb
import fl_music_sites_common as c
import verify_music_alert_sites_fl as verifier

ROOT = Path(__file__).resolve().parents[2]
EXE = c.DEFAULT_EXE


class FlMusicAlertNative(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-fl-music-alert-')
        cls.run_ = None
        if fb.available() and EXE.exists():
            cls.run_ = fb.run(fb.build('music_alert', cls.tmp.name), EXE)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        if self.run_ is None:
            self.skipTest('needs i686-w64-mingw32-g++, WSL interop and the installed X3FL.exe')

    def test_fixture_passes(self):
        out = self.run_.stdout
        self.assertEqual(self.run_.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:3000])
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+) failures=0', out).group(1)), 25)
        cases, refused = map(int, re.search(r'FLIPS windows=13 cases=(\d+) refused=(\d+)', out).groups())
        self.assertEqual(cases, refused)
        self.assertIn('FAULTS taken=2', out)
        rows = re.findall(r'^LOG (music_alert .*)$', out, re.M)
        self.assertTrue(all(r.endswith('edition=fl') for r in rows), rows[:3])
        edges = [r for r in rows if r.startswith('music_alert edge')]
        self.assertTrue(any('reason=b' in r and 'cause=stamp now=2000 step=16 seta=65536' in r for r in edges), edges)
        self.assertTrue(any('age_ms=20000 cause=expired now=22000' in r for r in edges), edges)
        self.assertTrue(any('from=0 to=2 vanilla=2' in r and 'reason=d' in r for r in edges), edges)


class FlMusicAlertSites(unittest.TestCase):
    def setUp(self):
        if not EXE.exists():
            self.skipTest('needs the installed X3FL.exe')

    def test_verifier_passes(self):
        report = verifier.verify(EXE)
        self.assertEqual(report['result'], 'PASS', report['failed'])
        self.assertEqual(report['alert_instructions'], 235)
        self.assertEqual([list(e) for e in report['seam_edges']], [['0x2fad5', '0x2fada']])

    def test_mutations_refused(self):
        data = bytearray(EXE.read_bytes())
        im = c.Image(EXE)
        cases = {'seam_span': 0x02fadb, 'alert_head_registers': 0x02f91f, 'callers_registry_update': 0x0782cb}
        for check, rva in cases.items():
            mutated = bytearray(data)
            mutated[im.pe.offset(rva)] ^= 0x01
            with tempfile.NamedTemporaryFile(suffix='.exe') as f:
                f.write(mutated)
                f.flush()
                report = verifier.verify(Path(f.name), sha256=None)
            self.assertEqual(report['result'], 'FAIL', check)
            self.assertIn(check, report['failed'])


class FlMusicAlertWiring(unittest.TestCase):
    def test_capture_and_binding(self):
        capture = re.sub(r'//.*', '', (ROOT / 'src/proxy/capture.cpp').read_text())
        fl_init = capture.rindex('music_alert::initialize();')
        self.assertLess(capture.rindex('music_keep::initialize();'), fl_init)
        self.assertLess(capture.rindex('fov::initialize();'), fl_init)
        sites = (ROOT / 'src/engine/fl/music_alert_sites_fl.h').read_text()
        self.assertIn('static_assert(cfg_bias == 0x10', sites)
        source = (ROOT / 'src/engine/fl/music_alert_fl.cpp').read_text()
        for needle in ('core::frame(memory, rule_, registry, player, hold_ms_)', 'at == core::cfg_va',
                       'at == core::player_root_va', 'cfg - F::cfg_bias', 'push ebx\n    push ebp', 'cld',
                       'W::first_mismatch(read_code, base, W::alert_windows, W::alert_fixups)', 'edition=fl'):
            self.assertIn(needle, source)


if __name__ == '__main__':
    unittest.main()
