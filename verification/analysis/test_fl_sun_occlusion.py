"""Host tests of the FL flare-probe hook and lens bracket (src/engine/fl/sun_occlusion_fl.cpp).

docs/architecture/farnhams-legacy-sun-occlusion.md; since own sun step 3 (docs/architecture/own-sun-pass.md section 14)
the FL probe answers "hidden" for the eligible record in a frame whose own-sun latch is set and forwards every other
call; the probe installs for the own sun or the bracket, the lens call for the bracket only. (a) The native fixture verification/probe/fl_sun_occlusion.cpp (MinGW i686, run natively through WSL interop)
claims a fake flare walk's probe call (one qword word, as base+0x8af0a) and a fake lens pass's call (straddling a qword,
as base+0x8bcb7) with the production engine_patch::claim_call and checks: the probe thunk's forward path (ECX/EDX and
EFLAGS into the original, `popfd; jmp` with ESP at the return address, LastError, XMM, x87) for eligible and ineligible
probes, the own sun's hidden answer (EAX 1 through the plain RET, ECX/EDX/EFLAGS/LastError/XMM as at the call, counters,
own_sun_hidden() per frame; latch clear, not requested, ineligible, foreign thread and unwind cases), the probe-only
install that leaves the lens call vanilla, the log mode's answer path (the original through the register ABI, EAX 0/1, LastError, XMM), the lens bracket
around the original with ECX/EDX and EFLAGS restored, an SEH unwind through the bracket, a foreign thread, the registry
main view, the 17-anchor gate on a synthetic relocated image, restore and late-window refusal; the hook paths are
audited for x87 and XMM use. (b) Edition wiring and the shared fog-transmittance lens gain.
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
        self.assertRegex(out, r'TIMING native_probe_ns=[\d.]+ forward_ns=[\d.]+ eligible_forward_ns=[\d.]+ .* '
                              r'hidden_ns=[\d.]+ own_frame_forward_ns=[\d.]+')
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+)', out).group(1)), 766)  # step 3: 464 -> 766
        self.assertEqual(audit.returncode, 0, audit.stdout[-2000:] + audit.stderr[-1000:])
        # 874 anchor bytes: 822 outside the 13 relocated operands, 52 inside; every single-bit flip refused.
        self.assertIn('ANCHOR anchors=17 flips=822 refused=822 operand_flips=52 operand_refused=52', out)
        self.assertEqual(len(re.findall(r'^LOG fl\.sun_occlusion_sample ', out, re.M)), 2)  # step 3 adds the own-sun row
        self.assertEqual(len(re.findall(r'^LOG sun_occlusion_released ', out, re.M)), 0)  # the deferral went with step 2


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
        fl_present = fl_present[:fl_present.index('#endif')]
        self.assertIn('own_sun_hook::present(ctx.frame + 1);', fl_present)
        # Own sun step 3: the latch precedes the frame boundary, as on AP.
        self.assertLess(fl_present.index('own_sun_hook::set_own_sun_frame(active);'),
                        fl_present.index('own_sun_hook::present(ctx.frame + 1);'))

        def fenced_ap(text, needle):
            before = text[:text.index(needle)]
            opened = [m.start() for m in re.finditer(r'#if !X3M_EDITION_FL\n', before)]
            closed = [m.start() for m in re.finditer(r'#endif', before)]
            return bool(opened) and (not closed or opened[-1] > closed[-1])
        self.assertNotIn('own_sun_hook::device_reset', capture)  # nothing of the hook survives a Reset since step 2
        for needle in ('own_sun_hook::initialize(lens_gain_wanted || volumetric_fog_requested)',
                       'lens_flare_fog = volumetric_fog_requested && sun_installed;',
                       'own_sun_hook::request_own_sun();',
                       'hooked.motion_output.configure_own_sun('):
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
        # The answers (0 / 1, also the own sun's hidden one) leave through the same popfd; ret after ECX/EDX are popped.
        self.assertTrue('    pop edx\n    pop ecx\n    cmp eax, 2\n    je 1f\n    popfd\n    ret\n1:' in code, 'answer path')
        # Own sun step 3: the hidden answer gated by the latch and the eligibility, recorded for own_sun_hidden().
        decide = code[code.index('int decide_probe(const std::uint32_t* record, const std::uint32_t* view) {\n    if'):]
        self.assertIn('if (!record || !view || (!own_sun_frame_ && !log_)) {', decide)
        self.assertIn('    if (own && own_sun_frame_) {\n        ++counters_.own_sun_hidden;\n        own_sun_hidden_seen_ = true;\n'
                      '        own_sun_hidden_frame_ = frame;\n        return core::Hidden;\n    }', decide)
        self.assertLess(decide.index('return core::Hidden;\n    }'), decide.index('const int vanilla ='))
        self.assertIn('own_sun_frame_ = patched_ && own_sun_wanted_ && active;', code)
        self.assertIn('return patched_ && own_sun_hidden_seen_ && own_sun_hidden_frame_ == frame_;', code)
        # The lens call is claimed for the bracket only (own-sun-pass.md section 8).
        self.assertIn('if (!bracket_wanted && !own_sun_wanted_) {', code)
        self.assertIn('bracket_wanted ? base + L::lens_site_rva : 0,', code)
        self.assertIn('if (with_lens && !engine_patch::claim_call(lens_site_', code)
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
                        '-o', str(cls.driver)], check=True)

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
        begin = inc[inc.index('void MotionOutput::lens_bracket_begin() noexcept {'):]
        begin = begin[:begin.index('\n}\n')]
        self.assertIn('if (!lens_gain_fog_) return;', begin)
        self.assertIn('engine_plumes_fog(&law);', begin)
        self.assertIn('renderer::fog_sky_lens_gain(law, lens_gain_base_, &lens_gain_fog_t_, &applied);', begin)
        self.assertIn('lens_gain_ = lens_flare_gain::law(applied);', begin)
        self.assertIn('base=%.4f fog=%u fog_t=%.4f', inc)
        # The FL lens bracket reaches the same listener (capture.cpp sun_lens_begin -> lens_bracket_begin).
        code = strip(PROVIDER.read_text())
        self.assertIn('if (listener_begin_) listener_begin_();', code)
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        self.assertIn('own_sun_hook::set_listener(&sun_lens_begin, nullptr);', capture)
        listener = capture[capture.index('void sun_lens_begin() {'):]
        self.assertIn('motion_output.lens_bracket_begin();', listener[:200])


if __name__ == '__main__':
    unittest.main()
