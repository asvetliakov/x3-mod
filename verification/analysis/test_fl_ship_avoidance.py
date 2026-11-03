"""Host tests of the FL ship avoidance (src/engine/fl/ship_avoidance_fl.cpp, ship_avoidance_sites_fl.h;
docs/reverse-engineering/fl/ship-avoidance.md, "Port (2026-11-03)").

(a) The native fixture verification/probe/fl_ship_avoidance.cpp (built and run as test_fl_ship_steering.py builds its fixture) over
the installed X3FL.exe mapped and relocated into a private image, with the engine-model harness's avoidance unit cases and its walk
check's station tree: window refusal (the avoidance or E alone), E's read-back rollback, the snapshot / detour / E verdict
differential against the shared cores, the never-toward-a-skipped-obstacle property, the station layout against the native
subtree-radius getter, E through the three real callers, the E stub's CPU state, recycled A and B, the SETA-scaled E window.
(b) The verifier against the installed X3FL.exe (also compares the header's window rows).
(c) The walk core's reader-bound TShips slot (the AP readers keep kShipTypeTableSlot).
"""
from pathlib import Path
import json
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SITES = ROOT / 'src/engine/fl/ship_avoidance_sites_fl.h'
sys.path.insert(0, str(ROOT / 'verification/analysis'))
sys.path.insert(0, str(ROOT / 'verification/probe'))
from test_fl_ship_steering import EXE, build_fixture, windows_path  # noqa: E402


class FlShipAvoidanceFixture(unittest.TestCase):
    def test_native_fixture(self):
        with tempfile.TemporaryDirectory(prefix='x3-fl-avoid-') as tmp:
            exe = build_fixture(tmp, 'fl_ship_avoidance')
            if exe is None:
                self.skipTest('needs i686-w64-mingw32-g++, WSL interop (a native Windows run) and the installed X3FL.exe')
            run = subprocess.run([str(exe), windows_path(EXE)], text=True, capture_output=True, timeout=600)
        out = run.stdout
        self.assertEqual(run.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:4000])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertIn('FLIPS windows=16 avoidance_refused=6 e_refused=10', out)
        self.assertIn('CASES cases=46 same=46 detours=45 suppressed=19 table_suppressed=1 m5_suppressed=0 sweep_suppressed=18 '
                      'never_toward_skipped=19', out)
        self.assertIn('SETA cases=9 ok=9 window_ms(16)=100 window_ms(200)=800', out)
        self.assertIn('RADIUS replicated=125000 native=125000', out)
        self.assertIn('CALLERS shapes=3 ok=3', out)
        self.assertIn('CPU runs=16 ok=16', out)
        self.assertRegex(out, r'(?m)^LOG ship_avoidance installed=1 edition=fl warn_site=0x[0-9a-f]{8} warn_rva=0x057600 warn_status=active '
                              r'write=atomic ')


class FlShipAvoidanceSites(unittest.TestCase):
    def test_verifier_against_the_installed_exe(self):
        if not EXE.is_file():
            self.skipTest(f'{EXE} not present')
        try:
            import capstone  # noqa: F401
        except ImportError:
            self.skipTest('needs capstone')
        import verify_ship_avoidance_sites_fl as probe
        report = probe.verify(EXE.read_bytes())
        self.assertEqual(report['result'], 'PASS', json.dumps({k: v['ok'] for k, v in report['checks'].items()}))
        checks = report['checks']
        self.assertEqual(checks['windows']['count'], 16)
        self.assertEqual(len(checks['site_e']['returns']), 5)
        self.assertEqual(checks['site_e']['callers'], ['0x057db5', '0x057dd0', '0x060581'])
        self.assertFalse(any(a['matches'] for a in checks['ap_refused']['ap_spans']))

    def test_header_rows(self):
        import verify_ship_avoidance_sites_fl as probe
        header = SITES.read_text()
        self.assertIn('warn_rva = 0x057600', header)
        self.assertIn('warn_length = 6, warn_ret_pop = 0xc', header)
        self.assertEqual(len(re.findall(r'\{"[a-z_0-9]+", 0x[0-9a-f]{6}, \d+, 0x[0-9a-f]{8}u, ', header)), len(probe.WINDOW_SPANS))


class WalkCoreTableBinding(unittest.TestCase):
    def test_reader_bound_slot(self):
        walk = (ROOT / 'src/proxy/ship_avoidance_walk_core.h').read_text()
        self.assertIn('const std::uint32_t table = ship_table_of(mem, 0);', walk)
        self.assertIn('return mem.u32(kShipTypeTableSlot);', walk)  # every AP reader
        internal = (ROOT / 'src/engine/fl/ship_steering_fl_internal.h').read_text()
        self.assertIn('std::uint32_t ship_table() const noexcept { return rd32(table_slot_); }', internal)


if __name__ == '__main__':
    unittest.main()
