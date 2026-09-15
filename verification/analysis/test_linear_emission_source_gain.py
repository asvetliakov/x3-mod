"""Host oracle of the source-only encoded emission gain (--emission-source-gain).

Transformer: gain 1 is byte-identical to the original; gain G adds exactly one
`def c31 = (G, 0, 0, 0)` and one `mul r0.xyz, r0, c31.x` immediately before
the untouched native `mov oC0, r0`; every original instruction, the native
output and raw alpha are retained (docs/architecture/linear-emission-cost.md,
"Implemented"). Launcher gate: requires --hdr only. No game assets bundled.
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

PIXELS = ('8360f422de08b5bd', '9975b706e5a1c999', 'ff2473e73a6bdfa1', '8559522220507d5e', '875e780adb131b16',
          '39f3b4d5b6a5aaed', '47e15e20d63b0e93', '846c5c1a549f9491', 'c6dacb8f74b65c97', 'f0c91793a75e1203')
GAINS = (1., 2., 3.5, 8.)
DEF, MUL, MOV, DCL = 81, 5, 1, 31
PREREQUISITES = ['--motion-output', '--hdr']


def span(words, item):
    return tuple(words[item['dword']:item['dword'] + item['length'] + 1])


def launch(directory, *args):
    spec = importlib.util.spec_from_file_location('source_gain_manage', ROOT / 'tools/manage.py')
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
            self.assertEqual((baseline['X3M_LINEAR_EMISSIONS'], baseline['X3M_LINEAR_MATERIALS'], baseline['X3M_TAA']), ('0', '0', '0'))
            code, output, error = launch(directory, *PREREQUISITES, '--emission-source-gain', '2'); self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual(env['X3M_EMISSION_SOURCE_GAIN'], '2.0')
            self.assertEqual({k: v for k, v in env.items() if k != 'X3M_EMISSION_SOURCE_GAIN'},
                             {k: v for k, v in baseline.items() if k != 'X3M_EMISSION_SOURCE_GAIN'})
            for bad in (('--motion-output', '--emission-source-gain', '2'),
                        (*PREREQUISITES, '--taa', '--hdr-tonemap', '--linear-emissions', '--emission-source-gain', '2'),
                        (*PREREQUISITES, '--emission-source-gain', '0.5'), (*PREREQUISITES, '--emission-source-gain', '9'),
                        (*PREREQUISITES, '--emission-source-gain', 'nan'), (*PREREQUISITES, '--emission-source-gain', 'inf')):
                code, _, error = launch(directory, *bad); self.assertEqual(code, 2, bad); self.assertIn('--emission-source-gain', error)
            for boundary in ('1', '8'):
                code, output, error = launch(directory, *PREREQUISITES, '--emission-source-gain', boundary); self.assertEqual(code, 0, error)

    def test_dll_gate_reads_the_variable_and_needs_hdr_only(self):
        # Source-substring guard against silent gate drift, not a semantics test.
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        block = source[source.index('X3M_EMISSION_SOURCE_GAIN=<g>'):][:2400]
        self.assertIn('GetEnvironmentVariableW(L"X3M_EMISSION_SOURCE_GAIN",setting,32)', block)
        self.assertIn('value>=1.f&&value<=8.f', block)
        self.assertIn('if(!hdr_requested||excluded)emission_source_gain=1.f;', block)
        self.assertIn('const bool excluded=linear_emission_requested;', block)
        self.assertIn('configure_emission_source_gain(emission_source_gain)', source)
        for absent in ('linear_material_requested', 'taa_requested', 'screen_ownership'):
            self.assertNotIn(absent, block)
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        admission = motion[motion.index('void MotionOutput::prepare_source_gain'):][:2600]
        for required in ('hdr_state_ != HdrState::Active', 'D3DBLEND_ONE', 'D3DBLENDOP_ADD', 'reason=blend', 'shadow_.composition_blend[3]'):
            self.assertIn(required, admission)
        self.assertIn('emission_source_gain_requested_ = renderer::linear_emission_source_gain_valid(gain) && gain != 1.f;', motion)


if __name__ == '__main__':
    unittest.main()
