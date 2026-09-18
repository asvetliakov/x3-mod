"""Host oracle of the hull-program emitter gain (emitter plan phase 3).

The twelve ps_3_0 material programs that draw the ADD ONE/ONE emitters the
twenty effects pairs cannot reach (position lights, deco flares, warning signs,
warp tunnels; docs/reverse-engineering/effect-shader-users.md, "Additive
emitters drawn by material programs"). Transformer: gain 1 is byte-identical to
the original; gain G adds exactly one `def c223 = (G, 0, 0, 0)` at the first
declaration, redirects the final colour instruction (`add oC0.xyz, r1, r0` or
the XT `mad oC0.xyz, r1, r2.z, r0`) to write r0.xyz with its opcode, _pp, mask
and operands kept, and adds one `mul oC0.xyz, r0, c223.x` in its place: the
whole colour output of the ONE/ONE draw is gained, because the emitter art of
these materials is diffuse-authored with the lightmap slot (the r0 sample)
black (archive check, 2026-09-17). Every other original word and the native
alpha MUL are retained in order, so the alpha lane and a black pixel are
bit-identical. Blend law: only ADD ONE/ONE admits; screen is never substituted
in this population. Launcher: --hull-emitters applies the existing
--emission-source-gain G to this population and requires it and --hdr.
No game assets bundled.
"""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
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

# Six XT_standard_lighting.fx programs, then six standard_lighting.fx programs.
PIXELS = ('5f82ecacd39529cd', '6733b119142c8d42', 'fffdabd910793aba', '496049cec2066ed3',
          'e6794b6ec37ff71a', 'f1b0e820c7b488c3', '0c1f3f0f440e4a0c', '7c83ed50c9894e44',
          '99153c144030c396', '64bac8bb307eb896', 'c1452981fd0bff64', 'e70adc744a38ca59')
MODULATED = PIXELS[:6]  # the XT tail is `mad oC0.xyz, r1, r2.z, r0`
GAINS = (1., 2., 3.5, 8.)
DEF, MUL, MOV, DCL, ADD, MAD = 81, 5, 1, 31, 2, 4
GAIN_CONSTANT = 223
COLOUR_OUTPUT = (8, 0)  # D3DSPR_COLOROUT oC0
PREREQUISITES = ['--motion-output', '--hdr']
# ps_3_0 executable budget (D3D9 MaxPixelShader30InstructionSlots minimum).
SLOT_BUDGET = 512


def launch_help():
    spec = importlib.util.spec_from_file_location('hull_gain_manage_help', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    output = io.StringIO()
    with mock.patch.object(sys, 'argv', ['manage.py', 'launch', '--help']), contextlib.redirect_stdout(output):
        with contextlib.suppress(SystemExit):
            module.main()
    return output.getvalue()


def launch(directory, *args):
    spec = importlib.util.spec_from_file_location('hull_gain_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    game = Path(directory) / 'game'; game.mkdir(exist_ok=True); (game / 'X3AP.exe').touch()
    wine = Path(directory) / 'wine'; wine.touch()
    argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
    output, error = io.StringIO(), io.StringIO()
    with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
            mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
            contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
        try: module.main()
        except SystemExit as exit_error: return exit_error.code, output.getvalue(), error.getvalue()
    return 0, output.getvalue(), error.getvalue()


def span(words, item):
    return tuple(words[item['dword']:item['dword'] + item['length'] + 1])


def slots(items):
    """Arithmetic and texture slots at the documented macro costs."""
    arithmetic = texture = 0
    for item in items:
        name = shader.OPCODES.get(item['opcode'])
        if name in ('dcl', 'def', 'defb', 'defi'):
            continue
        if name in shader.TEXTURE_OPCODES:
            texture += 1
        else:
            arithmetic += shader.SLOT_COSTS.get(name, 1)
    return arithmetic, texture


class HullGainTransformerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.originals = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
        if not all((cls.originals / f'ps_{key}.bin').is_file() for key in PIXELS):
            reason = f'local twelve-original ps_3_0 hull corpus unavailable under {cls.originals} (X3M_SHADER_PROGRAM_DIRECTORY)'
            if os.environ.get('X3M_REQUIRE_SHADER_CORPUS') == '1':
                raise AssertionError(reason)
            print('SKIP:', reason, file=sys.stderr)
            raise unittest.SkipTest(reason)
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise RuntimeError('host C++ compiler required')
        temporary = tempfile.TemporaryDirectory(prefix='x3-emission-hull-gain-')
        cls.addClassCleanup(temporary.cleanup)
        cls.directory = Path(temporary.name)
        executable = cls.directory / 'structure'
        build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                str(ROOT / 'verification/probe/linear_emission_hull_gain_structure.cpp'),
                                '-o', str(executable)], capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)
        run = subprocess.run([str(executable), str(cls.originals), str(cls.directory)], capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.driver = json.loads(run.stdout)

    def test_driver_covers_the_twelve_programs(self):
        self.assertEqual((self.driver['programs'], self.driver['modulated'], self.driver['variants'],
                          self.driver['identical']), (12, 6, 48, 12))
        # Only ADD ONE/ONE admits, out of the 128 enabled/sRGB/factor/op combinations.
        self.assertEqual((self.driver['blend_admitted'], self.driver['blend_refused']), (1, 127))

    def test_gain_one_is_byte_identical_to_the_original(self):
        for key in PIXELS:
            self.assertEqual((self.directory / f'ps_{key}-hull-0.bin').read_bytes(),
                             (self.originals / f'ps_{key}.bin').read_bytes(), key)

    def test_gain_adds_one_def_and_one_whole_output_mul(self):
        for key in PIXELS:
            original, original_items, _ = shader.instructions((self.originals / f'ps_{key}.bin').read_bytes())
            self.assertEqual(original[0], 0xffff0300, key)
            native_colour, native_alpha = original_items[-2], original_items[-1]
            self.assertEqual(native_colour['opcode'], MAD if key in MODULATED else ADD, key)
            self.assertEqual((shader.register_of(native_colour['words'][0]), shader.mask_of(native_colour['words'][0])), (COLOUR_OUTPUT, 'xyz'))
            for g, gain in enumerate(GAINS[1:], 1):
                words, items, _ = shader.instructions((self.directory / f'ps_{key}-hull-{g}.bin').read_bytes())
                self.assertEqual(len(words), len(original) + 10, (key, gain))
                self.assertEqual(words[0], original[0], 'shader model retained')
                originals = {span(original, o) for o in original_items}
                added = [item for item in items if span(words, item) not in originals]
                self.assertEqual([item['opcode'] for item in added], [DEF, native_colour['opcode'], MUL], (key, gain))
                definition, colour, multiply = added
                self.assertEqual(shader.register_of(definition['words'][0]), (2, GAIN_CONSTANT))
                self.assertEqual(struct.unpack('<4f', struct.pack('<4I', *definition['words'][1:])), (gain, 0., 0., 0.))
                # The DEF precedes every declaration of the original.
                self.assertLess(definition['dword'], min(item['dword'] for item in items if item is not definition))
                # The redirected colour instruction is third from the end, the
                # whole-output MUL second, the native alpha MUL (verbatim) last.
                self.assertIs(items[-3], colour); self.assertIs(items[-2], multiply)
                self.assertEqual(span(words, items[-1]), span(original, native_alpha), 'the native alpha MUL is verbatim')
                self.assertEqual(colour['token'], native_colour['token'], 'opcode, length and modifiers of the colour instruction kept')
                self.assertEqual((shader.register_of(colour['words'][0]), shader.mask_of(colour['words'][0])), ((0, 0), 'xyz'), 'redirected to r0.xyz')
                self.assertEqual(colour['words'][0] & 0x00f00000, native_colour['words'][0] & 0x00f00000, 'destination _pp kept')
                self.assertEqual(colour['words'][1:], native_colour['words'][1:], 'operands verbatim')
                self.assertEqual(shader.register_of(colour['words'][-1]), (0, 0), 'the added operand is r0')
                destination, source, constant = multiply['words']
                self.assertEqual(destination, native_colour['words'][0], 'the MUL writes oC0.xyz with the original destination token')
                self.assertEqual((shader.register_of(source), shader.swizzle_of(source)), ((0, 0), 'xyzw'))
                self.assertEqual((shader.register_of(constant), shader.swizzle_of(constant)), ((2, GAIN_CONSTANT), 'xxxx'))
                # Every original instruction before the colour site, in order.
                retained = [span(words, item) for item in items[:-3] if item is not definition]
                self.assertEqual(retained, [span(original, o) for o in original_items[:-2]], (key, gain))

    def test_slot_counts_and_the_untouched_gain_constant(self):
        for key in PIXELS:
            original, original_items, _ = shader.instructions((self.originals / f'ps_{key}.bin').read_bytes())
            original_arithmetic, original_texture = slots(original_items)
            # The original never reads or defines the gain constant, and no
            # operand is relative (DEF literals and DCL payloads are not registers).
            for item in original_items:
                if item['opcode'] == DEF:
                    self.assertNotEqual(shader.register_of(item['words'][0]), (2, GAIN_CONSTANT), key)
                    continue
                if item['opcode'] == DCL:
                    continue
                for word in item['words']:
                    self.assertNotEqual(shader.register_of(word), (2, GAIN_CONSTANT), key)
                    self.assertFalse(word & 0x2000, (key, 'no relative addressing'))
            for g in (1, 2, 3):
                words, items, _ = shader.instructions((self.directory / f'ps_{key}-hull-{g}.bin').read_bytes())
                arithmetic, texture = slots(items)
                self.assertEqual((arithmetic, texture), (original_arithmetic + 1, original_texture), key)
                self.assertEqual(len(items), len(original_items) + 2, key)
                self.assertLessEqual(arithmetic + texture, SLOT_BUDGET, key)
                self.assertEqual(sum(item['opcode'] == DEF for item in items),
                                 sum(item['opcode'] == DEF for item in original_items) + 1, key)
                self.assertEqual(sum(item['opcode'] == DCL for item in items),
                                 sum(item['opcode'] == DCL for item in original_items), key)

    def test_alpha_and_the_lit_term_are_untouched(self):
        for key in PIXELS:
            original, original_items, _ = shader.instructions((self.originals / f'ps_{key}.bin').read_bytes())
            writers = lambda items, register: [item for item in items if item['opcode'] != DEF and item['words'] and
                                               shader.register_of(item['words'][0]) == register]
            for g in (1, 2, 3):
                words, items, _ = shader.instructions((self.directory / f'ps_{key}-hull-{g}.bin').read_bytes())
                # oC0 has the same two writers (colour then alpha); the alpha
                # writer is the original's; r1 (the lit term) is never
                # rewritten; the only new r0 writer is the redirected colour
                # instruction, whose colour lanes the MUL scales.
                colour_writers, original_colour_writers = writers(items, COLOUR_OUTPUT), writers(original_items, COLOUR_OUTPUT)
                self.assertEqual(len(colour_writers), len(original_colour_writers), key)
                self.assertEqual(span(words, colour_writers[-1]), span(original, original_colour_writers[-1]), key)
                self.assertEqual([span(words, i) for i in writers(items, (0, 1))],
                                 [span(original, i) for i in writers(original_items, (0, 1))], key)
                self.assertEqual(len(writers(items, (0, 0))), len(writers(original_items, (0, 0))) + 1, key)
                for item in items[-3:-1]:
                    self.assertNotIn('w', shader.mask_of(item['words'][0]), 'neither the redirect nor the gain MUL writes alpha')


class LauncherGateTests(unittest.TestCase):
    def test_default_off_and_requires_the_gain_and_hdr(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = launch(directory, *PREREQUISITES); self.assertEqual(code, 0, error)
            baseline = json.loads(output)['env']
            self.assertEqual(baseline['X3M_HULL_EMISSION_GAIN'], '1.0')
            # The guide lights follow the effects gain and its key: an
            # --emission-source-gain above 1 implies --hull-emitters and hands
            # them its value, so the user selects both with one option.
            code, output, error = launch(directory, *PREREQUISITES, '--emission-source-gain', '2')
            self.assertEqual(code, 0, error)
            implied = json.loads(output)['env']
            self.assertEqual((implied['X3M_HULL_EMISSION_GAIN'], implied['X3M_EMISSION_SOURCE_GAIN']), ('2.0', '2.0'))
            code, output, error = launch(directory, *PREREQUISITES, '--emission-source-gain', '2', '--hull-emitters')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_HULL_EMISSION_GAIN'], env['X3M_EMISSION_SOURCE_GAIN']), ('2.0', '2.0'))
            self.assertEqual(env, implied, 'the explicit switch adds nothing to the implied population')
            # Gain 1 is off: no variant, so nothing is implied either.
            code, output, error = launch(directory, *PREREQUISITES, '--emission-source-gain', '1')
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_HULL_EMISSION_GAIN'], '1.0')
            # The converted route takes the same implication: the guide lights
            # are the effects family (the twelve programs are original hull
            # programs either way), so --linear-materials changes nothing here.
            code, output, error = launch(directory, *PREREQUISITES, '--hdr-tonemap', '--linear-materials',
                                         '--emission-source-gain', '2')
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_HULL_EMISSION_GAIN'], '2.0')
            # An explicit hull gain still overrides the effects gain's value,
            # with or without the switch.
            code, output, error = launch(directory, *PREREQUISITES, '--emission-source-gain', '2', '--hull-emission-gain', '3')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_HULL_EMISSION_GAIN'], env['X3M_EMISSION_SOURCE_GAIN']), ('3.0', '2.0'))
            # Own gain: --hull-emission-gain G stands without the effects gain
            # (the hull population is bracketed alone) and wins over it.
            code, output, error = launch(directory, *PREREQUISITES, '--hull-emitters', '--hull-emission-gain', '4')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_HULL_EMISSION_GAIN'], env['X3M_EMISSION_SOURCE_GAIN']), ('4.0', '1.0'))
            code, output, error = launch(directory, *PREREQUISITES, '--emission-source-gain', '2', '--hull-emitters', '--hull-emission-gain', '8')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_HULL_EMISSION_GAIN'], env['X3M_EMISSION_SOURCE_GAIN']), ('8.0', '2.0'))
            for bad, message in ((PREREQUISITES + ['--hull-emitters'], '--emission-source-gain'),
                                 (PREREQUISITES + ['--hull-emitters'], '--hull-emission-gain'),
                                 (['--motion-output', '--emission-source-gain', '2', '--hull-emitters'], '--emission-source-gain requires --hdr'),
                                 (['--motion-output', '--hull-emitters', '--hull-emission-gain', '2'], '--hull-emitters requires --hdr'),
                                 (PREREQUISITES + ['--emission-source-gain', '9', '--hull-emitters'], '--emission-source-gain'),
                                 (PREREQUISITES + ['--hull-emission-gain', '2'], '--hull-emission-gain requires --hull-emitters'),
                                 (PREREQUISITES + ['--hull-emitters', '--hull-emission-gain', '9'], '--hull-emission-gain must be finite'),
                                 (PREREQUISITES + ['--hull-emitters', '--hull-emission-gain', '0.5'], '--hull-emission-gain must be finite'),
                                 (PREREQUISITES + ['--hull-emitters', '--hull-emission-gain', '1'], 'must be above 1')):
                code, _, error = launch(directory, *bad); self.assertEqual(code, 2, bad); self.assertIn(message, error)
            for boundary in ('1', '8'):
                code, _, error = launch(directory, *PREREQUISITES, '--emission-source-gain', boundary, '--hull-emitters')
                self.assertEqual(code, 0, error)
            for boundary in ('1.5', '8'):
                code, _, error = launch(directory, *PREREQUISITES, '--hull-emitters', '--hull-emission-gain', boundary)
                self.assertEqual(code, 0, error)
        help_text = launch_help()
        self.assertIn('--hull-emitters', help_text); self.assertIn('--hull-emission-gain', help_text)
        self.assertIn('Ctrl+Shift+F6', help_text)  # the guide lights moved to the effects key


class FixtureCoverageTests(unittest.TestCase):
    """Source-substring guard against silent drift of the GPU slice contract."""

    def test_fixture_submits_the_original_pair_and_requires_gain_one_identity(self):
        fixture = (ROOT / 'verification/probe/linear_material_fixture.cpp').read_text()
        transform = fixture.index('Words transform(const Case &c, unsigned mode, bool pixel)')
        block = fixture[fixture.index('    if (mode == 12) {', transform):][:1400]
        for required in ('require(!xt_default(c), "hull emitter gain runs on original pairs only");',
                         'linear_emission_hull_source_gain_variant(original.data(), original.size(), 1.0f, identity)',
                         'identity == original, "gain 1 is the original program"',
                         'output.size() == original.size() + 10'):
            self.assertIn(required, block)
        self.assertIn('const bool targets = mode != 0 && mode != 12;', fixture)
        self.assertIn('linear_emission.cpp', (ROOT / 'verification/probe/build_linear_material.sh').read_text())

    def test_runner_slice_covers_one_pair_per_submittable_program(self):
        runner = (ROOT / 'verification/probe/run_linear_material.py').read_text()
        self.assertIn('HULL_GAIN_PAIRS = (40, 41, 50, 51, 60, 61, 150, 151, 152, 153)', runner)
        self.assertIn("'src/renderer/linear_emission.cpp', 'src/renderer/linear_emission.h',", runner)
        self.assertIn('--hull-emission-gain', runner)


if __name__ == '__main__':
    unittest.main()
