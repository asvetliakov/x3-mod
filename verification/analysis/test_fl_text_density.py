"""Host tests of the FL text density (src/engine/fl/text_density_fl.cpp, src/engine/fl/text_density_sites_fl.h).

Sites and ABI: docs/reverse-engineering/fl/text-density.md, "Verified 2026-11-03 against X3FL.exe" (verifier
verification/probe/verify_text_density_sites_fl.py). (a) The native fixture verification/probe/fl_text_density.cpp (MinGW
i686, run natively through WSL interop) lays the 30 identity windows into a 64 KB-aligned private image (fixups relocated
to that base), installs with the production module and engine_patch, and runs the claimed spans against the vanilla
spans and the documented outputs: every GPR, EFLAGS (DF included), ESP, the stack words, XMM0-7, the x87 image, MXCSR and
LastError; the font request scaled; the blits drawn from the d x shadow through the FL helper ABIs (or left to the engine:
flagged source, unflagged destination, the blit recorder on); KC's image block (N = 1, N = d, clamped); the Materials rows
of a synthetic FL table edited per the plan with AP-layout decoys and cfg+0x784 untouched; cfg+0x774, the four cockpit
bytes and the style byte with the write-order witness (style after the field, restored before it; the texture-limit fall
from 3 to 2); refusals (settings, identity, every window byte, the order witness, a missing font, -L081, density 1, late
window); read-back/chain faults in the claims group and the edits group roll everything back. (b) Edition wiring and the
thunk envelope. (c) The header's windows and the verifier on the installed X3FL.exe when present. (d) `manage.py
density-fonts --edition fl` on a temporary game directory.
"""
import contextlib
import hashlib
import io
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / 'src/engine/fl/text_density_fl.cpp'
SITES = ROOT / 'src/engine/fl/text_density_sites_fl.h'
FIXTURE = ROOT / 'verification/probe/fl_text_density.cpp'
SHIM = ROOT / 'verification/probe/fl_text_density_shim.h'
VERIFIER = ROOT / 'verification/probe/verify_text_density_sites_fl.py'
EMIT = ROOT / 'verification/results/fl-text-density/emit_header_windows.py'
EXE = Path('/mnt/c/X3FL/X3FL.exe')


def build(tmp):
    """Builds the fixture from the production sources (CMake per-file flags) into `tmp` (the fixture writes its f\\ font
    files beside itself); None when the toolchain or interop is absent."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    tmp = Path(tmp)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_TEXT_DENSITY_FL_FIXTURE', '-msse2', '-mfpmath=sse']
    units = (('src/engine/fl/text_density_fl.cpp', ['-fno-exceptions', '-include', str(SHIM), '-DX3M_TEXT_DENSITY_FL_SHIM']),
             ('src/proxy/engine_patch.cpp', []), ('src/engine/fl/executable_identity_fl.cpp', []),
             ('src/proxy/engine_memory.cpp', ['-mno-sse', '-mno-mmx', '-mfpmath=387']))
    objects = []
    for source, flags in units:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_text_density.exe'
    subprocess.run([compiler, *common, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    return exe


def source_text(path):
    return re.sub(r'\s+', '', path.read_text())


class FlTextDensityNativeFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-fl-text-density-')
        cls.exe = build(cls.tmp.name)
        cls.full = cls.late = None
        if cls.exe:
            cls.full = subprocess.run([str(cls.exe)], text=True, capture_output=True, timeout=600)
            cls.late = subprocess.run([str(cls.exe), 'late'], text=True, capture_output=True, timeout=300)

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
        self.assert_pass(self.full, 260)
        rows = [line[4:] for line in self.full.stdout.splitlines() if line.startswith('LOG ')]
        self.assertTrue(rows and all(r.endswith('edition=fl') for r in rows if r.startswith('text_density')))
        installs = [r for r in rows if r.startswith('text_density_install ') and 'status=patched ' in r]
        self.assertEqual(len(installs), 5)  # auto d=2, fixed 3, fixed 3 under the texture limits (twice), -L081 with UmePlusCLP46
        self.assertTrue(any('reason=density_1' in r and 'image_claim=retained_inert' in r for r in rows))
        self.assertTrue(any(r.startswith('text_density_recorder first_pass frame=77') for r in rows))
        self.assertTrue(any('reason=umeplusclp_missing' in r and 'missing=UmePlusCLP46' in r for r in rows))
        self.assertTrue(any(r.startswith('text_density_rows status=applied') and 'caps_limited_d=2' in r for r in rows))

    def test_late_window(self):
        self.assert_pass(self.late, 40)
        self.assertIn('status=refused reason=late_claim', self.late.stdout)


class FlTextDensityWiring(unittest.TestCase):
    def test_edition_wiring(self):
        cmake = source_text(ROOT / 'CMakeLists.txt')
        self.assertIn('target_sources(d3d9PRIVATEsrc/engine/fl/text_density_fl.cpp)', cmake)
        self.assertIn('set_source_files_properties(src/engine/fl/text_density_fl.cppPROPERTIESCOMPILE_OPTIONS"-fno-exceptions")', cmake)
        linked = cmake[cmake.index('set(X3M_AP_ENGINE_LINKED_SOURCES'):]
        self.assertNotIn('src/proxy/text_density.cpp', linked[:linked.index(')')])  # AP only now
        loader = (ROOT / 'src/proxy/loader.cpp').read_text()
        fl_block = loader[loader.index('#if X3M_EDITION_FL\n    // FL edition: the X3FL.exe identity row'):]
        fl_block = fl_block[:fl_block.index('#endif')]
        self.assertNotIn('text_density installed=0', fl_block)
        self.assertLess(fl_block.index('x3m::ui_scale::initialize();'), fl_block.index('x3m::text_density::initialize();'))
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        flat = source_text(ROOT / 'src/proxy/capture.cpp')
        for call in ('text_density::present(ctx.frame);', 'text_density::before_reset();',
                     'text_density::device_created(ui_scale::scale(),out?*out:nullptr);'):
            self.assertEqual(flat.count(call), 2, call)  # the AP block and the FL #else branch
        fl_create = capture[capture.index('cull_small_parts::set_backbuffer_width(p->BackBufferWidth); // FL provider'):]
        self.assertLess(fl_create.index('ui_scale::device_created('), fl_create.index('text_density::device_created('))
        self.assertLess(fl_create.index('text_density::device_created('), fl_create.index('#endif'))
        module = source_text(PROVIDER)
        for needle in ('L"X3M_TEXT_DENSITY"', 'x3m::fl::module_base()', 'install_window_open()', '"late_claim"', '"late_window"',
                       '"chain_failed"', '"readback_failed"', '"rollback_failed"', '"pin_failed"', '"density_1"', '"fonts_missing"',
                       '"umeplusclp_missing"', '"config_unwritable"', '"cockpit_failed"', '"cockpit_readback_failed"', '"style_failed"',
                       '"style_order"', '"objects_exist"', '"tables_unreadable"', '"caps"', 'GET_MODULE_HANDLE_EX_FLAG_PIN',
                       'SetLastError(error);', 'edition=fl', 'windows_refused(base)', 'before_vm_and_materials()',
                       'engine_patch::claim_call(materials_site_,at,base_+F::materials_callee_rva,',
                       'conststd::uint32_trecorder=', 'sites::plan_row(*row,clear_nofiltering)', 'sites::plan_image_blit(N,density_,',
                       'sites::clip_destination(object_width(dst_object)/d,object_height(dst_object)/d,',
                       # the order rule: the style byte only while the field holds 3, put back first
                       'if(!read_config_field(&field)||field!=3)', 'constboolstyle=restore_style();constboolcockpit=restore_cockpit();if(!owned_[F::Style])restore_config();',
                       # the image block is never rewritten once the order witness lapses (kept in place, inert)
                       'if(i==F::BltImage&&!before_vm_and_materials()){image_retained_=true;continue;}', 'image_claim=%s',
                       'text_density_recorderfirst_passframe=%llurecorded_passes=%uedition=fl'):
            self.assertIn(needle, module, needle)
        text = PROVIDER.read_text()
        # The envelope: EFLAGS and GPRs, then XMM0-7, the FNSAVE image and MXCSR, restored in reverse; DF cleared.
        enter = text[text.index('.macro X3M_FL_TD_ENTER'):text.index('.endm')]
        for i in range(8):
            self.assertIn(f'movdqa xmmword ptr [esp+{16 * i}], xmm{i}' if i else 'movdqa xmmword ptr [esp], xmm0', enter)
        for needle in ('pushfd\n    pushad\n    mov ebx, esp', 'fnsave [esp+128]', 'stmxcsr dword ptr [esp+240]', 'cld'):
            self.assertIn(needle, enter)
        leave = text[text.index('.macro X3M_FL_TD_LEAVE'):]
        leave = leave[:leave.index('.endm')]
        for needle in ('ldmxcsr dword ptr [esp+240]', 'frstor [esp+128]', 'movdqa xmm7, xmmword ptr [esp+112]', 'mov esp, ebx'):
            self.assertIn(needle, leave)
        # The handled paths: blit `ret` after popfd; the image skip reserves the 0x20 bytes with `lea` (flags kept).
        self.assertIn('mov dword ptr [esp+28], eax\n    popad\n    popfd\n    ret', text)
        self.assertIn('popad\n    popfd\n    lea esp, [esp-32]\n    jmp dword ptr [_x3m_fl_text_density_blt_image_return]', text)
        for forbidden in ('new ', 'malloc', 'std::string', 'std::vector'):
            self.assertNotIn(forbidden, text)
        header = SITES.read_text()
        self.assertNotIn('windows.h', header)
        order = [header.index(f'"text_density_{n}_fl"') for n in ('font', 'blt_block', 'blt_alpha', 'blt_image', 'text_line', 'rect_fill')]
        self.assertEqual(order, sorted(order))

    def test_schema_wording(self):
        schema = (ROOT / 'tools/config/schema.py').read_text()
        ui = schema[schema.index("entry('ui_scale'"):schema.index("entry('text_density'")]
        self.assertNotIn('Albion Prelude only', ui)
        density = schema[schema.index("entry('text_density'"):schema.index("entry('chase_pitch_down_deg'")]
        self.assertIn('X3FL.exe', density)
        self.assertIn('density-fonts', density)
        ini = (ROOT / 'assets/x3m.ini').read_text()
        self.assertNotIn('Albion Prelude only', ini[ini.index('; Size of the in-game menus'):ini.index(';text_density = auto')])


@unittest.skipUnless(EXE.is_file(), 'installed X3FL.exe not present')
class FlTextDensityExecutable(unittest.TestCase):
    def test_header_windows_and_verifier(self):
        run = subprocess.run([sys.executable, '-I', str(EMIT), '--exe', str(EXE), '--check'], capture_output=True, text=True, timeout=300)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr[-2000:])
        self.assertIn('header windows match (30 windows', run.stdout)
        run = subprocess.run([sys.executable, str(VERIFIER), '--exe', str(EXE)], capture_output=True, text=True, timeout=900)
        self.assertEqual(run.returncode, 0, run.stdout[-3000:] + run.stderr[-2000:])
        self.assertEqual(json.loads(run.stdout)['result'], 'PASS')


def load_manage():
    import importlib.util
    spec = importlib.util.spec_from_file_location('manage_fl_fonts', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FlDensityFontsCommand(unittest.TestCase):
    NAMES = ['Tahoma26.abc', 'Tahoma26.tga', 'Zekton52.abc', 'Zekton52.tga']

    def run_command(self, module, *argv, running=False):
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(module, '_image_running', return_value=running), contextlib.redirect_stdout(output), \
                contextlib.redirect_stderr(error):
            code = module.density_fonts_command(list(argv))
        return code, output.getvalue(), error.getvalue()

    def test_fl_install_check_remove(self):
        module = load_manage()
        with tempfile.TemporaryDirectory() as directory:
            fonts = Path(directory) / 'F'
            fonts.mkdir()
            for name in self.NAMES:
                (fonts / name).write_bytes(b'font:' + name.encode())
            game = Path(directory) / 'X3FL'
            game.mkdir()
            common = ('--game-dir', str(game), '--fonts-dir', str(fonts))
            code, _, error = self.run_command(module, '--edition', 'fl', *common, '--install')
            self.assertEqual(code, 2)
            self.assertIn('X3FL.exe not found', error)
            (game / 'X3FL.exe').write_bytes(b'not the real executable')
            code, _, error = self.run_command(module, '--edition', 'fl', *common, '--install', running=True)
            self.assertEqual(code, 2)
            self.assertIn('X3FL.exe is running', error)
            code, out, _ = self.run_command(module, '--edition', 'fl', *common, '--check')
            self.assertEqual((code, json.loads(out)['missing']), (1, self.NAMES))
            code, out, _ = self.run_command(module, '--edition', 'fl', *common, '--install')
            self.assertEqual(code, 0)
            report = json.loads(out)
            self.assertEqual((report['edition'], report['copied'], report['retained']), ('fl', self.NAMES, []))
            self.assertEqual(report['executable'], {'name': 'X3FL.exe', 'sha256': hashlib.sha256(b'not the real executable').hexdigest(),
                                                    'bytes': 23, 'known': False})
            manifest = json.loads((game / 'f' / module.FONTS_MANIFEST).read_text())
            self.assertEqual((manifest['edition'], manifest['executable']['name'], sorted(manifest['files'])), ('fl', 'X3FL.exe', self.NAMES))
            self.assertEqual((game / 'f' / 'Tahoma26.tga').read_bytes(), b'font:Tahoma26.tga')
            code, out, _ = self.run_command(module, '--edition', 'fl', *common)
            self.assertEqual((code, json.loads(out)['owned'], json.loads(out)['missing']), (0, self.NAMES, []))
            # An AP directory refuses the FL edition and the reverse.
            code, _, error = self.run_command(module, '--edition', 'ap', *common, '--install')
            self.assertEqual(code, 2)
            self.assertIn('X3AP.exe not found', error)
            # The game running, or a process list that cannot be read: --remove and --install refuse, nothing changes.
            for running, message in ((True, 'X3FL.exe is running'), (None, 'process list is unreadable')):
                for action in ('--remove', '--install'):
                    code, _, error = self.run_command(module, '--edition', 'fl', *common, action, running=running)
                    self.assertEqual(code, 2, (running, action))
                    self.assertIn(message, error)
            self.assertEqual(sorted(p.name for p in (game / 'f').iterdir()), sorted(self.NAMES + [module.FONTS_MANIFEST]))
            code, out, _ = self.run_command(module, '--edition', 'fl', *common, '--remove')
            self.assertEqual((code, json.loads(out)['removed']), (0, self.NAMES))
            self.assertEqual(list((game / 'f').iterdir()), [])
            # The committed set is the 16 AP files (FL asks the same four families at d = 2 and 3).
            self.assertEqual(len(module.density_font_files(ROOT / 'assets/fonts/generated/F')), 16)


if __name__ == '__main__':
    unittest.main()
