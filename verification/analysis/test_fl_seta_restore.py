"""Host tests of the FL SETA restore and key latch (src/engine/fl/seta_restore_fl.cpp, seta_restore_sites_fl.h, the FL
consumer path of src/proxy/chase_transition.cpp; docs/reverse-engineering/fl/seta.md, "Port (2026-11-03)").

(a) The native fixture verification/probe/fl_seta_restore.cpp (MinGW i686, the project's code flags, run natively
through WSL interop) over the installed X3FL.exe mapped and relocated into a private image and a KC VM image of the
local FL x3story.obj (built into a temporary file by story_vm_image.py; never kept): Ready-provider and window refusals
before any write, the attach and read-back rollbacks, the factor hook's CPU contract against the live span, the real FL
story recognized, the gate ticket through defer/Ready/two frames/remote view/selector-32 StartFastForward (accepted
without a factor write), AL 0, five cancels, the player re-bind, the key latch (held + tail, nested fire, ramp guard,
new press, VM release, escaped dispatch, AL 0) and the late window. Skipped without the compiler, WSL interop, the exe or
the story.
(b) The verifier against the installed X3FL.exe (also compares the header's window rows).
(c) Wiring: capture.cpp order, CMake, schema, the chase_transition consumer admitted on FL with ECX and selector 32, no AP
address in the FL module.
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
SITES = ROOT / 'src/engine/fl/seta_restore_sites_fl.h'
MODULE = ROOT / 'src/engine/fl/seta_restore_fl.cpp'
FIXTURE = ROOT / 'verification/probe/fl_seta_restore.cpp'
EXE = Path('/mnt/c/X3FL/X3FL.exe')
STORY_CAT, STORY_MEMBER = Path('/mnt/c/X3FL/addon2/03.cat'), 'addon2/L/x3story.obj'
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
         '-mincoming-stack-boundary=2', '-msse2', '-mfpmath=sse', '-DX3M_EDITION_FL=1']
sys.path.insert(0, str(ROOT / 'verification/probe'))
sys.path.insert(0, str(ROOT / 'verification/results/ship-navigation-ai/gate-queue'))
sys.path.insert(0, str(ROOT / 'tools/analysis'))


def compiler():
    return shutil.which('i686-w64-mingw32-g++')


def strip(text):
    return re.sub(r'//.*', '', text)


ORDER_MODES = ('chase_only', 'chase_first', 'seta_first')


def run_fixture(tmp, modes=('',)):
    """Builds the fixture in `tmp` and runs it natively on the installed X3FL.exe and a temporary VM image of the local
    FL x3story.obj (build 1636047691), once per mode ('' = the full run, else an init-order mode): {mode:
    CompletedProcess}, or None when the toolchain, interop, exe or story is absent."""
    if not compiler() or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    if not EXE.is_file() or not STORY_CAT.is_file():
        return None
    from kc_obj import Obj, load_member
    import story_vm_image
    raw = load_member(STORY_CAT, STORY_MEMBER)
    if Obj(raw).build != 1636047691:
        raise AssertionError(f'unexpected FL story build {Obj(raw).build}')
    tmp = Path(tmp)
    image = tmp / 'vm.bin'  # script bytes: temporary only
    image.write_bytes(story_vm_image.build_image(raw))
    objects = []
    for name, source, extra in (
            ('engine_memory.o', ROOT / 'src/proxy/engine_memory.cpp', ['-mno-sse', '-mno-mmx', '-mfpmath=387']),
            ('engine_patch.o', ROOT / 'src/proxy/engine_patch.cpp', []),
            ('seta_restore.o', MODULE, ['-fno-exceptions', '-DX3M_SETA_RESTORE_FL_FIXTURE']),
            ('fixture.o', FIXTURE, ['-fno-exceptions', '-DX3M_CHASE_TRANSITION_FL_FIXTURE'])):
        subprocess.run([compiler(), *FLAGS, *extra, '-c', str(source), '-o', str(tmp / name)], check=True)
        objects.append(str(tmp / name))
    exe = tmp / 'fl_seta_restore.exe'
    subprocess.run([compiler(), *FLAGS, '-static', *objects, '-o', str(exe)], check=True)
    win = [subprocess.run(['wslpath', '-w', str(p)], capture_output=True, text=True, check=True).stdout.strip()
           for p in (EXE, image)]
    return {m: subprocess.run([str(exe), *win, *([m] if m else [])], text=True, capture_output=True, timeout=300)
            for m in modes}


class FlSetaRestoreFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with tempfile.TemporaryDirectory(prefix='x3-fl-seta-') as tmp:
            cls.runs = run_fixture(tmp, ('',) + ORDER_MODES)

    def setUp(self):
        if self.runs is None:
            self.skipTest('needs i686-w64-mingw32-g++, WSL interop, the installed X3FL.exe and addon2/03.cat')

    def test_init_orders_leave_the_chase_family_unchanged(self):
        chase_rows = {}
        for mode in ORDER_MODES:
            run = self.runs[mode]
            out = run.stdout
            self.assertEqual(run.returncode, 0, mode + '\n' + '\n'.join(l for l in out.splitlines() if l.startswith('FAIL')))
            self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
            self.assertIn('CHASE_PLAN_BEFORE_CHASE plan=none window=26', out, mode)
            chase_rows[mode] = [l for l in out.splitlines() if l.startswith('CHASE_ROW ')]
        # the chase family's install rows (addresses masked) are those of the chase family alone, whatever the order
        self.assertEqual(chase_rows['chase_first'], chase_rows['chase_only'])
        self.assertEqual(chase_rows['seta_first'], chase_rows['chase_only'])
        rows = '\n'.join(chase_rows['chase_only'])
        self.assertIn('CHASE_ROW chase_transition installed=1 status=active edition=fl ', rows)
        self.assertIn('CHASE_ROW chase_view_restore installed=1 requested=1 status=ok edition=fl ', rows)
        self.assertEqual(len(chase_rows['chase_only']), 14)  # two install rows, eight witness/observer and four restore sites
        self.assertRegex(self.runs['seta_first'].stdout, r'(?m)^LOG seta_restore refused reason=chase_pending edition=fl .* '
                                                         r'hook_status=unclaimed .* inject=unclaimed patched=0')
        for mode in ('chase_first', 'seta_first'):
            self.assertRegex(self.runs[mode].stdout, r'(?m)^LOG seta_restore installed=1 edition=fl .* inject=ok ')

    def test_native_fixture(self):
        run = self.runs['']
        out = run.stdout
        self.assertEqual(run.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith(('FAIL', '  ['))))
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+)', out).group(1)), 200)
        self.assertIn('FLIPS windows=22 flips=36 refused=36', out)
        self.assertRegex(out, r'(?m)^LOG seta_restore installed=1 edition=fl base=0x[0-9a-f]{4}0000 hook=0x[0-9a-f]{8} '
                              r'hook_rva=0x0ce09e write=plain .* inject_rva=0x0cfd00 inject=ok call=0x[0-9a-f]{8} '
                              r'call_rva=0x0bb500 selector=32 key=0x96 method=StartFastForward ready=ready ')
        self.assertRegex(out, r'(?m)^LOG seta_restore armed edition=fl .* ip=0x18d39 ret=0x199d6 ret_name=gate .* '
                              r'build=1636047691 autopilot=1 ')
        self.assertRegex(out, r'(?m)^LOG seta_restore restored edition=fl .* result=1 .* method=StartFastForward ')
        self.assertRegex(out, r'(?m)^LOG seta_restore restore_failed edition=fl .* result=0 ')
        for reason in ('factor_write', 'vm_clear', 'vm_load', 'epoch', 'ship_change', 'call_unwound'):
            self.assertRegex(out, rf'(?m)^LOG seta_restore cancelled edition=fl reason={reason} ')
        self.assertRegex(out, r'(?m)^LOG seta_restore rebound edition=fl ')
        self.assertRegex(out, r'(?m)^LOG seta_key_latch released edition=fl reason=released .* held=3 zeros=8 calls=11 '
                              r'fires=1')
        for reason in ('call_unwound', 'call_unresolved'):
            self.assertRegex(out, rf'(?m)^LOG seta_key_latch refused edition=fl reason={reason} ')
        self.assertRegex(out, r'(?m)^LOG seta_restore refused reason=late_claim edition=fl ')


class FlSetaRestoreSites(unittest.TestCase):
    def test_verifier_against_the_installed_exe(self):
        if not EXE.is_file():
            self.skipTest(f'{EXE} not present')
        try:
            import capstone  # noqa: F401
        except ImportError:
            self.skipTest('needs capstone')
        import verify_seta_restore_sites_fl as probe
        report = probe.verify(EXE.read_bytes())
        self.assertEqual(report['result'], 'PASS', json.dumps({k: v['ok'] for k, v in report['checks'].items()}))
        hook = report['checks']['factor_hook']
        self.assertEqual((hook['fixups'], hook['fixup_target'], hook['write']), ([4], '0x1a83b0', 'plain'))
        self.assertEqual(report['checks']['ti_case']['name_slot_9'], 'TI_SetTimeWarpFactor')
        self.assertEqual(report['checks']['windows']['count'], 22)

    def test_header_constants_match_the_verifier(self):
        import verify_seta_restore_sites_fl as probe
        header = SITES.read_text()
        self.assertIn(f'hook_rva = 0x{probe.HOOK:06x}', header)
        self.assertIn(f'hook_next_rva = 0x{probe.HOOK_NEXT:06x}', header)
        self.assertIn(f'script_call_rva = 0x{probe.WRAPPER:06x}, callback_none = 32', header)
        self.assertIn(f'cfg_slot_rva = 0x{probe.CFG_SLOT:06x}', header)
        self.assertIn(f'action_counters_rva = 0x{probe.COUNTERS:06x}', header)
        self.assertIn(f'inject_rva = 0x{probe.INJECT:06x}', header)


class FlSetaRestoreWiring(unittest.TestCase):
    def test_loader_order_after_the_chase_family(self):
        loader = strip((ROOT / 'src/proxy/loader.cpp').read_text())
        fl = loader[loader.index('if (x3m::fl::verified())'):]
        fl = fl[:fl.index('#endif')]
        chase = ('    if (x3m::chase_camera::wanted()) {\n'
                 '        if (x3m::chase_camera::initialize()) x3m::chase_transition::initialize();\n'
                 '        x3m::chase_lead::initialize();\n'
                 '        x3m::chase_fire::initialize();\n'
                 '        x3m::log("chase_aim_trace installed=0 status=ap_only edition=fl reason=diagnostic_not_ported");\n'
                 '    }\n')
        self.assertEqual(fl.count(chase), 1)  # the chase family's install path as before the port
        after = fl[fl.index(chase) + len(chase):]
        self.assertLess(after.index('x3m::seta_restore::initialize();'), after.index('x3m::seta_manual_flight::initialize();'))
        self.assertNotIn('seta_restore::initialize', fl[:fl.index(chase)])
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        init = capture[capture.index('void initialize_log(HMODULE module)'):]
        for block in re.findall(r'#if X3M_EDITION_FL\n(.*?)\n#endif', init, re.S):
            self.assertNotIn('seta_restore::initialize', block)
        module = MODULE.read_text()
        self.assertIn('return x3m::chase_camera::wanted() && !std::strcmp(x3m::chase_camera::status(), "disabled");', module)

    def test_consumer_admitted_on_fl(self):
        source = (ROOT / 'src/proxy/chase_transition.cpp').read_text()
        attach = source[source.index('bool inject_attach(const InjectConsumer* consumer'):]
        attach = attach[:attach.index('\n}\n')]
        self.assertNotIn('"ap_only"', attach)
        self.assertIn('fl_admit(base)', attach)
        consumer = source[source.index('void consumer_inject(std::uint32_t* regs'):]
        consumer = consumer[:consumer.index('\n}\n')]
        fl = consumer[consumer.index('#if X3M_EDITION_FL'):consumer.index('#else')]
        self.assertIn('controlled = regs[6];', fl)
        self.assertIn('detail::restore_callback_none', fl)
        self.assertIn('const detail::RestoreEngineCall engine{callback, 0,', consumer)

    def test_capture_cmake_schema(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        fl = cmake[cmake.index('else()\n  add_library(d3d9 SHARED ${X3M_SHARED_SOURCES}'):]
        fl = fl[:fl.index('endif()')]
        self.assertIn('src/engine/fl/seta_restore_fl.cpp', fl)
        schema = (ROOT / 'tools/config/schema.py').read_text()
        entry = schema[schema.index("entry('seta_restore',"):]
        entry = entry[:entry.index('\n    entry(')]
        self.assertNotIn('Albion Prelude only', entry)  # both editions: no edition note (convention 2026-11-04)

    def test_module_binds_fl_only(self):
        module = strip(MODULE.read_text())
        for ap in ('cfg_ptr_va', 'vm_ptr_va', 'manager_ptr_va', 'game_root_va', 'script_call_va', 'hook_va',
                   'inject_site_va', 'seta_counter_va', 'ring_read_va', 'input_ctx_ptr_va', 'fog_prefill::walk(',
                   '0x004b148c', '0x0049f4c0', '0x00410100', '0x00606f34'):
            self.assertNotIn(ap, module, ap)
        self.assertNotRegex(module, r'[^:]\bcfg_factor\b')  # only R::cfg_factor (0xbc), never AP's 0xcc
        self.assertIn('x3m::fl::fog_prefill_fl::walk(result, departure_id)', module)
        self.assertIn('push esi\n    push eax\n    call _x3m_seta_restore_factor_write', module)
        self.assertIn('fnsave [esp+0x80]', module)


if __name__ == '__main__':
    unittest.main()
