"""Host tests of the FL UI scale (src/engine/fl/ui_scale_fl.cpp, src/engine/fl/ui_scale_sites_fl.h).

Sites and ABI: docs/reverse-engineering/fl/ui-scale.md, "Verified 2026-11-03 against X3FL.exe" (verifier
verification/probe/verify_ui_scale_sites_fl.py). (a) The native fixture verification/probe/fl_ui_scale.cpp (MinGW i686,
run natively through WSL interop) lays the identity windows into a 64 KB-aligned private image (fixups relocated to that
base), installs with the production module and engine_patch, and runs the real span bytes of all eleven sites on the
vanilla image, at s = 1.5 (auto, 1440 lines), 1.25 (fixed), 2 (Reset to 2160) and at the identity cells (Reset to 1080),
against the models of ui_scale_sites.h / ui_scale_sites_fl.h, with every register, the outgoing stack words, DF and
(projection) the arithmetic flags, XMM1/XMM4/XMM7, the x87 CW, MXCSR and LastError as documented; the excluded camera
through a fake registry; every refusal row; one flipped byte per window; read-back / chain / restore faults (all or none,
patched_unverified when a rollback cannot complete); and, in a second run, the late-window refusal. (b) The emitted stubs
decoded with Capstone: the documented registers only, the documented memory writes, no call. (c) Edition wiring. (d) The
verifier on the installed X3FL.exe when present.
"""
from pathlib import Path
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / 'src/engine/fl/ui_scale_fl.cpp'
SITES = ROOT / 'src/engine/fl/ui_scale_sites_fl.h'
FIXTURE = ROOT / 'verification/probe/fl_ui_scale.cpp'
SHIM = ROOT / 'verification/probe/fl_ui_scale_shim.h'
VERIFIER = ROOT / 'verification/probe/verify_ui_scale_sites_fl.py'
EXE = Path('/mnt/c/X3FL/X3FL.exe')


def build(tmp):
    """Builds the fixture from the production sources (CMake per-file flags); None when the toolchain or interop is
    absent."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    tmp = Path(tmp)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_UI_SCALE_FL_FIXTURE', '-msse2', '-mfpmath=sse']
    units = (('src/engine/fl/ui_scale_fl.cpp', ['-fno-exceptions', '-include', str(SHIM), '-DX3M_UI_SCALE_FL_SHIM']),
             ('src/proxy/engine_patch.cpp', []), ('src/engine/fl/executable_identity_fl.cpp', []),
             ('src/proxy/engine_memory.cpp', ['-mno-sse', '-mno-mmx', '-mfpmath=387']))
    objects = []
    for source, flags in units:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_ui_scale.exe'
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


def source_text(path):
    return re.sub(r'\s+', '', path.read_text())


class FlUiScaleNativeFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-fl-ui-scale-')
        cls.exe = build(cls.tmp.name)
        cls.full = cls.late = cls.stubs = None
        if cls.exe:
            cls.full = subprocess.run([str(cls.exe)], text=True, capture_output=True, timeout=300)
            cls.late = subprocess.run([str(cls.exe), 'late'], text=True, capture_output=True, timeout=120)
            cls.stubs = subprocess.run([str(cls.exe), 'stubs'], text=True, capture_output=True, timeout=60)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        if self.exe is None:
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')

    def assert_pass(self, run, minimum):
        self.assertEqual(run.returncode, 0, run.stdout[-4000:] + run.stderr[-2000:])
        result = re.search(r'RESULT checks=(\d+) failures=(\d+)', run.stdout)
        self.assertIsNotNone(result, run.stdout[-2000:])
        self.assertEqual(int(result.group(2)), 0, run.stdout[-4000:])
        self.assertGreaterEqual(int(result.group(1)), minimum)
        self.assertNotIn('FAIL ', run.stdout)

    def test_full_run(self):
        self.assert_pass(self.full, 900)
        rows = [line[4:] for line in self.full.stdout.splitlines() if line.startswith('LOG ')]
        installs = [r for r in rows if r.startswith('ui_scale_install ') and 'status=patched ' in r]
        self.assertEqual(len(installs), 2)  # auto at 1440 lines and the fixed 1.25
        self.assertTrue(all(r.endswith('edition=fl') for r in rows if r.startswith('ui_scale')))

    def test_late_window(self):
        self.assert_pass(self.late, 4)
        self.assertIn('status=refused reason=late_claim', self.late.stdout)

    def stub(self, name):
        for line in self.stubs.stdout.splitlines():
            parts = line.split()
            if len(parts) == 4 and parts[0] == 'STUB' and parts[1] == name:
                self.assertEqual(parts[2], '1', f'{name}: encoder length differs from its constant')
                return decode(parts[3])
        self.fail(f'no stub {name}')

    def registers_and_writes(self, insns):
        import capstone
        from capstone import x86
        written, mem_writes, mnemonics = set(), [], []
        for i in insns:
            mnemonics.append(i.mnemonic)
            if i.mnemonic in ('push', 'pushfd'):
                mem_writes.append((i.mnemonic, 'esp', 0))  # the implicit store below ESP
            _, regs_written = i.regs_access()
            written |= {i.reg_name(r) for r in regs_written}
            for op in i.operands:
                # Capstone does not flag stmxcsr's operand as written.
                if op.type == x86.X86_OP_MEM and ((op.access & capstone.CS_AC_WRITE) or i.mnemonic == 'stmxcsr'):
                    base = i.reg_name(op.mem.base) if op.mem.base else None
                    mem_writes.append((i.mnemonic, base, op.mem.disp & 0xffffffff))
        return written, mem_writes, mnemonics

    def test_stubs_decode(self):
        expect = {  # registers written (besides ESP/EFLAGS/EIP) and memory writes (base register, displacement)
            'size4': ({'eax'}, {('esp', 0)}),
            'size6': ({'eax'}, {('esp', 0)}),
            'delta': ({'eax', 'edx'}, {(None, 0x30303030), (None, 0x30303034), ('esp', 8), ('esp', 12)}),
            'cursor': ({'eax'}, set()),
            'overlay': ({'ecx'}, {('esp', 0)}),
            'aim': ({'edx', 'edi'}, set()),
            'map_label': ({'eax', 'edx'}, {('esp', 0)}),
            'map_pick': ({'ecx'}, {('esp', 0), ('esp', 8), ('esp', 12)}),
            'projection': ({'ecx', 'xmm1'}, {('esp', 0), ('esi', 0), ('esi', 0x14), ('esi', 0x30), ('esi', 0x34),
                                              (None, 0x70707070), (None, 0x70707074), ('esp', 0x10)}),
        }
        for name, (registers, writes) in expect.items():
            insns = self.stub(name)
            written, mem_writes, mnemonics = self.registers_and_writes(insns)
            written -= {'esp', 'eflags', 'eip'}
            self.assertEqual(written, registers, name)
            self.assertEqual({(b, d) for _, b, d in mem_writes}, writes, name)
            self.assertNotIn('call', mnemonics, name)
            self.assertFalse([m for m in mnemonics if m.startswith('f')], name)  # no x87
            self.assertEqual((insns[-1].mnemonic, insns[-1].op_str), ('jmp', 'dword ptr [0x40404040]'), name)
        # The projection stub keeps ECX, EFLAGS, XMM1 and MXCSR around the arithmetic: pushfd/push ecx first, popfd last.
        proj = self.stub('projection')
        self.assertEqual([i.mnemonic for i in proj[:5]], ['pushfd', 'push', 'sub', 'movups', 'stmxcsr'])
        self.assertEqual([i.mnemonic for i in proj[-6:]], ['ldmxcsr', 'movups', 'add', 'pop', 'popfd', 'jmp'])
        self.assertEqual(proj[5].op_str, 'ebx, dword ptr [0x60606060]')  # the camera against the excluded cell


class FlUiScaleWiring(unittest.TestCase):
    def test_edition_wiring(self):
        cmake = source_text(ROOT / 'CMakeLists.txt')
        self.assertIn('target_sources(d3d9PRIVATEsrc/engine/fl/ui_scale_fl.cpp)', cmake)
        self.assertIn('set_source_files_properties(src/engine/fl/ui_scale_fl.cppPROPERTIESCOMPILE_OPTIONS"-fno-exceptions")', cmake)
        loader = (ROOT / 'src/proxy/loader.cpp').read_text()
        fl_block = loader[loader.index('#if X3M_EDITION_FL\n    // FL edition: the X3FL.exe identity row'):]
        fl_block = fl_block[:fl_block.index('#endif')]
        self.assertIn('x3m::ui_scale::initialize();', fl_block)
        self.assertIn('x3m::text_density::initialize();', fl_block)  # the FL text density follows (test_fl_text_density)
        self.assertLess(fl_block.index('x3m::chase_fire::initialize();'), fl_block.index('x3m::ui_scale::initialize();'))
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        flat = source_text(ROOT / 'src/proxy/capture.cpp')
        # One call per edition: the AP block and the FL #else branch.
        self.assertEqual(flat.count('ui_scale::present(ctx.frame);'), 2)
        self.assertEqual(flat.count('ui_scale::after_reset(p?p->BackBufferWidth:0u,p?p->BackBufferHeight:0u);'), 2)
        self.assertEqual(flat.count('ui_scale::device_created(p->BackBufferWidth,p->BackBufferHeight);'), 2)
        self.assertLess(capture.index('fov::present(ctx.frame); // FL'), capture.rindex('ui_scale::present(ctx.frame);'))
        self.assertLess(capture.index('cull_small_parts::set_backbuffer_width(p->BackBufferWidth); // FL provider'),
                        capture.rindex('ui_scale::device_created('))
        module = source_text(PROVIDER)
        for needle in ('L"X3M_UI_SCALE"', 'x3m::fl::module_base()', 'install_window_open()', '"late_claim"', '"arena_full"',
                       '"chain_failed"', '"readback_failed"', '"rollback_failed"', '"pin_failed"', '"off_auto"',
                       'GET_MODULE_HANDLE_EX_FLAG_PIN', 'set_identity();', 'restore_all();', 'SetLastError(error);',
                       'edition=fl', 'windows_refused(base_)', 'attached!=scene'):
            self.assertIn(needle, module)
        header = source_text(SITES)
        self.assertNotIn('windows.h', header)
        order = [header.index(f'"ui_scale_{n}_fl"') for n in ('width', 'height', 'delta_direct', 'delta_mapped', 'cursor_x',
                                                                'cursor_y', 'overlay_icon', 'cursor_aim', 'map_label',
                                                                'map_pick', 'projection')]
        self.assertEqual(order, sorted(order))  # the projection claimed last

    def test_schema_wording(self):
        schema = (ROOT / 'tools/config/schema.py').read_text()
        entry = schema[schema.index("entry('ui_scale'"):schema.index("entry('text_density'")]
        self.assertNotIn('Albion Prelude only', entry)  # both editions: no edition note (convention 2026-11-04)


@unittest.skipUnless(EXE.is_file(), 'installed X3FL.exe not present')
class FlUiScaleVerifier(unittest.TestCase):
    def test_installed_executable(self):
        run = subprocess.run([sys.executable, str(VERIFIER), '--exe', str(EXE), '--source', '--json'], capture_output=True,
                             text=True, timeout=900, cwd=ROOT / 'verification/probe')
        self.assertEqual(run.returncode, 0, run.stdout[-3000:] + run.stderr[-2000:])
        report = json.loads(run.stdout)
        self.assertEqual(report['result'], 'PASS')
        self.assertFalse([k for k, v in report['checks'].items() if not v['ok']])


if __name__ == '__main__':
    unittest.main()
