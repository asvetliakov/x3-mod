"""Original token fixtures for the narrow rigid-position proof, not game shaders."""
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
from inspect_rigid_positions import destination as dst, source as src, prove


def pack(words):
    return struct.pack(f'<{len(words)}I', *words)


def fixture(major=3, extra_before=(), extra_after=(), rows=(0, 1, 2, 3), modifiers=0):
    def op(opcode, *args):
        return [opcode | (len(args) << 24 if major >= 2 else 0), *args]
    output_kind = 6 if major == 3 else 4
    body = [0xfffe0000 | (major << 8) | (1 if major == 1 else 0)]
    body += op(81, dst(2, 4), 0x3f800000, 0, 0, 0)
    body += op(31, 0x80000000, dst(1, 0))
    if major == 3:
        body += op(31, 0x80000000, dst(6, 0))
    body += op(4, dst(0, 0), src(1, 0, 0x24), src(2, 4, 0x40), src(2, 4, 0x15))
    body += list(extra_before)
    for mask, row in zip((1, 2, 4, 8), rows):
        body += op(9, dst(output_kind, 0, mask) | modifiers, src(0, 0), src(2, row))
    body += list(extra_after)
    return body + [0xffff]


class RigidPositionTests(unittest.TestCase):
    def test_exact_contract_all_observed_shader_models(self):
        for major in (1, 2, 3):
            with self.subTest(major=major):
                result = prove(pack(fixture(major)))
                self.assertTrue(result['qualified_position_math'])
                self.assertFalse(result['input_w_used'])
                self.assertEqual(result['matrix_first_register'], 0)
                self.assertEqual(len(result['position_dot_dwords_xyzw']), 4)

    def test_unrelated_loop_does_not_invalidate_protected_position_temp(self):
        # REP i0; MOV r1,v0; ENDREP never writes protected r0.
        result = prove(pack(fixture(extra_before=(0x01000026, src(7, 0),
            0x02000001, dst(0, 1), src(1, 0), 0x00000027))))
        self.assertTrue(result['qualified_position_math'])

    def test_conditional_temp_write_is_not_ignored(self):
        result = prove(pack(fixture(extra_before=(0x01000028, src(14, 0),
            0x02000001, dst(0, 0), src(1, 0), 0x0000002b))))
        self.assertFalse(result['qualified_position_math'])
        self.assertIn('single_definition', result['reason'])

    def test_temporary_reuse_after_last_position_use_is_safe(self):
        result = prove(pack(fixture(extra_after=(0x02000001, dst(0, 0), src(1, 0)))))
        self.assertTrue(result['qualified_position_math'])

    def test_later_output_write_rejects_four_dot_motif(self):
        result = prove(pack(fixture(extra_after=(0x02000001, dst(6, 0), src(1, 0)))))
        self.assertFalse(result['qualified_position_math'])

    def test_position_modifiers_and_wrong_row_order_rejected(self):
        for modifier in (0x00100000, 0x00200000):
            self.assertFalse(prove(pack(fixture(modifiers=modifier)))['qualified_position_math'])
        self.assertFalse(prove(pack(fixture(rows=(1, 0, 2, 3))))['qualified_position_math'])

    def test_host_constant_or_wrong_homogeneous_constructor_rejected(self):
        words = fixture()
        words[3] = 0  # The literal X is no longer one.
        self.assertFalse(prove(pack(words))['qualified_position_math'])
        words = fixture()
        words[words.index(src(1, 0, 0x24))] = src(1, 0)  # Reads input W.
        self.assertFalse(prove(pack(words))['qualified_position_math'])
        self.assertFalse(prove(pack(fixture(rows=(4, 5, 6, 7))))['qualified_position_math'])

    def test_control_flow_position_writes_and_early_return_rejected(self):
        words = fixture(extra_before=(0x01000028, src(14, 0)), extra_after=(0x0000002b,))
        self.assertFalse(prove(pack(words))['qualified_position_math'])
        self.assertFalse(prove(pack(fixture(extra_before=(0x0000001c,))))['qualified_position_math'])

    def test_comments_cannot_invent_a_position_definition(self):
        words = fixture()
        # A comment containing opcode-like words is skipped as an indivisible block.
        words[1:1] = [0x0003fffe, 0x02000001, dst(6, 0), src(1, 0)]
        self.assertTrue(prove(pack(words))['qualified_position_math'])
        with self.assertRaises(ValueError):
            prove(pack(words[:-1]))


if __name__ == '__main__':
    unittest.main()
