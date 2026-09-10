"""Original shader-token fixtures for complete archive position classification."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
from inspect_archive_positions import classify
from inspect_rigid_positions import destination as dst, source as src


def op(opcode, *args):
    return [opcode | (len(args) << 24), *args]


def code(body):
    words = [0xfffe0300, *body, 0xffff]
    return struct.pack(f'<{len(words)}I', *words)


def declarations():
    return op(31, 0x80000000, dst(1, 2)) + op(31, 0x80000000, dst(6, 1))


def literal():
    return op(81, dst(2, 32), 0x3f800000, 0, 0, 0)


def constructor(target):
    return op(4, target, src(1, 2, 0x24), src(2, 32, 0x40), src(2, 32, 0x15))


def billboard(extra=(), add_mask=3, add_before_view=False, projection_pp=0):
    body = declarations() + op(31, 0x80000005, dst(1, 4)) + literal()
    body += constructor(dst(0, 3))
    add = op(2, dst(0, 5, add_mask), src(0, 5), src(1, 4))
    if add_before_view:
        body += add
    for row, mask in enumerate((1, 2, 4, 8)):
        body += op(9, dst(0, 5, mask), src(0, 3), src(2, 8 + row))
    if not add_before_view:
        body += add
    body += list(extra)
    for row, mask in enumerate((1, 2, 4, 8)):
        body += op(9, dst(6, 1, mask) | projection_pp, src(0, 5), src(2, 12 + row))
    return code(body)


class ArchivePositionTests(unittest.TestCase):
    def test_direct_xyzw_keeps_input_w(self):
        result = classify(code(declarations() + op(1, dst(6, 1), src(1, 2))))
        self.assertEqual(result['category'], 'direct_clip_xyzw')
        self.assertTrue(result['proof']['input_w_used'])

    def test_direct_xyz_one_requires_shader_literal(self):
        result = classify(code(declarations() + literal() + constructor(dst(6, 1))))
        self.assertEqual(result['category'], 'direct_clip_xyz_w_one')
        self.assertFalse(result['proof']['input_w_used'])
        self.assertEqual(classify(code(declarations() + constructor(dst(6, 1))))['category'], 'unknown')

    def test_direct_partial_modified_and_later_writes_rejected(self):
        for dest in (dst(6, 1, 7), dst(6, 1) | 0x00100000):
            self.assertEqual(classify(code(declarations() + op(1, dest, src(1, 2))))['category'], 'unknown')
        body = declarations() + op(1, dst(6, 1), src(1, 2)) + op(1, dst(6, 1, 1), src(1, 2))
        self.assertEqual(classify(code(body))['category'], 'unknown')

    def test_billboard_proves_two_matrices_and_offset_semantic(self):
        result = classify(billboard())
        self.assertEqual(result['category'], 'view_xy_billboard_projection')
        self.assertEqual(result['proof']['view']['matrix_first_register'], 8)
        self.assertEqual(result['proof']['projection']['matrix_first_register'], 12)
        self.assertEqual(result['proof']['offset_input_register'], 4)

    def test_billboard_wrong_offset_lane_order_and_precision_rejected(self):
        for data in (billboard(add_mask=7), billboard(add_before_view=True), billboard(projection_pp=0x00200000)):
            self.assertEqual(classify(data)['category'], 'unknown')

    def test_billboard_mutated_temporary_before_projection_rejected(self):
        result = classify(billboard(extra=op(1, dst(0, 5, 4), src(1, 2))))
        self.assertEqual(result['category'], 'unknown')

    def test_control_flow_is_not_ignored_in_exception_paths(self):
        body = declarations() + op(40, src(14, 0)) + op(1, dst(6, 1), src(1, 2)) + op(43)
        self.assertEqual(classify(code(body))['category'], 'unknown')


if __name__ == '__main__':
    unittest.main()
