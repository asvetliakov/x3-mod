"""Original synthetic D3D9 snippets; no extracted game code is retained here."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/analysis'))
from shader_semantics import analyze


def output(result, register):
    return next(item for item in result['outputs'] if item['register'] == register.lower())


def constant(name, register, count=1, register_set=2):
    return dict(name=name, register=register, count=count, register_set=register_set)


class ShaderSemanticTests(unittest.TestCase):
    def test_missing_model_is_unknown(self):
        self.assertEqual(analyze('preshader\nmov r0, c0')['unknown_reasons'], ['missing_shader_model'])

    def test_last_actual_model_excludes_preshader_and_other_sections(self):
        result = analyze('''vs_1_1
            mov oPos, c99
            preshader
            sin c4.x, (12)
            // vs_3_0
            ps_2_x
            mov oC0, v0
            // approx 1 instructions
            \0''')
        self.assertEqual(result['model'], '2_x')
        self.assertEqual(result['stage'], 'ps')
        self.assertEqual(result['shader_model_sections'], 2)
        self.assertTrue(result['preshader_excluded'])
        self.assertEqual(result['read_registers'], ['v0'])
        self.assertEqual(result['instruction_count'], 1)

    def test_model_forms(self):
        for model in ('vs_1_1', 'ps.1.4', 'vs_2_x', 'ps_2_a', 'vs_2_b', 'ps_3_0'):
            with self.subTest(model=model):
                self.assertNotEqual(analyze(model)['status'], 'unknown')

    def test_declarations_semantics_and_modifiers(self):
        result = analyze('''vs_3_0
            dcl_position v4
            dcl_position o2
            dcl_texcoord3_centroid o6.xy
            mov o2, v4
            mov o6.xy, v4''')
        position = result['position_outputs'][0]
        self.assertEqual(position['register'], 'o2')
        self.assertEqual(position['all']['input_semantics'], ['position0'])
        self.assertEqual(position['all']['status'], 'complete_conservative')
        self.assertEqual(result['declarations'][2]['semantic'], 'texcoord3')
        self.assertEqual(result['declarations'][2]['modifiers'], ['centroid'])
        self.assertEqual(set(output(result, 'o6')['lanes']), {'x', 'y'})
        self.assertEqual(output(result, 'o6')['all']['status'], 'complete_conservative')

    def test_dp4_matrix_register_parameter_dependency(self):
        result = analyze('''vs_2_0
            dcl_position v0
            dp4 oPos.x, v0, c4
            dp4 oPos.y, v0, c5
            dp4 oPos.z, v0, c6
            dp4 oPos.w, v0, c7''', [constant('ObjectTransform', 4, 4)])
        dependency = result['position_outputs'][0]['all']
        self.assertEqual(dependency['status'], 'complete_conservative')
        self.assertEqual(dependency['source_registers'], ['c4', 'c5', 'c6', 'c7', 'v0'])
        self.assertEqual(dependency['parameters'][0]['referenced_registers'], ['c4', 'c5', 'c6', 'c7'])
        self.assertEqual(len(result['dot_operations']), 4)

    def test_matrix_macro_expands_all_constant_rows(self):
        result = analyze('vs_1_1\nm4x4 oPos, v0, c12', [constant('Matrix', 12, 4)])
        self.assertEqual(result['position_outputs'][0]['lanes']['z']['source_registers'], ['c14', 'v0'])
        self.assertEqual(result['parameter_references'][0]['referenced_registers'], ['c12', 'c13', 'c14', 'c15'])
        self.assertEqual(result['matrix_operations'][0]['opcode'], 'm4x4')

    def test_partial_write_preserves_alpha_and_lane_dependencies(self):
        result = analyze('''ps_3_0
            dcl_color v0
            dcl_texcoord v1
            mov r0, v1
            mov_sat_pp r0.xyz, v0
            mov oC0, r0''')
        color = output(result, 'oC0')
        self.assertEqual(color['rgb']['source_components'], ['v0.x', 'v0.y', 'v0.z'])
        self.assertEqual(color['alpha']['source_components'], ['v1.w'])
        self.assertEqual(result['saturation_sites'], [dict(instruction_index=3, opcode='mov',
                          destination_register='r0', mask='xyz', source_registers=['v0'])])
        self.assertEqual(result['modifier_counts']['pp'], 1)

    def test_self_swizzle_reads_old_register_for_all_lanes(self):
        result = analyze('ps_2_0\nmov r0, v0\nmov r0.xy, r0.yxzw\nmov oC0, r0')
        lanes = output(result, 'oC0')['lanes']
        self.assertEqual(lanes['x']['source_components'], ['v0.y'])
        self.assertEqual(lanes['y']['source_components'], ['v0.x'])

    def test_scalar_source_replication(self):
        result = analyze('ps_2_0\nmov oC0, v0.z')
        self.assertEqual(output(result, 'oC0')['all']['source_components'], ['v0.z'])

    def test_source_modifiers_before_swizzle(self):
        result = analyze('ps_2_0\nmov oC0, -v0_abs.y')
        self.assertEqual(output(result, 'oC0')['all']['source_components'], ['v0.y'])
        self.assertEqual(output(result, 'oC0')['all']['status'], 'complete_conservative')

    def test_projective_source_modifier_adds_denominator(self):
        result = analyze('ps_1_4\nmov r0.x, v0_dw\nmov r0.yzw, v1')
        self.assertEqual(output(result, 'r0')['lanes']['x']['source_components'], ['v0.w', 'v0.x'])

    def test_literal_definition_is_not_host_parameter_dependency(self):
        result = analyze('vs_2_0\ndef c0, 1, 2, 3, 4\nmov oPos, c0', [constant('World', 0)])
        dependency = result['position_outputs'][0]['all']
        self.assertEqual(dependency['parameters'], [])
        self.assertEqual(dependency['source_components'], ['literal:c0.w', 'literal:c0.x', 'literal:c0.y', 'literal:c0.z'])

    def test_constants_in_unused_computation_still_count_as_references(self):
        result = analyze('vs_2_0\nmul r0, v0, c8\nmov oPos, v0',
                         {'constants': [constant('TimeScale', 8), constant('UnusedFog', 9)]})
        self.assertEqual([p['name'] for p in result['parameter_references']], ['TimeScale'])
        self.assertEqual(result['position_outputs'][0]['all']['parameters'], [])
        self.assertEqual(result['name_hints_only']['time_animation'], ['TimeScale'])
        self.assertEqual(result['name_hints_only']['fog'], ['UnusedFog'])

    def test_same_numeric_register_different_namespace(self):
        result = analyze('ps_3_0\nmov oC0, c2', [constant('Float', 2), constant('Bool', 2, register_set=0)])
        self.assertEqual([p['name'] for p in result['parameter_references']], ['Float'])

    def test_control_flow_is_unknown_even_for_apparently_independent_position(self):
        result = analyze('''vs_3_0
            dcl_position o0
            if_gt c0.x, c1.x
            mov r0, v1
            else
            mov r0, v2
            endif
            mov o0, v0''')
        self.assertEqual(result['control_flow_counts'], {'else': 1, 'endif': 1, 'ifc': 1})
        self.assertIn('control_flow', result['position_outputs'][0]['all']['unknown_reasons'])

    def test_relative_addressing_never_proves_bounded_array(self):
        result = analyze('''vs_3_0
            dcl_position o0
            mova a0.x, v1.x
            mov o0, c4[a0.x]''', [constant('SkinPalette', 4, 8)])
        dependency = result['position_outputs'][0]['all']
        self.assertEqual(dependency['status'], 'unknown')
        self.assertIn('relative_constant_addressing', dependency['unknown_reasons'])
        self.assertEqual(dependency['parameters'], [])
        self.assertEqual(result['parameter_references'][0]['name'], 'SkinPalette')
        self.assertEqual(result['relative_addressing_sites'], [{'instruction_index': 2, 'base_register': 'c4'}])

    def test_unknown_operation_invalidates_all_output_proofs(self):
        result = analyze('vs_2_0\nfuture_op r0, c9\nmov oPos, v0')
        self.assertEqual(result['unsupported_opcodes'], ['future'])
        self.assertEqual(result['position_outputs'][0]['all']['status'], 'unknown')

    def test_unparsed_line_cannot_silently_certify_program(self):
        result = analyze('vs_2_0\n???\nmov oPos, v0')
        self.assertIn('unparsed_instruction', result['position_outputs'][0]['all']['unknown_reasons'])

    def test_invalid_destination_is_unknown_and_keeps_parameter_reference(self):
        result = analyze('ps_2_x\nmov unusual123.x, c5\nmov oC0, v0', [constant('Observed', 5)])
        self.assertIn('unsupported_destination_operand', result['unknown_reasons'])
        self.assertEqual(result['parameter_references'][0]['name'], 'Observed')

    def test_undefined_temporary_component_does_not_become_external_input(self):
        result = analyze('vs_2_0\nmov r0.xyz, v0\nmov oPos, r0')
        dependency = result['position_outputs'][0]['all']
        self.assertIn('uninitialized_register:r0.w', dependency['unknown_reasons'])
        self.assertNotIn('r0', dependency['source_registers'])

    def test_unwritten_position_component_unknown(self):
        result = analyze('vs_3_0\ndcl_position o0\nmov o0.xyz, v0')
        self.assertIn('unwritten_output_lane', result['position_outputs'][0]['all']['unknown_reasons'])

    def test_texture_depth_kill_and_sampler_parameters(self):
        result = analyze('''ps_3_0
            dcl_texcoord v0
            dcl_2d s2
            texld r0, v0, s2
            texkill r0
            mov oDepth, r0.x
            mov oC0, r0''', [constant('ShadowTexture', 2, register_set=3)])
        self.assertEqual(len(result['texture_fetches']), 1)
        self.assertEqual(result['depth_write_instruction_indices'], [4])
        self.assertEqual(result['texkill_instruction_indices'], [3])
        self.assertFalse(result['vertex_texture_fetch'])
        self.assertIn('texture:s2', output(result, 'oC0')['all']['source_components'])
        self.assertEqual(output(result, 'oDepth')['all']['parameters'][0]['name'], 'ShadowTexture')

    def test_vertex_texture_fetch_is_marked(self):
        result = analyze('vs_3_0\ndcl_position o0\ntexldl o0, v0, s0')
        self.assertTrue(result['vertex_texture_fetch'])

    def test_sm1_output_and_unsupported_legacy_texture(self):
        result = analyze('ps_1_1\ntex t0\nmov r0, t0')
        self.assertEqual(output(result, 'r0')['semantic'], 'color0')
        self.assertEqual(output(result, 'r0')['all']['status'], 'unknown')
        self.assertEqual(result['unsupported_opcodes'], ['tex'])

    def test_coissue_and_predication_invalidate_proofs(self):
        for instruction, reason in [('+mov r0, v0', 'coissued_instruction'),
                                    ('(p0) mov r0, v0', 'predicated_instruction')]:
            with self.subTest(instruction=instruction):
                result = analyze('ps_2_0\n' + instruction + '\nmov oC0, r0')
                self.assertIn(reason, output(result, 'oC0')['all']['unknown_reasons'])

    def test_dp2add_and_scalar_operations_have_component_dependencies(self):
        result = analyze('''ps_2_0
            dp2add r0, v0, v1, c0.w
            rcp oC0, r0.z''')
        self.assertEqual(output(result, 'oC0')['all']['source_components'], ['c0.w', 'v0.x', 'v0.y', 'v1.x', 'v1.y'])

    def test_normalize_w_is_unknown(self):
        result = analyze('vs_2_0\nnrm oPos, v0')
        self.assertIn('undefined_vector_result_w', result['position_outputs'][0]['all']['unknown_reasons'])

    def test_sincos_never_claims_complete_without_model_specific_contract(self):
        # Malformed zero/two-source forms and plausible SM2/3 source counts all
        # remain unknown until arity AND undefined side-effect lanes are modeled.
        for source in ('', ', v0.x', ', v0.x, c0', ', v0.x, c0, c1'):
            for model, destination in (('vs_3_0', 'o0'), ('vs_2_0', 'oPos')):
                with self.subTest(source=source, model=model):
                    declaration = '\ndcl_position o0' if model == 'vs_3_0' else ''
                    result = analyze(model + declaration + '\nsincos ' + destination + source)
                    self.assertEqual(result['unsupported_opcodes'], ['sincos'])
                    self.assertIn('unsupported_instruction', result['unknown_reasons'])
                    self.assertEqual(result['position_outputs'][0]['all']['status'], 'unknown')

    def test_output_is_json_safe_and_omits_instruction_and_literal_text(self):
        result = analyze('vs_2_0\ndef c0, 17.123456, 0, 0, 0\nmov oPos, c0')
        serialized = json.dumps(result)
        self.assertNotIn('17.123456', serialized)
        self.assertNotIn('mov oPos', serialized)


if __name__ == '__main__':
    unittest.main()
