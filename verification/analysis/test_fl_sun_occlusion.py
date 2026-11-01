"""Host tests of the FL sun occlusion hooks (src/engine/fl/sun_occlusion_fl.cpp).

docs/architecture/farnhams-legacy-sun-occlusion.md. (a) The native fixture verification/probe/fl_sun_occlusion.cpp
(MinGW i686, run natively through WSL interop) claims a fake flare walk's probe call (one qword word, as base+0x8af0a)
and a fake lens pass's call (straddling a qword, as base+0x8bcb7) with the production engine_patch::claim_call and
checks: the probe thunk's answer path (EAX 0/1, EFLAGS, ECX/EDX, ESP, LastError, XMM, x87) and forward path (ECX/EDX
and EFLAGS into the original, `popfd; jmp` with ESP at the return address), the three early gates with the flags word
at config +0xec, readiness, the deferral (record+0x30 read), the lens bracket around the original with ECX/EDX and
EFLAGS restored, an SEH unwind through the bracket, a foreign thread, the registry main view, the log mode's call of
the original through the register ABI, the 17-anchor gate on a synthetic relocated image, restore and late-window
refusal; the hook paths are audited for x87 and XMM use. (b) Edition wiring and the shared fog-transmittance lens gain.
"""
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / 'src/engine/fl/sun_occlusion_fl.cpp'
FIXTURE = ROOT / 'verification/probe/fl_sun_occlusion.cpp'
AUDIT = ROOT / 'verification/results/fl-sun-occlusion/hook_cpu_state_audit.py'


def build_and_run(tmp):
    """Builds the fixture from the production sources (CMake per-file flags), runs it natively and audits the hook
    paths. Returns (run, audit) CompletedProcess objects; None when the toolchain or interop is absent. Also used by
    verification/results/fl-sun-occlusion/run_fixture.py and mutation_check.py."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    tmp = Path(tmp)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_SUN_OCCLUSION_FL_FIXTURE',
              ]
    light = ['-mno-sse', '-mno-mmx', '-mfpmath=387']
    sse = ['-msse2', '-mfpmath=sse']
    objects = []
    for source, flags in (('src/engine/fl/sun_occlusion_fl.cpp', ['-fno-exceptions', *light]),
                          ('src/proxy/engine_memory.cpp', light), ('src/proxy/engine_patch.cpp', sse),
                          ('src/engine/fl/executable_identity_fl.cpp', sse),
                          ('src/engine/fl/sector_background_fl.cpp', ['-fno-exceptions', *sse])):
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_sun_occlusion.exe'
    subprocess.run([compiler, *common, *sse, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=120)
    audit = subprocess.run(['python3', str(AUDIT), str(exe)], text=True, capture_output=True)
    return run, audit


class FlSunOcclusionNativeFixture(unittest.TestCase):
    """The production sun_occlusion_fl.cpp, engine_patch.cpp, engine_memory.cpp, executable_identity_fl.cpp and
    sector_background_fl.cpp, linked into the fixture and run natively (WSL interop)."""

    def test_thunks_gates_bracket_unwind_anchors_and_late_window(self):
        with tempfile.TemporaryDirectory(prefix='x3-fl-sun-') as tmp:
            outcome = build_and_run(tmp)
        if outcome is None:
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        result, audit = outcome
        out = result.stdout
        self.assertEqual(result.returncode, 0, "\n".join(l for l in out.splitlines() if l.startswith("FAIL"))[:6000] + result.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0 ')
        self.assertNotIn('FAIL ', out)
        self.assertRegex(out, r'TIMING native_probe_ns=[\d.]+ forward_ns=[\d.]+ answered_ns=[\d.]+ ')
        self.assertEqual(audit.returncode, 0, audit.stdout[-2000:] + audit.stderr[-1000:])
        # 874 anchor bytes: 822 outside the 13 relocated operands, 52 inside; every single-bit flip refused.
        self.assertIn('ANCHOR anchors=17 flips=822 refused=822 operand_flips=52 operand_refused=52', out)
        self.assertEqual(len(re.findall(r'^LOG fl\.sun_occlusion_sample ', out, re.M)), 1)
        self.assertEqual(len(re.findall(r'^LOG sun_occlusion_released ', out, re.M)), 1)


def strip(text):
    return re.sub(r'//.*', '', text)


class FlSunOcclusionWiring(unittest.TestCase):
    def test_edition_lists_flags_and_capture(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        linked = re.search(r'set\(X3M_AP_ENGINE_LINKED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        shared = re.search(r'set\(X3M_SHARED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        ap = re.search(r'set\(X3M_AP_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('own_sun_hook.cpp', linked + shared)
        self.assertIn('src/proxy/own_sun_hook.cpp', ap)
        self.assertIn('target_sources(d3d9 PRIVATE src/engine/fl/sun_occlusion_fl.cpp)', cmake)
        self.assertIn('set_source_files_properties(src/engine/fl/sun_occlusion_fl.cpp\n'
                      '  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")', cmake)
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        # The FL Present block calls the frame boundary; the Reset and the install are outside every AP fence.
        fl_present = capture[capture.index('scene_hook::fl_sample_row();'):]
        self.assertIn('sun_occlusion::present(ctx.frame + 1);', fl_present[:fl_present.index('#endif')])

        def fenced_ap(text, needle):
            before = text[:text.index(needle)]
            opened = [m.start() for m in re.finditer(r'#if !X3M_EDITION_FL\n', before)]
            closed = [m.start() for m in re.finditer(r'#endif', before)]
            return bool(opened) and (not closed or opened[-1] > closed[-1])
        for needle in ('sun_occlusion::device_reset();', 'sun_occlusion::initialize(lens_gain_wanted || volumetric_fog_requested)',
                       'lens_flare_fog = volumetric_fog_requested && sun_installed;'):
            self.assertIn(needle, capture)
            self.assertFalse(fenced_ap(capture, needle), needle)
        self.assertIn('configure_lens_flare_gain(lens_flare_gain_value, lens_flare_fog)', capture)

    def test_provider_contract(self):
        code = strip(PROVIDER.read_text())
        self.assertNotIn('0x400000', code)
        for va in ('0x48af0a', '0x48bcb7', '0x4a3fd0', '0x497680', '0x5aa100', '0x5a83b0', '0x471630', '0x472491'):
            self.assertNotIn(va, code.lower())
        self.assertIn('x3m::fl::module_base()', code)
        self.assertIn('#include "sun_occlusion_fl_inc.h"', code)
        self.assertIn('engine_patch::claim_call', code)
        self.assertIn('engine_patch::install_window_open()', code)
        self.assertIn('L::config_video_flags', code)
        self.assertNotIn('core::config_video_flags)', code)
        for status in ('"disabled"', '"executable_mismatch"', '"anchor_mismatch"', '"late_claim"', '"active"',
                       '"rollback_failed"', '"restored"'):
            self.assertIn(status, code)
        self.assertIn('"fl.sun_occlusion status=%s', code)
        self.assertIn('"fl.sun_occlusion_sample row=%lu', code)
        for banned in ('new ', 'malloc', 'std::string', 'float', 'double'):
            self.assertNotIn(banned, code)
        # Both thunks: EFLAGS saved, DF cleared around the C handlers; the probe forwards with popfd; jmp.
        self.assertTrue('1:  popfd\n    jmp dword ptr [_x3m_fl_sun_probe_target]' in code, 'probe forward')
        self.assertTrue('    popfd\n    call dword ptr [_x3m_fl_sun_lens_target]\n    pushfd\n    push eax\n    push ecx\n'
                        '    push edx\n    cld\n    call _x3m_fl_sun_lens_end\n    pop edx\n    pop ecx\n    pop eax\n'
                        '    popfd\n    ret' in code, 'lens epilogue')


class FogLensGain(unittest.TestCase):
    """The shared lens-gain rule (both editions): G x T_sun with the volumetric fog, T_sun = the fog law's
    transmittance at the look's column cap (renderer::fog_sky_lens_gain), G unchanged when the law is off."""

    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory(prefix='x3-fog-lens-gain-')
        cls.driver = Path(cls.dir.name) / 'own_sun_hook_host'
        compiler = shutil.which('clang++') or shutil.which('c++')
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'verification/probe/own_sun_hook_host.cpp'),
                        str(ROOT / 'src/renderer/lens_visibility_variant.cpp'), '-o', str(cls.driver)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def gain(self, *args):
        out = subprocess.check_output([str(self.driver), 'foggain', *map(str, args)], text=True).split()
        return {k: float(v) for k, v in (f.split('=') for f in out)}

    def test_fog_off_keeps_g_and_fog_on_scales_by_t(self):
        # foggain G on extinction start cap chroma_r chroma_g chroma_b tint
        for g in (1.0, 0.6, 0.0):
            off = self.gain(g, 0, 0, 65000, 112500, 1, 1, 1, 0.6)
            self.assertEqual(off['t'], 1.0)
            self.assertAlmostEqual(off['gain'], g, places=6)  # G exactly (as a float)
        # T = 0.5 exactly at the cap: D(cap) = start + (cap - start) / 2, extinction = ln 2 / D, grey chroma (k = 1).
        import math
        depth = 65000 + (112500 - 65000) / 2
        for g in (1.0, 0.6):
            on = self.gain(g, 1, math.log(2) / depth, 65000, 112500, 1, 1, 1, 0.6)
            self.assertAlmostEqual(on['t'], 0.5, places=5)
            self.assertAlmostEqual(on['gain'], 0.5 * g, places=5)
        # A tinted fog: the luminance of T_rgb (k = 1 + tint (1 - chroma)), clamped to [0, 1].
        e = 1e-5
        on = self.gain(1.0, 1, e, 65000, 112500, 0.2, 1.0, 0.12, 0.6)
        k = [1 + 0.6 * (1 - c) for c in (0.2, 1.0, 0.12)]
        lum = sum(w * math.exp(-kk * e * depth) for w, kk in zip((.2126, .7152, .0722), k))
        self.assertAlmostEqual(on['t'], lum, places=4)
        self.assertEqual(self.gain(1.0, 1, 1.0, 65000, 112500, 1, 1, 1, 0.6)['t'], 0.0)

    def test_both_editions_reach_the_rule(self):
        inc = strip((ROOT / 'src/proxy/motion_output_lens_bracket_inc.h').read_text())
        self.assertNotIn('X3M_EDITION', inc)
        begin = inc[inc.index('void MotionOutput::sun_occlusion_begin() noexcept {'):]
        begin = begin[:begin.index('if (!sun_occlusion_.requested && !sun_occlusion_.log) return;')]
        self.assertIn('if (lens_gain_fog_) {', begin)
        self.assertIn('engine_plumes_fog(&law);', begin)
        self.assertIn('renderer::fog_sky_lens_gain(law, lens_gain_base_, &lens_gain_fog_t_, &applied);', begin)
        self.assertIn('lens_gain_ = lens_flare_gain::law(applied);', begin)
        self.assertIn('base=%.4f fog=%u fog_t=%.4f', inc)
        # The FL lens bracket reaches the same listener (capture.cpp sun_lens_begin -> sun_occlusion_begin).
        code = strip(PROVIDER.read_text())
        self.assertIn('if (listener_begin_) listener_begin_();', code)
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        self.assertIn('sun_occlusion::set_listener(&sun_lens_begin, &sun_lens_end);', capture)
        listener = capture[capture.index('void sun_lens_begin() {'):]
        self.assertIn('motion_output.sun_occlusion_begin();', listener[:200])


if __name__ == '__main__':
    unittest.main()
