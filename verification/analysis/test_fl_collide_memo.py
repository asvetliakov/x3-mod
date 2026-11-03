"""Host test of the FL no-contact collision memo (src/engine/fl/collide_memo_fl.cpp, collide_memo_sites_fl.h).

Boundary, key and invalidation: docs/reverse-engineering/fl/collision-memo.md, "Port (2026-11-03)". The native fixture
verification/probe/fl_collide_memo.cpp (MinGW i686, -mstackrealign -mincoming-stack-boundary=2 -msse2 -mfpmath=sse, run
natively through WSL interop) maps two relocated private copies of X3FL.exe: M carries the production memo (real
engine_patch), N is the untouched native reference, and every key fires on both with two synthetic built models. Phase A
runs production verify mode (every would-be hit re-run and compared by store(); M's outputs and CPU image equal N's; a
below-root geometry change must produce exactly one mismatch). Phase B runs replay mode: every replay's contacts,
counters, first-contact flag, root block, contact record and running minimum equal N's engine run of the same key, every
replay preserves the CPU image, every miss equals N's CPU image (x87, MXCSR status, XMM); the below-root control must be
detected. Then invalidation (movement, ship change, root-box change, frame gap), the FP context gates, device_reset, the
300-frame row, shutdown and late_claim. Contacts are never stored (the shared policy); contact pairs run as misses.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / 'src/engine/fl/collide_memo_fl.cpp'
SITES = ROOT / 'src/engine/fl/collide_memo_sites_fl.h'
FIXTURE = ROOT / 'verification/probe/fl_collide_memo.cpp'
EXE = Path('/mnt/c/X3FL/X3FL.exe')


def build(tmp):
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    tmp = Path(tmp)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_COLLIDE_MEMO_FL_FIXTURE', '-msse2',
              '-mfpmath=sse']
    units = (('src/engine/fl/collide_memo_fl.cpp', ['-fno-exceptions']), ('src/proxy/engine_patch.cpp', []))
    objects = []
    for source, flags in units:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_collide_memo.exe'
    subprocess.run([compiler, *common, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    return exe


class FlCollideMemoNativeFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-fl-memo-')
        cls.exe = build(cls.tmp.name)
        cls.proc = None
        if cls.exe and EXE.is_file():
            win = subprocess.run(['wslpath', '-w', str(EXE)], capture_output=True, text=True, check=True).stdout.strip()
            cls.proc = subprocess.run([str(cls.exe), win], text=True, capture_output=True, timeout=600)

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
                         '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:4000] + self.proc.stderr[-1000:])
        m = re.search(r'RESULT PASS checks=(\d+) failures=0', out)
        self.assertTrue(m, out[-2000:])
        self.assertGreaterEqual(int(m.group(1)), 5000)

    def test_differential(self):
        out = self.proc.stdout
        v = re.search(r'VERIFY queries=(\d+) contact_runs=(\d+) verified=(\d+) verify_mismatches=(\d+) '
                      r'miss_cpu_differences=(\d+) aba_control_mismatches=(\d+)', out)
        self.assertTrue(v, out[-2000:])
        queries, contact_runs, verified, mismatches, miss_cpu, control = map(int, v.groups())
        self.assertGreater(verified, 1000)   # production verify mode compared real engine runs
        self.assertEqual(mismatches, 0)
        self.assertEqual(miss_cpu, 0)        # the thunk's engine path equals the native call bit for bit
        self.assertEqual(control, 1)         # the below-root change is detected: the comparison can fail
        self.assertGreater(contact_runs, 100)
        m = re.search(r'DIFFERENTIAL queries=(\d+) hits=(\d+) misses=(\d+) stored=(\d+) contacts=(\d+) '
                      r'contact_pairs=(\d+) replays=(\d+) replay_disagreements=(\d+) miss_disagreements=(\d+) '
                      r'replay_cpu_differences=(\d+) miss_cpu_differences=(\d+) aba_control_detected=(\d+)', out)
        self.assertTrue(m, out[-2000:])
        (queries, hits, misses, stored, contacts, contact_pairs, replays, replay_dis, miss_dis, replay_cpu, miss_cpu,
         detected) = map(int, m.groups())
        self.assertGreater(replays, 1000)    # real replays, each compared with the native run of the same key
        self.assertGreater(contact_pairs, 100)
        self.assertEqual((replay_dis, miss_dis, replay_cpu, miss_cpu), (0, 0, 0, 0))
        self.assertEqual(detected, 1)        # the replay comparison detects the below-root change

    def test_emitted_thunk(self):
        import capstone
        # Decode the production thunk's first bytes: it saves XMM then the input GPRs around the C lookup.
        compiler = shutil.which('i686-w64-mingw32-g++')
        if not compiler:
            self.skipTest('needs the toolchain')
        text = PROVIDER.read_text()
        self.assertIn('movdqu [esp], xmm0', text)   # the thunk saves all eight XMM registers
        self.assertIn('movdqu [esp+112], xmm7', text)
        self.assertIn('call _x3m_collide_memo_fl_lookup', text)
        self.assertIn('call _x3m_collide_memo_fl_store', text)


class FlCollideMemoWiring(unittest.TestCase):
    def test_sources_and_wiring(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn('src/engine/fl/collide_memo_fl.cpp', cmake)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('collide_memo::initialize();', capture)
        self.assertIn('collide_memo::present(ctx.id, ctx.frame, ctx.capture); // FL provider', capture)
        self.assertIn('collide_memo::device_reset(); // FL provider', capture)
        # the shared AP memo core and table are reused, not copied.
        self.assertIn('#include "../../proxy/collide_memo.h"', (ROOT / 'src/engine/fl/collide_memo_fl.h').read_text())
        self.assertIn('x3m::collide_memo::core', SITES.read_text())


if __name__ == '__main__':
    unittest.main()
