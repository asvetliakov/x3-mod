"""Constant hemispherical fill in the converted material law.

Compiles the real transformer through the three local structural drivers
(hull/asteroid/palette, glass, XT) and inspects what the fill adds. No game
bytes are bundled; variants stay in a TemporaryDirectory. Design and law:
docs/architecture/fill-light.md.
"""
import hashlib
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
import inspect_motion_output_profiles as motion

FILL = 0.06
FILL_BITS = struct.unpack('<I', struct.pack('<f', FILL))[0]
FILL_CONSTANT = 215
LIGHT0_TEMPORARY = 12
# Every non-fill variant of the three drivers, byte for byte, as the fill-less
# build produces them: the fill is off by default and emits nothing at zero.
ZERO_FILL_DIGEST = '3db6100189f38fa0b6300f6ff38c6871aa1a9adf59b5bd1bd91e93009c31dde8'
ZERO_FILL_OUTPUTS = 1388
DRIVERS = ('linear_material_structure', 'glass_material_structure', 'xt_material_structure')


def load(path):
    words, items, _ = motion.instructions(path.read_bytes())
    return list(words), items


def spans(path):
    words, items = load(path)
    return [tuple(words[item['dword']:item['dword'] + item['length'] + 1]) for item in items]


def added_instructions(base, variant):
    before, after = spans(base), spans(variant)
    removed = [row for row in before if before.count(row) > after.count(row)]
    return [row for row in after if after.count(row) > before.count(row)], removed


class LinearMaterialFillTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.originals = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
        if not cls.originals.is_dir():
            raise unittest.SkipTest('local original archive corpus unavailable')
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            raise RuntimeError('A host C++ compiler is required')
        temporary = tempfile.TemporaryDirectory(prefix='x3-linear-material-fill-')
        cls.addClassCleanup(temporary.cleanup)
        cls.output = Path(temporary.name)
        sources = [str(ROOT / 'src/renderer/linear_material.cpp'), str(ROOT / 'src/renderer/material_motion.cpp')]

        def build(name):
            executable = cls.output / name
            result = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                     str(ROOT / f'verification/probe/{name}.cpp'), *sources, '-o', str(executable)],
                                    capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
            return executable

        cls.reports = {}
        for name in DRIVERS:
            run = subprocess.run([str(build(name)), str(cls.originals), str(cls.output)],
                                 capture_output=True, text=True)
            if run.returncode:
                raise AssertionError(run.stdout + run.stderr)
            cls.reports[name] = json.loads(run.stdout)
        probe = subprocess.run([str(build('linear_material_fill_probe'))], capture_output=True, text=True)
        cls.probe = (probe.returncode, probe.stdout, probe.stderr)

    def fill_pairs(self):
        """(original variant, filled variant) for every pixel program of the corpus."""
        for path in sorted(self.output.glob('ps_*-fill.dat')):
            stem = path.name[:-len('-fill.dat')]
            # The XT driver carries its linear flag in the name; gain 1 is the
            # configuration the fill variants were produced from.
            base = self.output / f'{stem}-1-1.bin' if (self.output / f'{stem}-1-1.bin').is_file() \
                else self.output / f'{stem}-1.bin'
            self.assertTrue(base.is_file(), base.name)
            yield base, path

    def test_zero_fill_keeps_the_whole_converted_corpus_byte_identical(self):
        digest = hashlib.sha256()
        outputs = sorted(self.output.glob('*.bin'))
        self.assertEqual(len(outputs), ZERO_FILL_OUTPUTS)
        for path in outputs:
            digest.update(path.name.encode() + b'\0' + path.read_bytes())
        self.assertEqual(digest.hexdigest(), ZERO_FILL_DIGEST)

    def test_every_converted_pixel_program_takes_exactly_one_fill(self):
        counts = {name: (report['fill_applied'], report['fill_pixel_programs'])
                  for name, report in self.reports.items()}
        self.assertEqual(counts, {'linear_material_structure': (180, 180),
                                  'glass_material_structure': (8, 8),
                                  'xt_material_structure': (28, 28)})
        seen = 0
        for base, filled in self.fill_pairs():
            added, removed = added_instructions(base, filled)
            self.assertEqual(removed, [], filled.name)
            self.assertEqual(len(added), 2, filled.name)
            definition = [row for row in added if row[0] & 0xffff == motion.DEF]
            instruction = [row for row in added if row[0] & 0xffff == 4]
            self.assertEqual((len(definition), len(instruction)), (1, 1), filled.name)
            constant = definition[0]
            self.assertEqual(motion.register_of(constant[1]), (2, FILL_CONSTANT), filled.name)
            self.assertEqual(constant[2:], (FILL_BITS, 0, 0, 0), filled.name)
            token, destination, light, scale, carry = instruction[0]
            self.assertEqual(token, (4 << 24) | 4, filled.name)
            register, number = motion.register_of(destination)
            self.assertEqual((register, motion.mask_of(destination), (destination >> 20) & 15),
                             (0, 'xyz', 0), filled.name)
            self.assertEqual((motion.register_of(light), motion.swizzle_of(light)),
                             ((0, LIGHT0_TEMPORARY), 'xyzw'), filled.name)
            self.assertEqual((motion.register_of(scale), motion.swizzle_of(scale)),
                             ((2, FILL_CONSTANT), 'xxxx'), filled.name)
            self.assertEqual((motion.register_of(carry), motion.swizzle_of(carry)),
                             ((0, number), 'xyzw'), filled.name)
            seen += 1
        self.assertEqual(seen, 216)

    def test_fill_costs_one_weighted_slot_and_holds_the_budget(self):
        report = self.reports['linear_material_structure']
        base = [report['weighted_slots_default_vs_ps_depth_off_on'][1][1],
                report['weighted_slots_bump_vs_ps_depth_off_on'][1][1]]
        self.assertEqual(base, [178, 190])
        self.assertEqual(report['fill_weighted_slots_default_bump'], [base[0] + 1, base[1] + 1])
        self.assertLessEqual(max(report['fill_weighted_slots_default_bump']), 512)

    def test_an_ambiguous_lobe_sum_is_refused_and_reported(self):
        code, output, error = self.probe
        self.assertEqual((code, error), (0, ''), output + error)
        self.assertEqual(json.loads(output), {'failures': 0})
        # The refusal is visible per program: a configured fill that did not
        # enter the program logs fill_applied=0 on its variant line.
        variant_log = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertIn('kind=ps original=%016llx transform=%u create=%08lx words=%u depth=%u fill_applied=%u',
                      variant_log)
        self.assertIn('fill=%g', (ROOT / 'src/proxy/capture.cpp').read_text())


if __name__ == '__main__':
    unittest.main()
