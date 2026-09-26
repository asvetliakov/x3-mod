"""Host oracle of the source-only encoded emission gain (--emission-source-gain).

Transformer: gain 1 is byte-identical to the original; gain G adds exactly one
`def c31 = (G, 0, 0, 0)` and one `mul r0.xyz, r0, c31.x` immediately before
the untouched native `mov oC0, r0`; every original instruction, the native
output and raw alpha are retained (docs/architecture/linear-emission-cost.md,
"Implemented"). Launcher gate: requires --hdr only; the retired
`--effect-source-gain` is gone from the parser (unrecognized). Blend law
(`linear_emission_source_gain_blend`, shared with the GPU fixture): ONE/ONE/ADD
admits whatever the separate alpha states are (run 26 refused every engine
draw on sepalpha=1), ONE/INVSRCCOLOR admits as `Screen` (the proxy substitutes
DESTBLEND ONE for that draw, "Screen substitution"), the rest refuses as
`blend`. One registry index per reviewed pair (`linear_emission_pair_index`),
one gain, no family. Log grammar: per-frame totals with the substituted
count, per-reason samples, the state-refusal sample and the once-per-pair
first admission. No game assets bundled.
"""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import inspect_motion_output_profiles as shader
from verification.analysis.test_capture_bloom_lifetime import extract_function

PIXELS = ('8360f422de08b5bd', '9975b706e5a1c999', 'ff2473e73a6bdfa1', '8559522220507d5e', '875e780adb131b16',
          '39f3b4d5b6a5aaed', '47e15e20d63b0e93', '846c5c1a549f9491', 'c6dacb8f74b65c97', 'f0c91793a75e1203')
GAINS = (1., 2., 3.5, 8.)
DEF, MUL, MOV, DCL = 81, 5, 1, 31
PREREQUISITES = ['--motion-output', '--hdr']
VERTICES = ('d5e1c75351ed3f04', '32e75459998d0388', '089091aab2d5eb13', '5b7a3ccd9e7df00a', '6435a84d8ac5908e',
            '89193868c61c3846', 'a520be365951c9dc', 'cfb2c31707d545bc')
# Registry order of the twenty pairs as (VS index, PS index).
PAIRS = ((0, 0), (1, 1), (1, 2), (2, 3), (2, 4), (3, 1), (3, 2), (4, 5), (4, 6), (4, 7), (4, 8),
         (5, 0), (5, 9), (6, 3), (6, 4), (7, 5), (7, 6), (7, 7), (7, 8), (0, 9))


def launch_help():
    spec = importlib.util.spec_from_file_location('source_gain_manage_help', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    output = io.StringIO()
    with mock.patch.object(sys, 'argv', ['manage.py', 'launch', '--help']), contextlib.redirect_stdout(output):
        with contextlib.suppress(SystemExit):
            module.main()
    return output.getvalue()


def span(words, item):
    return tuple(words[item['dword']:item['dword'] + item['length'] + 1])


def launch(directory, *args):
    spec = importlib.util.spec_from_file_location('source_gain_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    game = Path(directory) / 'game'; game.mkdir(exist_ok=True); (game / 'X3AP.exe').touch()
    wine = Path(directory) / 'wine'; wine.touch()
    argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
    output, error = io.StringIO(), io.StringIO()
    with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
            mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
            contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
        try: module.main()
        except SystemExit as exit_error: return exit_error.code, output.getvalue(), error.getvalue()
    return 0, output.getvalue(), error.getvalue()


class SourceGainTransformerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.originals = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
        if not all((cls.originals / f'ps_{key}.bin').is_file() for key in PIXELS):
            reason = f'local ten-original PS2/PS2.x corpus unavailable under {cls.originals} (X3M_SHADER_PROGRAM_DIRECTORY)'
            if os.environ.get('X3M_REQUIRE_SHADER_CORPUS') == '1':
                raise AssertionError(reason)
            print('SKIP:', reason, file=sys.stderr)
            raise unittest.SkipTest(reason)
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise RuntimeError('host C++ compiler required')
        temporary = tempfile.TemporaryDirectory(prefix='x3-emission-source-gain-')
        cls.addClassCleanup(temporary.cleanup)
        cls.directory = Path(temporary.name)
        executable = cls.directory / 'structure'
        build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                str(ROOT / 'verification/probe/linear_emission_source_gain_structure.cpp'),
                                '-o', str(executable)], capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)
        run = subprocess.run([str(executable), str(cls.originals), str(cls.directory)], capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.driver = json.loads(run.stdout)

    def test_driver_covers_the_ten_programs_and_twenty_pairs(self):
        self.assertEqual((self.driver['programs'], self.driver['pairs'], self.driver['variants'], self.driver['identical']), (10, 20, 40, 10))
        self.assertLessEqual(self.driver['max_arithmetic'], 8)  # PS 2.0 arithmetic budget is 64

    def test_gain_one_is_byte_identical_to_the_original(self):
        for key in PIXELS:
            self.assertEqual((self.directory / f'ps_{key}-source-0.bin').read_bytes(), (self.originals / f'ps_{key}.bin').read_bytes(), key)

    def test_gain_adds_one_def_and_one_colour_mul_before_the_native_output(self):
        for key in PIXELS:
            original, original_items, _ = shader.instructions((self.originals / f'ps_{key}.bin').read_bytes())
            for g, gain in enumerate(GAINS[1:], 1):
                words, items, _ = shader.instructions((self.directory / f'ps_{key}-source-{g}.bin').read_bytes())
                self.assertEqual(len(words), len(original) + 10, (key, gain))
                self.assertEqual(words[0], original[0], 'shader model retained')
                added = [item for item in items if span(words, item) not in {span(original, o) for o in original_items}]
                self.assertEqual([item['opcode'] for item in added], [DEF, MUL], (key, gain))
                definition, multiply = added
                self.assertEqual(shader.register_of(definition['words'][0]), (2, 31))
                self.assertEqual(struct.unpack('<4f', struct.pack('<4I', *definition['words'][1:])), (gain, 0., 0., 0.))
                # The DEF precedes the first declaration; the MUL is the penultimate instruction.
                first_dcl = next(item for item in items if item['opcode'] == DCL)
                self.assertLess(definition['dword'], first_dcl['dword'])
                self.assertEqual(items[-1]['opcode'], MOV)
                self.assertIs(items[-2], multiply)
                destination, source, constant = multiply['words']
                self.assertEqual((shader.register_of(destination), shader.mask_of(destination)), ((0, 0), 'xyz'))
                self.assertEqual((shader.register_of(source), shader.swizzle_of(source)), ((0, 0), 'xyzw'))
                self.assertEqual((shader.register_of(constant), shader.swizzle_of(constant)), ((2, 31), 'xxxx'))
                # The native output MOV and every original instruction are retained in order.
                retained = [span(words, item) for item in items if item is not definition and item is not multiply]
                self.assertEqual(retained, [span(original, o) for o in original_items], (key, gain))
                self.assertEqual(span(words, items[-1]), span(original, original_items[-1]))

    def test_alpha_and_original_constants_are_untouched(self):
        for key in PIXELS:
            original, original_items, _ = shader.instructions((self.originals / f'ps_{key}.bin').read_bytes())
            for g in (1, 2, 3):
                words, items, _ = shader.instructions((self.directory / f'ps_{key}-source-{g}.bin').read_bytes())
                writers_of_alpha = [item for item in items if item['opcode'] != DEF and 'w' in shader.mask_of(item['words'][0])
                                    and shader.register_of(item['words'][0]) == (0, 0)]
                original_writers = [item for item in original_items if item['opcode'] != DEF and 'w' in shader.mask_of(item['words'][0])
                                    and shader.register_of(item['words'][0]) == (0, 0)]
                self.assertEqual([span(words, i) for i in writers_of_alpha], [span(original, i) for i in original_writers], key)
                self.assertNotIn((2, 31), {shader.register_of(item['words'][0]) for item in original_items if item['opcode'] == DEF})


class LauncherGateTests(unittest.TestCase):
    def test_default_off_and_requires_hdr_only(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = launch(directory, *PREREQUISITES); self.assertEqual(code, 0, error)
            baseline = json.loads(output)['env']
            self.assertEqual(baseline['X3M_EMISSION_SOURCE_GAIN'], '1.0')
            # --linear-emissions / --linear-materials left the launcher on 2026-09-25: neither variable is sent.
            self.assertNotIn('X3M_LINEAR_EMISSIONS', baseline); self.assertNotIn('X3M_LINEAR_MATERIALS', baseline)
            self.assertEqual(baseline['X3M_TAA'], '0')
            code, output, error = launch(directory, *PREREQUISITES, '--emission-source-gain', '2'); self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual(env['X3M_EMISSION_SOURCE_GAIN'], '2.0')
            # The guide lights follow this gain and its key, so a gain above 1
            # also arms them (X3M_HULL_EMISSION_GAIN, same value); nothing else
            # in the environment moves.
            self.assertEqual(env['X3M_HULL_EMISSION_GAIN'], '2.0')
            self.assertEqual({k: v for k, v in env.items() if k not in ('X3M_EMISSION_SOURCE_GAIN', 'X3M_HULL_EMISSION_GAIN')},
                             {k: v for k, v in baseline.items() if k not in ('X3M_EMISSION_SOURCE_GAIN', 'X3M_HULL_EMISSION_GAIN')})
            self.assertEqual(baseline['X3M_HULL_EMISSION_GAIN'], '1.0')
            for bad in (('--motion-output', '--emission-source-gain', '2'),
                        (*PREREQUISITES, '--emission-source-gain', '0.5'), (*PREREQUISITES, '--emission-source-gain', '9'),
                        (*PREREQUISITES, '--emission-source-gain', 'nan'), (*PREREQUISITES, '--emission-source-gain', 'inf')):
                code, _, error = launch(directory, *bad); self.assertEqual(code, 2, bad); self.assertIn('--emission-source-gain', error)
            for boundary in ('1', '8'):
                code, output, error = launch(directory, *PREREQUISITES, '--emission-source-gain', boundary); self.assertEqual(code, 0, error)

    def test_effect_gain_option_is_gone(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = launch(directory, *PREREQUISITES, '--emission-source-gain', '5'); self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual(env['X3M_EMISSION_SOURCE_GAIN'], '5.0')
            self.assertNotIn('X3M_EFFECT_SOURCE_GAIN', env)
            for bad in ((*PREREQUISITES, '--effect-source-gain', '2'), (*PREREQUISITES, '--emission-source-gain', '2', '--effect-source-gain', '1'),
                        ('--motion-output', '--effect-source-gain', '2')):
                code, _, error = launch(directory, *bad); self.assertEqual(code, 2, bad)
                self.assertIn('unrecognized arguments', error); self.assertIn('--effect-source-gain', error)
        self.assertNotIn('--effect-source-gain', launch_help())

    def test_dll_gate_reads_the_variable_and_needs_hdr_only(self):
        # Source-substring guard against silent gate drift, not a semantics test.
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        block = source[source.index('X3M_EMISSION_SOURCE_GAIN=<g>'):][:2400]
        self.assertIn('x3m::config::get(L"X3M_EMISSION_SOURCE_GAIN",setting,32)', block)
        self.assertIn('value>=1.f&&value<=8.f', block)
        self.assertIn('if(!hdr_requested||excluded)emission_source_gain=1.f;', block)
        self.assertIn('const bool excluded=linear_emission_requested;', block)
        self.assertIn('configure_emission_source_gain(emission_source_gain)', source)
        for absent in ('linear_material_requested', 'taa_requested', 'screen_ownership', 'X3M_EFFECT_SOURCE_GAIN', 'effect_source_gain'):
            self.assertNotIn(absent, source if absent.startswith(('X3M_EFFECT', 'effect_')) else block)
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        admission = motion[motion.index('void MotionOutput::prepare_source_gain'):][:6400]
        for required in ('hdr_state_ != HdrState::Active', 'renderer::linear_emission_source_gain_blend(shadow_.states[3], shadow_.states[5]',
                         'shadow_.composition_blend[0], shadow_.composition_blend[1], shadow_.composition_blend[2])',
                         'if (verdict == renderer::SourceGainBlend::Blend) {', 'reason=blend ', 'source_gain_logged_[0]',
                         'const bool screen = verdict == renderer::SourceGainBlend::Screen;', 'reason=screen_substitute_failed', 'source_gain_logged_[1]'):
            self.assertIn(required, admission)
        # The separate alpha states are shadowed (no per-draw getter) and logged, never gated.
        self.assertIn('D3DRS_SRCBLENDALPHA, D3DRS_DESTBLENDALPHA, D3DRS_BLENDOPALPHA, D3DRS_BLENDFACTOR};', motion)
        self.assertIn('for (unsigned i = 0; i < 3; ++i) known = blend_known(i) && known; // the colour triple gates', admission)
        self.assertNotIn('composition_blend[3]', admission[:admission.index('log(')])
        self.assertNotIn('GetRenderState', admission)
        configure = motion[motion.index('void MotionOutput::configure_emission_source_gain(float gain)'):][:600]
        for required in ('emission_source_gain_requested_ = renderer::linear_emission_source_gain_valid(gain) && gain != 1.f;',
                         'emission_source_gain_ = emission_source_gain_requested_ ? gain : 1.f;'):
            self.assertIn(required, configure)
        # One variant per original program at the one gain; the bound pair selects it.
        contract = motion[motion.index('void MotionOutput::refresh_linear_emission_contract()'):][:1800]
        self.assertIn('renderer::linear_emission_pair_index(shadow_.vs_hash, shadow_.ps_hash)', contract)
        self.assertIn('shadow_.source_gain_eligible_variant = pair_reviewed ? shadow_.ps_source_gain_variant : nullptr;', contract)
        creation = motion[motion.index('// Source-only encoded gain (linear-emission-cost.md, "Implemented"):'):][:1400]
        self.assertIn('reinterpret_cast<const std::uint32_t*>(code), bytes / 4, emission_source_gain_, words);', creation)
        self.assertNotIn('family', creation)
        for absent in ('LinearEmissionFamily', 'source_gain_family', 'linear_emission_pair_info', 'source_gain_variant['):
            self.assertNotIn(absent, motion)
            self.assertNotIn(absent, (ROOT / 'src/proxy/motion_output.h').read_text())

    def test_screen_substitution_is_applied_before_the_bind_and_restored_after_the_draw(self):
        # The substitution is the additive option's (prepare_screen_additive:
        # DESTBLEND ONE set unconditionally, restored through the setter
        # shadow); the ONE/ONE path never touches DESTBLEND.
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        admission = motion[motion.index('void MotionOutput::prepare_source_gain'):motion.index('void MotionOutput::finish_source_gain')]
        substitute = admission.index('direct_call<SetRenderStateFn>(SetRenderState, D3DRS_DESTBLEND, D3DBLEND_ONE);')
        bind = admission.index('native<SetPsFn>(SetPixelShader)(device_, shadow_.source_gain_eligible_variant);')
        self.assertLess(admission.index('const bool screen = verdict == renderer::SourceGainBlend::Screen;'), substitute)
        self.assertLess(substitute, bind)
        self.assertEqual(admission.count('D3DRS_DESTBLEND, D3DBLEND_ONE'), 1)
        # Refusal of the substitution: nothing applied, counted as refused_screen, the draw stays native.
        refusal = admission[substitute:bind]
        self.assertIn('++source_gain_counts_.refused_screen;', refusal)
        self.assertIn('route.source_gain_screen = true;', refusal)
        self.assertNotIn('route.source_gain = true;', refusal)
        # A failed bind after the substitution unwinds it through the shadow.
        rollback = admission[bind:admission.index('route.source_gain = true;')]
        self.assertIn('direct_call<SetRenderStateFn>(SetRenderState, D3DRS_DESTBLEND, shadow_.composition_blend[1]);', rollback)
        self.assertIn('route.source_gain_screen = false;', rollback)
        self.assertIn('if (screen) ++source_gain_counts_.admitted_screen;', admission)
        finish = extract_function(motion, 'void MotionOutput::finish_source_gain(')
        # The program goes back from the route's owned restoration reference
        # (docs/architecture/ownership-shadow-lifetime-diagnosis.md), before
        # the blend state.
        self.assertLess(finish.index('SetPixelShader)(device_, route.restore_ps)'),
                        finish.index('D3DRS_DESTBLEND, shadow_.composition_blend[1]'))
        # No path writes a borrowed shadow shader pointer back to the device.
        for name in ('motion_output.cpp', 'sun_share_lane_inc.h'):
            text = (ROOT / 'src/proxy' / name).read_text()
            self.assertNotRegex(text, r'Set(Vertex|Pixel)Shader\)\(device_,\s*shadow_\.(vs|ps)\)')
        self.assertIn('if (route.source_gain_screen) {', finish)
        self.assertIn('invalidate_render_states();', finish)
        self.assertIn('what=source_gain', finish)
        self.assertIn('if (route.source_gain) finish_source_gain(route);', motion)
        # Screen with sRGB write refuses as blend: the substitution needs the linear FP16 target.
        renderer = (ROOT / 'src/renderer/linear_emission.cpp').read_text()
        law = extract_function(renderer, 'SourceGainBlend linear_emission_source_gain_blend(')
        self.assertLess(law.index('srgb_write'), law.index('SourceGainBlend::Screen'))
        header = (ROOT / 'src/proxy/motion_output.h').read_text()
        self.assertIn('bool source_gain_screen = false;', header)
        self.assertIn('void finish_source_gain(MotionRoute&) noexcept;', header)


REFUSED = re.compile(r'^emission_source_gain_refused device=(\d+) frame=(\d+) vs=([0-9a-f]{16}) ps=([0-9a-f]{16}) reason=(blend|screen_substitute_failed) '
                     r'blend=(\d+) src=(\d+) dst=(\d+) op=(\d+) sepalpha=(-1|\d+) srcalpha=(-1|\d+) dstalpha=(-1|\d+) opalpha=(-1|\d+) srgb=(\d+)( result=[0-9a-f]{8})?$')
FRAME = re.compile(r'^emission_source_gain_frame device=(\d+) frame=(\d+) gain=(\S+) admitted=(\d+) admitted_screen=(\d+) '
                   r'refused_blend=(\d+) refused_screen=(\d+) refused_other=(\d+) refused_unknown=(\d+) refused_state=(\d+) bind_failures=(\d+)$')
PAIR = re.compile(r'^emission_source_gain_pair device=(\d+) frame=(\d+) vs=([0-9a-f]{16}) ps=([0-9a-f]{16}) gain=(\S+) screen=([01])$')
STATE = re.compile(r'^emission_source_gain_refused_state device=(\d+) frame=(\d+) vs=([0-9a-f]{16}) ps=([0-9a-f]{16}) '
                   r'hdr=([01]) scene=([01]) open=([01]) recording=([01]) primitives=(\d+) msaa=([01]) blend=(-1|\d+) src=(-1|\d+) dst=(-1|\d+) op=(-1|\d+) sepalpha=(-1|\d+)$')


def rendered(fmt, *values):
    """printf-style rendering of a log format literal extracted from the source."""
    values_iter = iter(values)
    return re.sub(r'%(?:0?16|08)?(?:ll|l)?[xudsg]', lambda m: str(next(values_iter)), fmt)


class LogGrammarTests(unittest.TestCase):
    """The frame-totals and per-reason sample lines as the source formats them."""
    @classmethod
    def setUpClass(cls):
        cls.motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()

    def literal(self, prefix, index=0):
        matches = re.findall(r'log\("(' + prefix + r'[^"]*)"', self.motion)
        self.assertGreater(len(matches), index, prefix)
        return matches[index]

    def test_refused_sample_lines_carry_the_reason_and_alpha_triple(self):
        fmt = self.literal('emission_source_gain_refused device')
        self.assertEqual(fmt.count('%'), 13)
        line = rendered(fmt, 1, 1501, 'd5e1c75351ed3f04', '8360f422de08b5bd', 1, 2, 2, 1, 1, 5, 6, 1, 0)
        match = REFUSED.match(line); self.assertIsNotNone(match, line)
        self.assertEqual(match.group(5), 'blend')
        fmt = self.literal('emission_source_gain_refused device', 1)
        self.assertEqual(fmt.count('%'), 14)
        line = rendered(fmt, 1, 1501, 'd5e1c75351ed3f04', '8360f422de08b5bd', 1, 2, 4, 1, -1, -1, -1, -1, 0, '8876086c')
        match = REFUSED.match(line); self.assertIsNotNone(match, line)
        self.assertEqual(match.group(5), 'screen_substitute_failed')

    def test_frame_totals_line_and_its_gate(self):
        fmt = self.literal('emission_source_gain_frame ')
        self.assertEqual(fmt.count('%'), 11)
        line = rendered(fmt, 1, 1501, '2', 7, 5, 3, 2, 1, 1, 0, 0)
        match = FRAME.match(line); self.assertIsNotNone(match, line)
        self.assertEqual([int(match.group(i)) for i in range(4, 9)], [7, 5, 3, 2, 1])
        block = self.motion[self.motion.index('// One line per frame that saw at least one candidate draw'):][:1200]
        self.assertIn('if (g.admitted || g.refused_blend || g.refused_screen || other)', block)
        self.assertIn('const std::uint32_t other = g.refused_unknown + g.refused_state + g.bind_failures;', block)
        self.assertNotIn('if (capture_)', block)
        self.assertIn('source_gain_counts_ = {};', block)

    def test_sample_cap_is_per_reason_and_per_device_epoch(self):
        header = (ROOT / 'src/proxy/motion_output.h').read_text()
        self.assertIn('std::uint32_t source_gain_logged_[4]{};', header)
        self.assertIn('admitted_screen = 0, refused_blend = 0, refused_screen = 0', header)
        self.assertIn('for (auto& logged : source_gain_logged_) logged = 0;', self.motion)
        self.assertIn('source_gain_pair_logged_ = 0; // and its first admission per pair', self.motion)
        self.assertIn('source_gain_logged_[2] < failure_log_limit', self.motion)
        self.assertIn('source_gain_logged_[3] < failure_log_limit', self.motion)
        self.assertIn('constexpr unsigned failure_log_limit = 16;', self.motion)

    def test_first_admission_per_pair_line_is_bounded_to_twenty(self):
        fmt = self.literal('emission_source_gain_pair ')
        self.assertEqual(fmt.count('%'), 6)
        line = rendered(fmt, 1, 1501, 'd5e1c75351ed3f04', '8360f422de08b5bd', '2', 1)
        match = PAIR.match(line); self.assertIsNotNone(match, line); self.assertEqual(match.group(6), '1')
        admission = self.motion[self.motion.index('void MotionOutput::prepare_source_gain'):][:7200]
        self.assertIn('1u << shadow_.source_gain_pair : 0u;', admission)
        self.assertIn('if (bit && !(source_gain_pair_logged_ & bit)) {', admission)
        self.assertIn('source_gain_pair_logged_ |= bit;', admission)
        self.assertIn('std::uint32_t source_gain_pair_logged_ = 0;', (ROOT / 'src/proxy/motion_output.h').read_text())

    def test_state_refusal_sample_names_the_failed_predicate(self):
        fmt = self.literal('emission_source_gain_refused_state ')
        self.assertEqual(fmt.count('%'), 15)
        line = rendered(fmt, 1, 3510, 'd5e1c75351ed3f04', '8360f422de08b5bd', 1, 0, 1, 0, 32, 0, 1, 2, 2, 1, 1)
        match = STATE.match(line); self.assertIsNotNone(match, line)
        self.assertEqual(match.group(6), '0')
        line = rendered(fmt, 1, 3510, 'd5e1c75351ed3f04', '8360f422de08b5bd', 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1)
        self.assertIsNotNone(STATE.match(line), line)


class RegistryTests(unittest.TestCase):
    """The twenty pairs in registry order, without a family, in the source and
    through the compiled lookup; unreviewed pairs return the count."""
    def test_registry_rows(self):
        source = (ROOT / 'src/renderer/linear_emission.cpp').read_text()
        table = source[source.index('constexpr Pair pairs[] = {'):source.index('};', source.index('constexpr Pair pairs[] = {'))]
        rows = re.findall(r'\{0x([0-9a-f]{16})ull,0x([0-9a-f]{16})ull\}', table)
        self.assertEqual(len(rows), 20)
        self.assertEqual(len(set(rows)), 20, 'a pair listed twice')
        self.assertEqual([(VERTICES[v], PIXELS[p]) for v, p in PAIRS], rows)
        self.assertNotIn('Family', table)
        self.assertIn('static_assert(sizeof(pairs)/sizeof(pairs[0])==linear_emission_pair_count', source)
        header = (ROOT / 'src/renderer/linear_emission.h').read_text()
        for absent in ('LinearEmissionFamily', 'linear_emission_pair_info', 'linear_emission_family_name'):
            self.assertNotIn(absent, header); self.assertNotIn(absent, source)

    def test_compiled_lookup_matches_the_table(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise unittest.SkipTest('host C++ compiler required')
        driver = '#include "%s"\n#include <cstdio>\nint main(){using namespace x3m::renderer;' % (ROOT / 'src/renderer/linear_emission.cpp')
        driver += 'const unsigned long long vs[]={%s};const unsigned long long ps[]={%s};' % (
            ','.join('0x%sull' % v for v in VERTICES), ','.join('0x%sull' % p for p in PIXELS))
        driver += ('for(unsigned v=0;v<8;++v)for(unsigned p=0;p<10;++p)std::printf("%u ",linear_emission_pair_index(vs[v],ps[p]));'
                   'std::printf("%d %u", int(linear_emission_pair_reviewed(vs[0],ps[0]))+int(linear_emission_pair_reviewed(vs[0],ps[1])), linear_emission_pair_count);return 0;}\n')
        with tempfile.TemporaryDirectory(prefix='x3-source-gain-registry-') as directory:
            source = Path(directory) / 'registry.cpp'; source.write_text(driver)
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(Path(directory) / 'registry')],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(Path(directory) / 'registry')], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        *cells, reviewed, count = run.stdout.split()
        self.assertEqual((reviewed, count), ('1', '20'))
        expected = {pair: str(index) for index, pair in enumerate(PAIRS)}
        self.assertEqual(cells, [expected.get((v, p), '20') for v in range(8) for p in range(10)])
        self.assertEqual(sum(cell != '20' for cell in cells), 20)


class BlendLawTests(unittest.TestCase):
    """Truth table of the shared blend admission, compiled from the renderer source."""
    ONE, INVSRCCOLOR, SRCALPHA, INVSRCALPHA, ZERO, ADD, SUBTRACT = 2, 4, 5, 6, 1, 1, 2
    CASES = [  # (blend_enable, srgb, src, dst, op) -> verdict (0 admit, 1 blend, 2 screen); sRGB write refuses the screen substitution too
        ((1, 0, 2, 2, 1), 0), ((1, 0, 2, 4, 1), 2), ((0, 0, 2, 2, 1), 1), ((1, 1, 2, 2, 1), 1),
        ((1, 0, 5, 6, 1), 1), ((1, 0, 2, 2, 2), 1), ((1, 0, 2, 4, 2), 1), ((1, 0, 1, 4, 1), 1),
        ((1, 0, 2, 1, 1), 1), ((1, 1, 2, 4, 1), 1), ((7, 0, 2, 2, 1), 0), ((1, 0, 2, 3, 1), 1), ((0, 0, 2, 4, 1), 1)]

    def test_truth_table(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise unittest.SkipTest('host C++ compiler required')
        driver = '#include "%s"\n#include <cstdio>\nint main(){using namespace x3m::renderer;' % (ROOT / 'src/renderer/linear_emission.cpp')
        driver += ''.join('std::printf("%%d ",int(linear_emission_source_gain_blend(%d,%d,%d,%d,%d)));' % inputs for inputs, _ in self.CASES)
        driver += 'return 0;}\n'
        with tempfile.TemporaryDirectory(prefix='x3-source-gain-blend-') as directory:
            source = Path(directory) / 'blend.cpp'; source.write_text(driver)
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(Path(directory) / 'blend')],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(Path(directory) / 'blend')], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual([int(v) for v in run.stdout.split()], [verdict for _, verdict in self.CASES])

    def test_fixture_and_runner_exercise_the_screen_substitution(self):
        fixture = (ROOT / 'verification/probe/linear_emission_fixture.cpp').read_text()
        for absent in ('family', 'Family', 'linear_emission_pair_info', 'split_gains'):
            self.assertNotIn(absent, fixture)
        experiment = fixture[fixture.index('void source_gain_experiment'):][:6400]
        for required in ('linear_emission_source_gain_blend(', 'x3m::renderer::linear_emission_pair_index(',
                         'if (v && verdict != SourceGainBlend::Blend && source_gains[slots[v - 1]] != 1) {',
                         'if (verdict == SourceGainBlend::Screen) {', 'f.rs(D3DRS_DESTBLEND, D3DBLEND_ONE);',
                         '"screen op admitted as screen"', 'need(case_substituted == (screen_case ? 3u : 0u), "substitution count per case");',
                         'admission=%s', 'substituted=%u'):
            self.assertIn(required, experiment)
        # The substitution comes after the state readback the verdict is taken from and before the bind.
        self.assertLess(experiment.index('api(device->GetRenderState(read[i], &state[i]));'), experiment.index('f.rs(D3DRS_DESTBLEND, D3DBLEND_ONE);'))
        self.assertLess(experiment.index('f.rs(D3DRS_DESTBLEND, D3DBLEND_ONE);'), experiment.index('api(device->SetPixelShader(f.source_gain_variants[pixel][slots[v - 1]].p));'))
        self.assertIn('constexpr float source_gains[] = {1, 2, 3.5f, 8, 5};', fixture)
        self.assertIn('constexpr unsigned screen_slots[] = {0, 1, 4, 3};', fixture)
        runner = (ROOT / 'verification/probe/run_linear_emission.py').read_text()
        for absent in ('family', 'ENGINE_PAIRS', 'SPLIT_CONFIGURATIONS'):
            self.assertNotIn(absent, runner)
        for required in ("label='source_gain_separate_alpha'", "label='source_gain_screen'", "alpha=2", "kind=2", 'SCREEN_GAINS = (1.,2.,5.,8.)',
                         "r['admission']==('screen' if c['screen'] else 'admit')", "(5,6)", "int(r['substituted'])==(3 if c['screen'] else 0)",
                         'for background in (0,2):', '8192 if background==2 else 0', "'gain 1 variant is not the native image'",
                         "low,high=half(lo+bg*(1-lo)),half(hi+bg*(1-hi))", "low,high=half(g*lo+bg),half(g*hi+bg)", "'native screen law'", "'substituted law'",
                         "'not brighter than the native screen draw'", 'screen_substituted=dict('):
            self.assertIn(required, runner)
        sys.path.insert(0, str(ROOT / 'verification/probe'))
        import run_linear_emission as module
        cases = module.source_gain_cases()
        screen = [c for c in cases if c['screen']]
        self.assertEqual((len(cases), len(screen)), (160, 40))
        self.assertEqual(sorted({c['background'] for c in screen}), [0, 2])
        self.assertEqual([c['label'] for c in cases[:80]], ['source_gain'] * 80)
        self.assertEqual(module.effective_gains(screen[0]), [1., 2., 5., 8.])
        self.assertEqual(module.effective_gains(cases[0]), [1., 2., 3.5, 8.])
        self.assertTrue(all(c['flags'] & 8192 == (8192 if c['background'] == 2 else 0) for c in cases))


if __name__ == '__main__':
    unittest.main()
