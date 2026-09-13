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


def legacy_rgb_semantic_annotations(record):
    """Restore only pre-COLOR1 resource annotations for historical digest checks.

    The shader/source/alpha proofs, registers, instruction costs and all other
    fields remain covered by their original checkpoint digests.
    """
    record = deepcopy(record)
    record.pop('material_rgb_semantic_proof')
    bump = any('bump' in family for family in record['families'])
    legacy = (8 if record['id'][3:] in ('167eb2d5629ab9d3','d44db87778a43b61') else 7) if bump else 6
    budget = record['budget']
    if legacy in budget['free_texcoord_semantic_indices']:
        budget['free_texcoord_semantic_indices'].remove(legacy)
    resources = budget['material_resources_proven_free_before_reservation']
    resources.pop('rgb_usage'); resources.pop('rgb_usage_index')
    resources['rgb_texcoord_index'] = legacy
    if 'explicit_reserved_texcoord_indices' in budget:
        budget['explicit_reserved_texcoord_indices'] = sorted(budget['explicit_reserved_texcoord_indices'] + [legacy])
    if 'material_abi' in record:
        abi = record['material_abi']
        abi.pop('material_rgb_usage'); abi.pop('material_rgb_usage_index')
        abi['material_rgb_texcoord'] = legacy
    return record


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
                   'declared_texcoord_input_indices': [0, 1, 2, 3], 'opcode_counts': {'mul': 1}, 'declarations': []}
        result = linear.budget(profile, 'ps', False)
        self.assertEqual(result['free_interpolator_registers'], [8, 9])
        self.assertEqual(result['free_temporary_ranges'], [[4, 4], [8, 31]])
        self.assertEqual(result['free_constant_ranges'], [[4, 211], [214, 215], [221, 223]])
        for key, value in [('temporary_registers', [5]), ('constant_registers_direct', [220]),
                           ('free_input_registers', [5, 7, 8, 9]),
                           ('free_input_registers', [5, 6, 8, 9]),
                           ('constant_registers_direct', [212]),
                           ('defined_constant_registers', [213]),
                           ('declared_texcoord_input_indices', [0, 1, 2, 3, 4])]:
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
        self.assertEqual(len(self.report['programs']), 115)
        self.assertEqual(len(self.report['pairs']), 148)
        self.assertEqual(sum(p['archive_pass_occurrences'] for p in self.report['pairs']), 536)
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
            if name[3:] in linear.ASTEROID_VERTICES or name[3:] in linear.PALETTE_VERTICES: continue
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
            if original['families'][0].startswith(('asteroid_','boron_','paranid_')): continue
            stage = original['id'][:2]
            bump = any('bump' in family for family in original['families'])
            b = original['budget']
            self.assertEqual(b['reservations_apply_to_current_depth_modes'], [False, True])
            self.assertEqual(6 in b['free_texcoord_semantic_indices'], not bump)
            chosen = b['material_resources_proven_free_before_reservation']
            self.assertEqual(chosen['rgb_interpolator_register'], (8 if stage == 'ps' else 9) if bump else (7 if stage == 'ps' else 8))
            self.assertEqual(chosen['def_constants'], [212, 213] if stage == 'ps' else [248, 249])
            if stage == 'vs' and not bump:
                profile = linear.motion.profile(self.codes[original['id']], original['id'], stage, '3_0')
                for key, value in [('constant_registers_direct', [248]), ('defined_constant_registers', [249]),
                                   ('free_output_registers', [6, 7, 9, 10, 11]),
                                   ('declared_texcoord_output_indices', [0, 1, 2, 3, 4])]:
                    broken = dict(profile, **{key: value})
                    with self.subTest(id=original['id'], collision=key), self.assertRaisesRegex(ValueError, 'collision'):
                        linear.budget(broken, stage, original['id'] != 'vs_badefd5143b3024f')

    def test_shared_family_coverage_and_future_negative_are_explicit(self):
        pairs = [row for row in self.report['pairs'] if row['family'] == 'shared_default']
        self.assertEqual(len(pairs), 10)
        self.assertEqual(sum(row['archive_pass_occurrences'] for row in pairs), 96)
        self.assertEqual(self.report['families']['shared_default']['production_status'], 'qualified_24930b5')
        self.assertEqual(self.report['future_negative_pair']['ps'], 'fffdabd910793aba')
        self.assertEqual(self.report['future_negative_pair']['vs'], '494fe349b8bc12ec')
        self.assertEqual(self.report['future_negative_pair']['motion_class'], 'C')
        self.assertNotIn((self.report['future_negative_pair']['vs'], 'fffdabd910793aba'), linear.PAIRS)
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
        records = [legacy_rgb_semantic_annotations(p) for p in self.report['programs'] if any(family in ('argon', 'shared_default') for family in p['families'])]
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
            self.assertEqual(result['material_resources_proven_free_before_reservation']['rgb_usage'], 'color')
            self.assertEqual(result['material_resources_proven_free_before_reservation']['rgb_usage_index'], 1)
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
        for key in dict(linear.EXTENDED_PIXELS, **linear.HULL_PIXELS):
            prove = linear.prove_hull_pixel if key in linear.HULL_PIXELS else linear.prove_extended_pixel
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
                        prove(broken,key)

    def test_extended_literals_and_application_roles_reject_mutations_without_hash_gate(self):
        for key in dict(linear.EXTENDED_PIXELS, **linear.HULL_PIXELS):
            prove = linear.prove_hull_pixel if key in linear.HULL_PIXELS else linear.prove_extended_pixel
            original = self.decoded_original('ps_'+key)
            proof = prove(original,key)
            for literal in proof['literal_sites']:
                broken = deepcopy(original)
                definition = broken[literal['definition_dword']]['item']
                words = list(definition['words'])
                words[literal['literal_dword']-literal['definition_dword']-1] = struct.unpack('<I',struct.pack('<f',literal['value']+1.))[0]
                definition['words'] = tuple(words)
                with self.subTest(key=key,literal=literal['literal_dword']), self.assertRaises(ValueError):
                    prove(broken,key)
            for role,sites in proof.get('application_coefficient_sites', {}).items():
                for site in sites:
                    broken = deepcopy(original)
                    source = next(s for s in broken[site['instruction_dword']]['sources'] if s['name'].startswith('c'))
                    source['swizzle'] = 'yyyy'
                    with self.subTest(key=key,role=role), self.assertRaises(ValueError):
                        prove(broken,key)

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
            if pair['family'].startswith(('asteroid_','boron_','paranid_')): continue
            self.assertEqual(pair['depth_texcoord_index'],7 if pair['vs'] == new[3:] else 6 if pair['motion_class'] == 'B' else 5)
        resources = linear.budget(original,'vs',True,False,7)
        self.assertNotIn(7,resources['free_texcoord_semantic_indices'])
        self.assertIn(6,resources['free_texcoord_semantic_indices'])
        self.assertIn(5,resources['free_texcoord_semantic_indices'])
        self.assertIn(7,linear.budget(original,'vs',True,False,6)['free_texcoord_semantic_indices'])
        with self.assertRaisesRegex(ValueError,'collision'):
            linear.budget(original,'vs',True,False,4)


    def test_remaining_hull_contracts_cover_complete_aliases_and_fixed_coefficients(self):
        inventory = json.loads(self.inventory_path.read_text())
        coefficients = {'shared_bump':(.5,6,.5), 'split_bump':(.5,10,1.),
                        'terran_default':(1.,5,1.), 'terran_bump':(1.,5,1.)}
        for family,expected in coefficients.items():
            pairs = [p for p in self.report['pairs'] if p['family'] == family]
            self.assertEqual((len(pairs),len({p['ps'] for p in pairs}),len({p['vs'] for p in pairs})),(10,6,3))
            self.assertEqual(sum(p['archive_pass_occurrences'] for p in pairs),96 if family == 'shared_bump' else 24)
            self.assertEqual({p['motion_class'] for p in pairs},{'A' if family == 'terran_default' else 'B'})
            c = self.report['families'][family]['coefficients']
            self.assertEqual((c['diffuse'],c['specular_power'],c['cube']),expected)
            for alias in linear.FAMILIES[family]['aliases']:
                broken = deepcopy(inventory)
                row = next(r for r in broken['pairs'] if r['ps'] == linear.FAMILIES[family]['pixels'][0] and alias in r['effects']['basenames'])
                row['effects']['basenames'].remove(alias)
                with self.subTest(family=family,alias=alias), self.assertRaisesRegex(ValueError,'archive coverage changed'):
                    linear.prove_archive_coverage(broken)
        self.assertEqual(len(linear.HULL_PIXELS),24)
        new_rows = [p for p in self.report['programs'] if p['id'][3:] in linear.HULL_PIXELS]
        self.assertEqual(max(p['word_count'] for p in new_rows),1358)
        self.assertEqual(max(p['budget']['original_static_weighted_slots'] for p in new_rows),71)
        self.assertEqual(max(len(p['rgb_precision_sites']) for p in new_rows),13)

    def test_previous_49_program_records_remain_exact_except_family_annotations(self):
        records = [legacy_rgb_semantic_annotations(p) for p in self.report['programs'] if p['id'][3:] not in linear.HULL_PIXELS and not p['families'][0].startswith(('asteroid_','boron_','paranid_'))]
        self.assertEqual(len(records),49)
        for record in records:
            record['families'] = [f for f in record['families'] if f not in ('shared_bump','split_bump','terran_default','terran_bump')]
        digest = hashlib.sha256(json.dumps(records,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        self.assertEqual(digest,'dee14417f9295670142ce62810433fdb8503e3e370f1d00b09c10cbaf20054e2')

    def test_hull_power_normal_cube_and_authored_precision_exceptions(self):
        for p in self.report['programs']:
            key = p['id'][3:]
            if key not in linear.HULL_PIXELS: continue
            proof = p['alpha_and_affine_proof']
            family = p['families'][0]
            tex,affine,_,direct,_ = linear.HULL_PIXELS[key]
            self.assertEqual(proof['specular_power'],6 if family == 'shared_bump' else 10 if family == 'split_bump' else 5)
            self.assertEqual(len(proof['native_pow_sites']),(2 if len(direct) == 2 else 1) if family == 'split_bump' else 0)
            self.assertEqual(proof['normal_encoding'],'geometric' if family == 'terran_default' else 'ag')
            if family != 'terran_default':
                self.assertEqual(proof['normal_channels'],{'alpha':'binormal_v5','green':'tangent_v4','red_blue':'unused'})
                self.assertEqual(p['texture_sources'][1]['role'],'normal_data_alpha_green')
                self.assertIsNone(p['texture_sources'][1]['conversion_after_dword'])
                self.assertEqual(proof['reflection_coordinate_dword'],tex[4]-5)
            if affine:
                decoded = self.decoded_original(p['id'])
                prep = linear.site(decoded,tex[0]+4)
                expected_pp = ['partial_precision'] if key in ('042c9ae16f41feff','68f0dd6791fd7d3d') else []
                self.assertEqual(prep['destination']['modifiers'],expected_pp)
                broken = deepcopy(decoded)
                broken[tex[0]+4]['destination']['modifiers'] = [] if expected_pp else ['partial_precision']
                with self.subTest(key=key,precision=True), self.assertRaises(ValueError):
                    linear.prove_hull_pixel(broken,key)
            if family == 'shared_bump':
                # Both distinct half-scale uses must survive: diffuse response
                # and RGB cube tint. An isolated literal check would miss this.
                literal = next(r for r in proof['literal_sites'] if r['value'] == .5)
                register,lane = literal['register'],literal['component']
                decoded = self.decoded_original(p['id'])
                uses = [(at,r) for at,r in decoded.items() if any(s['name']==register and lane in s['swizzle'] for s in r['sources'])]
                cube = next((at,r) for at,r in uses if r['destination']['mask']=='xyz' and at > tex[0])
                self.assertIn(cube[0],[r['instruction_dword'] for r in p['rgb_precision_sites']])
                broken = deepcopy(decoded)
                next(s for s in broken[cube[0]]['sources'] if s['name']==register)['swizzle']='xxxx'
                with self.subTest(key=key,cube_half=True), self.assertRaises(ValueError):
                    linear.prove_hull_pixel(broken,key)

    def test_previous_73_program_records_remain_exact(self):
        records = {p['id']:legacy_rgb_semantic_annotations(p) for p in self.report['programs'] if not p['families'][0].startswith(('asteroid_','boron_','paranid_'))}
        self.assertEqual(len(records),73)
        digest = hashlib.sha256(json.dumps(records,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        self.assertEqual(digest,'3782907d2037efb7ef3c05dad34480bdb175ba07471304745744b00bc36fedfc')

    def test_asteroid_complete_alias_toggle_groups(self):
        inventory = json.loads(self.inventory_path.read_text())
        for family in ('asteroid_default','asteroid_bump'):
            pairs = [p for p in self.report['pairs'] if p['family']==family]
            self.assertEqual(len(pairs),3)
            self.assertEqual(sum(p['archive_pass_occurrences'] for p in pairs),16)
        for target in linear.ASTEROID_PAIRS:
            for field in ('basenames','toggle_directories','catalogues','pass_occurrences','effect_entries'):
                broken = deepcopy(inventory)
                row = next(r for r in broken['pairs'] if (r['vs'],r['ps'])==target)
                value = row['effects'][field]
                if isinstance(value,list): value.pop()
                else: row['effects'][field] -= 1
                with self.subTest(pair=target,field=field),self.assertRaisesRegex(ValueError,'asteroid archive'):
                    linear.prove_archive_coverage(broken)
        broken = deepcopy(inventory)
        row = next(r for r in broken['pairs'] if r['ps']=='517540ae6d5e5410')
        row['ps']='7a0c3388065bb08d'
        with self.assertRaisesRegex(ValueError,'asteroid archive'):
            linear.prove_archive_coverage(broken)

    def test_asteroid_every_executable_operand_is_proven_without_hash_gate(self):
        # Mutate decoded operands directly, including interleaved geometry,
        # point attenuation, cubic lobe, scalar weights and early output alpha.
        # This tests the actual proof predicates; no fingerprint is consulted.
        for stage,keys,prove in [('vs',linear.ASTEROID_VERTICES,linear.prove_asteroid_vertex),
                                 ('ps',linear.ASTEROID_PIXELS,linear.prove_asteroid_pixel)]:
            for key in keys:
                original = self.decoded_original(stage+'_'+key)
                for at,row in original.items():
                    if row['item']['opcode'] in (31,81): continue
                    mutations = ['opcode','predication','coissue']
                    if row['destination']: mutations += ['destination','precision','write_mask']
                    mutations += [('source',n) for n in range(len(row['sources']))]
                    for mutation in mutations:
                        broken = deepcopy(original)
                        r = broken[at]
                        if mutation=='opcode': r['item']['opcode']=1 if r['item']['opcode']!=1 else 5
                        elif mutation=='predication': r['item']['predicated']=True
                        elif mutation=='coissue': r['item']['coissued']=True
                        elif mutation=='destination': r['destination']['name']='r31'
                        elif mutation=='precision': r['destination']['modifiers']=['saturate','partial_precision','centroid']
                        elif mutation=='write_mask': r['destination']['mask']='w' if r['destination']['mask']!='w' else 'xyz'
                        else:
                            source=r['sources'][mutation[1]]
                            source['swizzle']='xxxx' if source['swizzle']!='xxxx' else 'yyyy'
                        with self.subTest(stage=stage,key=key,at=at,mutation=mutation),self.assertRaises(ValueError):
                            prove(broken,key)
                # Deletion and extra temporary writes fail the complete-chain
                # inventory, rather than relying solely on selected motifs.
                at = next(at for at,r in original.items() if r['item']['opcode'] not in (31,81))
                for kind in ('delete','extra'):
                    broken=deepcopy(original)
                    if kind=='delete': del broken[at]
                    else: broken[10000]=deepcopy(broken[at])
                    with self.subTest(stage=stage,key=key,kind=kind),self.assertRaises(ValueError):
                        prove(broken,key)

    def test_asteroid_literals_and_relative_address_liveness_without_hash_gate(self):
        for stage,keys,prove in [('vs',linear.ASTEROID_VERTICES,linear.prove_asteroid_vertex),
                                 ('ps',linear.ASTEROID_PIXELS,linear.prove_asteroid_pixel)]:
            for key in keys:
                original=self.decoded_original(stage+'_'+key)
                definition=next(at for at,r in original.items() if r['item']['opcode']==81)
                for lane in range(4):
                    broken=deepcopy(original)
                    words=list(broken[definition]['item']['words'])
                    words[lane+1]=struct.unpack('<I',struct.pack('<f',7.25))[0]
                    broken[definition]['item']['words']=tuple(words)
                    with self.subTest(key=key,lane=lane),self.assertRaises(ValueError): prove(broken,key)
                for at,row in original.items():
                    for n,source in enumerate(row['sources']):
                        if not source['relative']: continue
                        for field,value in [('name','c7'),('address_register','a1'),('address_component','x'),
                                            ('relative_operand_dword',source['relative_operand_dword']+1)]:
                            broken=deepcopy(original); broken[at]['sources'][n][field]=value
                            with self.subTest(key=key,at=at,field=field),self.assertRaises(ValueError): prove(broken,key)

    def test_asteroid_four_explicit_abis_and_collisions(self):
        layouts=set()
        for p in self.report['programs']:
            if not p['families'][0].startswith('asteroid_'): continue
            stage,key=p['id'].split('_')
            bump,base,*_= (linear.ASTEROID_VERTICES if stage=='vs' else linear.ASTEROID_PIXELS)[key]
            loop=linear.ASTEROID_VERTICES[key][2] if stage=='vs' else False
            abi=p['material_abi']; layouts.add((abi['material_vs_rgb_output'],abi['material_rgb_usage_index'],abi['depth_texcoord']))
            profile=linear.motion.profile(self.codes[p['id']],p['id'],stage,'3_0')
            self.assertEqual(p['budget']['reservations_apply_to_current_depth_modes'],[False,True])
            for index in p['budget']['explicit_reserved_interpolator_registers']:
                broken=deepcopy(profile)
                broken['free_input_registers' if stage=='ps' else 'free_output_registers'].remove(index)
                with self.subTest(key=key,register=index),self.assertRaisesRegex(ValueError,'collision'):
                    linear.asteroid_budget(broken,stage,loop,abi)
            for index in p['budget']['explicit_reserved_texcoord_indices']:
                broken=deepcopy(profile)
                broken['declared_texcoord_input_indices' if stage=='ps' else 'declared_texcoord_output_indices'].append(index)
                with self.subTest(key=key,semantic=index),self.assertRaisesRegex(ValueError,'collision'):
                    linear.asteroid_budget(broken,stage,loop,abi)
            for name,indices in [('temporary_registers',abi[f'material_{stage}_temporaries']),
                                 ('constant_registers_direct',abi[f'material_{stage}_def_constants'])]:
                broken=deepcopy(profile);broken[name]+=indices
                with self.subTest(key=key,resource=name),self.assertRaisesRegex(ValueError,'collision'):
                    linear.asteroid_budget(broken,stage,loop,abi)
            for n,d in enumerate(profile['declarations']):
                for field,value in [('mask','w'),('modifiers',['saturate'])]:
                    broken=deepcopy(profile);broken['declarations'][n][field]=value
                    with self.subTest(key=key,decl=d['name'],field=field),self.assertRaises(ValueError):
                        linear.asteroid_declarations(broken,stage,bump,base)
        self.assertEqual(layouts,{(8,1,5),(8,1,3),(10,1,7),(9,1,6)})
        for pair in self.report['pairs']:
            if not pair['family'].startswith('asteroid_'):continue
            abi=pair['material_abi']
            self.assertEqual(pair['depth_plan']['depth_texcoord_index'],abi['depth_texcoord'])
            self.assertEqual(pair['motion_plan']['ps_temporaries'],[5,6,7])

    def test_asteroid_detail_alpha_lobe_and_interleaved_position_contracts(self):
        slots={'517540ae6d5e5410':38,'7a0c3388065bb08d':25,'d44db87778a43b61':47,'550c2a4d4d3ed70f':34}
        for p in self.report['programs']:
            key=p['id'][3:]
            if key in linear.ASTEROID_PIXELS:
                bump,base,_=linear.ASTEROID_PIXELS[key]
                proof=p['alpha_and_affine_proof']
                self.assertEqual(p['budget']['original_static_weighted_slots'],slots[key])
                self.assertEqual(proof['alpha_model'],'base_alpha_times_vertex_alpha')
                self.assertEqual((proof['specular_power'],proof['specular_outer_scale'],proof['grazing_scale']),(3,1.,3.))
                self.assertIsNone(p['alpha_interpolation_site'])
                self.assertLess(p['alpha_output_sites'][0]['instruction_dword'],p['final_rgb_sites'][0]['instruction_dword'])
                self.assertEqual([x['role'] for x in p['texture_sources']],
                                 ['diffuse_rgb','normal_data_alpha_green','specular_data_red','detail_rgb'] if bump else
                                 ['diffuse_rgb','specular_data_red','detail_rgb'])
                for texture in (p['texture_sources'][0],p['texture_sources'][-1]):
                    self.assertEqual(texture['conversion_after_dword'],texture['fetch']['end_dword'])
                    self.assertEqual(texture['conversion_rgb_register'],'r0')
                self.assertEqual(p['texture_sources'][-1]['fetch']['sources'][0]['swizzle'],'xyzw' if base else 'zwzw')
                self.assertEqual(p['detail_weighting']['base']['register'],'c5' if base else 'c3')
                self.assertEqual(p['detail_weighting']['detail']['register'],'c4' if base else 'c2')
                self.assertNotIn(p['alpha_output_sites'][0]['instruction_dword'],[r['instruction_dword'] for r in p['rgb_precision_sites']])
            elif key in linear.ASTEROID_VERTICES:
                _,base,loop,_=linear.ASTEROID_VERTICES[key]
                q=p['point_and_alpha_proof']; sites=q['position_dp4_dwords']
                self.assertEqual(q['position_matrix_register'],24 if loop else 0)
                self.assertEqual(sites[-1]-sites[0],12 if base else 17 if loop else 21)
                self.assertEqual(q['point_loop'].get('runtime_count_range_required'),[0,8] if loop else None)


    def test_all_115_original_color1_semantics_are_free_and_unsaturated(self):
        for p in self.report['programs']:
            semantic=p['material_rgb_semantic_proof']
            self.assertEqual(semantic['authored_declaration_contract'],
                             {'usage':'color','usage_index':1,'mask':'xyz','modifiers':[]})
            self.assertFalse(semantic['authored_rgb_saturate_modifier'])
            self.assertFalse(semantic['authored_rgb_partial_precision_modifier'])
            self.assertTrue(semantic['native_color0_preserved'])
            self.assertTrue(semantic['separate_physical_register_required'])
            self.assertEqual([d['usage_index'] for d in semantic['original_stage_color_declarations']],[0])
            chosen=p['budget']['material_resources_proven_free_before_reservation']
            self.assertEqual((chosen['rgb_usage'],chosen['rgb_usage_index']),('color',1))
            self.assertNotIn('rgb_texcoord_index',chosen)
        for abi in [self.report['reserved_abi'],self.report['bump_reserved_abi']] + [p['material_abi'] for p in self.report['pairs'] if 'material_abi' in p]:
            self.assertEqual((abi['material_rgb_usage'],abi['material_rgb_usage_index']),('color',1))
            self.assertNotIn('material_rgb_texcoord',abi)

    def test_actual_color1_dcl_collision_rejected_without_fingerprint_gate(self):
        for p in self.report['programs']:
            identifier=p['id']; stage,key=identifier.split('_')
            code=self.codes[identifier]
            profile=linear.motion.profile(code,identifier,stage,'3_0')
            role='output' if stage=='vs' else 'input'
            declaration=next(d for d in profile['declarations'] if d['role']==role and d.get('usage')==5)
            # Change an ORIGINAL stage TEXCOORD declaration to COLOR1, keeping
            # its register/mask/precision intact. Decode real DCL words again;
            # bypass inspect_program so rejection cannot come from its hash.
            words=list(struct.unpack(f'<{len(code)//4}I',code))
            at=declaration['dword']+1
            words[at]=(words[at] & ~0x000f001f) | (1<<16) | 10
            broken=linear.motion.profile(pack(words),identifier,stage,'3_0')
            self.assertTrue(any(d.get('usage')==10 and d['usage_index']==1 for d in broken['declarations'] if d['role']==role))
            with self.subTest(id=identifier),self.assertRaisesRegex(ValueError,'COLOR1'):
                if key in linear.PALETTE_VERTICES or key in linear.PALETTE_PIXELS:
                    linear.palette_budget(broken,stage,p['budget']['relative_constant_bound_required'],p['material_abi'],p['scalar_relocations'])
                elif key in linear.ASTEROID_VERTICES or key in linear.ASTEROID_PIXELS:
                    loop=linear.ASTEROID_VERTICES[key][2] if stage=='vs' else False
                    linear.asteroid_budget(broken,stage,loop,p['material_abi'])
                else:
                    bump=any('bump' in family for family in p['families'])
                    linear.budget(broken,stage,p['budget']['relative_constant_bound_required'],bump)

    def test_color1_reservation_releases_only_the_previous_rgb_texcoord(self):
        # Restore only the explicitly changed semantic/precision annotations.
        # The exact pre-phase digest still binds every original site, operand,
        # alpha/math proof, register, resource range and instruction cost.
        current_records=[p for p in self.report['programs'] if p['id'][3:] not in linear.PALETTE_VERTICES and p['id'][3:] not in linear.PALETTE_PIXELS]
        records=[legacy_rgb_semantic_annotations(p) for p in current_records]
        self.assertEqual(len(records),83)
        digest=hashlib.sha256(json.dumps(records,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        self.assertEqual(digest,'ec2244eaac288eba5facdfb65b4f4af415cc2d63e5ef2a43e246eb0969c15e5a')
        for current,old in zip(current_records,records):
            legacy=old['budget']['material_resources_proven_free_before_reservation']['rgb_texcoord_index']
            self.assertEqual(set(current['budget']['free_texcoord_semantic_indices']),
                             set(old['budget']['free_texcoord_semantic_indices'])|{legacy})
            if 'material_abi' in current:
                abi=current['material_abi']
                self.assertEqual(current['budget']['explicit_reserved_texcoord_indices'],
                                 sorted([abi['motion_texcoord'],abi['depth_texcoord']]))

    def test_previous_83_programs_and_116_pairs_remain_exact(self):
        for field,expected in [('programs','fac72430479ef8338ea07d3fbb603a81dc7625adaabf53297374aac2ac1d39ca'),
                               ('pairs','d7f6ca168b34e3e42c2f8177b2d450327dcfd36eee879331c3c4c3662e82b791')]:
            records=[p for p in self.report[field] if not (p['families'][0] if field=='programs' else p['family']).startswith(('boron_','paranid_'))]
            self.assertEqual(len(records),83 if field=='programs' else 116)
            self.assertEqual(hashlib.sha256(json.dumps(records,sort_keys=True,separators=(',',':')).encode()).hexdigest(),expected)

    def test_palette_all_alias_toggle_and_pair_local_motion_contracts(self):
        inventory=json.loads(self.inventory_path.read_text())
        for family,count in [('boron_default',6),('boron_bump',6),('paranid_default',10),('paranid_bump',10)]:
            rows=[p for p in self.report['pairs'] if p['family']==family]
            self.assertEqual(len(rows),count)
            self.assertEqual(sum(p['archive_pass_occurrences'] for p in rows),24)
            for row in rows:
                self.assertEqual(row['motion_class'],'B')
                self.assertEqual(row['motion_plan']['ps_temporaries'],[5,6,7] if row['ps'] in ('39eb3c2258a516e1','57acf59d19c73791','f917d48ee826da1f','77a5b2d62fb3be48','c997a37560e266df','675f9077d8fd21c4') else [6,7,8])
                self.assertEqual(row['material_abi']['material_ps_temporaries'],[10,11,12,13])
        for target in linear.PALETTE_PAIRS:
            for field in ('basenames','toggle_directories','catalogues','pass_occurrences','effect_entries','techniques'):
                broken=deepcopy(inventory);row=next(r for r in broken['pairs'] if (r['vs'],r['ps'])==target)
                value=row['effects'][field]
                if isinstance(value,list):value.pop()
                else:row['effects'][field]-=1
                with self.subTest(pair=target,field=field),self.assertRaisesRegex(ValueError,'palette archive'):
                    linear.prove_archive_coverage(broken)

    def test_palette_every_executable_operand_is_proven_without_hash_gate(self):
        mutations_checked=0
        for stage,keys,prove in [('vs',linear.PALETTE_VERTICES,linear.prove_palette_vertex),('ps',linear.PALETTE_PIXELS,linear.prove_palette_pixel)]:
            for key in keys:
                original=self.decoded_original(stage+'_'+key)
                prove(original,key)
                for at,row in original.items():
                    if row['item']['opcode'] in (31,81):continue
                    mutations=['opcode','predication','coissue']
                    if row['destination']:mutations+=['destination','precision','write_mask']
                    mutations += [(field,n) for n in range(len(row['sources'])) for field in ('swizzle','register','modifier','relative')]
                    for mutation in mutations:
                        broken=deepcopy(original);r=broken[at]
                        if mutation=='opcode':r['item']['opcode']=1 if r['item']['opcode']!=1 else 5
                        elif mutation=='predication':r['item']['predicated']=True
                        elif mutation=='coissue':r['item']['coissued']=True
                        elif mutation=='destination':r['destination']['name']='r31'
                        elif mutation=='precision':r['destination']['modifiers']=['saturate','partial_precision','centroid']
                        elif mutation=='write_mask':r['destination']['mask']='w' if r['destination']['mask']!='w' else 'xyz'
                        else:
                            field,n=mutation;s=r['sources'][n]
                            if field=='swizzle':s['swizzle']='xxxx' if s['swizzle']!='xxxx' else 'yyyy'
                            elif field=='register':s['name']='r31'
                            elif field=='modifier':s['source_modifier']=1 if s['source_modifier']!=1 else 0
                            else:s['relative']=not s['relative']
                        with self.subTest(stage=stage,key=key,at=at,mutation=mutation),self.assertRaises(ValueError):prove(broken,key)
                        mutations_checked+=1
                at=next(at for at,r in original.items() if r['item']['opcode'] not in (31,81))
                for kind in ('delete','extra'):
                    broken=deepcopy(original)
                    if kind=='delete':del broken[at]
                    else:broken[10000]=deepcopy(broken[at])
                    with self.subTest(stage=stage,key=key,kind=kind),self.assertRaises(ValueError):prove(broken,key)
        self.assertGreater(mutations_checked,20000)

    def test_palette_every_DEF_lane_and_palette_source_is_exact_without_hash_gate(self):
        bindings=0
        for stage,keys in [('vs',linear.PALETTE_VERTICES),('ps',linear.PALETTE_PIXELS)]:
            for key in keys:
                original=self.decoded_original(stage+'_'+key)
                literals,palettes=linear.palette_constant_proof(original,stage,key);bindings+=len(palettes)
                for at,row in original.items():
                    if row['item']['opcode']!=81:continue
                    for lane in range(4):
                        broken=deepcopy(original);words=list(broken[at]['item']['words']);words[1+lane]^=1;broken[at]['item']['words']=words
                        with self.subTest(stage=stage,key=key,at=at,lane=lane),self.assertRaisesRegex(ValueError,'DEF lane bits'):
                            linear.palette_constant_proof(broken,stage,key)
                for palette in palettes:
                    use=palette['uses'][0];broken=deepcopy(original)
                    source=next(s for s in broken[use['instruction_dword']]['sources'] if s['operand_dword']==use['operand_dword'])
                    source['source_modifier']=1
                    with self.subTest(key=key,role=palette['role']),self.assertRaises(ValueError):linear.palette_constant_proof(broken,stage,key)
                    self.assertEqual(use['source_operand']['operand_dword'],use['operand_dword'])
        self.assertEqual(bindings,96)

    def test_palette_scalar_lane_carriers_and_resource_collisions_without_hash_gate(self):
        for p in self.report['programs']:
            stage,key=p['id'].split('_')
            if key not in linear.PALETTE_VERTICES and key not in linear.PALETTE_PIXELS:continue
            original=self.decoded_original(p['id']);profile=linear.motion.profile(self.codes[p['id']],p['id'],stage,'3_0')
            relocations=p['scalar_relocations'];abi=p['material_abi'];loop=p['budget']['relative_constant_bound_required']
            for field,values in [('constant_registers_direct',abi[f'material_{stage}_palette_constants']),('defined_constant_registers',abi[f'material_{stage}_def_constants']),('temporary_registers',abi[f'material_{stage}_temporaries'])]:
                for value in values:
                    broken=deepcopy(profile);broken[field].append(value)
                    with self.subTest(id=p['id'],field=field,value=value),self.assertRaisesRegex(ValueError,'collision'):linear.palette_budget(broken,stage,loop,abi,relocations)
            role='output' if stage=='vs' else 'input'
            for semantic in (abi['motion_texcoord'],abi['depth_texcoord']):
                broken=deepcopy(profile);broken[f'declared_texcoord_{role}_indices'].append(semantic)
                with self.subTest(id=p['id'],semantic=semantic),self.assertRaisesRegex(ValueError,'collision'):linear.palette_budget(broken,stage,loop,abi,relocations)
            for scalar in relocations:
                self.assertEqual(scalar['destination_lane'],'w');self.assertEqual(scalar['extended_carrier_mask'],'xyzw')
                for field,value in [('mask','xyzw'),('modifiers',['centroid'])]:
                    broken=deepcopy(profile);carrier=next(d for d in broken['declarations'] if d['name']==scalar['destination_register']);carrier[field]=value
                    with self.subTest(id=p['id'],role=scalar['role'],field=field),self.assertRaises(ValueError):linear.palette_scalar_relocations(original,broken,stage,key)
                broken=deepcopy(original)
                if stage=='vs':
                    at=scalar['producer_sites'][0]['instruction_dword'];broken[at]['destination']['name']=scalar['destination_register'];broken[at]['destination']['mask']='w'
                else:
                    use=scalar['consumer_sources'][0];source=next(s for s in broken[use['instruction_dword']]['sources'] if s['operand_dword']==use['operand_dword']);source['name']=scalar['destination_register'];source['swizzle']='wwww'
                with self.subTest(id=p['id'],role=scalar['role'],occupied_w=True),self.assertRaises(ValueError):linear.palette_scalar_relocations(broken,profile,stage,key)

    def test_palette_precision_and_math_metadata_are_complete(self):
        rows=[p for p in self.report['programs'] if p['families'][0].startswith(('boron_','paranid_'))]
        self.assertEqual(len(rows),32)
        self.assertEqual(sum(p['palette_varying'] is not None for p in rows),8)
        for p in rows:
            self.assertTrue(p['rgb_precision_sites'])
            self.assertTrue(all(s['destination']['mask']=='xyz' for s in p['rgb_precision_sites']))
            if p['palette_varying']:
                self.assertEqual(p['palette_varying']['authored_modifiers'],[])
                self.assertEqual(p['palette_varying']['native_alpha_or_scalar_lanes'],[])
            if p['id'].startswith('ps_'):
                self.assertEqual(len([t for t in p['texture_sources'] if t['conversion_after_dword']]),3)
                self.assertEqual(p['alpha_output_sites'][0]['destination']['mask'],'w')
                self.assertEqual(p['lobe_coefficients']['reflection_outer_scale'],1)
                self.assertEqual(p['lobe_coefficients']['palette_albedo_mix'],.5)
                self.assertEqual(p['lobe_coefficients']['specular_power'],10)



if __name__ == '__main__':
    unittest.main()
