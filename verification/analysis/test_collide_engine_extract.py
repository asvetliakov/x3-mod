"""Host-only extraction fidelity checks; no compilation, Wine or game launch."""
import hashlib
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import build_collide_memo as build

Instruction = build.site.common.Instruction
LEAF = Instruction(0x4e2367, bytes.fromhex('dc1d08565600'), 'fcomp', 'QWORD PTR ds:0x565608')
LEGACY_RANGES = tuple((va, 12 if va == 0x565600 else size) for va, size in build.RANGES)


class OperandWidths(unittest.TestCase):
    def test_leaf_double_must_fit_in_full(self):
        with self.assertRaisesRegex(RuntimeError, r'0x565608\+8'):
            build.audit_absolute_memory([LEAF], LEGACY_RANGES)
        report = build.audit_absolute_memory([LEAF], build.RANGES)
        self.assertEqual(report['constant_spans'], [{'va': 0x565608, 'width': 8}])

    def test_last_byte_of_each_operand_must_be_covered(self):
        for name, width in [('BYTE', 1), ('WORD', 2), ('DWORD', 4), ('QWORD', 8), ('TBYTE', 10), ('XMMWORD', 16)]:
            with self.subTest(name=name):
                row = Instruction(0x400000, b'\x90', 'mov', f'{name} PTR ds:0x700000,eax')
                build.audit_absolute_memory([LEAF, row], (*build.RANGES, (0x700000, width)))
                with self.assertRaisesRegex(RuntimeError, 'unmapped absolute memory span'):
                    build.audit_absolute_memory([LEAF, row], (*build.RANGES, (0x700000, width - 1)))

    def test_mutable_blocks_have_width_bounds_too(self):
        good = Instruction(0x400000, b'\x90', 'mov', 'DWORD PTR ds:0x60854c,eax')
        bad = Instruction(0x400000, b'\x90', 'mov', 'QWORD PTR ds:0x60854c,eax')
        build.audit_absolute_memory([LEAF, good], build.RANGES)
        with self.assertRaisesRegex(RuntimeError, r'0x60854c\+8'):
            build.audit_absolute_memory([LEAF, bad], build.RANGES)

    def test_only_unprefixed_moffs32_gets_implicit_width(self):
        for raw, operands in [(b'\xa1', 'eax,ds:0x6619ec'), (b'\xa3', 'ds:0x6619ec,eax')]:
            build.audit_absolute_memory([LEAF, Instruction(0x400000, raw, 'mov', operands)], build.RANGES)
        row = Instruction(0x400000, b'\x66\xa1', 'mov', 'ax,ds:0x6619ec')
        with self.assertRaisesRegex(RuntimeError, 'unknown absolute memory width'):
            build.audit_absolute_memory([LEAF, row], build.RANGES)


@unittest.skipUnless(Path(build.sites.DEFAULT_EXE).is_file() and shutil.which(build.OBJDUMP),
                     'installed X3AP.exe and objdump required')
class InstalledExtraction(unittest.TestCase):
    def test_shared_fragment_has_complete_engine_constants(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(build, 'BUILD', Path(directory)):
            report = build.engine_fragment()
            self.assertTrue((Path(directory) / 'engine_ranges_inc.h').is_file())
        self.assertEqual(report['bytes'], 7296)
        self.assertEqual(report['sha256'], 'dde1e76bae547b14f2ea73bc37cfaea02795604f4b7586abec45f15ddca04f4b')
        self.assertEqual(report['data_audit']['reachable_instructions'], 2334)
        self.assertEqual(report['data_audit']['absolute_memory_operands'], 75)
        self.assertEqual(report['data_audit']['constant_spans'], [
            {'va': 0x5654e0, 'width': 4}, {'va': 0x565600, 'width': 4},
            {'va': 0x565604, 'width': 4}, {'va': 0x565608, 'width': 8}])

    def test_legacy_truncation_fails_before_overwriting_fragment(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(build, 'BUILD', Path(directory)):
            output = Path(directory) / 'engine_ranges_inc.h'
            output.write_text('preserved prior artifact')
            before = hashlib.sha256(output.read_bytes()).hexdigest()
            with mock.patch.object(build, 'RANGES', LEGACY_RANGES), self.assertRaisesRegex(RuntimeError, r'0x565608\+8'):
                build.engine_fragment()
            self.assertEqual(hashlib.sha256(output.read_bytes()).hexdigest(), before)

    def test_missing_code_tail_rejects_control_edge(self):
        ranges = ((build.RANGES[0][0], 9), *build.RANGES[1:])
        image = build.site.common.Image(Path(build.sites.DEFAULT_EXE).read_bytes())
        with self.assertRaisesRegex(RuntimeError, 'control edge outside extraction'):
            build.reachable_engine_instructions(build.sites.DEFAULT_EXE, image, ranges)


if __name__ == '__main__':
    unittest.main()
