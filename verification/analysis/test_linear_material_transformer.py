"""Compile the real pure transformer and inspect all seventy-three local original programs.

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


DEFAULT_ORIGINALS = ARGON_ORIGINALS | {
    'ps_3b94320087e81945', 'ps_e3b7acc16da9932d', 'ps_7a14d4dcb28f27e5',
    'ps_8ab6188a40ca15ea', 'ps_8df6143d0e77d92e', 'ps_e16a9806ee3544c3',
}
BUMP_ORIGINALS = {
    'vs_4944d81dfe531b37', 'vs_19a246a56e9d9700', 'vs_44c4a41ca92ae2e3',
    'ps_ca6bfa4a6cca7e2a', 'ps_5e0a10fe752b6140', 'ps_63379470db8d2a86',
    'ps_68915563dd0aac9a', 'ps_d086fde54698070c', 'ps_f17fffd88d134b04',
}


EXTENSION_ORIGINALS = {
    'vs_494fe349b8bc12ec',
    'ps_02606104fa59fb29',
    'ps_0c1f3f0f440e4a0c',
    'ps_1d638938d93421b3',
    'ps_462342e3e5781384',
    'ps_4f052209611387f0',
    'ps_55826dc176afe464',
    'ps_64bac8bb307eb896',
    'ps_789449ffd931d23e',
    'ps_7c83ed50c9894e44',
    'ps_827d8d2d617bedce',
    'ps_99153c144030c396',
    'ps_abf3c0fad53456d8',
    'ps_b0f9313b77cc78ee',
    'ps_bd4d51c08486c6e0',
    'ps_c1452981fd0bff64',
    'ps_cf449bcb069aec4f',
    'ps_d514bf852d8a9c58',
    'ps_db644b73b68c0547',
    'ps_de2dd381fa64193d',
    'ps_dff6a3d360603fa2',
    'ps_e70adc744a38ca59',
    'ps_f1d14a7dbf7c6173',
    'ps_f6a501717c3e5ca8',
    'ps_ff32b602a271c327',
}
EXTENSION_BUMP_ORIGINALS = {
    'ps_0c1f3f0f440e4a0c',
    'ps_4f052209611387f0',
    'ps_64bac8bb307eb896',
    'ps_789449ffd931d23e',
    'ps_99153c144030c396',
    'ps_abf3c0fad53456d8',
    'ps_b0f9313b77cc78ee',
    'ps_c1452981fd0bff64',
    'ps_cf449bcb069aec4f',
    'ps_d514bf852d8a9c58',
    'ps_dff6a3d360603fa2',
    'ps_f1d14a7dbf7c6173',
}


HULL_ORIGINALS = {
    'ps_1ed1bf0fdec00e1a',
    'ps_1f26d41bcb7dac1e',
    'ps_2b04461d0dae038b',
    'ps_78963cdc7c710e04',
    'ps_acc83ed2509d84a1',
    'ps_bdcdb3ab996ae4e0',
    'ps_22cc5b05a55ef61e',
    'ps_3006f8030a467739',
    'ps_769c3814fc0efba8',
    'ps_d6e8bdde0e4c515f',
    'ps_e5ea78b8b0b0fe07',
    'ps_f42202faf57a3c89',
    'ps_3755809bd40afc13',
    'ps_61418505e5d8f998',
    'ps_91b6c09eb47f8555',
    'ps_b5f1d4145171026b',
    'ps_cc09f17db377fd9e',
    'ps_ef2bf556f207b8bd',
    'ps_042c9ae16f41feff',
    'ps_3602b05ce11ca6ff',
    'ps_5c823b8507fa1442',
    'ps_68f0dd6791fd7d3d',
    'ps_8e58ac79b59b02b1',
    'ps_a6e1328c0bb3f401',
}
HULL_BUMP_ORIGINALS = {
    'ps_1ed1bf0fdec00e1a',
    'ps_1f26d41bcb7dac1e',
    'ps_2b04461d0dae038b',
    'ps_78963cdc7c710e04',
    'ps_acc83ed2509d84a1',
    'ps_bdcdb3ab996ae4e0',
    'ps_22cc5b05a55ef61e',
    'ps_3006f8030a467739',
    'ps_769c3814fc0efba8',
    'ps_d6e8bdde0e4c515f',
    'ps_e5ea78b8b0b0fe07',
    'ps_f42202faf57a3c89',
    'ps_042c9ae16f41feff',
    'ps_3602b05ce11ca6ff',
    'ps_5c823b8507fa1442',
    'ps_68f0dd6791fd7d3d',
    'ps_8e58ac79b59b02b1',
    'ps_a6e1328c0bb3f401',
}


def family_resources(profile):
    bump = profile['id'] in BUMP_ORIGINALS | EXTENSION_BUMP_ORIGINALS | HULL_BUMP_ORIGINALS
    pixel = profile['id'].startswith('ps_')
    temporal_base = (7 if len({source['name'] for source in profile['directional_rgb_sources']}) == 2
                     else 6 if profile['diffuse_affine_completion'] else 5) if bump and pixel else 5
    depth_texcoord = 7 if profile['id'] in {'vs_494fe349b8bc12ec', 'ps_7c83ed50c9894e44', 'ps_e70adc744a38ca59'} else 6 if bump else 5
    return {'depth_texcoord': depth_texcoord, 'vs_rgb': 9 if bump else 8, 'ps_rgb': 8 if bump else 7, 'rgb_texcoord': 7 if bump else 6,
            'vs_temporal': (7, 8) if bump else (6, 7), 'ps_temporal': (6, 7) if bump else (5, 6),
            'temporary_base': temporal_base, 'scratch': 10 if bump else 9}


class LinearMaterialTransformerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = json.loads((ROOT / 'docs/reverse-engineering/linear-material-profiles.json').read_text())
        # Explicit implemented corpus; future offline families cannot silently
        # enlarge production qualification merely by entering the report.
        implemented = DEFAULT_ORIGINALS | BUMP_ORIGINALS | EXTENSION_ORIGINALS | HULL_ORIGINALS
        cls.report['programs'] = [row for row in cls.report['programs'] if row['id'] in implemented]
        if {row['id'] for row in cls.report['programs']} != implemented or not all(
                (row['id'] in EXTENSION_ORIGINALS | HULL_ORIGINALS or ('argon_bump' if row['id'] in BUMP_ORIGINALS else 'argon' if row['id'] in ARGON_ORIGINALS else 'shared_default') in row['families'])
                for row in cls.report['programs']):
            raise AssertionError('All seventy-three implemented profiles must remain present')
        cls.originals = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
        if not all((cls.originals / (p['id'] + '.bin')).is_file() for p in cls.report['programs']):
            raise unittest.SkipTest('local seventy-three-original archive corpus unavailable')
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
        self.assertEqual((self.driver['programs'], self.driver['pairs'], self.driver['variants']), (73, 110, 584))
        self.assertGreaterEqual(self.driver['checks'], 2800)

    def test_all_392_preceding_outputs_remain_byte_exact(self):
        # Captured from qualified checkpoint 73f5c51 before the next hull rows:
        # 49 originals, both depth modes, gains0/1/4/16. No game bytes embedded.
        digest = hashlib.sha256()
        outputs = sorted(path for path in self.output.glob('*.bin')
                         if '-motion-' not in path.name and path.name.split('-')[0] in DEFAULT_ORIGINALS | BUMP_ORIGINALS | EXTENSION_ORIGINALS)
        self.assertEqual(len(outputs), 392)
        for path in outputs:
            data = path.read_bytes()
            digest.update(path.name.encode() + b'\0')
            digest.update(struct.pack('<I', len(data)))
            digest.update(data)
        self.assertEqual(digest.hexdigest(), 'b9753e6337fd36cbb8bf15851e5821361bd003ed428b9ec859d80289321f6e02')

    def test_all_192_installed_outputs_remain_byte_exact(self):
        # Captured before this 40-pair extension from the accepted 24-program
        # implementation: DEFAULT + Argon BUMP, both depth modes and four gains.
        digest = hashlib.sha256()
        outputs = sorted(path for path in self.output.glob('*.bin')
                         if '-motion-' not in path.name and path.name.split('-')[0] in DEFAULT_ORIGINALS | BUMP_ORIGINALS)
        self.assertEqual(len(outputs), 192)
        for path in outputs:
            data = path.read_bytes()
            digest.update(path.name.encode() + b'\0')
            digest.update(struct.pack('<I', len(data)))
            digest.update(data)
        self.assertEqual(digest.hexdigest(), '8c27bf32d6e0f006ab51f937b4321aecefa30073040dcbdac140b9e25dd4ea84')

    def test_all_previous_default_outputs_remain_byte_exact(self):
        # Frozen before BUMPMAP core edits at 40ee4e1: all 120 DEFAULT
        # variants, not only Argon; each basename and byte length is framed.
        digest = hashlib.sha256()
        outputs = sorted(path for path in self.output.glob('*.bin')
                         if '-motion-' not in path.name and path.name.split('-')[0] in DEFAULT_ORIGINALS)
        self.assertEqual(len(outputs), 120)
        for path in outputs:
            data = path.read_bytes()
            digest.update(path.name.encode() + b'\0')
            digest.update(struct.pack('<I', len(data)))
            digest.update(data)
        self.assertEqual(digest.hexdigest(), '797e97fd80ac54f7133249b4b3965ff9c438758b78169f784982d133a01aaff5')

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
            abi = family_resources(profile)
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
                        tokens[1] = (tokens[1] & ~0x7ff) | abi['vs_rgb']
                        tokens[emissive['operand_dword'] - at] = 0x80e40007
                else:
                    if at in linear:
                        tokens[1] &= ~0x200000
                    if at == profile['color0_rgb_clamp']['instruction_dword']:
                        tokens[1] &= ~0x100000
                        tokens[2] = 0x90e40000 | abi['ps_rgb']
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
                temporal = (destination_type == 0 and abi['temporary_base'] <= number <= abi['temporary_base']+2 and not vertex) or (
                    destination_type == (6 if vertex else 8) and number in (abi['vs_temporal'] if vertex else (1, 2)))
                if temporal:
                    continue
                self.assertEqual((tokens[1] >> 20) & 15, 0)
                self.assertEqual((tokens[1] >> 16) & 8, 0)
                if vertex:
                    self.assertNotEqual(op, 88, 'CMP is unsupported in VS3')
            # Alpha and position instruction streams are preserved exactly by
            # the full original reconstruction above, including declarations.

    def test_temporal_insertions_are_identical_to_motion_only(self):
        def temporal(words, items, vertex, abi):
            result = []
            for item in items:
                op, operands = item['opcode'], item['words']
                if op == motion.DCL:
                    kind, number = motion.register_of(operands[1])
                    selected = kind == (6 if vertex else 1) and number in (abi['vs_temporal'] if vertex else abi['ps_temporal'])
                elif op == motion.DEF:
                    kind, number = motion.register_of(operands[0])
                    selected = not vertex and kind == 2 and 216 <= number <= 220
                else:
                    dest, _ = motion.split_operands(item, 3)
                    selected = bool(dest) and ((dest['register_type'] == (6 if vertex else 8) and
                                               dest['register'] in (abi['vs_temporal'] if vertex else (1, 2))) or
                                              (not vertex and dest['register_type'] == 0 and abi['temporary_base'] <= dest['register'] <= abi['temporary_base']+2))
                if selected:
                    result.append(span(words, item))
            return result
        for profile, depth, _, words, items in self.each():
            vertex = profile['id'].startswith('vs_')
            abi = family_resources(profile)
            old, old_items = load(self.output / f"{profile['id']}-motion-{depth}.bin")
            self.assertEqual(temporal(words, items, vertex, abi), temporal(old, old_items, vertex, abi))

    def test_full_precision_varying_defs_caps_and_exact_zero_polarity(self):
        maxima = {'default': {'vs':[0,0], 'ps':[0,0]}, 'bump': {'vs':[0,0], 'ps':[0,0]}}
        for profile, depth, gain, words, items in self.each():
            vertex = profile['id'].startswith('vs_')
            abi = family_resources(profile)
            base = 248 if vertex else 212
            definitions, varying, temporal_varyings, slots = {}, [], [], 0
            samplers = {motion.name_of(*motion.register_of(i['words'][1])): (i['words'][0] >> 27) & 15
                        for i in items if i['opcode'] == motion.DCL and motion.register_of(i['words'][1])[0] == 10}
            for item in items:
                if item['opcode'] == motion.DEF:
                    register = motion.register_of(item['words'][0])[1]
                    definitions[register] = struct.unpack('<4f', struct.pack('<4I', *item['words'][1:]))
                elif item['opcode'] == motion.DCL:
                    usage, register_token = item['words']
                    kind, number = motion.register_of(register_token)
                    if kind == (6 if vertex else 1) and number == (abi['vs_rgb'] if vertex else abi['ps_rgb']):
                        varying.append((usage & 31, (usage >> 16) & 15, motion.mask_of(register_token), (register_token >> 20) & 15))
                    if kind == (6 if vertex else 1) and number in (abi['vs_temporal'] if vertex else abi['ps_temporal']):
                        temporal_varyings.append((number, (usage >> 16) & 15))
                else:
                    name = motion.OPCODES[item['opcode']]
                    costs = dict.fromkeys(('mov','add','mad','mul','rcp','rsq','dp3','dp4','min','max','slt','abs','cmp','mova','else','endif'), 1)
                    costs.update({'nrm':3,'pow':3,'rep':3,'if':3,'endrep':2,'lrp':2,'dp2add':2})
                    if name == 'texld':
                        sampled = motion.name_of(*motion.register_of(item['words'][-1]))
                        self.assertIn(samplers[sampled], (2,3))
                        slots += 4 if samplers[sampled] == 3 else 1
                    else:
                        self.assertIn(name, costs, 'Unreviewed opcode has no inferred unit cost')
                        slots += costs[name]
                    destination, sources = motion.split_operands(item, 3)
                    for operand in ([destination] if destination else []) + sources:
                        kind, number = operand['register_type'], operand['register']
                        if kind == 0:
                            self.assertLess(number, 32)
                        if kind == 2:
                            self.assertLess(number, 256 if vertex else 224)
                    if item['opcode'] == 88 and destination['name'] in ('r0', 'r1', 'r3', 'r4', 'r11', 'r12', 'r13'):
                        # These are the newly authored decode/encode selections.
                        source_token = item['words'][1]
                        if (source_token >> 24) & 15 == 1 and sources[1]['name'] == 'c212':
                            self.assertEqual(sources[0]['name'], destination['name'])
                            self.assertEqual(sources[1]['swizzle'], 'yyyy')
                            self.assertEqual(sources[2]['name'], f"r{abi['scratch']}")
            self.assertEqual(varying, [(5, abi['rgb_texcoord'], 'xyz', 0)])
            registers = abi['vs_temporal'] if vertex else abi['ps_temporal']
            self.assertEqual(temporal_varyings, [(registers[0], abi['rgb_texcoord']-2)] +
                             ([(registers[1], abi['depth_texcoord'])] if depth else []))
            self.assertNotEqual(abi['depth_texcoord'], abi['rgb_texcoord'])
            self.assertLessEqual(slots, 512)
            family = 'bump' if profile['id'] in BUMP_ORIGINALS | EXTENSION_BUMP_ORIGINALS | HULL_BUMP_ORIGINALS else 'default'
            stage = 'vs' if vertex else 'ps'
            maxima[family][stage][depth] = max(maxima[family][stage][depth], slots)
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

        for family in ('default', 'bump'):
            self.assertEqual([maxima[family]['vs'], maxima[family]['ps']],
                             self.driver[f'weighted_slots_{family}_vs_ps_depth_off_on'])
        self.assertEqual(maxima['default'], {'vs':[80,82], 'ps':[166,168]})
        self.assertEqual(maxima['bump'], {'vs':[85,87], 'ps':[178,180]})

    def test_sample_conversion_boundaries_preserve_data_and_use_proved_rgb_registers(self):
        # Distinguish affine r4 from DEFAULT r3 and both BUMP data samplers;
        # a correctly framed extra fragment in the wrong temporary must fail.
        for profile, depth, gain, words, items in self.each():
            if not profile['id'].startswith('ps_'):
                continue
            abi = family_resources(profile)
            original, originals = load(self.originals / (profile['id'] + '.bin'))
            by_offset = {i['dword']: i for i in originals}
            for texture in profile['texture_sources']:
                fetch = texture['fetch']['instruction_dword']
                original_fetch = span(original, by_offset[fetch])
                found = [n for n, item in enumerate(items) if span(words, item) == original_fetch]
                self.assertEqual(len(found), 1)
                if texture['conversion_after_dword'] is None:
                    # Normal/specular data keep their original immediate
                    # consumer; no transfer may be slipped after the sample.
                    before = by_offset[fetch]
                    after = next(i for i in originals if i['dword'] == fetch+before['length']+1)
                    self.assertEqual(span(words, items[found[0]+1]), span(original, after))
                    continue
                end = texture['conversion_after_dword']
                boundary = next(i for i in originals if i['dword']+i['length']+1 == end)
                at = next(n for n,i in enumerate(items) if span(words,i) == span(original,boundary))
                fragment = items[at+1:at+8]
                self.assertEqual([i['opcode'] for i in fragment], [11,10,11,32,32,32,88])
                decoded = [motion.split_operands(i,3) for i in fragment]
                target = texture['conversion_rgb_register']
                scratch = f"r{abi['scratch']}"
                self.assertEqual([d['name'] for d,_ in decoded], [target,target,scratch,scratch,scratch,scratch,target])
                self.assertEqual([d['mask'] for d,_ in decoded], ['xyz','xyz','xyz','x','y','z','xyz'])
                for d,_ in decoded:
                    self.assertEqual(d['modifiers'], [])
                self.assertEqual([(src['name'],src['swizzle']) for src in decoded[0][1]], [(target,'xyzw'),('c212','yyyy')])
                self.assertEqual([(src['name'],src['swizzle']) for src in decoded[1][1]], [(target,'xyzw'),('c212','zzzz')])
                self.assertEqual([(src['name'],src['swizzle']) for src in decoded[2][1]], [(target,'xyzw'),('c212','wwww')])
                for lane, (_, sources) in zip('xyz', decoded[3:6]):
                    self.assertEqual([(src['name'],src['swizzle']) for src in sources], [(scratch,lane*4),('c212','xxxx')])
                if texture['role'] == 'lightmap_emissive_rgb':
                    d,sources = motion.split_operands(items[at+8],3)
                    self.assertEqual((items[at+8]['opcode'],d['name'],d['mask']), (5,target,'xyz'))
                    self.assertEqual([(src['name'],src['swizzle']) for src in sources], [(target,'xyzw'),('c213','zzzz')])



if __name__ == '__main__':
    unittest.main()
