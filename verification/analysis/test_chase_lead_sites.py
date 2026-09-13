"""Instruction and fail-closed ABI tests for native predictive-marker hooks."""
import dataclasses
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_chase_lead_sites as probe
from verify_chase_aim_sites import Instruction


class PatchedImage:
    def __init__(self, image, overrides=()):
        self.image = image
        self.image_base = image.image_base
        self.overrides = dict(overrides)

    def read(self, va, size):
        raw = self.image.read(va, size)
        if raw is None:
            return None
        value = bytearray(raw)
        for at, blob in self.overrides.items():
            for offset, byte in enumerate(blob):
                if va <= at + offset < va + size:
                    value[at + offset - va] = byte
        return bytes(value)


@unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed X3AP.exe unavailable')
class LeadSites(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = probe.Image(probe.DEFAULT_EXE.read_bytes())
        cls.decoded = probe.disassemble_functions(probe.DEFAULT_EXE)
        cls.source = probe.DEFAULT_SOURCE.read_text()

    def report(self, image=None, decoded=None, source=None):
        return probe.inspect(image or self.image, decoded or self.decoded,
                             self.source if source is None else source)

    def test_installed_image_and_source(self):
        report = probe.verify_path()
        self.assertEqual(report['result'], 'PASS', report)
        self.assertEqual([x['whole_instructions'] for x in report['sites']], [True]*3)
        self.assertEqual([len(x['instructions']) for x in report['sites']], [1]*3)

    def test_relocation_field_is_actual_last_cpp_field(self):
        changed = self.source.replace('6, 0, 2}', '6, 2, 0}')
        self.assertNotEqual(changed, self.source)
        self.assertFalse(probe.source_checks(changed)['source_specs'])

    def test_each_site_byte_corruption(self):
        for spec in probe.SITES:
            with self.subTest(site=spec.name):
                report = self.report(PatchedImage(self.image, [(spec.va, b'\x90')]))
                self.assertEqual(report['result'], 'FAIL')
                self.assertFalse(next(x for x in report['sites'] if x['name'] == spec.name)['bytes_ok'])

    def test_real_incoming_interior_branch_each_site(self):
        for spec in probe.SITES:
            with self.subTest(site=spec.name):
                decoded = dict(self.decoded)
                bounds = (spec.function_start, spec.function_end)
                decoded[bounds] = list(decoded[bounds]) + [Instruction(
                    spec.function_start, b'\xe9\0\0\0\0', 'jmp', f'0x{spec.va+1:x}')]
                row = next(x for x in self.report(decoded=decoded)['sites'] if x['name'] == spec.name)
                self.assertFalse(row['no_interior_branch'])
                self.assertFalse(row['ok'])

    def test_opcode_alias_is_not_an_incoming_branch(self):
        decoded = dict(self.decoded)
        decoded[probe.OVERLAY] = list(decoded[probe.OVERLAY]) + [Instruction(
            0x42a400, b'\x68\x74\x10\0\0', 'push', '0x1074')]
        self.assertEqual(self.report(decoded=decoded)['result'], 'PASS')

    def test_site_inside_decoded_instruction_rejected(self):
        for spec in probe.SITES:
            with self.subTest(site=spec.name):
                decoded = dict(self.decoded)
                bounds = (spec.function_start, spec.function_end)
                decoded[bounds] = [x for x in decoded[bounds] if x.va != spec.va]
                self.assertFalse(self.report(decoded=decoded)['checks']['sites'])

    def test_wrong_jz_target_rejected_even_with_expected_image_bytes(self):
        decoded = dict(self.decoded)
        decoded[probe.OVERLAY] = [dataclasses.replace(x, operands='0x42aad3')
                                  if x.va == probe.SITES[0].va else x for x in decoded[probe.OVERLAY]]
        self.assertFalse(self.report(decoded=decoded)['sites'][0]['relocated_jz'])

    def test_context_corruptions(self):
        for name, (va, raw) in probe.WITNESSES.items():
            with self.subTest(context=name):
                original = bytes.fromhex(raw)
                report = self.report(PatchedImage(self.image, [(va, bytes([original[0] ^ 1]))]))
                self.assertFalse(report['checks']['context'])

    def test_full_helper_corruption_including_detach_and_return(self):
        for offset in range(len(probe.HIDE_BYTES)):
            with self.subTest(offset=offset):
                changed = bytes([probe.HIDE_BYTES[offset] ^ 1])
                report = self.report(PatchedImage(self.image, [(probe.HIDE[0]+offset, changed)]))
                self.assertFalse(report['checks']['hide_full_bytes'])

    def test_helper_bad_call_target_and_stack_return_rejected(self):
        for at, replacement, check in (
            (0x426291, {'operands': '0x489e91'}, 'hide_call_target'),
            (0x42629d, {'raw': b'\xc2\x04\0'}, 'hide_abi'),
        ):
            with self.subTest(check=check):
                decoded = dict(self.decoded)
                decoded[probe.HIDE] = [dataclasses.replace(x, **replacement)
                                       if x.va == at else x for x in decoded[probe.HIDE]]
                self.assertFalse(self.report(decoded=decoded)['checks'][check])

    def test_helper_source_missing_or_truncated_rejected(self):
        for source in ('', self.source.replace('0x59, 0xc3', '0x59')):
            self.assertFalse(probe.source_checks(source)['source_hide_bytes'])

    def test_missing_function_decode_rejected(self):
        for bounds in self.decoded:
            with self.subTest(bounds=bounds):
                decoded = dict(self.decoded)
                del decoded[bounds]
                self.assertEqual(self.report(decoded=decoded)['result'], 'FAIL')


if __name__ == '__main__':
    unittest.main()
