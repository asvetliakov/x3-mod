"""Host tests of the FL dust leak fix (src/engine/fl/dust_leak_fix_fl.cpp, src/engine/fl/dust_leak_fix_sites_fl.h).

Site and ABI: docs/reverse-engineering/fl/dust-light.md, section 1 and "Port (2026-11-03)" (verifier
verification/probe/verify_dust_leak_fix_site_fl.py). (a) The native fixture verification/probe/fl_dust_leak_fix.cpp
(MinGW i686, run natively through WSL interop) lays the eight FL windows into a 64 KB-aligned private image, installs
with the production module and engine_patch, and runs the real tail bytes and the real release prologue/epilogue:
stock leak vs patched release-and-leave, one release per failed node with ECX = node, EBX = the count, every other
register, ESP, the frame, XMM7, the x87 CW, MXCSR and LastError kept, the tail's flags kept (recorder), every
single-bit window change refused, chain/read-back/restore faults rolled back (registered when the rollback fails),
initialize()'s rows, shutdown only over our jump, late_claim, and a second run where the window closes with the
site live (restore_retained). (b) The emitted stub decoded with Capstone against its live operands. (c) The verifier
on the installed exe and its row parsers. (d) Edition wiring.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
PROVIDER = ROOT / 'src/engine/fl/dust_leak_fix_fl.cpp'
SITES = ROOT / 'src/engine/fl/dust_leak_fix_sites_fl.h'
FIXTURE = ROOT / 'verification/probe/fl_dust_leak_fix.cpp'
SHIM = ROOT / 'verification/probe/fl_dust_leak_fix_shim.h'
EXE = Path('/mnt/c/X3FL/X3FL.exe')


def build(tmp):
    """Builds the fixture from the production sources (CMake per-file flags); None when the toolchain or interop is
    absent."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    tmp = Path(tmp)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_DUST_LEAK_FIX_FL_FIXTURE', '-msse2',
              '-mfpmath=sse']
    units = (('src/engine/fl/dust_leak_fix_fl.cpp', ['-fno-exceptions', '-include', str(SHIM),
                                                     '-DX3M_DUST_LEAK_FIX_FL_SHIM']),
             ('src/proxy/engine_patch.cpp', []), ('src/engine/fl/executable_identity_fl.cpp', []),
             ('src/proxy/engine_memory.cpp', ['-mno-sse', '-mno-mmx', '-mfpmath=387']))
    objects = []
    for source, flags in units:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_dust_leak_fix.exe'
    subprocess.run([compiler, *common, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    return exe


class FlDustLeakFixNativeFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-fl-dust-')
        cls.exe = build(cls.tmp.name)
        cls.full = cls.retain = None
        if cls.exe:
            cls.full = subprocess.run([str(cls.exe)], text=True, capture_output=True, timeout=300)
            cls.retain = subprocess.run([str(cls.exe), 'retain'], text=True, capture_output=True, timeout=120)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        if self.exe is None:
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')

    def assert_pass(self, run, minimum):
        out = run.stdout
        self.assertEqual(run.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:4000]
                         + run.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+)', out).group(1)), minimum)

    def test_full_run(self):
        self.assert_pass(self.full, 40)
        out = self.full.stdout
        self.assertRegex(out, r'WINDOWS alloc_mismatch@020950=ea6c5d54 body_mismatch@020a17=11cd18f1 '
                              r'tail_mismatch@020aba=fda11cf4 release_mismatch@0a3300=267af6be '
                              r'release_exit_mismatch@0a359a=efe79144 release_exit_mismatch@0a35cf=efe79144 '
                              r'attach_mismatch@0a56b0=b3a1cfde clear_mismatch@08a875=6bcaf233')
        self.assertIn('FLIPS windows=8 bits=1856 refused=1856 fixup=32 fixup_refused=32', out)
        self.assertIn('FAULTS taken=5', out)
        self.assertIn('RECORDER null=1 attached=1 failed=1 clear=1', out)
        self.assertRegex(out, r'RUNS hits=5 releases=\d+')
        rows = re.findall(r'^LOG (dust_leak_fix site=.*)$', out, re.M)
        self.assertTrue(rows and all(' edition=fl ' in r for r in rows), rows)
        import verify_dust_leak_fix_site_fl as verifier
        parsed = [verifier.parse_log_line(r) for r in rows]
        self.assertTrue(all(parsed), rows)
        self.assertEqual(sum(p['patched'] and p['write'] == 'plain' and p['reason'] == 'ok' for p in parsed), 1)
        self.assertEqual(parsed[-1]['reason'], 'late_claim')
        hits = [verifier.parse_hits_line(l) for l in out.splitlines() if 'dust_leak_fix hits=' in l]
        self.assertEqual([h['frame'] for h in hits], [900, 1200])
        restores = [verifier.parse_restore_line(l) for l in out.splitlines() if l.startswith('ROW ')]
        self.assertEqual([r['status'] for r in restores], ['restored', 'restore_not_owned', 'restored', 'restored'])

    def test_retain_run(self):
        self.assert_pass(self.retain, 3)
        self.assertRegex(self.retain.stdout, r'RETAIN span=e9[0-9a-f]{8}')
        self.assertIn('status=restore_retained', self.retain.stdout)

    def test_emitted_stub(self):
        import capstone
        import capstone.x86 as x86
        m = re.search(r'^STUB at=([0-9a-f]{8}) release=([0-9a-f]{8}) get=([0-9a-f]{8}) set=([0-9a-f]{8}) '
                      r'hits=([0-9a-f]{8}) arena=([0-9a-f]{8}) size=(\d+) bytes=([0-9a-f]+)$', self.full.stdout, re.M)
        self.assertIsNotNone(m)
        at, release, get, set_, hits, arena = (int(m.group(k), 16) for k in range(1, 7))
        size, code = int(m.group(7)), bytes.fromhex(m.group(8))
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        md.detail = True
        insns = list(md.disasm(code, at))
        self.assertEqual(sum(i.size for i in insns), len(code))
        calls = [int(i.op_str, 16) for i in insns if i.mnemonic == 'call']
        self.assertEqual(calls, [get, release, set_])
        inc = next(i for i in insns if i.mnemonic == 'inc')
        self.assertEqual(inc.operands[0].mem.disp & 0xffffffff, hits)
        self.assertFalse(arena <= hits < arena + size)  # the written counter is outside the RX arena
        jmp = insns[-1]
        self.assertEqual(jmp.mnemonic, 'jmp')
        slot = jmp.operands[0].mem.disp & 0xffffffff
        self.assertTrue(arena <= slot < arena + size and slot % 4 == 0 and slot >= at + len(code))
        # The node is read once (the scene test) before the call; nothing reads or writes through ESI afterwards.
        call_release = next(k for k, i in enumerate(insns) if i.mnemonic == 'call' and int(i.op_str, 16) == release)
        for i in insns[call_release:]:
            for op in i.operands:
                self.assertFalse(op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_ESI, i.op_str)
        self.assertEqual(insns[0].mnemonic, 'pushfd')
        self.assertEqual(insns[-2].mnemonic, 'popfd')


class FlDustLeakFixVerifier(unittest.TestCase):
    def test_installed_exe_passes(self):
        if not EXE.exists():
            self.skipTest('no /mnt/c/X3FL/X3FL.exe')
        import verify_dust_leak_fix_site_fl as verifier
        report = verifier.verify(EXE)
        self.assertEqual(report['result'], 'PASS', report['failed'])
        self.assertEqual(report['raw_edges_to_site'], ['0x2095a', '0x20a19', '0x20a2f', '0x20a3e'])
        self.assertEqual(report['raw_edges_into_span'], [])
        self.assertEqual(sorted(report['never_claim']), ['0x20ac5', '0xe4908'])

    def test_parsers(self):
        import verify_dust_leak_fix_site_fl as verifier
        row = verifier.parse_log_line('dust_leak_fix site=00f20ac4 rva=020ac4 edition=fl status=patched reason=ok mode=on '
                                      'setting=on write=plain stub=02a300d0')
        self.assertEqual((row['site'], row['patched'], row['write'], row['stub']), (0xf20ac4, True, 'plain', 0x2a300d0))
        self.assertIsNone(verifier.parse_log_line('dust_leak_fix site=0041f4d1 status=patched reason=ok mode=on setting=on '
                                                  'write=atomic stub=00a300d0'))  # the AP row
        restore = verifier.parse_restore_line('dust_leak_fix_restore site=00f20ac4 rva=020ac4 edition=fl '
                                              'status=restore_retained found=e912345678 registered=1')
        self.assertEqual((restore['status'], restore['registered']), ('restore_retained', True))
        self.assertEqual(verifier.parse_hits_line('dust_leak_fix hits=1 total=9 frame=900 edition=fl'),
                         {'hits': 1, 'total': 9, 'frame': 900})


def strip(text):
    return re.sub(r'//.*', '', text)


class FlDustLeakFixWiring(unittest.TestCase):
    def test_edition_lists_capture_loader(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        linked = re.search(r'set\(X3M_AP_ENGINE_LINKED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('dust_leak_fix', linked)  # AP only: the fl target links its own provider
        self.assertIn('target_sources(d3d9 PRIVATE src/engine/fl/dust_leak_fix_fl.cpp)', cmake)
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        second = capture.rindex('dust_leak_fix::initialize();')
        fov_fl = capture.rindex('fov::initialize();')
        self.assertLess(fov_fl, second)
        self.assertIn('#if X3M_EDITION_FL', capture[capture.rindex('#if', 0, fov_fl):fov_fl])
        self.assertNotIn('#endif', capture[fov_fl:second])  # in the same FL block as fov
        report = capture.index('dust_leak_fix::report(ctx.frame);')
        self.assertNotIn('#if !X3M_EDITION_FL', capture[report - 400:report])  # both editions
        loader = strip((ROOT / 'src/proxy/loader.cpp').read_text())
        at = loader.index('x3m::dust_leak_fix::shutdown();')  # the AP call only: FL unload calls are AP only
        self.assertEqual(loader.count('x3m::dust_leak_fix::shutdown();'), 1)
        self.assertIn('#if !X3M_EDITION_FL', loader[loader.rindex('#if', 0, at) - 1:at])

    def test_provider_contract(self):
        code = strip(PROVIDER.read_text())
        self.assertNotIn('0x400000', code)
        for va in ('0x420ac4', '0x4a3300', '0x41f4d1', '0x487be0'):
            self.assertNotIn(va, code.lower())
        self.assertIn('x3m::fl::module_base()', code)
        self.assertIn('engine_patch::install_window_open()', code)
        order = ['F::plan(w, now, base)', '!pin_self()', 'emit_stub(base + F::release_rva, &stub, &slot)',
                 'engine_patch::claim(site_, spec)', 'engine_patch::store_pointer(slot, *site_.entry)',
                 'take_back("readback_mismatch")']
        positions = [code.index(n) for n in order]
        self.assertEqual(positions, sorted(positions))
        self.assertIn('"dust_leak_fix site=%08lx rva=%06lx edition=fl status=%s reason=%s mode=%s setting=%s write=%s '
                      'stub=%08lx"', code)
        self.assertIn('reason = "restore_retained";', code)
        header = strip(SITES.read_text())
        self.assertNotIn('windows.h', header)
        for rva in ('0x020ac4', '0x020950', '0x020ace', '0x0a3300', '0x0a56b0', '0x08a875', '0x1aa100', '0x020ac5',
                    '0x0e4908'):
            self.assertIn(rva, header)


if __name__ == '__main__':
    unittest.main()
