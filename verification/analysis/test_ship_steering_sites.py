"""The five ship-steering sites (four of step 1 and the warn test of step 2) (verify_ship_steering_sites.py): the installed EXE verifies PASS when present, the
header's SiteSpec table is the derived table, and the structural checks refuse an interior branch, a truncated span
and a changed byte (on the installed image's decoded listings, read-only)."""
import dataclasses
import unittest
from pathlib import Path
import verify_ship_steering_sites as probe

ROOT = Path(__file__).resolve().parents[2]
EXE = probe.DEFAULT_EXE


@unittest.skipUnless(EXE.is_file(), 'installed X3AP.exe not present')
class ShipSteeringSites(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = probe.common.image_bytes(EXE)
        cls.image = probe.common.Image(cls.data)
        cls.decoded = probe.decode(EXE)

    def test_installed_exe_passes(self):
        report = probe.verify(EXE, whole_text=False)  # the whole-.text sweep runs in the acceptance command
        failed = {k: v for k, v in report['checks'].items() if not v['ok']}
        self.assertEqual(failed, {}, failed)
        self.assertEqual(report['result'], 'PASS')
        self.assertEqual(report['checks']['b_incoming']['raw_encodings'], 100)
        self.assertEqual(report['checks']['b_incoming']['order_jump_table_entries'], 1)
        self.assertEqual(len(report['checks']['b_incoming']['dword_edges']), 2)
        self.assertEqual(report['checks']['no_data_dword_into_window']['interior'], {s.name: [] for s in probe.SITES})
        self.assertEqual(report['checks']['large_predicate_mask']['derived_mask'], '0xe91d')
        self.assertEqual(report['checks']['warn_test_site']['callers'], ['0x44e522', '0x44e540', '0x456c02'])
        self.assertTrue(report['checks']['warn_test_site']['out_local_initialised_and_unread'])
        self.assertTrue(all(report['checks']['snapshot_walk_facts']['facts'].values()))

    def test_header_specs_match(self):
        self.assertEqual(probe.header_specs(probe.SITES_HEADER.read_text()), probe.expected_specs())

    def test_site_w_removed(self):
        # stage B deletion (2026-10-15): site W 0x00465bac is neither claimed by the header nor checked (the other sites unchanged)
        text = probe.SITES_HEADER.read_text()
        self.assertNotIn('ship_gate_warp_in', text)
        self.assertIn('GateCount = 3', text)
        self.assertNotIn(0x00465bac, [s.va for s in probe.SITES])
        self.assertEqual([s.name for s in probe.SITES if s.name.startswith('ship_gate_')],
                         ['ship_gate_stop_a', 'ship_gate_stop_b', 'ship_gate_launch', 'ship_gate_arrival_launch'])

    def test_all_spans_are_whole_and_plain(self):
        for spec in probe.SITES:
            with self.subTest(site=spec.name):
                self.assertTrue(probe.common.inspect_site(self.image, spec, self.decoded[(spec.function_start, spec.function_end)])['ok'])

    def test_interior_branch_rejected_each_site(self):
        for spec in probe.SITES:
            with self.subTest(site=spec.name):
                incoming = probe.common.Instruction(spec.function_end - 10, b'\xe9\x00\x00\x00\x00', 'jmp', hex(spec.va + 1))
                listing = self.decoded[(spec.function_start, spec.function_end)] + [incoming]
                self.assertFalse(probe.common.inspect_site(self.image, spec, listing)['no_interior_branch'])

    def test_truncated_spans_rejected(self):
        for spec in probe.SITES:
            with self.subTest(site=spec.name):
                listing = [i for i in self.decoded[(spec.function_start, spec.function_end)] if i.end != spec.end]
                self.assertFalse(probe.common.inspect_site(self.image, spec, listing)['whole_instructions'])

    def test_changed_byte_rejected_each_site(self):
        for spec in probe.SITES:
            with self.subTest(site=spec.name):
                wrong = dataclasses.replace(spec, expected=bytes([spec.expected[0] ^ 1]) + spec.expected[1:])
                self.assertFalse(probe.common.inspect_site(self.image, wrong, self.decoded[(spec.function_start, spec.function_end)])['bytes_ok'])


if __name__ == '__main__':
    unittest.main()
