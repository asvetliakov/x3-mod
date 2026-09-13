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
        self.assertEqual([x['whole_instructions'] for x in report['sites']], [True]*9)
        self.assertEqual([len(x['instructions']) for x in report['sites']], [1,1,1,1,1,2,1,2,1])

    def test_relocation_field_is_actual_last_cpp_field(self):
        changed = self.source.replace('6, 0, 2}', '6, 2, 0}')
        self.assertNotEqual(changed, self.source)
        self.assertFalse(probe.source_checks(changed)['source_specs'])

    def test_each_site_byte_corruption(self):
        for spec in probe.ALL_SITES:
            with self.subTest(site=spec.name):
                report = self.report(PatchedImage(self.image, [(spec.va, b'\x90')]))
                self.assertEqual(report['result'], 'FAIL')
                self.assertFalse(next(x for x in report['sites'] if x['name'] == spec.name)['bytes_ok'])

    def test_real_incoming_interior_branch_each_site(self):
        for spec in probe.ALL_SITES:
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
        for spec in probe.ALL_SITES:
            with self.subTest(site=spec.name):
                decoded = dict(self.decoded)
                bounds = (spec.function_start, spec.function_end)
                decoded[bounds] = [x for x in decoded[bounds] if x.va != spec.va]
                self.assertFalse(self.report(decoded=decoded)['checks']['sites'])

    def test_wrong_jz_target_rejected_even_with_expected_image_bytes(self):
        for spec in (probe.SITES[0], probe.HUD_SITE):
            with self.subTest(site=spec.name):
                decoded = dict(self.decoded)
                decoded[probe.OVERLAY] = [dataclasses.replace(x, operands='0x42aad3')
                                          if x.va == spec.va else x for x in decoded[probe.OVERLAY]]
                row = next(x for x in self.report(decoded=decoded)['sites'] if x['name'] == spec.name)
                self.assertFalse(row['relocated_jz'])

    def test_hud_spec_missing_or_wrong_relocation_rejected(self):
        match = probe.HUD_SOURCE_RE.search(self.source)
        self.assertIsNotNone(match)
        for changed in (probe.HUD_SOURCE_RE.sub('', self.source),
                        self.source[:match.start()] + match.group().replace('6, 0, 2', '6, 2, 0') +
                        self.source[match.end():]):
            self.assertFalse(probe.source_checks(changed)['source_hud_spec'])
            self.assertTrue(probe.source_checks(changed)['source_specs'])

    def test_hud_source_name_address_and_bytes_rejected(self):
        match = probe.HUD_SOURCE_RE.search(self.source)
        self.assertIsNotNone(match)
        for old, new in (('chase_central_hud_gate', 'other_hud'), ('0x0042aae0', '0x0042aae1'),
                         ('0x9c', '0x9d')):
            with self.subTest(field=old):
                replacement = match.group().replace(old, new)
                self.assertNotEqual(replacement, match.group())
                changed = self.source[:match.start()] + replacement + self.source[match.end():]
                self.assertFalse(probe.source_checks(changed)['source_hud_spec'])

    def test_hud_context_corruptions(self):
        for name, (va, raw) in probe.HUD_WITNESSES.items():
            with self.subTest(context=name):
                original = bytes.fromhex(raw)
                report = self.report(PatchedImage(self.image, [(va, bytes([original[0] ^ 1]))]))
                self.assertFalse(report['checks']['hud_context'])

    def test_timing_source_missing_or_wrong_relocation_rejected(self):
        match = probe.TIMING_SOURCE_RE.search(self.source)
        self.assertIsNotNone(match)
        changes = [probe.TIMING_SOURCE_RE.sub('', self.source)]
        for old, new in (('5, 0, 1', '5, 1, 0'), ('8, 0, 4', '8, 0, 2'),
                         ('0x0042aed6', '0x0042aed7'),
                         ('chase_native_distance_end', 'other_distance_end')):
            replacement = match.group().replace(old, new)
            self.assertNotEqual(replacement, match.group())
            changes.append(self.source[:match.start()] + replacement + self.source[match.end():])
        for changed in changes:
            self.assertFalse(probe.source_checks(changed)['source_timing_specs'])
            self.assertTrue(probe.source_checks(changed)['source_specs'])
            self.assertTrue(probe.source_checks(changed)['source_hud_spec'])

    def test_all_relative_targets_rejected_if_decode_disagrees(self):
        for spec in probe.ALL_SITES:
            if spec.va not in probe.RELATIVE_SITES:
                continue
            with self.subTest(site=spec.name):
                _, control_va, _ = probe.RELATIVE_SITES[spec.va]
                decoded = dict(self.decoded)
                bounds = (spec.function_start, spec.function_end)
                decoded[bounds] = [dataclasses.replace(x, operands='0x42aad3')
                                   if x.va == control_va else x for x in decoded[bounds]]
                row = next(x for x in self.report(decoded=decoded)['sites'] if x['name'] == spec.name)
                self.assertFalse(row['relocated_control'])
                self.assertFalse(row['ok'])

    def test_failure_and_hide_paths_must_reach_common_endpoints(self):
        for va, target, check in (
            (0x42a799, '0x42c155', 'solver_success_failure_endpoint'),
            (0x42a6fe, '0x42c155', 'lead_common_endpoint'),
            (0x42aae0, '0x42aedc', 'central_draw_hide_endpoint'),
            (0x42ae80, '0x42aedc', 'central_draw_hide_endpoint'),
        ):
            with self.subTest(edge=hex(va)):
                decoded = dict(self.decoded)
                decoded[probe.OVERLAY] = [dataclasses.replace(x, operands=target)
                                         if x.va == va else x for x in decoded[probe.OVERLAY]]
                self.assertFalse(self.report(decoded=decoded)['checks'][check])

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


class NormalPathProof(unittest.TestCase):
    def test_returning_call_reaches_endpoint(self):
        instructions = [Instruction(0x100, b'\xe8\0\0\0\0', 'call', '0x900'),
                        Instruction(0x105, b'\xc3', 'ret', '')]
        self.assertTrue(probe.all_paths_reach(instructions, 0x100, 0x105))

    def test_cycle_indirect_escape_and_missing_endpoint_rejected(self):
        for instruction in (
            Instruction(0x100, b'\xeb\xfe', 'jmp', '0x100'),
            Instruction(0x100, b'\xff\xe0', 'jmp', 'eax'),
            Instruction(0x100, b'\xeb\x7e', 'jmp', '0x180'),
            Instruction(0x100, b'\xc3', 'ret', ''),
            Instruction(0x100, b'\x90', 'nop', ''),
        ):
            with self.subTest(operation=instruction.mnemonic, operands=instruction.operands):
                self.assertFalse(probe.all_paths_reach([instruction], 0x100, 0x105))


if __name__ == '__main__':
    unittest.main()
