"""Host test of the FL collide_sat_sse2 report (src/engine/fl/collide_sat_sse2_fl.cpp, collide_sat_sse2_sites_fl.h).

Decision: docs/reverse-engineering/fl/collision-sat.md, "Port (2026-11-03)". The native fixture
verification/probe/fl_collide_sat.cpp (MinGW i686, -mstackrealign -mincoming-stack-boundary=2 -msse2 -mfpmath=sse, run
natively through WSL interop) runs the production initialize() against a relocated private copy of X3FL.exe: the
`status=native_sse` row for a set, unset and off setting, one row per load, nothing claimed (no arena use, the whole
image byte-identical to the pristine copy), LastError kept, the window refusals and executable_mismatch.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'verification/probe/fl_collide_sat.cpp'
EXE = Path('/mnt/c/X3FL/X3FL.exe')


def build(tmp):
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    tmp = Path(tmp)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_COLLIDE_SAT_FL_FIXTURE', '-msse2',
              '-mfpmath=sse']
    units = (('src/engine/fl/collide_sat_sse2_fl.cpp', ['-fno-exceptions']), ('src/proxy/engine_patch.cpp', []))
    objects = []
    for source, flags in units:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_collide_sat.exe'
    subprocess.run([compiler, *common, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    return exe


class FlCollideSatNativeFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-fl-sat-')
        cls.exe = build(cls.tmp.name)
        cls.proc = None
        if cls.exe and EXE.is_file():
            win = subprocess.run(['wslpath', '-w', str(EXE)], capture_output=True, text=True, check=True).stdout.strip()
            cls.proc = subprocess.run([str(cls.exe), win], text=True, capture_output=True, timeout=300)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        if self.exe is None:
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        if self.proc is None:
            self.skipTest('needs /mnt/c/X3FL/X3FL.exe')

    def test_full_run(self):
        out = self.proc.stdout
        self.assertEqual(self.proc.returncode, 0,
                         '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:3000] + self.proc.stderr[-800:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')

    def test_rows(self):
        rows = re.findall(r'^LOG (collide_sat_sse2 .*)$', self.proc.stdout, re.M)
        self.assertTrue(all(' edition=fl ' in r and ' patched=0 ' in r for r in rows), rows)
        native = [r for r in rows if 'status=native_sse reason=engine_already_sse' in r]
        self.assertEqual(len(native), 3)  # set, unset, off
        refused = [re.search(r'reason=(\S+)', r).group(1) for r in rows if 'status=refused' in r]
        self.assertEqual(refused, ['sat_body_mismatch', 'sat_call_mismatch', 'sat_body_mismatch', 'executable_mismatch'])


class FlCollideSatWiring(unittest.TestCase):
    def test_wiring(self):
        self.assertIn('src/engine/fl/collide_sat_sse2_fl.cpp', (ROOT / 'CMakeLists.txt').read_text())
        self.assertIn('collide_sat_sse2::initialize();', (ROOT / 'src/proxy/capture.cpp').read_text())
        source = (ROOT / 'src/engine/fl/collide_sat_sse2_fl.cpp').read_text()
        self.assertNotIn('claim', source.split('#include <cstring>')[1])  # nothing claimed on FL


if __name__ == '__main__':
    unittest.main()
