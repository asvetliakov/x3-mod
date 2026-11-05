"""Collision damage (X3M_COLLISION_DAMAGE, docs/reverse-engineering/collision-damage.md, "Implementation"): the core's
tables against verify_collision_damage.py (13 windows, both spans, the hashes), the fixture
verification/probe/collision_damage_fixture.cpp on the host (comparator, every-byte refusal, fault-injected install and
rollback) and natively through WSL interop (the two stubs executed against a fabricated register, stack and code image
for both editions), the config entry and the production wiring. No game, no Wine."""
import importlib.util
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/config'))
import schema  # noqa: E402

FIXTURE = ROOT / 'verification/probe/collision_damage_fixture.cpp'
VERIFIER = ROOT / 'verification/results/collision-damage/verify_collision_damage.py'
VERIFIER_OUTPUT = ROOT / 'verification/results/collision-damage/verify_collision_damage_output.json'
MINGW_FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-msse2', '-mfpmath=sse', '-mstackrealign',
               '-mincoming-stack-boundary=2']


def verifier():
    saved = sys.argv
    sys.argv = [str(VERIFIER)]  # its exe paths come from argv at import; only the tables are used here
    try:
        spec = importlib.util.spec_from_file_location('verify_collision_damage', VERIFIER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    finally:
        sys.argv = saved
    return module


def windows_file(directory):
    path = Path(directory) / 'windows.txt'
    path.write_text(''.join('%s %s 0x%08x %s\n' % (ed, name, va, hx.replace(' ', ''))
                            for ed, e in verifier().ED.items() for name, va, hx in e['windows']))
    return path


class Fixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('c++') or shutil.which('clang++')
        if compiler is None:
            raise unittest.SkipTest('A host C++ compiler is required')
        cls.dir = tempfile.TemporaryDirectory(prefix='x3-cd-')
        cls.host = Path(cls.dir.name) / 'collision_damage_host'
        build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                str(FIXTURE), '-o', str(cls.host)], capture_output=True, text=True, timeout=180)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def test_tables_equal_the_verifier(self):
        out = subprocess.run([str(self.host), '--tables'], capture_output=True, text=True, timeout=60).stdout.splitlines()
        rows = [line.split() for line in out]
        v, recorded = verifier(), json.loads(VERIFIER_OUTPUT.read_text())
        windows = 0
        for ed, e in v.ED.items():
            core = [(r[2], int(r[3], 16), r[4]) for r in rows if r[0] == 'window' and r[1] == ed]
            self.assertEqual(core, [(n, va, hx.replace(' ', '')) for n, va, hx in e['windows']], ed)
            windows += len(core)
            spans = {r[2]: (int(r[3], 16), int(r[4])) for r in rows if r[0] == 'span' and r[1] == ed}
            self.assertEqual(spans, {n: (va, length) for n, va, length in e['spans']}, ed)
            hashes = {r[2]: r[3:] for r in rows if r[0] == 'hash' and r[1] == ed}
            self.assertEqual(set(hashes), {'response'})  # the EXE file hash stays out of production (test_exe_identity)
            self.assertEqual(recorded[ed]['sha256'], e['sha'])  # the windows were recorded on the pinned EXE
            lo, hi = e['response']
            self.assertEqual(hashes['response'], ['0x%08x' % lo, str(hi - lo), recorded[ed]['response']['sha256']])
        self.assertEqual(windows, 13)
        self.assertEqual(recorded['result'], 'PASS')

    def test_host_checks_on_the_verifier_bytes(self):
        with tempfile.TemporaryDirectory(prefix='x3-cd-w-') as d:
            run = subprocess.run([str(self.host), '--windows', str(windows_file(d))], capture_output=True, text=True,
                                 timeout=120)
        self.assertEqual(run.returncode, 0, run.stdout[-4000:] + run.stderr)
        self.assertIn('windows=verifier', run.stdout)
        self.assertIn('checks=96 failures=0 PASS', run.stdout)
        for ed, flips in (('ap', 133), ('fl', 141)):
            self.assertIn('%s flips=%d refused=%d' % (ed, flips, flips), run.stdout)
            self.assertIn('%s faults=8 rolled_back=8 persistent=8 kept_registered=7' % ed, run.stdout)

    def test_stubs_execute_natively(self):
        compiler = shutil.which('i686-w64-mingw32-g++')
        if not compiler:
            self.skipTest('MinGW cross compiler unavailable')
        if not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
            self.skipTest('WSL interop (a Windows host) is required to execute the i686 fixture')
        with tempfile.TemporaryDirectory(prefix='x3-cd-native-') as d:
            exe = Path(d) / 'collision_damage_fixture.exe'
            build = subprocess.run([compiler, *MINGW_FLAGS, '-I', str(ROOT / 'src/proxy'), str(FIXTURE), '-static', '-s',
                                    '-o', str(exe)], capture_output=True, text=True, timeout=180)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            try:
                run = subprocess.run([str(exe), '--windows', str(windows_file(d))], capture_output=True, text=True,
                                     timeout=120)
            except OSError as error:
                self.skipTest('the fixture cannot be executed here: %s' % error)
        self.assertEqual(run.returncode, 0, run.stdout[-4000:] + run.stderr)
        self.assertIn('native=1', run.stdout)
        self.assertGreaterEqual(int(re.search(r'checks=(\d+) failures=0 PASS', run.stdout).group(1)), 220)


class Config(unittest.TestCase):
    def test_schema_entry(self):
        e = schema.BY_KEY['collision_damage']
        self.assertEqual((e['env'], e['type'], e['default'], e['builtin'], e['launcher'], e['developer'], e['section']),
                         ('X3M_COLLISION_DAMAGE', 'bool', None, '1', None, False, 'engine'))
        self.assertRegex((ROOT / 'assets/x3m.ini').read_text(), r'\n;collision_damage = 1\n')

    def test_read_site(self):
        source = (ROOT / 'src/proxy/collision_damage.cpp').read_text()
        self.assertIn('x3m::config::get(L"X3M_COLLISION_DAMAGE", setting, 4)', source)
        self.assertIn("zero = length == 1 && setting[0] == L'0'", source)  # only "0" patches


class Wiring(unittest.TestCase):
    def test_production_wiring(self):
        source = (ROOT / 'src/proxy/collision_damage.cpp').read_text()
        # identity, late window, every window, pin, the return bound, then the writes
        order = [source.index(s) for s in ('if (!base) return "executable_mismatch"', 'install_window_open()',
                                           'core::compare_windows(read, base, edition_, mismatch_)', 'pin_self())',
                                           'collision_damage::bind(edition_, base)', 'core::install(ops, base, edition_,')]
        self.assertEqual(order, sorted(order))
        self.assertIn('collision_damage edition=%s status=%s reason=%s', source)
        self.assertIn('collision_damage_suppressed edition=%s frame=%llu events=%lu', source)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertEqual(capture.count('collision_damage::initialize();'), 1)  # shared by both editions
        self.assertEqual(capture.count('collision_damage::present(ctx.frame);'), 2)  # the AP and the FL Present branch
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertEqual(cmake.count('src/proxy/collision_damage.cpp'), 2)  # the AP list and the FL target
        self.assertIn('x3m::collision_damage::shutdown();', (ROOT / 'src/proxy/loader.cpp').read_text())
        roots = (ROOT / 'verification/probe/check_no_x87.py').read_text()
        self.assertIn("'_x3m_collision_damage_ap_stub', '_x3m_collision_damage_fl_stub'", roots)


if __name__ == '__main__':
    unittest.main()
