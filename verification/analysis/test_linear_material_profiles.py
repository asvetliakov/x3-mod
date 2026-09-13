"""Offline site proof tests; synthetic words are authored, not game assets.

Local archive checks skip when the extracted corpus is unavailable. Set
X3M_SHADER_PROGRAM_DIRECTORY to select another local extraction directory.
"""
from copy import deepcopy
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import inspect_linear_materials as linear


def pack(words):
    return struct.pack(f'<{len(words)}I', *words)


class FramingTests(unittest.TestCase):
    def test_comment_payload_is_opaque_and_relative_offsets_are_exact(self):
        # Authored MOV r0.xyz, c1[a0.w]; preceding fake instruction/END words
        # belong exclusively to a comment, so are not instructions or operands.
        code = pack([0xfffe0300, 0x0003fffe, 0x03000001, 0xffff, 0x800f0007,
                     0x03000001, 0x80070000, 0xa0e42001, 0xb0ff0000, 0xffff])
        _, items, end = linear.motion.instructions(code)
        self.assertEqual([item['dword'] for item in items], [5])
        self.assertEqual(end, 9)
        row = linear.site(linear.decode_sites(items), 5)
        self.assertEqual(row['destination']['operand_dword'], 6)
        self.assertEqual(row['sources'][0]['operand_dword'], 7)
        self.assertEqual(row['sources'][0]['relative_operand_dword'], 8)
        self.assertEqual(row['sources'][0]['address_component'], 'w')
        with self.assertRaisesRegex(ValueError, 'not an instruction'):
            linear.site(linear.decode_sites(items), 2)

    def test_unknown_original_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'unreviewed'):
            linear.inspect_program(pack([0xffff0300, 0xffff]), 'ps_unreviewed')

    def test_incomplete_or_extra_program_is_rejected_by_framing(self):
        for words in ([0xffff0300, 0x02000001, 0x80070000],
                      [0xffff0300, 0xffff, 0xffff],
                      [0xffff0300, 0x0003fffe, 0xffff]):
            with self.subTest(words=len(words)), self.assertRaises(ValueError):
                linear.motion.instructions(pack(words))

    def test_budget_excludes_both_temporal_paths_and_rejects_collisions(self):
        profile = {'constant_registers_direct': [0, 1, 2], 'defined_constant_registers': [3],
                   'temporary_registers': [0, 1, 2, 3], 'free_input_registers': list(range(5, 10)),
                   'declared_texcoord_input_indices': [0, 1, 2, 3], 'opcode_counts': {'mul': 1}}
        result = linear.budget(profile, 'ps', False)
        self.assertEqual(result['free_interpolator_registers'], [8, 9])
        self.assertEqual(result['free_temporary_ranges'], [[4, 4], [8, 31]])
        self.assertEqual(result['free_constant_ranges'], [[4, 211], [214, 215], [221, 223]])
        for key, value in [('temporary_registers', [5]), ('constant_registers_direct', [220]),
                           ('free_input_registers', [5, 7, 8, 9]),
                           ('free_input_registers', [5, 6, 8, 9]),
                           ('constant_registers_direct', [212]),
                           ('defined_constant_registers', [213]),
                           ('declared_texcoord_input_indices', [0, 1, 2, 3, 6])]:
            broken = dict(profile, **{key: value})
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'collision'):
                linear.budget(broken, 'ps', False)

    def test_report_writer_is_valid_and_lossless(self):
        report = {'schema': 1, 'pairs': [{'vs': 'authored'}], 'programs': [{'id': 'authored'}]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'report.json'
            linear.write_report(path, report)
            self.assertEqual(json.loads(path.read_text()), report)


class LocalOriginalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
        if not all((cls.directory / (name + '.bin')).is_file() for name in linear.ORIGINALS):
            raise unittest.SkipTest('local nine-original archive corpus unavailable')
        cls.codes = {name: (cls.directory / (name + '.bin')).read_bytes() for name in linear.ORIGINALS}
        cls.inventory_path = ROOT / 'verification/results/motion-output-profiles.json'
        cls.report = linear.inspect(cls.directory, cls.inventory_path)

    def test_all_originals_pairs_and_checked_in_profile_reproduce(self):
        self.assertEqual(len(self.report['programs']), 15)
        self.assertEqual(len(self.report['pairs']), 20)
        self.assertEqual(sum(p['archive_pass_occurrences'] for p in self.report['pairs']), 120)
        self.assertEqual(self.report, json.loads((ROOT / 'docs/reverse-engineering/linear-material-profiles.json').read_text()))
        serialized = json.dumps(self.report)
        for forbidden in ('"token"', '"words"', '"expected"', '"replacement"'):
            self.assertNotIn(forbidden, serialized)

    def test_every_original_rejects_comment_definition_source_and_truncation_mutations(self):
        for name, code in self.codes.items():
            _, items, _ = linear.motion.instructions(code)
            decoded = linear.decode_sites(items)
            definition = next(i for i in items if i['opcode'] == linear.motion.DEF)
            source = next(s['operand_dword'] for row in decoded.values() for s in row['sources'])
            # DWORD 2 lies inside the original leading comment in these nine.
            self.assertGreater(items[0]['dword'], 2)
            for at in (2, definition['dword'] + 2, source):
                mutated = bytearray(code)
                mutated[at * 4] ^= 1
                with self.subTest(name=name, dword=at), self.assertRaisesRegex(ValueError, 'fingerprint'):
                    linear.inspect_program(bytes(mutated), name)
            with self.subTest(name=name, truncated=True), self.assertRaisesRegex(ValueError, 'fingerprint'):
                linear.inspect_program(code[:-4], name)

    def test_point_relative_bound_and_fixed_point_are_distinct(self):
        vertices = {p['id']: p for p in self.report['programs'] if p['id'].startswith('vs_')}
        for name, profile in vertices.items():
            point = profile['point_rgb_sources'][0]
            fixed = name.endswith('badefd5143b3024f')
            self.assertEqual(point['instruction_dword'], 389 if fixed else 428)
            self.assertEqual(point['operand_dword'], 392 if fixed else 431)
            self.assertEqual(point['color_constant_indices'], [5] if fixed else list(range(1, 24, 3)))
            self.assertEqual(point.get('relative', False), not fixed)
            self.assertEqual(profile['budget']['free_interpolator_registers'], [9, 10, 11])

    def test_every_pixel_preserves_alpha_precision_constraints(self):
        for profile in self.report['programs']:
            if not profile['id'].startswith('ps_'):
                continue
            with self.subTest(id=profile['id']):
                self.assertEqual(profile['color0_declaration']['mask'], 'xyzw')
                self.assertIn('partial_precision', profile['color0_declaration']['modifiers'])
                self.assertEqual(profile['final_rgb_sites'][0]['destination']['mask'], 'xyz')
                self.assertEqual(profile['alpha_output_sites'][0]['destination']['mask'], 'w')
                self.assertEqual(profile['alpha_output_sites'][0]['sources'][1]['name'], 'v0')
                self.assertEqual(profile['alpha_output_sites'][0]['sources'][1]['swizzle'], 'wwww')
                for texture in profile['texture_sources']:
                    self.assertIn('partial_precision', texture['fetch']['destination']['modifiers'])
                    self.assertEqual(texture['conversion_write_mask'], None if texture['sampler'] == 1 else 'xyz')

    def test_missing_site_rejected_even_for_the_original(self):
        decoded = linear.decode_sites(linear.motion.instructions(self.codes['vs_53a0a641107ed76c'])[1])
        del decoded[428]
        with self.assertRaisesRegex(ValueError, 'source inventory changed'):
            linear.source_role(decoded, 'c1', [428], True)

    def decoded_original(self, name):
        return linear.decode_sites(linear.motion.instructions(self.codes[name])[1])

    def test_alpha_proofs_reject_broken_metadata_without_hash_gate(self):
        # Exercise proof predicates directly; no altered hashes or allowlist entries.
        for key in linear.PIXELS:
            original = self.decoded_original('ps_' + key)
            tex, _, _, _, final = linear.PIXELS[key]
            lrp = tex[2] + 4
            mutations = [
                (lrp, 'source_swizzle'), (final + 4, 'source_register'),
                (tex[0] + 4, 'overwrite_diffuse_alpha'), (final, 'output_alpha'),
                (tex[3] + 4, 'extra_output'), (lrp, 'opcode'),
            ]
            for at, mutation in mutations:
                broken = deepcopy(original)
                row = broken[at]
                if mutation == 'source_swizzle':
                    row['sources'][2]['swizzle'] = 'xxxx'
                elif mutation == 'source_register':
                    row['sources'][1]['name'] = 'v1'
                elif mutation == 'overwrite_diffuse_alpha':
                    row['destination'].update(name='r1', mask='w')
                elif mutation == 'output_alpha':
                    row['destination']['mask'] = 'xyzw'
                elif mutation == 'extra_output':
                    row['destination'].update(name='oC1', register_type=8, register=1)
                else:
                    row['item']['opcode'] = 4
                with self.subTest(key=key, mutation=mutation), self.assertRaises(ValueError):
                    linear.prove_pixel(broken, key)
        for key in ('53a0a641107ed76c', 'badefd5143b3024f'):
            loop = key != 'badefd5143b3024f'
            original = self.decoded_original('vs_' + key)
            at = 500 if loop else 455
            for mutation in ('alpha_source', 'extra_color_output'):
                broken = deepcopy(original)
                if mutation == 'alpha_source':
                    broken[at]['sources'][1]['swizzle'] = 'yyyy'
                else:
                    broken[447 if loop else 402]['destination']['name'] = 'o1'
                with self.subTest(key=key, mutation=mutation), self.assertRaises(ValueError):
                    linear.prove_vertex(broken, loop)

    def test_loop_proof_rejects_metadata_and_literal_changes_without_hash_gate(self):
        original = self.decoded_original('vs_53a0a641107ed76c')
        for mutation in ('rep_count', 'stride_literal', 'counter_initialization', 'counter_overwrite',
                         'address_component', 'relative_base', 'relative_operand_offset', 'extra_flow'):
            broken = deepcopy(original)
            if mutation == 'rep_count':
                broken[384]['sources'][0]['name'] = 'i1'
            elif mutation == 'stride_literal':
                definition = next(row['item'] for row in broken.values() if row['item']['opcode'] == linear.motion.DEF)
                words = list(definition['words'])
                words[3] = struct.unpack('<I', struct.pack('<f', 4.0))[0]
                definition['words'] = tuple(words)
            elif mutation == 'counter_initialization':
                broken[381]['sources'][0]['swizzle'] = 'xxxx'
            elif mutation == 'counter_overwrite':
                broken[409]['destination'].update(name='r0', mask='w')
            elif mutation == 'address_component':
                broken[428]['sources'][1]['address_component'] = 'x'
            elif mutation == 'relative_base':
                broken[420]['sources'][0]['name'] = 'c1'
            elif mutation == 'relative_operand_offset':
                broken[428]['sources'][1]['relative_operand_dword'] += 1
            else:
                broken[398]['item']['opcode'] = 38
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                linear.prove_vertex(broken, True)

    def test_affine_and_rgb_site_proofs_reject_metadata_changes_without_hash_gate(self):
        for key, (_, affine, clamp, _, final) in linear.PIXELS.items():
            original = self.decoded_original('ps_' + key)
            for at in (clamp, final) + ((affine - 8, affine - 4, affine) if affine else ()):
                broken = deepcopy(original)
                broken[at]['item']['opcode'] = 5
                with self.subTest(key=key, at=at), self.assertRaisesRegex(ValueError, 'opcode changed'):
                    linear.prove_pixel(broken, key)
            if affine:
                broken = deepcopy(original)
                broken[affine - 4]['sources'][1]['name'] = 'c0'
                with self.subTest(key=key, affine_source=True), self.assertRaisesRegex(ValueError, 'source shape'):
                    linear.prove_pixel(broken, key)
        for key, loop, point, emissive in [('53a0a641107ed76c', True, 428, 443),
                                          ('badefd5143b3024f', False, 389, 397)]:
            for at in (point, emissive):
                broken = self.decoded_original('vs_' + key)
                broken[at]['item']['opcode'] = 1
                with self.subTest(key=key, at=at), self.assertRaisesRegex(ValueError, 'opcode changed'):
                    linear.prove_vertex(broken, loop)

    def test_selected_material_resources_are_reserved_in_both_depth_modes(self):
        for original in self.report['programs']:
            stage = original['id'][:2]
            b = original['budget']
            self.assertEqual(b['reservations_apply_to_current_depth_modes'], [False, True])
            self.assertNotIn(6, b['free_texcoord_semantic_indices'])
            chosen = b['material_resources_proven_free_before_reservation']
            self.assertEqual(chosen['rgb_interpolator_register'], 7 if stage == 'ps' else 8)
            self.assertEqual(chosen['def_constants'], [212, 213] if stage == 'ps' else [248, 249])
            if stage == 'vs':
                profile = linear.motion.profile(self.codes[original['id']], original['id'], stage, '3_0')
                for key, value in [('constant_registers_direct', [248]), ('defined_constant_registers', [249]),
                                   ('free_output_registers', [6, 7, 9, 10, 11]),
                                   ('declared_texcoord_output_indices', [0, 1, 2, 3, 6])]:
                    broken = dict(profile, **{key: value})
                    with self.subTest(id=original['id'], collision=key), self.assertRaisesRegex(ValueError, 'collision'):
                        linear.budget(broken, stage, original['id'] != 'vs_badefd5143b3024f')

    def test_shared_family_coverage_and_future_negative_are_explicit(self):
        pairs = [row for row in self.report['pairs'] if row['family'] == 'shared_default']
        self.assertEqual(len(pairs), 10)
        self.assertEqual(sum(row['archive_pass_occurrences'] for row in pairs), 96)
        self.assertEqual(self.report['families']['shared_default']['production_status'], 'offline_proof_only')
        self.assertEqual(self.report['future_negative_pair']['ps'], '462342e3e5781384')
        self.assertNotIn((self.report['future_negative_pair']['vs'], '462342e3e5781384'), linear.PAIRS)
        self.assertEqual(max(p['word_count'] for p in self.report['programs']), 1296)

    def test_every_shared_lobe_rejects_mutations_without_a_hash_gate(self):
        for key in linear.FAMILIES['shared_default']['pixels']:
            original = self.decoded_original('ps_' + key)
            proof = linear.prove_shared_lobe(original, key)
            for mutation in ('half_literal', 'power_source', 'diffuse_opcode', 'cube_source',
                             'response_source', 'specular_strength_literal', 'cosine_saturation',
                             'mask_liveness', 'diffuse_liveness', 'mask_to_specular_source'):
                broken = deepcopy(original)
                if mutation in ('half_literal', 'specular_strength_literal'):
                    literal = proof['diffuse_and_cube_literal' if mutation == 'half_literal' else 'specular_strength_literal']
                    definition = broken[literal['definition_dword']]['item']
                    words = list(definition['words'])
                    words[literal['literal_dword'] - literal['definition_dword'] - 1] = struct.unpack(
                        '<I', struct.pack('<f', 0.4 if mutation == 'half_literal' else 2.0))[0]
                    definition['words'] = tuple(words)
                elif mutation == 'power_source':
                    at = proof['specular_power_chains'][0]['multiply_sites'][-1]['instruction_dword']
                    broken[at]['sources'][0]['name'] = 'c0'
                elif mutation == 'diffuse_opcode':
                    at = proof['diffuse_coefficient_site']['instruction_dword']
                    broken[at]['item']['opcode'] = 2
                elif mutation == 'cube_source':
                    at = proof['cube_coefficient_site']['instruction_dword']
                    broken[at]['sources'][0]['name'] = 'r1'
                elif mutation == 'cosine_saturation':
                    link = next(link for link in proof['verified_producer_consumer_links']
                                if link['role'] in ('light1_cosine_to_response', 'cosine_to_packed_diffuse_response'))
                    broken[link['producer_dword']]['destination']['modifiers'].remove('saturate')
                elif mutation == 'mask_liveness':
                    sample_diffuse = linear.PIXELS[key][0][0]
                    broken[sample_diffuse]['destination']['name'] = 'r0'
                elif mutation == 'diffuse_liveness':
                    link = next(link for link in proof['verified_producer_consumer_links']
                                if link['role'] in ('diffuse_rgb_sum_to_half_scale', 'half_diffuse_to_lobe_sum'))
                    at = next(at for at, row in broken.items()
                              if link['producer_dword'] < at < link['consumer_dword'] and row['destination'])
                    broken[at]['destination'].update(name=link['register'], mask=link['lanes'])
                elif mutation == 'mask_to_specular_source':
                    link = next(link for link in proof['verified_producer_consumer_links']
                                if link['role'] in ('specular_rgb_sum_to_mask', 'specular_response_to_mask'))
                    # Base: r4 producer must multiply the RGB sum by scaled
                    # mask r0.w. Single: r0.w must multiply x6 response by s1.x.
                    broken[link['consumer_dword']]['sources'][1]['swizzle'] = 'yyyy'
                else:
                    at = proof['specular_power_chains'][0]['response_multiply']['instruction_dword']
                    broken[at]['sources'][0]['swizzle'] = 'xxxx'
                with self.subTest(key=key, mutation=mutation), self.assertRaises(ValueError):
                    linear.prove_shared_lobe(broken, key)
            # Kill the still-live x^2 value before the third multiply. Checking
            # the scalar proof directly makes this independent of other sites.
            power = proof['specular_power_chains'][0]
            broken = deepcopy(original)
            third = power['multiply_sites'][-1]['instruction_dword']
            second = power['multiply_sites'][1]['instruction_dword']
            overwrite = next(at for at in broken if second < at < third)
            source = broken[third]['sources'][0]
            broken[overwrite]['destination'].update(name=source['name'], mask=source['swizzle'][0])
            with self.subTest(key=key, mutation='power_liveness'), self.assertRaisesRegex(ValueError, 'intervening write'):
                linear.sixth_power(broken, power['dot']['instruction_dword'],
                                   [row['instruction_dword'] for row in power['multiply_sites']],
                                   power['response_multiply']['instruction_dword'])

    def test_each_alias_must_retain_every_shared_family_pair(self):
        inventory = json.loads(self.inventory_path.read_text())
        row = next(row for row in inventory['pairs'] if row['vs'] == linear.BASE_VS and row['ps'] == '3b94320087e81945')
        self.assertEqual(set(row['effects']['basenames']), {'khaak', 'teladi', 'teladi_nodiff', 'xenon'})
        row['effects']['basenames'].remove('teladi')
        # Other aliases still retain this exact shader pair: a union-only
        # coverage check would miss the broken Teladi contract.
        with self.assertRaisesRegex(ValueError, 'teladi: complete DEFAULT archive coverage changed'):
            linear.prove_archive_coverage(inventory)

    def test_stale_pair_splice_is_rejected(self):
        inventory = json.loads(self.inventory_path.read_text())
        row = next(row for row in inventory['pairs'] if (row['vs'], row['ps']) in linear.PAIRS)
        row['insertion_plan']['ps_append_dword'] += 1
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'inventory.json'
            path.write_text(json.dumps(inventory))
            with self.assertRaisesRegex(ValueError, 'splice plan changed'):
                linear.inspect(self.directory, path)


if __name__ == '__main__':
    unittest.main()
