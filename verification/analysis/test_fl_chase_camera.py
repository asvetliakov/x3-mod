"""Host tests of the FL chase camera pose seam (src/proxy/chase_camera.cpp with X3M_EDITION_FL=1,
src/engine/fl/chase_camera_sites_fl.h; docs/reverse-engineering/fl/chase-camera.md, "Verified 2026-11-02").

(a) The native fixture verification/probe/fl_chase_camera.cpp (MinGW i686, run natively through WSL interop) builds
the production module for FL, lays the three identity windows into a 64 KB-aligned private image (the plane fixup
relocated to that base), installs the seam through engine_patch::claim and drives the thunk: the displaced compare and
branch replayed from the tail with the re-based rel32 (both outcomes), the shared handler run with EBX = a synthetic
cockpit (the pose written through it), every GPR, EFLAGS, ESP, XMM0-7, the x87 control word and stack, MXCSR and
LastError intact at the native continuation for every 4-byte ESP skew; every single-bit window change refused before any
write, a read-back failure rolled back to the original ten bytes, shutdown restoring them, and the late-window refusal.
(b) Edition wiring: no AP dependency reachable from the FL module, the loader/capture FL calls and the AP refusal rows.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CAMERA = ROOT / 'src/proxy/chase_camera.cpp'
SITES = ROOT / 'src/engine/fl/chase_camera_sites_fl.h'
FIXTURE = ROOT / 'verification/probe/fl_chase_camera.cpp'
COMMON = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
          '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_CHASE_CAMERA_FL_FIXTURE', '-msse2', '-mfpmath=sse']
# CMake's per-file flags for the production sources the fixture links.
SOURCES = (('src/proxy/chase_camera.cpp', ['-fno-exceptions']), ('src/proxy/engine_patch.cpp', []),
           ('src/proxy/engine_memory.cpp', ['-mno-sse', '-mno-mmx', '-mfpmath=387']),
           ('src/engine/fl/executable_identity_fl.cpp', []), ('src/engine/fl/chase_lead_fl.cpp', ['-fno-exceptions']))


def compiler():
    return shutil.which('i686-w64-mingw32-g++')


def build(tmp):
    tmp = Path(tmp)
    objects = []
    for source, flags in SOURCES:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler(), *COMMON, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(obj)
    exe = tmp / 'fl_chase_camera.exe'
    subprocess.run([compiler(), *COMMON, '-static', str(FIXTURE), *map(str, objects), '-o', str(exe)], check=True)
    return exe, objects[0]


class FlChaseCameraNativeFixture(unittest.TestCase):
    def test_thunk_install_rollback_and_late_window(self):
        if not compiler() or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        with tempfile.TemporaryDirectory(prefix='x3-fl-cc-') as tmp:
            exe, camera_obj = build(tmp)
            run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=120)
            undefined = subprocess.run(['i686-w64-mingw32-nm', '-u', '-C', str(camera_obj)], text=True,
                                       capture_output=True, check=True).stdout
        out = run.stdout
        self.assertEqual(run.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:4000]
                         + run.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+)', out).group(1)), 35)
        self.assertIn('WINDOWS seam_fnv1a=391b90a2 target_fnv1a=466a42e2 plane_fnv1a_masked=2df2f1ce site_mod8=5', out)
        self.assertIn('FLIPS seam=528 refused=528 target=56 refused=56 plane=88 refused=88', out)
        self.assertRegex(out, r'(?m)^LOG chase_camera requested=1 installed=1 status=active site=0x[0-9a-f]{8} length=10 '
                              r'rel32_offset=6 atomic_write=0 .* edition=fl rva=0x0227ad rollback=none '
                              r'unbound_phase=ap_only dependencies=chase_lead$')
        self.assertRegex(out, r'(?m)^LOG chase_camera requested=1 installed=0 status=readback_mismatch .* rollback=restored ')
        self.assertRegex(out, r'(?m)^LOG chase_camera requested=1 installed=1 status=readback_mismatch .* rollback=failed ')
        self.assertRegex(out, r'(?m)^LOG chase_camera requested=1 installed=0 status=late_claim '
                              r'window_closed_by=fixture_present edition=fl ')
        self.assertIn('STATS frames=9 applied=8 refused=1 inactive=2 unbound=0', out)
        # The FL module reaches none of the AP-only camera modules and no AP executable identity (chase_lead is the FL
        # edition, src/engine/fl/chase_lead_fl.cpp).
        for name in ('chase_fire', 'chase_aim_trace', 'chase_transition', 'object_trace'):
            self.assertNotIn(f'x3m::{name}::', undefined)


def strip(text):
    return re.sub(r'//.*', '', text)


class FlChaseCameraWiring(unittest.TestCase):
    def test_constants_match_the_verifier(self):
        import sys
        sys.path.insert(0, str(ROOT / 'verification/probe'))
        import verify_chase_camera_site as probe
        sites = SITES.read_text()
        self.assertIn('site_rva = 0x0227ad', sites)
        self.assertIn('continuation_rva = 0x0227b7', sites)
        self.assertIn('branch_target_rva = 0x022928', sites)
        self.assertIn('default_view_plane_slot_rva = 0x1a82f8', sites)
        self.assertIn(', '.join(f'0x{b:02x}' for b in probe.FL_SITE_BYTES), sites)
        self.assertIn(', '.join(f'0x{r:06x}' for r in probe.FL_INBOUND_EDGES), sites)
        for name, (start, length, fnv, _) in probe.FL_WINDOWS.items():
            self.assertIn(f'0x{fnv:08x}', sites)
            self.assertIn(f'0x{start:06x}', sites)

    def test_fl_module_has_no_ap_dependency(self):
        code = CAMERA.read_text()
        fl = []
        depth, active = 0, [True]
        for line in code.splitlines():  # the lines an FL build compiles (one level of #if X3M_EDITION_FL / !...)
            s = line.strip()
            if s.startswith('#if'):
                active.append(active[-1] and (s != '#if !X3M_EDITION_FL'))
                depth += 1
            elif s.startswith('#else'):
                outer = active[-2]
                active[-1] = outer and not active[-1]
            elif s.startswith('#endif'):
                active.pop()
                depth -= 1
            elif active[-1]:
                fl.append(line)
        fl = strip('\n'.join(fl))
        for name in ('chase_fire::', 'chase_aim_trace::', 'chase_transition::', 'object_trace::',
                     '0x00420e06', '0x00606f38'):
            self.assertNotIn(name, fl)
        self.assertIn('chase_lead::invalidate_pose();', fl)
        self.assertIn('chase_lead::camera_context(cockpit, ref_object, camera, context_ok', fl)
        self.assertIn('admits_pose(ref_object, view_object, false)', fl)
        self.assertIn('x3m::fl::module_base()', fl)

    def test_loader_and_capture(self):
        loader = strip((ROOT / 'src/proxy/loader.cpp').read_text())
        fl = loader[loader.index('#if X3M_EDITION_FL\n    \n    \n    if (x3m::fl::verified())'):]
        fl = fl[:fl.index('#endif')]
        self.assertIn('x3m::chase_camera::initialize();', fl)
        self.assertIn('{"chase_transition", "chase_fire", "chase_aim_trace"}', fl)
        self.assertIn('x3m::chase_lead::initialize();', fl)
        self.assertIn('"%s installed=0 status=ap_only edition=fl reason=ap_only"', fl)
        self.assertLess(fl.index('x3m::fl::scale_fix::initialize();'), fl.index('x3m::chase_camera::initialize();'))
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        self.assertIn('#else\n    if (ctx.capture || ctx.frame % 300 == 0) {\n        chase_camera::report(ctx.frame); \n'
                      '        chase_lead::report(ctx.frame);   \n    }\n#endif', capture)
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        linked = re.search(r'set\(X3M_AP_ENGINE_LINKED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertIn('src/proxy/chase_camera.cpp', linked)


if __name__ == '__main__':
    unittest.main()
