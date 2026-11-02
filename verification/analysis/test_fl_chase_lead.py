"""Host tests of the FL chase lead marker and central HUD (src/engine/fl/chase_lead_fl.cpp,
src/engine/fl/chase_lead_sites_fl.h; docs/reverse-engineering/fl/chase-lead.md, "Verified 2026-11-02").

(a) The native fixture verification/probe/fl_chase_lead.cpp (MinGW i686, run natively through WSL interop) builds the
FL module, lays the twelve identity windows into a 64 KB-aligned private image (relocated dwords = that base) and drives
all four thunks: the gate and HUD gate (ZF cleared only when admitted, both je outcomes through the re-based rel32),
the publication (ECX/EDX replaced, the solver point read at P+0xe0 and not at AP's P+0xc0), the final FOV (the replayed
compare's flags, zoom finalization, the five-node forward anchor), the native hide helper through the detacher, the
HUD refusals; every GPR, EFLAGS with DF, ESP, XMM0-7, x87 control word and stack, MXCSR and LastError at the native
continuation for every 4-byte ESP skew; every window bit flip refused before any write, both-or-neither rollback of the
marker's three seams, the independent HUD seam, shutdown and the late-window refusal.
(b) The FL verifier on the installed X3FL.exe (skipped without it) and the header/verifier constants.
(c) Edition wiring: the loader, capture, CMake and chase camera FL calls; the AP module untouched.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'verification/probe/fl_chase_lead.cpp'
SITES = ROOT / 'src/engine/fl/chase_lead_sites_fl.h'
COMMON = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
          '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_CHASE_LEAD_FL_FIXTURE', '-msse2', '-mfpmath=sse']
# CMake's per-file flags for the production sources the fixture links.
SOURCES = (('src/engine/fl/chase_lead_fl.cpp', ['-fno-exceptions']), ('src/proxy/engine_patch.cpp', []),
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
    exe = tmp / 'fl_chase_lead.exe'
    subprocess.run([compiler(), *COMMON, '-static', str(FIXTURE), *map(str, objects), '-o', str(exe)], check=True)
    return exe, objects[0]


class FlChaseLeadNativeFixture(unittest.TestCase):
    def test_thunks_cpu_contract_rollback_and_late_window(self):
        if not compiler() or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        with tempfile.TemporaryDirectory(prefix='x3-fl-cl-') as tmp:
            exe, lead_obj = build(tmp)
            run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=300)
            undefined = subprocess.run(['i686-w64-mingw32-nm', '-u', '-C', str(lead_obj)], text=True,
                                       capture_output=True, check=True).stdout
        out = run.stdout
        self.assertEqual(run.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:4000]
                         + run.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+)', out).group(1)), 60)
        self.assertIn('WINDOWS count=12 bytes=823 gate_mod8=1 publish_mod8=7 fov_mod8=5 hud_mod8=6', out)
        self.assertIn('FLIPS bits=6584 refused=6584 reloc_bits=288', out)
        self.assertIn('EXPECT lead=231,-65 ', out)
        self.assertRegex(out, r'(?m)^LOG chase_lead installed=1 status=active scope=applied_main_chase_same_sector '
                              r'native_solver=retained telemetry=1 edition=fl base=0x[0-9a-f]{8} '
                              r'rva=0x02c7b9,0x02cbe7,0x022cf5 window=- rollback=none update=pose_seam$')
        self.assertRegex(out, r'(?m)^LOG chase_central_hud installed=1 status=active cleanup=native_when_extra_active '
                              r'hud_anchor=forward edition=fl rva=0x02cc1e rollback=none$')
        self.assertIn('LOG chase_native_timing installed=0 status=not_ported sites=0 edition=fl reason=telemetry_only',
                      out)
        self.assertRegex(out, r'(?m)^LOG chase_lead installed=0 status=readback_mismatch .* rollback=failed ')
        self.assertRegex(out, r'(?m)^LOG chase_lead installed=0 status=late_claim ')
        self.assertRegex(out, r'(?m)^LOG chase_lead_window frame=300 ready=\d+ committed=\d+ final_fov=1 ')
        # The FL module reaches no AP-only module, no AP executable identity and no transition service.
        for name in ('chase_transition', 'chase_fire', 'chase_aim_trace', 'object_trace'):
            self.assertNotIn(f'x3m::{name}::', undefined)


class FlChaseLeadSites(unittest.TestCase):
    def test_verifier_on_installed_exe(self):
        sys.path.insert(0, str(ROOT / 'verification/probe'))
        import verify_chase_camera_site as camera
        import verify_chase_lead_sites_fl as probe
        if not camera.FL_DEFAULT_EXE.is_file():
            self.skipTest('X3FL.exe not installed')
        try:
            import capstone  # noqa: F401
        except ImportError:
            self.skipTest('capstone missing')
        report = probe.verify(camera.FL_DEFAULT_EXE.read_bytes())
        failed = {k: v for k, v in report['checks'].items() if not v['ok']}
        self.assertEqual(report['result'], 'PASS', str(failed)[:3000])
        self.assertEqual(report['checks']['overlay_caller']['callers'], ['0x0229c2'])
        self.assertEqual(report['checks']['updater_cfg']['reachable_without_pose_seam'],
                         {'0x0229c2': False, '0x022cf5': False})
        self.assertEqual(set(report['checks']['overlay_stack']['depth_at'].values()), {0, -0x14})
        self.assertGreaterEqual(report['checks']['anchors']['count'], 80)

    def test_header_matches_verifier_constants(self):
        sys.path.insert(0, str(ROOT / 'verification/probe'))
        import verify_chase_lead_sites_fl as probe
        text = SITES.read_text()
        for name, start, end, relocs in probe.WINDOWS:
            self.assertIn(f'{{"{name}", 0x{start:06x}, {end - start}, {name}_window, 0x{probe.WINDOW_FNV[name]:08x}u,',
                          text)
            for target in relocs.values():
                self.assertIn(f'0x{target:06x}', text)
        for name, rva, hexbytes, _, _ in probe.SEAMS:
            spaced = ', '.join(f'0x{b:02x}' for b in bytes.fromhex(hexbytes))
            self.assertIn(f'0x{rva:06x}, {{{spaced}}}', text)


def strip(text):
    return re.sub(r'//.*', '', text)


class FlChaseLeadWiring(unittest.TestCase):
    def test_loader_capture_cmake_camera(self):
        loader = strip((ROOT / 'src/proxy/loader.cpp').read_text())
        fl = loader[loader.index('if (x3m::chase_camera::wanted()) {\n        x3m::chase_camera::initialize();\n'
                                 '        x3m::chase_lead::initialize();'):]
        fl = fl[:fl.index('#endif')]
        self.assertNotIn('"chase_lead"', fl)
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        tail = capture[capture.index('chase_camera::report(ctx.frame); \n        chase_lead::report(ctx.frame);'):]
        self.assertLess(tail.index('chase_lead::report'), tail.index('#endif'))
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        linked = re.search(r'set\(X3M_AP_ENGINE_LINKED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('src/proxy/chase_lead.cpp', linked)
        self.assertIn('target_sources(d3d9 PRIVATE src/engine/fl/chase_lead_fl.cpp)', cmake)
        self.assertIn('src/engine/fl/chase_lead_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions"', cmake)
        camera = strip((ROOT / 'src/proxy/chase_camera.cpp').read_text())
        fl_end = camera[camera.index('ReleaseSRWLockExclusive(&stats_lock); \n    const bool context_ok'):]
        self.assertLess(fl_end.index('chase_lead::camera_context('), fl_end.index('#else'))
        self.assertIn('void handle(uint32_t* regs) {\n    \n    chase_lead::invalidate_pose();', camera)

    def test_ap_module_untouched_by_fl(self):
        ap = (ROOT / 'src/proxy/chase_lead.cpp').read_text()
        self.assertNotIn('X3M_EDITION_FL', ap)
        fl = (ROOT / 'src/engine/fl/chase_lead_fl.cpp').read_text()
        for literal in ('0x608504', '0x606f38', '0x426280', '0x3ac', '0xc4,', 'chase_transition::'):
            self.assertNotIn(literal, strip(fl))


if __name__ == '__main__':
    unittest.main()
