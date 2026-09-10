"""Coverage gate controls made from original token fixtures, never game assets."""
import copy
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/analysis'))
from inspect_pixel_coverage import inspect


R0 = 0x800f0000
COLOR0 = 0x800f0800
DEPTH0 = 0x900f0800
V0_SOURCE = 0x90e40000
R0_SOURCE = 0x80e40000
T0 = 0xb00f0000
C0 = 0xa00f0000


def words(*values):
    return struct.pack('<%dI' % len(values), *values)


def instruction(opcode, *operands, model='2_0', flags=0):
    length = 0 if model.startswith('1_') else len(operands) << 24
    return [opcode | length | flags, *operands]


def shader(*instructions, model='2_0'):
    major, minor = map(int, model.split('_'))
    return words(0xffff0000 | major << 8 | minor, *(w for inst in instructions for w in inst), 0xffff)


def features(opcodes=None, model='2_0', **updates):
    opcodes = {'mov': 1} if opcodes is None else opcodes
    result = dict(stage='ps', model='2_x' if model == '2_1' else model,
                  instruction_count=sum(opcodes.values()), opcode_counts=opcodes,
                  unknown_reasons=[], texkill_instruction_indices=[], depth_write_instruction_indices=[])
    result.update(updates)
    return result


def ordinary(model='2_0'):
    return shader(instruction(1, R0 if model.startswith('1_') else COLOR0, V0_SOURCE, model=model), model=model)


class PixelCoverageTests(unittest.TestCase):
    def test_supported_shader_models(self):
        for model in ('1_1', '1_2', '1_3', '1_4', '2_0', '2_1', '3_0'):
            with self.subTest(model=model):
                result = inspect(ordinary(model), features(model=model))
                self.assertTrue(result['qualified'], result)
                self.assertEqual(result['instruction_count'], 1)
                self.assertEqual(result['shader_model'], model)

    def test_missing_evidence_never_means_absence(self):
        for name in features():
            with self.subTest(name=name):
                inventory = features()
                del inventory[name]
                self.assertEqual(inspect(ordinary(), inventory)['reason'], 'missing_inventory_evidence')
        self.assertFalse(inspect(ordinary(), None)['qualified'])

    def test_malformed_evidence_types_rejected_without_exception(self):
        values = {'stage': {}, 'model': {}, 'opcode_counts': [], 'unknown_reasons': None,
                  'texkill_instruction_indices': False, 'depth_write_instruction_indices': {},
                  'instruction_count': True}
        for name, value in values.items():
            with self.subTest(name=name):
                inventory = features(**{name: value})
                self.assertFalse(inspect(ordinary(), inventory)['qualified'])

    def test_inventory_positive_discard_or_depth_claim_denies(self):
        for field in ('texkill_instruction_indices', 'depth_write_instruction_indices'):
            with self.subTest(field=field):
                self.assertFalse(inspect(ordinary(), features(**{field: [0]}))['qualified'])

    def test_malformed_evidence_elements_rejected(self):
        for change in ({'texkill_instruction_indices': [True]}, {'depth_write_instruction_indices': [-1]},
                       {'unknown_reasons': [None]}, {'opcode_counts': {'mov': True}},
                       {'opcode_counts': {'mov': 0}}, {'opcode_counts': {'mov': -1}},
                       {'opcode_counts': {}}, {'instruction_count': 0}):
            with self.subTest(change=change):
                self.assertFalse(inspect(ordinary(), features(**change))['qualified'])

    def test_raw_texkill_denies_even_when_inventory_is_falsely_empty(self):
        code = shader(instruction(65, R0), instruction(1, COLOR0, V0_SOURCE))
        self.assertEqual(inspect(code, features({'texkill': 1, 'mov': 1}))['reason'], 'raw_shader_discard')

    def test_raw_implicit_depth_opcodes_deny(self):
        for opcode, name, operands in ((84, 'texm3x2depth', (T0, R0_SOURCE)),
                                       (87, 'texdepth', (R0,))):
            with self.subTest(opcode=opcode):
                code = shader(instruction(opcode, *operands, model='1_4'), model='1_4')
                self.assertEqual(inspect(code, features({name: 1}, model='1_4'))['reason'], 'raw_shader_depth_write')

    def test_raw_depth_destination_denies(self):
        code = shader(instruction(1, DEPTH0, V0_SOURCE))
        self.assertEqual(inspect(code, features())['reason'], 'raw_depth_output_destination')

    def test_depth_destination_on_dcl_is_also_denied(self):
        code = shader(instruction(31, 0x80000000, DEPTH0))
        self.assertEqual(inspect(code, features({'dcl': 1}))['reason'], 'raw_depth_output_destination')

    def test_unsupported_inventory_destination_denies(self):
        result = inspect(ordinary(), features(unknown_reasons=['unsupported_destination_operand']))
        self.assertEqual(result['reason'], 'unresolved_inventory_syntax')

    def test_raw_unknown_destination_bank_denies_independently(self):
        # Original type-24 destination token reproduces the anomaly class, not
        # the copyrighted offending program or its register number/mask.
        code = shader(instruction(1, 0x800f1800, V0_SOURCE))
        self.assertEqual(inspect(code, features())['reason'], 'malformed_or_unsupported_destination')

    def test_non_output_constant_destination_denies(self):
        self.assertFalse(inspect(shader(instruction(1, C0, V0_SOURCE)), features())['qualified'])

    def test_comment_decoys_are_not_instructions_or_destinations(self):
        decoys = [65, 84, 87, DEPTH0, 0xffff, 0xffff0300]
        comment = [len(decoys) << 16 | 0xfffe, *decoys]
        code = shader(comment, instruction(1, COLOR0, V0_SOURCE))
        self.assertTrue(inspect(code, features())['qualified'])

    def test_definition_literal_decoys_are_data(self):
        code = shader(instruction(81, C0, 65, 84, 87, DEPTH0), instruction(1, COLOR0, V0_SOURCE))
        self.assertTrue(inspect(code, features({'def': 1, 'mov': 1}))['qualified'])

    def test_register_low_bits_equal_dangerous_opcode_are_not_instructions(self):
        code = shader(instruction(1, COLOR0, 0xa0e40041))
        self.assertTrue(inspect(code, features())['qualified'])

    def test_legacy_texture_and_coissue_remain_eligible(self):
        code = shader(instruction(66, T0, model='1_1'),
                      instruction(1, R0, V0_SOURCE, model='1_1', flags=0x40000000), model='1_1')
        inventory = features({'tex': 1, 'mov': 1}, model='1_1',
                             unknown_reasons=['unsupported_instruction', 'coissued_instruction'])
        self.assertTrue(inspect(code, inventory)['qualified'])

    def test_ps14_texturecoord_phase_texture_are_eligible(self):
        code = shader(instruction(64, R0, 0xb0e40000, model='1_4'),
                      instruction(0xfffd, model='1_4'),
                      instruction(66, R0, 0xb0e40000, model='1_4'), model='1_4')
        self.assertTrue(inspect(code, features({'texcrd': 1, 'phase': 1, 'texld': 1}, model='1_4'))['qualified'])

    def test_control_flow_without_coverage_ops_is_eligible(self):
        code = shader(instruction(40, 0xe0e40800), instruction(1, COLOR0, V0_SOURCE), instruction(43))
        inventory = features({'if': 1, 'mov': 1, 'endif': 1}, unknown_reasons=['control_flow'])
        self.assertTrue(inspect(code, inventory)['qualified'])

    def test_count_and_opcode_histogram_mismatch_deny(self):
        for inventory in (features({'add': 1}), features(instruction_count=2), features({'mov': 2}),
                          features({'future': 1})):
            with self.subTest(inventory=inventory):
                self.assertFalse(inspect(ordinary(), inventory)['qualified'])

    def test_inventory_version_and_stage_mismatch_deny(self):
        for inventory in (features(stage='vs'), features(model='3_0')):
            self.assertFalse(inspect(ordinary(), inventory)['qualified'])

    def test_unknown_inventory_reason_deny(self):
        self.assertFalse(inspect(ordinary(), features(unknown_reasons=['future_ambiguity']))['qualified'])

    def test_unknown_raw_opcode_deny(self):
        code = shader(instruction(49))
        self.assertEqual(inspect(code, features())['reason'], 'unknown_raw_opcode')

    def test_source_instruction_smuggling_deny(self):
        code = shader(instruction(1, COLOR0, 65))
        self.assertEqual(inspect(code, features())['reason'], 'malformed_register_operand')

    def test_wrong_encoded_length_deny(self):
        code = shader([0x03000001, COLOR0, V0_SOURCE, 65])
        self.assertEqual(inspect(code, features())['reason'], 'instruction_length_mismatch')

    def test_truncated_bytecode_and_extra_tail_deny(self):
        for code in (b'', b'abc', ordinary()[:-4], ordinary()[:-5], ordinary() + words(0),
                     words(0xffff0200, 0x02000001, COLOR0)):
            with self.subTest(code_length=len(code)):
                self.assertFalse(inspect(code, features())['qualified'])

    def test_truncated_or_end_consuming_comment_deny(self):
        for code in (words(0xffff0200, 0x0004fffe, 0xffff), words(0xffff0200, 0x0001fffe, 0xffff)):
            self.assertFalse(inspect(code, features())['qualified'])

    def test_unsupported_stage_and_version_deny(self):
        for version in (0xfffe0200, 0xffff0202, 0xffff0400, 0xffff0100):
            code = words(version) + ordinary()[4:]
            self.assertEqual(inspect(code, features())['reason'], 'unsupported_pixel_shader_version')

    def test_predication_relative_and_unknown_flags_deny(self):
        cases = [shader(instruction(1, COLOR0, V0_SOURCE, flags=flag))
                 for flag in (0x10000000, 0x20000000, 0x40000000, 0x80000000, 0x00800000)]
        cases += [shader(instruction(1, COLOR0, 0xa0e42000)),
                  shader(instruction(1, COLOR0 | 0x2000, V0_SOURCE))]
        for code in cases:
            self.assertFalse(inspect(code, features())['qualified'])

    def test_does_not_mutate_inventory(self):
        inventory = features()
        before = copy.deepcopy(inventory)
        inspect(ordinary(), inventory)
        self.assertEqual(inventory, before)


if __name__ == '__main__':
    unittest.main()
