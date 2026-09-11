"""Original metadata controls for the combined-DLL portable upload expectations."""
import copy
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

PATH = Path(__file__).parents[1] / 'probe/verify_ownership_integration.py'
SPEC = importlib.util.spec_from_file_location('portable_integration_expectations', PATH)
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


def metrics():
    records = []
    for device in (1, 2):
        for frame, phase in [(f, 'present') for f in range(6)] + [(6, 'reset_before'), (6, 'reset_after')]:
            records.append(dict(device=str(device), frame=str(frame), phase=phase,
                result='00000000', status='00000000', requested='1', active='1',
                generation='2' if phase == 'reset_after' else '1', uploads='3' if frame >= 4 else '2',
                publications='3' if frame >= 4 else '2', scans='3' if frame >= 4 else '2',
                classified_bytes='126' if frame >= 4 else '66', allocation_failures='0',
                position_components='0', peak_payload_bytes='10', sidecars='2', metadata_bytes='200',
                payload_bytes='0' if phase == 'reset_after' else '10'))
    return records


def draw(indexed=True, success=True):
    known = indexed and success
    return dict(kind='indexed' if indexed else 'indexed_up', draw_result={'result': '00000000' if success else '8876086c'},
        motion_input_matches_draw=True, motion_geometry_matches_draw=True,
        motion_input=dict(vertex_finite_verified='0', lifetime_verified='0', ib_revision='1'),
        motion_geometry=dict(source_qualified='0', source_hash='0000000000000000', source_words='0', finite_state='0',
            index_required=str(int(indexed)), index_requested=str(int(known)), index_known=str(int(known)),
            index_range_verified=str(int(known)), index_exact=str(int(known)), index_status='00000000',
            index_reason='0', index_generation='1', index_revision='1', index_min='0', index_max='2'))


def trace(records, count=12):
    return '\n'.join(['finite_upload_mode requested=1 enabled=1 payload_retained=0'] +
                     ['finite_upload_metric ' + ' '.join(f'{k}={v}' for k, v in m.items()) for m in records] +
                     ['motion_geometry index=1'] * count)


class PortableIntegrationExpectationsTests(unittest.TestCase):
    def test_authored_upload_byte_counts_and_reset_pass(self):
        result = VERIFY.verify_portable_capture_metrics(metrics())
        self.assertEqual(result['classified_bytes_per_device'], 126)
        self.assertEqual(result['uploads_per_device'], 3)
        self.assertTrue(result['cumulative_samples_not_summed'])

    def test_missing_duplicate_or_wrong_owner_sample_rejected(self):
        samples = metrics()
        for records in (samples[:-1], samples + samples[:1], [dict(samples[0], device='3')] + samples[1:]):
            with self.assertRaises(AssertionError):
                VERIFY.verify_portable_capture_metrics(records)

    def test_wrong_counts_or_no_readable_allocation_rejected(self):
        for name, value in [('publications', '0'), ('uploads', '3'), ('scans', '0'), ('classified_bytes', '76'),
                            ('payload_bytes', '0'), ('sidecars', '0'), ('generation', '0'),
                            ('position_components', '3'), ('allocation_failures', '1')]:
            records = metrics(); records[0][name] = value
            with self.subTest(name=name), self.assertRaises(AssertionError):
                VERIFY.verify_portable_capture_metrics(records)
        records = metrics(); records[-1]['payload_bytes'] = '10'
        with self.assertRaises(AssertionError):
            VERIFY.verify_portable_capture_metrics(records)

    def test_index_evidence_does_not_grant_position_or_source_proof(self):
        draws = [draw() for _ in range(8)] + [draw(False), draw(False), draw(success=False), draw(success=False)]
        with patch.object(VERIFY, 'summarize', return_value={'frames': {'1:1': {'draws': draws}}}):
            result = VERIFY.verify_geometry(trace(metrics()), True, True, portable_capture=True)
        self.assertEqual(result['known_index_draws'], 8)
        for field, value in [('source_qualified', '1'), ('finite_state', '1'), ('index_min', '1'), ('index_revision', '2'), ('index_exact', '0')]:
            broken = copy.deepcopy(draws); broken[0]['motion_geometry'][field] = value
            with self.subTest(field=field), patch.object(VERIFY, 'summarize', return_value={'frames': {'1:1': {'draws': broken}}}):
                with self.assertRaises(AssertionError):
                    VERIFY.verify_geometry(trace(metrics()), True, True, portable_capture=True)

    def test_missing_index_failed_draw_cannot_report_ib_evidence(self):
        draws = [draw() for _ in range(8)] + [draw(False), draw(False), draw(success=False), draw(success=False)]
        draws[-1]['motion_geometry']['index_known'] = '1'
        with patch.object(VERIFY, 'summarize', return_value={'frames': {'1:1': {'draws': draws}}}):
            with self.assertRaises(AssertionError):
                VERIFY.verify_geometry(trace(metrics()), True, True, portable_capture=True)

    def test_empty_enabled_owner_requires_exact_fixed_metadata(self):
        record=dict(metrics()[0], payload_bytes='0', peak_payload_bytes='0', sidecars='0',
                    metadata_bytes='8192', global_payload_bytes='0', global_sidecars='0',
                    uploads='0', publications='0', scans='0', classified_bytes='0')
        with patch.object(VERIFY, 'summarize', return_value={'frames': {}}):
            VERIFY.verify_geometry(trace([record], count=0), True, True)
            for value in ('0', '8191', '8193', '16384'):
                with self.subTest(metadata=value), self.assertRaises(AssertionError):
                    VERIFY.verify_geometry(trace([dict(record, metadata_bytes=value)], count=0), True, True)
            fallback=dict(record, result='80070057', requested='0', active='0', metadata_bytes='0')
            VERIFY.verify_geometry(trace([fallback], count=0), True, True, native_fallback=True)
            with self.assertRaises(AssertionError):
                VERIFY.verify_geometry(trace([dict(fallback, metadata_bytes='8192')], count=0), True, True, native_fallback=True)

    def test_old_native_contract_refusal_cannot_replace_portable_acceptance(self):
        data = trace(metrics()) + '\nfinite_upload_reason reason=9 count=1'
        with self.assertRaises(AssertionError):
            VERIFY.verify_geometry(data, True, True, portable_capture=True)


if __name__ == '__main__':
    unittest.main()
