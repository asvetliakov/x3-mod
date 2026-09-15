"""Host oracle of the AdditiveGain PS2 outputs (--screen-emission-additive G).

Transformer: `LinearEmissionSm1Outputs::AdditiveGain` authors the nine SM1
bullet pairs' native PS2 path with `mul r0.rgb, r0, c31.x` inserted immediately
before the output `mov oC0, r0`; alpha keeps the native value, so the alpha test
is native (docs/architecture/screen-emission-region.md, "Additive option"). At
G = 1 the runtime creates no variant and binds the original program, so the
bound bytes are the original's. This module pins only the AdditiveGain report of
verification/probe/screen_emission_additive_structure.cpp; the SM1 sweep's own
report and its pinned counters are untouched. No game assets bundled.
"""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import inspect_motion_output_profiles as shader

GAINS = (1., 2., 3.5, 8.)
DEF, MUL, MOV = 81, 5, 1
PS2_ARITHMETIC_SLOTS, PS2_TEXTURE_SLOTS, PS2_FLOAT_CONSTANTS = 64, 32, 32
TEXTURE_OPCODES = {66}  # texld
SLOT_COST = {32: 3}  # pow


def span(words, item):
    return tuple(words[item['dword']:item['dword'] + item['length'] + 1])


class AdditiveGainTransformTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.originals = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
        if not cls.originals.is_dir():
            reason = f'local shader corpus unavailable under {cls.originals} (X3M_SHADER_PROGRAM_DIRECTORY)'
            if os.environ.get('X3M_REQUIRE_SHADER_CORPUS') == '1':
                raise AssertionError(reason)
            print('SKIP:', reason, file=sys.stderr)
            raise unittest.SkipTest(reason)
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise RuntimeError('host C++ compiler required')
        temporary = tempfile.TemporaryDirectory(prefix='x3-screen-emission-additive-')
        cls.addClassCleanup(temporary.cleanup)
        cls.directory = Path(temporary.name)
        executable = cls.directory / 'structure'
        build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                str(ROOT / 'verification/probe/screen_emission_additive_structure.cpp'),
                                '-o', str(executable)], capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)
        run = subprocess.run([str(executable), str(cls.originals), str(cls.directory)], capture_output=True, text=True)
        if run.returncode:
            missing = 'open original' in run.stderr
            if missing and os.environ.get('X3M_REQUIRE_SHADER_CORPUS') != '1':
                print('SKIP:', f'nine-pair SM1 originals absent from {cls.originals}', file=sys.stderr)
                raise unittest.SkipTest('nine-pair SM1 originals absent')
            raise AssertionError(run.stdout + run.stderr)
        cls.driver = json.loads(run.stdout)
        cls.rows = cls.driver['rows']

    def program(self, index, suffix):
        return shader.instructions((self.directory / f'additive_{index}-{suffix}.bin').read_bytes())

    def test_driver_covers_the_nine_pairs_at_four_gains(self):
        self.assertEqual((self.driver['pairs'], self.driver['programs'], self.driver['gains']), (9, 6, 4))
        self.assertEqual((self.driver['variants'], self.driver['identical']), (36, 9))
        self.assertEqual(len(self.rows), 9)
        self.assertEqual(len({row['pixel'] for row in self.rows}), 6)
        self.assertEqual(len({row['vertex'] for row in self.rows}), 9)

    def test_gain_one_binds_the_original_bytes(self):
        for index, row in enumerate(self.rows):
            self.assertEqual((self.directory / f'additive_{index}-0.bin').read_bytes(),
                             (self.originals / f"ps_{row['pixel']}.bin").read_bytes(), row['pixel'])

    def test_variant_is_ps_2_0_with_one_added_colour_mul_and_its_constant(self):
        for index, row in enumerate(self.rows):
            native, native_items, _ = self.program(index, 'native')
            for g, gain in enumerate(GAINS[1:], 1):
                words, items, _ = self.program(index, g)
                key = (row['pixel'], gain)
                self.assertEqual(words[0], 0xffff0200, key)
                self.assertEqual(len(words), len(native) + 10, key)
                added = [item for item in items if span(words, item) not in {span(native, n) for n in native_items}]
                self.assertEqual([item['opcode'] for item in added], [DEF, MUL], key)
                definition, multiply = added
                self.assertEqual(shader.register_of(definition['words'][0]), (2, 31), key)
                self.assertEqual(struct.unpack('<4f', struct.pack('<4I', *definition['words'][1:])), (gain, 1., 0., 0.), key)
                destination, source, constant = multiply['words']
                self.assertEqual((shader.register_of(destination), shader.mask_of(destination)), ((0, 0), 'xyz'), key)
                self.assertEqual((shader.register_of(source), shader.swizzle_of(source)), ((0, 0), 'xyzw'), key)
                self.assertEqual((shader.register_of(constant), shader.swizzle_of(constant)), ((2, 31), 'xxxx'), key)
                self.assertFalse(multiply['coissued'] or multiply['predicated'], key)
                # Exactly one added mul; the native path keeps all its own.
                self.assertEqual(len([i for i in items if i['opcode'] == MUL]),
                                 len([i for i in native_items if i['opcode'] == MUL]) + 1, key)

    def test_output_instruction_is_preserved_after_the_mul(self):
        for index, row in enumerate(self.rows):
            native, native_items, _ = self.program(index, 'native')
            for g, gain in enumerate(GAINS[1:], 1):
                words, items, _ = self.program(index, g)
                key = (row['pixel'], gain)
                output = items[-1]
                self.assertEqual(output['opcode'], MOV, key)
                self.assertEqual(shader.register_of(output['words'][0]), (8, 0), key)
                self.assertEqual(shader.mask_of(output['words'][0]), 'xyzw', key)
                self.assertEqual(shader.register_of(output['words'][1]), (0, 0), key)
                # The native output MOV is retained verbatim and follows the MUL.
                self.assertEqual(span(words, output), span(native, native_items[-1]), key)
                self.assertEqual(items[-2]['opcode'], MUL, key)
                self.assertEqual(shader.mask_of(items[-2]['words'][0]), 'xyz', key)
                # Every native instruction survives in order alongside the two added ones.
                added = {id(items[-2])}
                retained = [span(words, i) for i in items
                            if id(i) not in added and not (i['opcode'] == DEF and shader.register_of(i['words'][0]) == (2, 31))]
                self.assertEqual(retained, [span(native, n) for n in native_items], key)

    def test_alpha_lane_is_untouched_between_the_mul_and_the_output(self):
        for index, row in enumerate(self.rows):
            native, native_items, _ = self.program(index, 'native')
            for g, gain in enumerate(GAINS[1:], 1):
                words, items, _ = self.program(index, g)
                key = (row['pixel'], gain)
                multiply = items[-2]
                between = [i for i in items if i['dword'] > multiply['dword'] and i is not items[-1]]
                self.assertEqual(between, [], key)  # the MUL is the penultimate instruction
                self.assertNotIn('w', shader.mask_of(multiply['words'][0]), key)
                writers = [span(words, i) for i in items if i['opcode'] != DEF
                           and shader.register_of(i['words'][0]) == (0, 0) and 'w' in shader.mask_of(i['words'][0])]
                native_writers = [span(native, i) for i in native_items if i['opcode'] != DEF
                                  and shader.register_of(i['words'][0]) == (0, 0) and 'w' in shader.mask_of(i['words'][0])]
                self.assertEqual(writers, native_writers, key)
                self.assertTrue(native_writers, key)  # the native path does write r0.a

    def test_instruction_and_constant_counts_stay_inside_ps_2_0(self):
        self.assertLessEqual(self.driver['max_arithmetic'], PS2_ARITHMETIC_SLOTS)
        for index, row in enumerate(self.rows):
            for g, gain in enumerate(GAINS[1:], 1):
                words, items, _ = self.program(index, g)
                key = (row['pixel'], gain)
                arithmetic = texture = 0
                constants = set()
                for item in items:
                    if item['opcode'] == DEF:
                        self.assertEqual(shader.mask_of(item['words'][0]), 'xyzw', key)
                        constants.add(shader.register_of(item['words'][0])[1])
                    elif item['opcode'] == 31:  # dcl
                        continue
                    elif item['opcode'] in TEXTURE_OPCODES:
                        texture += 1
                    else:
                        arithmetic += SLOT_COST.get(item['opcode'], 1)
                self.assertLessEqual(arithmetic, PS2_ARITHMETIC_SLOTS, key)
                self.assertLessEqual(texture, PS2_TEXTURE_SLOTS, key)
                self.assertLessEqual(len(constants), PS2_FLOAT_CONSTANTS, key)
                self.assertLess(max(constants), PS2_FLOAT_CONSTANTS, key)
                self.assertIn(31, constants, key)


if __name__ == '__main__':
    unittest.main()
