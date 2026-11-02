"""Host tests of the FL chase cursor fire (src/engine/fl/chase_fire_fl.cpp, src/engine/fl/chase_fire_sites_fl.h, the
shared policy src/proxy/chase_fire_core.h; docs/reverse-engineering/fl/chase-fire-aim.md, "Verified 2026-11-03").

(a) The native fixture verification/probe/fl_chase_fire.cpp (MinGW i686, run natively through WSL interop) builds the
FL module, lays the five identity windows into a 64 KB-aligned private image (relocated dwords = that base) and enters
the thunk through the native `cmp [cursor active],0`: the relocated JE both ways (admitted: fallthrough with only ZF
cleared; refused or natively active: the original branch and flags), every GPR, EFLAGS with DF, ESP, XMM0-7, x87 control
word and stack, MXCSR and LastError at the native continuation for every 4-byte ESP skew; the fire-call filters on FL's
ABI ([EBX+0xc] group, [EBX+0x10] flags, [EBP-0x3c] ship, a player ship in AP's EBX+8 slot refused), the witness and
live-scope refusals, the admitted cursor set against the native unprojection's viewport and an off-centre aim ray; every
window bit flip refused before any write, the read-back rollback, shutdown and the late-window refusal.
(b) The FL verifier on the installed X3FL.exe (skipped without it) and the header/verifier constants.
(c) Edition wiring: loader, capture, CMake and the chase camera FL calls; the AP module's FL independence.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'verification/probe/fl_chase_fire.cpp'
SITES = ROOT / 'src/engine/fl/chase_fire_sites_fl.h'
COMMON = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
          '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_CHASE_FIRE_FL_FIXTURE', '-msse2', '-mfpmath=sse']
# CMake's per-file flags for the production sources the fixture links.
SOURCES = (('src/engine/fl/chase_fire_fl.cpp', ['-fno-exceptions']), ('src/proxy/engine_patch.cpp', []),
           ('src/proxy/engine_memory.cpp', ['-mno-sse', '-mno-mmx', '-mfpmath=387']),
           ('src/engine/fl/executable_identity_fl.cpp', []))


def compiler():
    return shutil.which('i686-w64-mingw32-g++')


def build(tmp):
    tmp = Path(tmp)
    objects = []
    for source, flags in SOURCES:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler(), *COMMON, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(obj)
    exe = tmp / 'fl_chase_fire.exe'
    subprocess.run([compiler(), *COMMON, '-static', str(FIXTURE), *map(str, objects), '-o', str(exe)], check=True)
    return exe, objects[0]


class FlChaseFireNativeFixture(unittest.TestCase):
    def test_thunk_cpu_contract_admission_rollback_and_late_window(self):
        if not compiler() or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        with tempfile.TemporaryDirectory(prefix='x3-fl-cf-') as tmp:
            exe, fire_obj = build(tmp)
            run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=300)
            undefined = subprocess.run(['i686-w64-mingw32-nm', '-u', '-C', str(fire_obj)], text=True,
                                       capture_output=True, check=True).stdout
        out = run.stdout
        self.assertEqual(run.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:4000]
                         + run.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+)', out).group(1)), 80)
        self.assertIn('WINDOWS count=5 bytes=481 seam_mod8=5', out)
        self.assertIn('FLIPS bits=3848 refused=3848 reloc_bits=352', out)
        self.assertIn('COUNTS override=22 stale=3 identity=5 view=4 cursor=4 read=2', out)
        self.assertIn('EARLY native_active=4 no_witness=5 group=1 flags=2 other_ship=1 read=0', out)
        self.assertIn('SWEEP cursors=3600 inside=1448 agreed=3600', out)
        self.assertRegex(out, r'(?m)^AIM cursor=1271,409 offset_16_16=10616,-7949 ')
        self.assertRegex(out, r'(?m)^LOG chase_fire installed=1 status=active scope=applied_chase_258_player_main_cursor '
                              r'lifetime=process telemetry_required=0 native_input_writes=0 edition=fl '
                              r'base=0x[0-9a-f]{8} rva=0x04ecad window=- rollback=none$')
        self.assertRegex(out, r'(?m)^LOG chase_fire installed=0 status=readback_mismatch .* rollback=restored$')
        self.assertRegex(out, r'(?m)^LOG chase_fire installed=0 status=readback_mismatch .* rollback=failed$')
        self.assertRegex(out, r'(?m)^LOG chase_fire installed=0 status=late_claim ')
        self.assertRegex(out, r'(?m)^LOG chase_fire_window frame=300 native_inactive_override=\d+ .* native_active=\d+ '
                              r'early_no_witness=\d+ early_group=\d+ early_flags=\d+ early_other_ship=\d+ '
                              r'early_read=\d+ edition=fl$')
        self.assertRegex(out, r'(?m)^TIMING n=5000 ')
        # The FL module reaches no AP-only module, no AP executable identity and no lead/transition service.
        for name in ('chase_aim_trace', 'chase_lead', 'chase_transition', 'object_trace'):
            self.assertNotIn(f'x3m::{name}::', undefined)


class FlChaseFireSites(unittest.TestCase):
    def test_verifier_on_installed_exe(self):
        sys.path.insert(0, str(ROOT / 'verification/probe'))
        import verify_chase_camera_site as camera
        import verify_chase_fire_sites_fl as probe
        if not camera.FL_DEFAULT_EXE.is_file():
            self.skipTest('X3FL.exe not installed')
        try:
            import capstone  # noqa: F401
        except ImportError:
            self.skipTest('capstone missing')
        report = probe.verify(camera.FL_DEFAULT_EXE.read_bytes())
        failed = {k: v for k, v in report['checks'].items() if not v['ok']}
        self.assertEqual(report['result'], 'PASS', str(failed)[:3000])
        fire = report['checks']['fire_function']
        self.assertEqual(fire['callers'], ['0x06bb81', '0x06bb9f', '0x06bbe4', '0x06bc03'])
        self.assertEqual(fire['ship_slot_writers'], ['0x04e350'])
        self.assertEqual(set(fire['arg_writes'].values()), {'ebx+0x8'})
        self.assertEqual(report['checks']['seam']['qword'], 'crosses')
        self.assertEqual(report['checks']['cursor_active_refs']['fixups'], ['0x04eca8', '0x07bc49'])
        eh = report['checks']['fire_eh_and_pointers']
        self.assertEqual((eh['highlow_into_function'], eh['n_try'], eh['magic']), ([], 0, '0x19930522'))
        self.assertGreaterEqual(report['checks']['anchors']['count'], 60)

    def test_header_matches_verifier_constants(self):
        sys.path.insert(0, str(ROOT / 'verification/probe'))
        import verify_chase_fire_sites_fl as probe
        text = SITES.read_text()
        for name, start, end, relocs in probe.WINDOWS:
            self.assertIn(f'{{"{name}", 0x{start:06x}, {end - start}, {name}_window, 0x{probe.WINDOW_FNV[name]:08x}u,',
                          text)
            for target in relocs.values():
                self.assertIn(f'0x{target:06x}', text)
        _, rva, hexbytes, _, target = probe.SEAM
        spaced = ', '.join(f'0x{b:02x}' for b in bytes.fromhex(hexbytes))
        self.assertIn(f'0x{rva:06x}, {{{spaced}}}, 2, w_admission', text)
        self.assertIn(f'admission_target_rva = 0x{target:06x}', text)


def strip(text):
    return re.sub(r'//.*', '', text)


class FlChaseFireWiring(unittest.TestCase):
    def test_loader_capture_cmake_camera(self):
        loader = strip((ROOT / 'src/proxy/loader.cpp').read_text())
        fl = loader[loader.index('if (x3m::chase_camera::wanted()) {\n        if (x3m::chase_camera::initialize()) '
                                 'x3m::chase_transition::initialize();\n        x3m::chase_lead::initialize();'):]
        fl = fl[:fl.index('#endif')]
        self.assertIn('x3m::chase_fire::initialize();', fl)
        self.assertNotIn('"chase_fire"', fl)
        self.assertIn('"chase_aim_trace installed=0 status=ap_only edition=fl reason=diagnostic_not_ported"', fl)
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        fl_report = capture[capture.index('chase_transition::report(ctx.frame); \n        chase_fire::report(ctx.frame);'):]
        self.assertLess(fl_report.index('chase_fire::report'), fl_report.index('#endif'))
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        linked = re.search(r'set\(X3M_AP_ENGINE_LINKED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('src/proxy/chase_fire.cpp', linked)
        self.assertIn('target_sources(d3d9 PRIVATE src/engine/fl/chase_fire_fl.cpp)', cmake)
        self.assertIn('src/engine/fl/chase_fire_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions"', cmake)
        camera = strip((ROOT / 'src/proxy/chase_camera.cpp').read_text())
        fl_end = camera[camera.index('ReleaseSRWLockExclusive(&stats_lock); \n    const bool context_ok'):]
        self.assertLess(fl_end.index('chase_fire::camera_context(cockpit, ref_object, camera, in.view_mode, '
                                     'in.connect_mode, in.flags_1a0, context_ok,\n                               now);'),
                        fl_end.index('#else'))
        self.assertIn('if (!active) {\n        chase_fire::invalidate_camera(cockpit);\n        return;\n    }', camera)

    def test_ap_module_and_core(self):
        ap = (ROOT / 'src/proxy/chase_fire.cpp').read_text()
        self.assertNotIn('X3M_EDITION_FL', ap)
        self.assertIn('core::decide(witness, ship, stamp, env)', ap)
        fl = strip((ROOT / 'src/engine/fl/chase_fire_fl.cpp').read_text())
        self.assertIn('core::decide(context, ship, stamp, env)', fl)
        for literal in ('0x607ce8', '0x00607ce8', '0x606f38', '0x608504', '0x445a41', 'object_trace::'):
            self.assertNotIn(literal, fl)
        core = (ROOT / 'src/proxy/chase_fire_core.h').read_text()
        self.assertNotIn('windows.h', core)


if __name__ == '__main__':
    unittest.main()
