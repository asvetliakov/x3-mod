"""Compile the real pure transformer and inspect all fifteen local original programs.

No game bytes are bundled. Generated variants stay in TemporaryDirectory.
These tests qualify instruction/ABI invariants, not GPU primitive behavior.
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


def load(path):
    data = path.read_bytes()
    words, items, _ = motion.instructions(data)
    return words, items


def span(words, item):
    return tuple(words[item['dword']:item['dword'] + item['length'] + 1])


def comment_spans(words):
    result, at = [], 1
    while at < len(words) - 1:
        token = words[at]
        count = (token >> 16) & 0x7fff if token & 0xffff == 0xfffe else (token >> 24) & 15
        if token & 0xffff == 0xfffe:
            result.append(tuple(words[at:at + count + 1]))
        at += count + 1
    return result


def rgb_dependencies(profile, items):
    """Derive full-precision destinations from reviewed RGB origins and lane flow.

    This does not import the C++ edit list. Source texture conversion boundaries
    and directional/COLOR0 roles come from the independently reviewed report.
    Samples clear overwritten register lanes; only RGB gains a linear tag.
    """
    tainted = {('v0', lane) for lane in 'xyz'}
    tainted.update((source['name'], lane) for source in profile['directional_rgb_sources'] for lane in 'xyz')
    conversions = {row['conversion_after_dword']: row['conversion_rgb_register']
                   for row in profile['texture_sources'] if row['conversion_after_dword']}
    rgb = set()
    for item in items:
        destination, sources = motion.split_operands(item, 3)
        if destination:
            lanes = destination['mask']
            # All linear material arithmetic is componentwise. DP3/DP4/NRM
            # are retained geometry or authored affine code-value operations.
            depends = any((source['name'], source['swizzle']['xyzw'.index(lane)]) in tainted
                          for source in sources for lane in lanes)
            for lane in lanes:
                tainted.discard((destination['name'], lane))
            if depends and item['opcode'] != 66:
                if lanes != 'xyz':
                    raise AssertionError('Unexpected partial-lane linear origin')
                rgb.add(item['dword'])
                tainted.update((destination['name'], lane) for lane in lanes)
        register = conversions.get(item['dword'] + item['length'] + 1)
        if register:
            tainted.update((register, lane) for lane in 'xyz')
    return rgb


ARGON_ORIGINALS = {
    'vs_53a0a641107ed76c', 'vs_719856ce0c213220', 'vs_badefd5143b3024f',
    'ps_8759c7838bbc86c2', 'ps_63f96eba9eea7880', 'ps_593e5dea9b3457d5',
    'ps_7a0bb00a8070496a', 'ps_8d5b2ba0fb4d13bf', 'ps_dab93928f26906f7',
}


class LinearMaterialTransformerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = json.loads((ROOT / 'docs/reverse-engineering/linear-material-profiles.json').read_text())
        # Explicit implemented corpus; future offline families cannot silently
        # enlarge production qualification merely by entering the report.
        implemented = ARGON_ORIGINALS | {
            'ps_3b94320087e81945', 'ps_e3b7acc16da9932d', 'ps_7a14d4dcb28f27e5',
            'ps_8ab6188a40ca15ea', 'ps_8df6143d0e77d92e', 'ps_e16a9806ee3544c3',
        }
        cls.report['programs'] = [row for row in cls.report['programs'] if row['id'] in implemented]
        if {row['id'] for row in cls.report['programs']} != implemented or not all(
                ('argon' if row['id'] in ARGON_ORIGINALS else 'shared_default') in row['families']
                for row in cls.report['programs']):
            raise AssertionError('All fifteen implemented profiles must remain present')
        cls.originals = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
        if not all((cls.originals / (p['id'] + '.bin')).is_file() for p in cls.report['programs']):
            raise unittest.SkipTest('local fifteen-original archive corpus unavailable')
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            raise RuntimeError('A host C++ compiler is required')
        temporary = tempfile.TemporaryDirectory(prefix='x3-linear-material-')
        cls.addClassCleanup(temporary.cleanup)
        cls.output = Path(temporary.name)
        executable = cls.output / 'structure'
        build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                str(ROOT / 'verification/probe/linear_material_structure.cpp'),
                                str(ROOT / 'src/renderer/linear_material.cpp'),
                                str(ROOT / 'src/renderer/material_motion.cpp'), '-o', str(executable)],
                               capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)
        run = subprocess.run([str(executable), str(cls.originals), str(cls.output)], capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.driver = json.loads(run.stdout)

    def each(self):
        for profile in self.report['programs']:
            for depth in (0, 1):
                for gain in (0, 1, 4, 16):
                    words, items = load(self.output / f"{profile['id']}-{depth}-{gain}.bin")
                    yield profile, depth, gain, words, items

    def test_all_variants_alias_and_failure_guards(self):
        self.assertEqual((self.driver['programs'], self.driver['pairs'], self.driver['variants']), (15, 20, 120))
        self.assertGreaterEqual(self.driver['checks'], 1200)

    def test_previous_argon_outputs_remain_byte_exact(self):
        # Captured from the qualified pre-extension transformer at c558b00:
        # nine originals, both depth modes, gains 0/1/4/16. Each basename and
        # byte length is framed before the generated bytes; no game payload is
        # embedded here. This includes all three shared VS variants.
        digest = hashlib.sha256()
        outputs = sorted(path for path in self.output.glob('*.bin')
                         if '-motion-' not in path.name and path.name.split('-')[0] in ARGON_ORIGINALS)
        self.assertEqual(len(outputs), 72)
        for path in outputs:
            data = path.read_bytes()
            digest.update(path.name.encode() + b'\0')
            digest.update(struct.pack('<I', len(data)))
            digest.update(data)
        self.assertEqual(digest.hexdigest(), '3b6d22c9155bd345e5b7cacf1f99c00216fccc6b71202626ba92452446b4c8fc')

    def test_original_instruction_alpha_position_and_comment_invariants(self):
        for profile, depth, gain, combined, changed_items in self.each():
            vertex = profile['id'].startswith('vs_')
            original, items = load(self.originals / (profile['id'] + '.bin'))
            self.assertEqual(comment_spans(original), comment_spans(combined))
            expected = []
            if not vertex:
                linear = rgb_dependencies(profile, items)
                direct = sorted({source['name'] for source in profile['directional_rgb_sources']})
                relocated = {name: 12 + i for i, name in enumerate(direct)}
                sources = {source['operand_dword']: relocated[source['name']] for source in profile['directional_rgb_sources']}
            for item in items:
                at = item['dword']
                tokens = list(span(original, item))
                if vertex:
                    point = profile['point_rgb_sources'][0]
                    emissive = profile['material_emissive_scaled_sources'][0]
                    if at == point['instruction_dword']:
                        tokens[point['operand_dword'] - at] = 0x80e40007
                        if point.get('relative'):
                            del tokens[point['relative_operand_dword'] - at]
                            tokens[0] -= 1 << 24
                    if at == emissive['instruction_dword']:
                        tokens[1] = (tokens[1] & ~0x7ff) | 8
                        tokens[emissive['operand_dword'] - at] = 0x80e40007
                else:
                    if at in linear:
                        tokens[1] &= ~0x200000
                    if at == profile['color0_rgb_clamp']['instruction_dword']:
                        tokens[1] &= ~0x100000
                        tokens[2] = 0x90e40007
                    for offset, target in sources.items():
                        if at < offset <= at + item['length']:
                            tokens[offset - at] = 0x80e40000 | target
                    if at == profile['final_rgb_sites'][0]['instruction_dword']:
                        tokens[1] = 0x8007000b
                expected.append(tuple(tokens))
            actual = [span(combined, item) for item in changed_items]
            position, inserted = 0, []
            for tokens in expected:
                while position < len(actual) and actual[position] != tokens:
                    inserted.append(actual[position])
                    position += 1
                self.assertLess(position, len(actual), (profile['id'], depth, gain, 'original instruction changed outside RGB contract'))
                position += 1
            inserted.extend(actual[position:])
            # Every extra body instruction is full precision. Extra writes into
            # original temporaries or oC0 affect RGB only; alpha cannot be hidden
            # in a shared full-vector scratch edit.
            for tokens in inserted:
                op = tokens[0] & 0xffff
                if op in (motion.DCL, motion.DEF):
                    continue
                destination_type, number = motion.register_of(tokens[1])
                temporal = (destination_type == 0 and 5 <= number <= 7 and not vertex) or (
                    destination_type == (6 if vertex else 8) and number in ((6, 7) if vertex else (1, 2)))
                if temporal:
                    continue
                self.assertEqual((tokens[1] >> 20) & 15, 0)
                self.assertEqual((tokens[1] >> 16) & 8, 0)
                if vertex:
                    self.assertNotEqual(op, 88, 'CMP is unsupported in VS3')
            # Alpha and position instruction streams are preserved exactly by
            # the full original reconstruction above, including declarations.

    def test_temporal_insertions_are_identical_to_motion_only(self):
        def temporal(words, items, vertex):
            result = []
            for item in items:
                op, operands = item['opcode'], item['words']
                if op == motion.DCL:
                    kind, number = motion.register_of(operands[1])
                    selected = kind == (6 if vertex else 1) and number in ((6, 7) if vertex else (5, 6))
                elif op == motion.DEF:
                    kind, number = motion.register_of(operands[0])
                    selected = not vertex and kind == 2 and 216 <= number <= 220
                else:
                    dest, _ = motion.split_operands(item, 3)
                    selected = bool(dest) and ((dest['register_type'] == (6 if vertex else 8) and
                                               dest['register'] in ((6, 7) if vertex else (1, 2))) or
                                              (not vertex and dest['register_type'] == 0 and 5 <= dest['register'] <= 7))
                if selected:
                    result.append(span(words, item))
            return result
        for profile, depth, _, words, items in self.each():
            vertex = profile['id'].startswith('vs_')
            old, old_items = load(self.output / f"{profile['id']}-motion-{depth}.bin")
            self.assertEqual(temporal(words, items, vertex), temporal(old, old_items, vertex))

    def test_full_precision_varying_defs_caps_and_exact_zero_polarity(self):
        for profile, depth, gain, words, items in self.each():
            vertex = profile['id'].startswith('vs_')
            base = 248 if vertex else 212
            definitions, varying, slots = {}, [], 0
            for item in items:
                if item['opcode'] == motion.DEF:
                    register = motion.register_of(item['words'][0])[1]
                    definitions[register] = struct.unpack('<4f', struct.pack('<4I', *item['words'][1:]))
                elif item['opcode'] == motion.DCL:
                    usage, register_token = item['words']
                    kind, number = motion.register_of(register_token)
                    if kind == (6 if vertex else 1) and number == (8 if vertex else 7):
                        varying.append((usage & 31, (usage >> 16) & 15, motion.mask_of(register_token), (register_token >> 20) & 15))
                else:
                    name = motion.OPCODES[item['opcode']]
                    slots += motion.SLOT_COSTS.get(name, 1)
                    destination, sources = motion.split_operands(item, 3)
                    for operand in ([destination] if destination else []) + sources:
                        kind, number = operand['register_type'], operand['register']
                        if kind == 0:
                            self.assertLess(number, 32)
                        if kind == 2:
                            self.assertLess(number, 256 if vertex else 224)
                    if item['opcode'] == 88 and destination['name'] in ('r0', 'r1', 'r3', 'r11', 'r12', 'r13'):
                        # These are the newly authored decode/encode selections.
                        source_token = item['words'][1]
                        if (source_token >> 24) & 15 == 1 and sources[1]['name'] == 'c212':
                            self.assertEqual(sources[0]['name'], destination['name'])
                            self.assertEqual(sources[1]['swizzle'], 'yyyy')
                            self.assertEqual(sources[2]['name'], 'r9')
            self.assertEqual(varying, [(5, 6, 'xyz', 0)])
            self.assertLessEqual(slots, 512)
            self.assertEqual(definitions[base][1:3], (0.0, 65504.0))
            self.assertAlmostEqual(definitions[base][0], 2.2, places=6)
            self.assertAlmostEqual(definitions[base][3] / 1e-10, 1, places=6)
            self.assertEqual(definitions[base + 1][0], gain)
            if vertex:
                self.assertEqual(definitions[base + 1][1:], (gain, 0, 0))
            else:
                self.assertAlmostEqual(definitions[base + 1][1], 1 / 2.2, places=6)
                self.assertEqual(definitions[base + 1][2], gain)
                self.assertAlmostEqual(definitions[base + 1][3] / 1e-22, 1, places=6)


if __name__ == '__main__':
    unittest.main()
