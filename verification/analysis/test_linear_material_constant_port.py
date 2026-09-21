"""Native ps_3_0 constant-read-port legality of converted material shaders.

The retained 8722072 transformer is the byte-level baseline. Local shader bytes
and generated variants remain in a TemporaryDirectory; no Wine or device runs.
"""
from verification.analysis.retired_tests import load_tests  # retired feature: hidden from default discovery
import hashlib
import json
import math
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

BASELINE = '8722072895b155e009356a2fb57ceeea3c4c8f55'
DRIVERS = ('linear_material_structure', 'glass_material_structure', 'xt_material_structure')
FADE_PS = ('517540ae6d5e5410', '7a0c3388065bb08d', 'd44db87778a43b61',
           '550c2a4d4d3ed70f', '64bac8bb307eb896')


def sun_program_ids():
    source = (ROOT / 'src/renderer/linear_material.cpp').read_text().split(
        'constexpr Pixel pixels[] = {')[1].split('\n};')[0]
    source += (ROOT / 'src/renderer/linear_xt_profiles_inc.h').read_text().split(
        'constexpr XtPixel xt_pixels[] = {')[1]
    import re
    return sorted(set(re.findall(r'\{0x([0-9a-f]+)ull,', source)))


def decoded(path):
    words, items, _ = motion.instructions(path.read_bytes())
    return list(words), items


def span(words, item):
    at = item['dword']
    return tuple(words[at:at + item['length'] + 1])


def constant_sources(item):
    if item['opcode'] in motion.HEADER_OPCODES:
        return set()
    _, sources = motion.split_operands(item, 3)
    return {(source['register_type'], source['register']) for source in sources
            if source['register_type'] in motion.CONSTANT_TYPES}


def ordered_max(value, floor):
    # D3D9 ordered source selection used by the existing sanitizer comment.
    return value if value >= floor else floor


def weighted_slots(path):
    words, items = decoded(path)
    dimensions = {}
    total = 0
    costs = {'pow': 3, 'nrm': 3, 'rep': 3, 'if': 3, 'ifc': 3,
             'endrep': 2, 'lrp': 2, 'dp2add': 2}
    for item in items:
        name = motion.OPCODES[item['opcode']]
        if name == 'dcl' and motion.register_of(item['words'][1])[0] == 10:
            dimensions[motion.register_of(item['words'][1])[1]] = (item['words'][0] >> 27) & 15
        if item['opcode'] in motion.HEADER_OPCODES:
            continue
        if name == 'texld':
            sampler = motion.register_of(item['words'][-1])[1]
            total += 4 if dimensions[sampler] == 3 else 1
        else:
            total += costs.get(name, 1)
    return total


class LinearMaterialConstantPortTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.originals = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY',
                                            '/tmp/x3-shader-sweep/programs'))
        if not cls.originals.is_dir():
            raise unittest.SkipTest('local original archive corpus unavailable')
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            raise RuntimeError('A host C++ compiler is required')
        temporary = tempfile.TemporaryDirectory(prefix='x3-material-constant-port-')
        cls.addClassCleanup(temporary.cleanup)
        cls.work = Path(temporary.name)
        cls.current = cls.work / 'current'
        cls.baseline = cls.work / 'baseline-output'
        cls.current.mkdir()
        cls.baseline.mkdir()
        old = cls.work / 'old'
        old.mkdir()
        baseline_cpp = old / 'linear_material.cpp'
        baseline_cpp.write_bytes(subprocess.check_output(
            ['git', 'show', f'{BASELINE}:src/renderer/linear_material.cpp'], cwd=ROOT))
        (old / 'linear_xt_material_inc.h').write_bytes(subprocess.check_output(
            ['git', 'show', f'{BASELINE}:src/renderer/linear_xt_material_inc.h'], cwd=ROOT))
        old_sun = cls.work / 'old-sun'
        old_sun.mkdir()
        baseline_sun_cpp = old_sun / 'linear_material.cpp'
        for name in ('linear_material.cpp', 'linear_xt_material_inc.h', 'linear_sun_share_inc.h'):
            (old_sun / name).write_bytes(subprocess.check_output(
                ['git', 'show', f'0234178:{"src/renderer/" + name}'], cwd=ROOT))
        common = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                  '-I', str(ROOT / 'src/renderer')]
        motion_cpp = str(ROOT / 'src/renderer/material_motion.cpp')

        def build(name, source, suffix):
            executable = cls.work / f'{name}-{suffix}'
            result = subprocess.run(common + [str(ROOT / f'verification/probe/{name}.cpp'),
                                               str(source), motion_cpp, '-o', str(executable)],
                                    capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
            return executable

        cls.reports = {'new': {}, 'old': {}}
        for name in DRIVERS:
            for source, output, suffix in ((ROOT / 'src/renderer/linear_material.cpp', cls.current, 'new'),
                                           (baseline_cpp, cls.baseline, 'old')):
                run = subprocess.run([str(build(name, source, suffix)), str(cls.originals), str(output)],
                                     capture_output=True, text=True)
                if run.returncode:
                    raise AssertionError(run.stdout + run.stderr)
                cls.reports[suffix][name] = json.loads(run.stdout)

        cls.fade_pairs = []
        for source, suffix in ((ROOT / 'src/renderer/linear_material.cpp', 'new'),
                               (baseline_cpp, 'old')):
            driver = build('linear_distance_fade_structure', source, f'fade-{suffix}')
            output = cls.current if suffix == 'new' else cls.baseline
            for shader in FADE_PS:
                for depth in (0, 1):
                    path = output / f'ps_{shader}-fade-{depth}.bin'
                    run = subprocess.run([str(driver), str(cls.originals / f'ps_{shader}.bin'),
                                          str(path), '1', '1', str(depth)],
                                         capture_output=True, text=True)
                    if run.returncode:
                        raise AssertionError(run.stdout + run.stderr)
                    if suffix == 'new':
                        cls.fade_pairs.append(path.name)

        cls.sun_pairs = []
        for source, suffix in ((ROOT / 'src/renderer/linear_material.cpp', 'new'),
                               (baseline_sun_cpp, 'old')):
            driver = build('linear_sun_share_structure', source, f'sun-{suffix}')
            output = cls.current if suffix == 'new' else cls.baseline
            for shader in sun_program_ids():
                for depth in (0, 1):
                    for fill in (0.0, 0.06):
                        stem = f'sun-{shader}-{depth}-{fill}'
                        prefix = output / stem
                        run = subprocess.run([str(driver), str(cls.originals / f'ps_{shader}.bin'),
                                              str(prefix), str(depth), str(fill)],
                                             capture_output=True, text=True)
                        if run.returncode:
                            raise AssertionError(run.stdout + run.stderr)
                        if suffix == 'new':
                            cls.sun_pairs.append(stem + '-share.bin')

    def pairs(self):
        names = sorted(path.name for path in self.current.glob('ps_*.bin')
                       if '-motion-' not in path.name)
        names += sorted(path.name for path in self.current.glob('ps_*-fill.dat'))
        names += sorted(self.sun_pairs)
        self.assertEqual(len(names), len(set(names)))
        for name in names:
            yield self.baseline / name, self.current / name

    def test_every_legacy_fill_fade_and_sun_pixel_instruction_has_one_constant_source(self):
        groups = {'legacy': [], 'fill': [], 'fade': [], 'sun': []}
        for _, path in self.pairs():
            group = ('sun' if path.name.startswith('sun-') else 'fade' if '-fade-' in path.name
                     else 'fill' if path.suffix == '.dat' else 'legacy')
            groups[group].append(path)
            _, items = decoded(path)
            for item in items:
                self.assertLessEqual(len(constant_sources(item)), 1,
                                     f'{path.name} DWORD {item["dword"]}')
        self.assertEqual({name: len(paths) for name, paths in groups.items()},
                         {'legacy': 904, 'fill': 216, 'fade': 10, 'sun': 432})

    def test_only_bytecode_change_is_exact_constant_staging(self):
        staged = 0
        staged_by_group = {'legacy': 0, 'fill': 0, 'fade': 0, 'sun': 0}
        added_slots_by_group = {'legacy': [], 'fill': [], 'fade': [], 'sun': []}
        affected = set()
        baseline_violations = 0
        for before_path, after_path in self.pairs():
            before_words, before_items = decoded(before_path)
            after_words, after_items = decoded(after_path)
            old = new = 0
            file_staged = 0
            while old < len(before_items):
                before = before_items[old]
                violation = (before['opcode'] == 11 and len(constant_sources(before)) > 1)
                if not violation:
                    self.assertEqual(span(before_words, before), span(after_words, after_items[new]),
                                     before_path.name)
                    old += 1
                    new += 1
                    continue
                baseline_violations += 1
                affected.add(('ps_' + before_path.name.split('-')[1])
                             if before_path.name.startswith('sun-') else
                             before_path.name.split('-')[0])
                move, maximum = after_items[new:new + 2]
                self.assertEqual(move['opcode'], 1, before_path.name)
                self.assertEqual(move['words'], before['words'][:-1], before_path.name)
                register = motion.register_of(before['words'][0])[1]
                staged_source = 0x80000000 | register | (0xe4 << 16)
                self.assertEqual(maximum['opcode'], 11, before_path.name)
                self.assertEqual(maximum['words'], (before['words'][0], staged_source,
                                                     before['words'][-1]), before_path.name)
                old += 1
                new += 2
                staged += 1
                file_staged += 1
                group = ('sun' if before_path.name.startswith('sun-') else
                         'fade' if '-fade-' in before_path.name else
                         'fill' if before_path.suffix == '.dat' else 'legacy')
                staged_by_group[group] += 1
            self.assertEqual(new, len(after_items), before_path.name)
            group = ('sun' if before_path.name.startswith('sun-') else
                     'fade' if '-fade-' in before_path.name else
                     'fill' if before_path.suffix == '.dat' else 'legacy')
            added_slots_by_group[group].append(file_staged)
        self.assertEqual(staged, baseline_violations)
        self.assertEqual(len(affected), 108)
        self.assertEqual(staged, sum(staged_by_group.values()))
        self.assertEqual(staged_by_group,
                         {'legacy': 1776, 'fill': 444, 'fade': 16, 'sun': 888})
        print('Staged sanitizer MOVs:', staged_by_group)
        print('Added slots per pixel variant:',
              {group: (min(values), max(values)) for group, values in added_slots_by_group.items()})
        slot_maxima = {}
        for group in added_slots_by_group:
            selected = [(before, after) for before, after in self.pairs()
                        if ('sun' if after.name.startswith('sun-') else
                            'fade' if '-fade-' in after.name else
                            'fill' if after.suffix == '.dat' else 'legacy') == group]
            slot_maxima[group] = (max(weighted_slots(before) for before, _ in selected),
                                  max(weighted_slots(after) for _, after in selected))
        # Re-pinned after 11c4a615 (sun-shadow receiver depth): the depth fragment gained `mov oC2.zw` (+1 PS slot with depth on); gain-1 outputs are otherwise unchanged.
        self.assertEqual(slot_maxima, {'legacy': (299, 306), 'fill': (300, 307),
                                       'fade': (214, 216), 'sun': (343, 350)})
        print('Maximum weighted PS slots before/after:', slot_maxima)
        report = self.reports['new']['linear_material_structure']
        self.assertEqual((report['weighted_slots_default_vs_ps_depth_off_on'][1][1],
                          report['weighted_slots_bump_vs_ps_depth_off_on'][1][1],
                          report['fill_weighted_slots_default_bump']), (181, 193, [182, 194]))

    def test_staging_preserves_ordered_sanitize_values(self):
        cap = 65504.0
        values = (float('-inf'), -1.0, -0.0, 0.0, 1e-10, 0.25, cap,
                  float('inf'), float('nan'))
        for value in values:
            old = min(ordered_max(value, 0.0), cap)
            staged = value
            new = min(ordered_max(staged, 0.0), cap)
            if math.isnan(old):
                self.assertTrue(math.isnan(new))
            else:
                self.assertEqual(struct.pack('<f', new), struct.pack('<f', old))

    def test_legacy_digest_change_is_intentional_and_not_ratified_here(self):
        digest = hashlib.sha256()
        outputs = sorted(path for path in self.current.glob('*.bin')
                         if '-fade-' not in path.name and not path.name.startswith('sun-'))
        for path in outputs:
            digest.update(path.name.encode() + b'\0' + path.read_bytes())
        self.assertEqual(len(outputs), 1388)
        self.assertNotEqual(digest.hexdigest(),
                            '3db6100189f38fa0b6300f6ff38c6871aa1a9adf59b5bd1bd91e93009c31dde8')
        print('Unratified constant-port corpus digest:', digest.hexdigest())


if __name__ == '__main__':
    unittest.main()
