"""Offline site proof tests; synthetic words are authored, not game assets.

Local archive checks skip when the extracted corpus is unavailable. Set
X3M_SHADER_PROGRAM_DIRECTORY to select another local extraction directory.
"""
from copy import deepcopy
import json
import hashlib
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
            raise unittest.SkipTest('local reviewed archive corpus unavailable')
        cls.codes = {name: (cls.directory / (name + '.bin')).read_bytes() for name in linear.ORIGINALS}
        cls.inventory_path = ROOT / 'verification/results/motion-output-profiles.json'
        cls.report = linear.inspect(cls.directory, cls.inventory_path)

    def test_all_originals_pairs_and_checked_in_profile_reproduce(self):
        self.assertEqual(len(self.report['programs']), 49)
        self.assertEqual(len(self.report['pairs']), 70)
        self.assertEqual(sum(p['archive_pass_occurrences'] for p in self.report['pairs']), 240)
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
            # DWORD 2 lies inside the original leading comment in every reviewed original.
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
            bump = 'argon_bump' in profile['families']
            fixed = name.endswith(('badefd5143b3024f', '19a246a56e9d9700'))
            self.assertEqual(point['instruction_dword'], (398 if fixed else 437) if bump else (389 if fixed else 428))
            self.assertEqual(point['operand_dword'], (401 if fixed else 440) if bump else (392 if fixed else 431))
            self.assertEqual(point['color_constant_indices'], [5] if fixed else list(range(1, 24, 3)))
            self.assertEqual(point.get('relative', False), not fixed)
            self.assertEqual(profile['budget']['free_interpolator_registers'], [10, 11] if bump else [9, 10, 11])

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
                    self.assertEqual(texture['conversion_write_mask'], None if texture['role'] in ('specular_data_red', 'normal_data_alpha_green', 'normal_data_xyz') else 'xyz')

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
            bump = any('bump' in family for family in original['families'])
            b = original['budget']
            self.assertEqual(b['reservations_apply_to_current_depth_modes'], [False, True])
            self.assertNotIn(6, b['free_texcoord_semantic_indices'])
            chosen = b['material_resources_proven_free_before_reservation']
            self.assertEqual(chosen['rgb_interpolator_register'], (8 if stage == 'ps' else 9) if bump else (7 if stage == 'ps' else 8))
            self.assertEqual(chosen['def_constants'], [212, 213] if stage == 'ps' else [248, 249])
            if stage == 'vs' and not bump:
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
        self.assertEqual(self.report['families']['shared_default']['production_status'], 'qualified_24930b5')
        self.assertEqual(self.report['future_negative_pair']['ps'], 'ef2bf556f207b8bd')
        self.assertNotIn((self.report['future_negative_pair']['vs'], 'ef2bf556f207b8bd'), linear.PAIRS)
        self.assertEqual(max(p['word_count'] for p in self.report['programs']), 1392)

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

    def test_prior_fifteen_profile_records_remain_exact_except_budget_annotation(self):
        records = deepcopy([p for p in self.report['programs'] if any(family in ('argon', 'shared_default') for family in p['families'])])
        self.assertEqual(len(records), 15)
        for record in records:
            record['families'] = [family for family in record['families'] if family in ('argon','shared_default')]
            record['budget'].pop('original_static_weighted_slots')
        digest = hashlib.sha256(json.dumps(records, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        # Original proof records at approved 03e0b62, including all source
        # offsets, alpha/liveness predicates, resource exclusions and identities.
        self.assertEqual(digest, '4aa8aed58580ceef0a23659a5a67991fa33f58d602dec314a9854c722417a1d8')

    def test_bump_inventory_has_distinct_qualified_contract(self):
        rows = [p for p in self.report['programs'] if 'argon_bump' in p['families']]
        pairs = [p for p in self.report['pairs'] if p['family'] == 'argon_bump']
        self.assertEqual((len(rows), len(pairs), sum(p['archive_pass_occurrences'] for p in pairs)), (9, 10, 24))
        self.assertEqual({p['motion_class'] for p in pairs}, {'B'})
        self.assertEqual(self.report['families']['argon_bump']['production_status'], 'qualified_df4dc09')
        self.assertEqual({(p['vs'], p['ps']) for p in pairs},
                         {('4944d81dfe531b37', ps) for ps in ('ca6bfa4a6cca7e2a', '5e0a10fe752b6140')} |
                         {(vs, ps) for vs in ('19a246a56e9d9700', '44c4a41ca92ae2e3') for ps in
                          ('63379470db8d2a86', '68915563dd0aac9a', 'd086fde54698070c', 'f17fffd88d134b04')})
        inventory = json.loads(self.inventory_path.read_text())
        row = next(r for r in inventory['pairs'] if r['ps'] == 'ca6bfa4a6cca7e2a' and r['vs'] == '4944d81dfe531b37')
        row['effects']['techniques'] = ['DEFAULT']
        with self.assertRaises(ValueError):
            linear.prove_archive_coverage(inventory)

    def test_bump_normal_reflection_and_conversion_roles_are_independent(self):
        # Separate literal expected sites from the implementation's shape table.
        expected = {
            'ca6bfa4a6cca7e2a': (1095, 1263, 1293, 'r4', 1260, 'r6', 65),
            '5e0a10fe752b6140': (1098, 1289, 1319, 'r4', 1286, 'r6', 70),
            '63379470db8d2a86': (1077, 1185, 1215, 'r4', 1182, 'r5', 51),
            '68915563dd0aac9a': (175, 283, 296, 'r1', 280, 'r4', 47),
            'd086fde54698070c': (1080, 1211, 1241, 'r4', 1208, 'r5', 56),
            'f17fffd88d134b04': (178, 309, 322, 'r1', 306, 'r4', 52),
        }
        for key, (normal, reflection, convert_after, albedo, clamp, clamp_reg, slots) in expected.items():
            row = next(p for p in self.report['programs'] if p['id'] == 'ps_' + key)
            proof = row['alpha_and_affine_proof']
            self.assertEqual(proof['normal_reconstruction_sites'][0]['instruction_dword'], normal)
            self.assertEqual(proof['normal_channels'], {'alpha':'binormal_v5', 'green':'tangent_v4', 'red_blue':'unused'})
            self.assertEqual(proof['reflection_coordinate_dword'], reflection)
            self.assertEqual(proof['specular_power'], 5)
            self.assertEqual(row['lobe_coefficients'], {'diffuse':0.4000000059604645, 'specular_power':5, 'cube':1.0})
            self.assertEqual([t['role'] for t in row['texture_sources']],
                             ['diffuse_rgb','normal_data_alpha_green','specular_data_red','lightmap_emissive_rgb','reflection_cube_rgb'])
            self.assertEqual((row['texture_sources'][0]['conversion_after_dword'], row['texture_sources'][0]['conversion_rgb_register']), (convert_after, albedo))
            self.assertEqual((row['color0_rgb_clamp']['instruction_dword'], row['color0_rgb_clamp']['destination']['name']), (clamp, clamp_reg))
            self.assertEqual(row['budget']['original_static_weighted_slots'], slots)
            self.assertTrue(all(t['destination']['mask'] == 'xyz' and 'partial_precision' in t['destination']['modifiers'] for t in row['rgb_precision_sites']))
            geometry = {t['instruction_dword'] for t in proof['normal_reconstruction_sites']}
            self.assertFalse(geometry & {t['instruction_dword'] for t in row['rgb_precision_sites']})

    def test_bump_pixel_rejects_semantic_mutations_without_identity_gate(self):
        # Each of six originals: channel swap, wrong basis, bad reciprocal,
        # face order, power, mask, reflect source, affine, and independent alpha.
        for key in linear.BUMP_PIXELS:
            original = self.decoded_original('ps_' + key)
            tex, affine, clamp, _, final = linear.BUMP_PIXELS[key]
            proof = linear.prove_bump_pixel(original, key)
            angular = proof['angular_and_mask_sites']
            normal = tex[1]
            mutations = [
                (normal+4, 'source_swizzle', 1, 'xyzw'),
                (normal+14, 'source_name', 1, 'v5'),
                (normal+21, 'source_name', 1, 'v4'),
                (normal+26, 'opcode', None, 7),
                (normal+9, 'source_modifier', 1, 0),
                (angular[5]['instruction_dword'], 'source_swizzle', 1, 'xxxx'),
                (tex[2]+4, 'source_swizzle', 0 if len(linear.BUMP_PIXELS[key][3]) == 2 else 1, 'yyyy'),
                (proof['reflection_coordinate_dword'], 'source_name', 2, 'v2'),
                (tex[4], 'source_name', 0, 'v4'),
                (tex[0]+4, 'destination_mask', None, 'xyzw' if not affine else 'xyz'),
                (clamp, 'destination_mask', None, 'xyzw'),
                (tex[3]+4, 'source_swizzle', 2, 'xxxx'),
                (final+4, 'source_name', 1, 'v1'),
                (final, 'destination_mask', None, 'xyzw'),
            ]
            if affine:
                mutations.append((affine-4, 'source_name', 1, 'c0'))
            if key in ('5e0a10fe752b6140','d086fde54698070c','f17fffd88d134b04'):
                mutations.append((normal+56, 'source_name', 0, 'r1'))
            for at, kind, operand, value in mutations:
                broken = deepcopy(original)
                if kind.startswith('source_'):
                    broken[at]['sources'][operand]['source_modifier' if kind == 'source_modifier' else kind[7:]] = value
                elif kind == 'opcode':
                    broken[at]['item']['opcode'] = value
                else:
                    broken[at]['destination']['mask'] = value
                with self.subTest(key=key, at=at, mutation=kind), self.assertRaises(ValueError):
                    linear.prove_bump_pixel(broken, key)
            for literal in proof['literal_sites']:
                broken = deepcopy(original)
                definition = broken[literal['definition_dword']]['item']
                words = list(definition['words'])
                words[literal['literal_dword'] - literal['definition_dword'] - 1] = struct.unpack('<I', struct.pack('<f', 0.5))[0]
                definition['words'] = tuple(words)
                with self.subTest(key=key, literal=literal['value']), self.assertRaises(ValueError):
                    linear.prove_bump_pixel(broken, key)
            # An extra write inserted in an otherwise reviewed scalar chain
            # kills a live normal lane; exact occupancy must reject it.
            broken = deepcopy(original)
            at = normal + 33
            broken[at] = deepcopy(broken[normal+14])
            broken[at]['item']['dword'] = at
            with self.subTest(key=key, extra_live_clobber=True), self.assertRaises(ValueError):
                linear.prove_bump_pixel(broken, key)

    def test_bump_vertex_proves_basis_fog_and_point_relative_model(self):
        for key in linear.BUMP_VERTICES:
            code = self.codes['vs_' + key]
            profile = linear.motion.profile(code, 'vs_' + key, 'vs', '3_0')
            original = self.decoded_original('vs_' + key)
            loop = key != '19a246a56e9d9700'
            linear.prove_bump_vertex(original, loop, profile)
            shift = 0 if loop else -45
            cases = [(437 if loop else 398, 1, 'name', 'c0'),
                     (452 if loop else 406, 1 if loop else 2, 'name', 'c0'),
                     (456+shift, 0, 'name', 'v3'), (461+shift, 0, 'name', 'v4'),
                     (527+shift, 1, 'swizzle', 'yyyy'), (522+shift, 1, 'source_modifier', 0),
                     (506+shift, 0, 'source_modifier', 0), (379, 1, 'name', 'c0')]
            if loop:
                cases += [(393, 0, 'name', 'i1'), (395, 1, 'swizzle', 'xxxx'),
                          (429, 0, 'address_component', 'x'), (437, 1, 'name', 'c2')]
            for at, operand, field, value in cases:
                broken = deepcopy(original)
                broken[at]['sources'][operand][field] = value
                with self.subTest(key=key, at=at, field=field), self.assertRaises(ValueError):
                    linear.prove_bump_vertex(broken, loop, profile)
            broken = deepcopy(original)
            broken[536+shift]['destination'].update(name='o1', mask='w')
            with self.subTest(key=key, extra_alpha=True), self.assertRaises(ValueError):
                linear.prove_bump_vertex(broken, loop, profile)
            broken_profile = deepcopy(profile)
            next(d for d in broken_profile['declarations'] if d['name'] == 'v3')['usage_name'] = 'tangent'
            with self.subTest(key=key, swapped_declaration=True), self.assertRaises(ValueError):
                linear.prove_bump_vertex(original, loop, broken_profile)

    def test_bump_class_b_budget_rejects_original_and_temporal_collisions(self):
        for key in list(linear.BUMP_VERTICES) + list(linear.BUMP_PIXELS):
            stage = 'vs' if key in linear.BUMP_VERTICES else 'ps'
            identifier = stage+'_'+key
            profile = linear.motion.profile(self.codes[identifier], identifier, stage, '3_0')
            loop = stage == 'vs' and key != '19a246a56e9d9700'
            result = linear.budget(profile, stage, loop, True)
            self.assertEqual(result['material_resources_proven_free_before_reservation']['rgb_texcoord_index'], 7)
            for field, value in [('constant_registers_direct', [212 if stage == 'ps' else 248]),
                                 ('defined_constant_registers', [216 if stage == 'ps' else 252]),
                                 ('temporary_registers', list(range(11)) if stage == 'ps' else list(range(8))),
                                 ('free_input_registers' if stage == 'ps' else 'free_output_registers', [9,10,11])]:
                broken = dict(profile, **{field:value})
                with self.subTest(key=key, field=field), self.assertRaises(ValueError):
                    linear.budget(broken, stage, loop, True)

    def test_weighted_slots_distinguish_cube_dp2add_repeat_and_unknown_opcode(self):
        name = 'ps_68915563dd0aac9a'
        decoded = self.decoded_original(name)
        profile = linear.motion.profile(self.codes[name], name, 'ps', '3_0')
        baseline = linear.weighted_slots(decoded, profile, 'ps')
        changed = deepcopy(profile)
        next(d for d in changed['declarations'] if d['name'] == 's4')['texture_type'] = 2
        self.assertEqual(linear.weighted_slots(decoded, changed, 'ps'), baseline-3)
        changed = deepcopy(decoded)
        changed[184]['item']['opcode'] = 8  # DP3: one slot instead of DP2ADD's two.
        self.assertEqual(linear.weighted_slots(changed, profile, 'ps'), baseline-1)
        changed[184]['item']['opcode'] = 32  # POW costs three slots in the extension.
        self.assertEqual(linear.weighted_slots(changed, profile, 'ps'), baseline+1)
        changed[184]['item']['opcode'] = 37  # SINCOS is outside the reviewed corpus.
        with self.assertRaisesRegex(ValueError, 'unreviewed static slot opcode'):
            linear.weighted_slots(changed, profile, 'ps')
        name = 'vs_4944d81dfe531b37'
        decoded = self.decoded_original(name)
        profile = linear.motion.profile(self.codes[name], name, 'vs', '3_0')
        self.assertEqual(linear.weighted_slots(decoded, profile, 'vs'), 62)
        del decoded[393]
        del decoded[451]
        self.assertEqual(linear.weighted_slots(decoded, profile, 'vs'), 57)

    def test_extended_families_cover_all_sm3_alias_and_toggle_pairs(self):
        inventory = json.loads(self.inventory_path.read_text())
        for family in ('split_default', 'standard_default', 'standard_bump', 'standard_bump_low'):
            pairs = [p for p in self.report['pairs'] if p['family'] == family]
            self.assertEqual((len(pairs), sum(p['archive_pass_occurrences'] for p in pairs)), (10,24))
            self.assertEqual(len({p['ps'] for p in pairs}), 6)
            self.assertEqual(len({p['vs'] for p in pairs}), 3)
            self.assertTrue(any(name.endswith('2s') for p in pairs for name in p['archive_aliases']))
            for technique in (linear.FAMILIES[family]['technique'],):
                broken = deepcopy(inventory)
                row = next(r for r in broken['pairs'] if r['ps'] == pairs[-1]['ps'] and r['vs'] == pairs[-1]['vs'])
                row['effects']['techniques'].remove(technique)
                with self.subTest(family=family), self.assertRaisesRegex(ValueError, 'archive coverage changed'):
                    linear.prove_archive_coverage(broken)

    def test_extended_coefficients_normal_encoding_and_pow_are_distinct(self):
        profiles = {p['id'][3:]:p for p in self.report['programs'] if p['id'][3:] in linear.EXTENDED_PIXELS}
        self.assertEqual(len(profiles),24)
        for key,profile in profiles.items():
            family = profile['families'][0]
            tex, affine, _, direct, _ = linear.EXTENDED_PIXELS[key]
            proof = profile['alpha_and_affine_proof']
            self.assertEqual(len(proof['native_pow_sites']), 2 if len(direct) == 2 else 1)
            if family == 'split_default':
                self.assertEqual(profile['lobe_coefficients'], {'diffuse':.5,'specular_power':10,'cube':1.})
                self.assertEqual(proof['application_coefficient_sites'], {})
                self.assertIn(10., [r['value'] for r in proof['literal_sites']])
            else:
                first = 8 if len(direct) == 2 else 6 if affine else 3
                for index,role in enumerate(('specular','power','reflection','diffuse')):
                    rows = proof['application_coefficient_sites'][role]
                    self.assertTrue(all(any(s['name'] == f'c{first+index}' and s['swizzle'] == 'xxxx' for s in r['sources']) for r in rows))
                self.assertEqual(profile['lobe_coefficients']['source'], 'application')
                self.assertEqual(proof['normal_encoding'], 'xyz' if family == 'standard_bump_low' else 'ag' if family == 'standard_bump' else 'geometric')
            if 'bump' in family:
                role = profile['texture_sources'][1]
                self.assertIsNone(role['conversion_after_dword'])
                self.assertEqual(role['role'], 'normal_data_xyz' if family.endswith('_low') else 'normal_data_alpha_green')
                self.assertEqual(proof['normal_channels']['green'], 'tangent_v4')
                if family.endswith('_low'):
                    self.assertEqual(proof['normal_channels']['red'], 'binormal_v5')
                    self.assertEqual(proof['normal_channels']['blue'], 'normal_v3')
                    self.assertNotIn('dp2add', [s['opcode'] for s in proof['normal_reconstruction_sites']])
                else:
                    self.assertEqual(proof['normal_channels']['alpha'], 'binormal_v5')
                    self.assertIn('dp2add', [s['opcode'] for s in proof['normal_reconstruction_sites']])

    def test_extended_producer_chains_reject_each_instruction_clobber_without_hash_gate(self):
        # Deleting or changing any executable producer must fail the complete
        # schedule predicate, including scalar-only and packed lane operations.
        for key in linear.EXTENDED_PIXELS:
            original = self.decoded_original('ps_'+key)
            for at,row in original.items():
                if row['item']['opcode'] in (31,81):
                    continue
                for mutation in ('source','destination','delete'):
                    broken = deepcopy(original)
                    if mutation == 'source':
                        broken[at]['sources'][0]['source_modifier'] ^= 1
                    elif mutation == 'destination':
                        broken[at]['destination']['mask'] = 'xyzw' if row['destination']['mask'] != 'xyzw' else 'xyz'
                    else:
                        del broken[at]
                    with self.subTest(key=key,dword=at,mutation=mutation), self.assertRaises(ValueError):
                        linear.prove_extended_pixel(broken,key)

    def test_extended_literals_and_application_roles_reject_mutations_without_hash_gate(self):
        for key in linear.EXTENDED_PIXELS:
            original = self.decoded_original('ps_'+key)
            proof = linear.prove_extended_pixel(original,key)
            for literal in proof['literal_sites']:
                broken = deepcopy(original)
                definition = broken[literal['definition_dword']]['item']
                words = list(definition['words'])
                words[literal['literal_dword']-literal['definition_dword']-1] = struct.unpack('<I',struct.pack('<f',literal['value']+1.))[0]
                definition['words'] = tuple(words)
                with self.subTest(key=key,literal=literal['literal_dword']), self.assertRaises(ValueError):
                    linear.prove_extended_pixel(broken,key)
            for role,sites in proof['application_coefficient_sites'].items():
                for site in sites:
                    broken = deepcopy(original)
                    source = next(s for s in broken[site['instruction_dword']]['sources'] if s['name'].startswith('c'))
                    source['swizzle'] = 'yyyy'
                    with self.subTest(key=key,role=role), self.assertRaises(ValueError):
                        linear.prove_extended_pixel(broken,key)

    def test_standard_base_vertex_body_and_depth_semantic_exception_are_explicit(self):
        old,new = 'vs_53a0a641107ed76c','vs_494fe349b8bc12ec'
        old_items = linear.motion.instructions(self.codes[old])[1]
        new_items = linear.motion.instructions(self.codes[new])[1]
        self.assertEqual([(r['dword'],r['words']) for r in old_items if r['opcode'] != 31],
                         [(r['dword'],r['words']) for r in new_items if r['opcode'] != 31])
        original = linear.motion.profile(self.codes[new],new,'vs','3_0')
        declarations = {d['name']:d for d in original['declarations']}
        self.assertIn('centroid',declarations['o2']['modifiers'])
        for name in ('o3','o4','o5'):
            self.assertNotIn('centroid',declarations[name]['modifiers'])
        for pair in self.report['pairs']:
            self.assertEqual(pair['depth_texcoord_index'],7 if pair['vs'] == new[3:] else 6 if pair['motion_class'] == 'B' else 5)
        resources = linear.budget(original,'vs',True,False,7)
        self.assertNotIn(7,resources['free_texcoord_semantic_indices'])
        self.assertNotIn(6,resources['free_texcoord_semantic_indices'])
        self.assertIn(5,resources['free_texcoord_semantic_indices'])
        with self.assertRaisesRegex(ValueError,'collision'):
            linear.budget(original,'vs',True,False,6)


if __name__ == '__main__':
    unittest.main()
