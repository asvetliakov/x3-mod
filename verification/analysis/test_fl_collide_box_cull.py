"""Host test of the FL collision box-cull (src/engine/fl/collide_box_cull_fl.cpp, collide_box_cull_sites_fl.h).

Sites, margin and the conservative proof: docs/reverse-engineering/fl/collision-box.md, "Port (2026-11-03)". (a) The
native fixture verification/probe/fl_collide_box_cull.cpp (MinGW i686, -mstackrealign -mincoming-stack-boundary=2 -msse2
-mfpmath=sse, run natively through WSL interop) maps a relocated private copy of X3FL.exe, installs with the production
module and engine_patch, and drives the two real sites with a full CPU image: claim/read-back/rollback, register/x87/
MXCSR/LastError preservation, the differential (a box cull is always a subset of the native dist>R reject, the hardware
verdict matches the model, class 7 on either object is never culled at P1) over a deterministic plus 4000-pair
randomised set (independent coordinate / radius / class draws, about 23% class-7 pairs) under eight MXCSR modes (each
rounding control, DAZ+FZ, and the two modes the FP gate refuses: no culls there), the counters and census row, a
per-pair timing with a harness-only and a disarmed baseline, shutdown and the late-window refusal. (b) The two emitted stubs decoded with
Capstone: integer only, no x87/SSE register, no call, one STMXCSR read, balanced stack. (c) Edition wiring.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / 'src/engine/fl/collide_box_cull_fl.cpp'
SITES = ROOT / 'src/engine/fl/collide_box_cull_sites_fl.h'
FIXTURE = ROOT / 'verification/probe/fl_collide_box_cull.cpp'
EXE = Path('/mnt/c/X3FL/X3FL.exe')


def build(tmp):
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    tmp = Path(tmp)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_COLLIDE_BOX_CULL_FL_FIXTURE', '-msse2',
              '-mfpmath=sse']
    units = (('src/engine/fl/collide_box_cull_fl.cpp', ['-fno-exceptions']),
             ('src/proxy/engine_patch.cpp', []))
    objects = []
    for source, flags in units:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_collide_box_cull.exe'
    subprocess.run([compiler, *common, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    return exe


class FlCollideBoxCullNativeFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-fl-box-')
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
        self.assertGreaterEqual(int(m.group(1)), 10000)

    def test_differential(self):
        out = self.proc.stdout
        rows = re.findall(r'DIFFERENTIAL site=(\d) mxcsr=([0-9a-f]{4}) pairs=(\d+) native_rejected=(\d+) '
                          r'box_culled=(\d+) over_culled=(\d+)(?: class7_pairs=(\d+) class7_culled=(\d+))?', out)
        self.assertEqual(len(rows), 16, out[-3000:])  # 8 MXCSR modes x 2 sites
        refused = {'1f80', '0fa0'}
        for site, csr, pairs, native, culled, over, c7, c7culled in rows:
            self.assertGreater(int(pairs), 4000)
            self.assertEqual(int(over), 0)  # never a cull of a pair the native distance keeps
            if csr in refused:
                self.assertEqual(int(culled), 0)  # the FP gate refuses: nothing culled
            else:
                self.assertGreater(int(culled), 1000)  # a meaningful share culled at both sites
            if site == '1':
                self.assertGreater(int(c7), 500)  # P1 sees a realistic share of class-7 pairs
                self.assertEqual(int(c7culled), 0)  # and never culls one
        total = re.search(r'DIFFERENTIAL_TOTAL modes=8 over_culled=0 p1_culled=(\d+) p2_culled=(\d+) class7_culled=0 '
                          r'refused_mode_culls=0', out)
        self.assertTrue(total, out[-2000:])

    def test_census_and_bench(self):
        out = self.proc.stdout
        self.assertRegex(out, r'BENCH harness_ns=[\d.]+ native_reject_ns=[\d.]+ stub_reject_ns=[\d.]+ '
                              r'native_keep_ns=[\d.]+ stub_keep_ns=[\d.]+ reject_saving_ns=-?[\d.]+ '
                              r'keep_overhead_ns=-?[\d.]+')  # the per-pair timing with its harness baseline
        self.assertRegex(out, r'IMAGE size=\d+ relocated=\d+')          # the relocated private image mapped
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')     # present/shutdown/late_claim ran clean

    def test_emitted_stubs(self):
        import capstone
        compiler = shutil.which('i686-w64-mingw32-g++')
        if not compiler:
            self.skipTest('needs the toolchain')
        # Decode both stubs the production encoders emit (counters on) and assert the CPU contract.
        src = r'''
#include "src/engine/fl/collide_box_cull_sites_fl.h"
#include <cstdio>
namespace F = x3m::fl::collide_box_cull_fl;
namespace ap = x3m::collide_box_cull::core;
int main() {
    unsigned char p1[F::stub_capacity], p2[F::stub_capacity];
    ap::Counters c{0xaa0000, 0xaa0004};
    unsigned n1 = F::encode_p1_stub(0x20000000, 0xbb0000, c, 0x20001000, 0x30000000, p1);
    unsigned n2 = F::encode_p2_stub(0x21000000, 0xbb0000, c, 0x21001000, 0x31000000, p2);
    std::printf("%u ", n1);
    for (unsigned i = 0; i < n1; ++i) std::printf("%02x", p1[i]);
    std::printf("\n%u ", n2);
    for (unsigned i = 0; i < n2; ++i) std::printf("%02x", p2[i]);
    std::printf("\n");
}'''
        with tempfile.TemporaryDirectory() as tmp:
            s = Path(tmp) / 'enc.cpp'
            s.write_text(src)
            exe = Path(tmp) / 'enc.exe'
            subprocess.run([compiler, '-std=c++17', '-O2', '-DX3M_EDITION_FL=1', f'-I{ROOT}', '-static', str(s),
                            '-o', str(exe)], check=True)
            run = subprocess.run([str(exe)], text=True, capture_output=True, check=True)
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        md.detail = True
        for line in run.stdout.strip().splitlines():
            length, hexbytes = line.split()
            data = bytes.fromhex(hexbytes)
            self.assertEqual(len(data), int(length))
            insns = list(md.disasm(data, 0x20000000))
            self.assertEqual(sum(i.size for i in insns), len(data), 'stub decodes whole')
            self.assertEqual(sum(1 for i in insns if i.mnemonic == 'stmxcsr'), 1, 'one MXCSR read')
            for i in insns:
                self.assertNotIn(i.mnemonic, ('call', 'ldmxcsr', 'fldcw', 'fsqrt', 'fadd'), i.mnemonic)
                self.assertFalse(i.mnemonic.startswith('f') and i.mnemonic not in ('fs',), i.mnemonic)
                for op in i.operands:
                    if op.type == capstone.x86.X86_OP_REG:
                        self.assertFalse(i.reg_name(op.reg).startswith(('xmm', 'st', 'mm')), i.mnemonic)


class FlCollideBoxCullWiring(unittest.TestCase):
    def test_sources_and_wiring(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn('src/engine/fl/collide_box_cull_fl.cpp', cmake)
        self.assertIn('collide_box_cull_fl.cpp src/engine/fl/collide_sat_sse2_fl.cpp', cmake)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('collide_box_cull::initialize();', capture)
        self.assertIn('collide_box_cull::present(ctx.id, ctx.frame, ctx.capture); // FL provider', capture)
        self.assertIn('margin = 256', SITES.read_text())
        self.assertIn('mxcsr_admit_mask = 0x1020', SITES.read_text())


if __name__ == '__main__':
    unittest.main()
