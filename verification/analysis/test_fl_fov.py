"""Host tests of the FL field of view (src/engine/fl/fov_fl.cpp, src/engine/fl/fov_sites_fl.h).

Sites and ABI: docs/reverse-engineering/fl/field-of-view.md, "Verified 2026-11-02 against X3FL.exe" (verifier
verification/probe/verify_fov_site_fl.py). (a) The native fixture verification/probe/fl_fov.cpp (MinGW i686, run natively
through WSL interop) lays the six FL windows into a 64 KB-aligned private image (fixups relocated to that base),
installs with the production module and engine_patch, and runs the real INS_SetFocus case and load-store bytes: every
vanilla (N << 16) / 360, N 0..180, the table values, vanilla + 1 and boundary values through both the unpatched and the
patched image, with EAX/EBX/ECX/EDX/ESI/EDI/EBP/ESP as documented, the outgoing stack words, DF and the other non-
arithmetic flags, XMM7, the x87 control word, MXCSR and LastError kept; the literal written (plain) and restored; every
single-bit window change refused before anything is emitted; read-back / chain / restore faults (all three or none,
registered when a rollback cannot complete); initialize()'s rows and refusals; current_focus()/present() through a slot;
the late-window refusal; and a second run where the window closes with the sites live (jumps restored, the crossing
literal retained). (b) The two emitted stubs decoded with Capstone: the documented registers only, no memory write, no
stack operation, no call. (c) Edition wiring.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / 'src/engine/fl/fov_fl.cpp'
SITES = ROOT / 'src/engine/fl/fov_sites_fl.h'
FIXTURE = ROOT / 'verification/probe/fl_fov.cpp'
SHIM = ROOT / 'verification/probe/fl_fov_shim.h'


def build(tmp):
    """Builds the fixture from the production sources (CMake per-file flags); None when the toolchain or interop is
    absent."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    tmp = Path(tmp)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_FOV_FL_FIXTURE', '-msse2', '-mfpmath=sse']
    units = (('src/engine/fl/fov_fl.cpp', ['-fno-exceptions', '-include', str(SHIM), '-DX3M_FOV_FL_SHIM']),
             ('src/proxy/engine_patch.cpp', []), ('src/engine/fl/executable_identity_fl.cpp', []),
             ('src/proxy/engine_memory.cpp', ['-mno-sse', '-mno-mmx', '-mfpmath=387']))
    objects = []
    for source, flags in units:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_fov.exe'
    subprocess.run([compiler, *common, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    return exe


def decode(hex_bytes):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    data = bytes.fromhex(hex_bytes)
    out = list(md.disasm(data, 0x10000000))
    assert sum(i.size for i in out) == len(data), 'stub does not decode to its full length'
    return out


class FlFovNativeFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-fl-fov-')
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
        self.assert_pass(self.full, 45)
        out = self.full.stdout
        self.assertRegex(out, r'WINDOWS constructor_mismatch=3edcd964 setfocus_mismatch=234b68a8 callee_mismatch=d3cdd4e6 '
                              r'load_mismatch=a2a1cf6b load_caller_mismatch=4130196b reader_mismatch=5f1bce8a ')
        self.assertIn('FLIPS windows=6 bits=1360 refused=1360 fixup=96 fixup_refused=96', out)
        self.assertIn('VECTORS setfocus=196 remapped=86 bad=0 load=358 remapped=83 bad=0', out)
        self.assertIn('FAULTS taken=9', out)
        self.assertIn('SAMPLES interval=64 frames=2817 rows=16', out)  # one sample per 64 Presents, 16-row cap
        rows = re.findall(r'^LOG (fov .*)$', out, re.M)
        self.assertTrue(all(' edition=fl ' in r for r in rows), rows)
        self.assertEqual(sum('status=patched reason=ok' in r and 'write=plain' in r and 'setfocus_write=atomic' in r
                             and 'load_write=atomic' in r for r in rows), 2)
        self.assertIn('status=refused reason=late_claim', rows[-1])

    def test_retain_run(self):
        self.assert_pass(self.retain, 5)
        self.assertRegex(self.retain.stdout, r'RETAIN literal=00003470')

    def test_emitted_stubs(self):
        out = self.full.stdout
        stubs = dict(re.findall(r'^STUB (setfocus|load)=([0-9a-f]+)$', out, re.M))
        self.assertEqual(set(stubs), {'setfocus', 'load'})
        import capstone
        import capstone.x86 as x86
        allowed = {'setfocus': {'eax', 'ecx', 'eflags'}, 'load': {'ecx', 'edx', 'eflags'}}
        for name, code in stubs.items():
            insns = decode(code)
            mnemonics = [i.mnemonic for i in insns]
            self.assertEqual(mnemonics[-1], 'jmp', name)
            written = set()
            for i in insns:
                self.assertNotIn(i.mnemonic, ('call', 'push', 'pop', 'pushfd', 'popfd', 'mov', 'ret'), (name, i.mnemonic))
                _, regs_write = i.regs_access()
                written |= {i.reg_name(r) for r in regs_write}
                for op in i.operands:  # memory only as a source (cmp / movzx reads, the final jmp's slot)
                    if op.type == x86.X86_OP_MEM:
                        self.assertTrue(op.access & capstone.CS_AC_READ and not op.access & capstone.CS_AC_WRITE,
                                        (name, i.mnemonic, i.op_str))
            self.assertEqual(written, allowed[name], name)
            self.assertEqual(len(insns), 9 if name == 'setfocus' else 11, (name, mnemonics))


def strip(text):
    return re.sub(r'//.*', '', text)


class FlFovWiring(unittest.TestCase):
    def test_edition_lists_and_capture(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        linked = re.search(r'set\(X3M_AP_ENGINE_LINKED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        ap = re.search(r'set\(X3M_AP_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('fov.cpp', linked)  # AP only: the fl target links its own provider
        self.assertIn('src/proxy/fov.cpp', ap)
        self.assertIn('target_sources(d3d9 PRIVATE src/engine/fl/fov_fl.cpp)', cmake)
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        second = capture.rindex('fov::initialize();')
        self.assertIn('#if X3M_EDITION_FL', capture[second - 300:second])
        self.assertLess(capture.index('run_in_background::initialize();'), second)
        self.assertLess(second, capture.index('own_sun_hook::initialize();'))
        present = capture.rindex('fov::present(ctx.frame);')
        self.assertIn('#else', capture[present - 60:present])

    def test_provider_contract(self):
        code = strip(PROVIDER.read_text())
        self.assertNotIn('0x400000', code)
        for va in ('0x41e08d', '0x430eaa', '0x41df09', '0x5a82e4', '0x608504', '0x0041c9dc', '0x0042dbf8'):
            self.assertNotIn(va, code.lower())
        self.assertIn('x3m::fl::module_base()', code)
        self.assertIn('engine_patch::install_window_open()', code)
        self.assertIn('x3m::fl::sector_background_fl::admitted()', code)
        self.assertNotIn('pin_self', code)  # the stubs reach only arena memory
        # Windows before anything is emitted; the stubs before any write; literal, INS_SetFocus, load.
        order = ['F::plan(w, now, base)', 'emit_setfocus_stub(&setfocus_slot, &table)', 'emit_load_stub(table, &load_slot)',
                 'F::install_literal(', '"fov_setfocus_fl"', '"fov_load_fl"']
        positions = [code.index(n) for n in order]
        self.assertEqual(positions, sorted(positions))
        self.assertIn('"fov site=%08lx rva=%06lx edition=fl status=%s reason=%s value=0x%04lx vertical_deg=%.2f setting=%s '
                      'write=%s registry=none setfocus=%s setfocus_write=%s load=%s load_write=%s"', code)
        self.assertIn('reason = "literal_retained";', code)
        header = strip(SITES.read_text())
        self.assertNotIn('windows.h', header)
        for rva in ('0x01e076', '0x01e08d', '0x030e9f', '0x030eaa', '0x030eaf', '0x0c1940', '0x01dedc', '0x01df09',
                    '0x01df0e', '0x020db6', '0x022a1c', '0x1a82e4', '0x1aa174'):
            self.assertIn(rva, header)


if __name__ == '__main__':
    unittest.main()
