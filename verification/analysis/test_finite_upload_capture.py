"""Original small traces for finite-upload metadata, no game or payload reads."""
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
from types import SimpleNamespace
from unittest.mock import patch
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('finite_upload_capture', Path(__file__).parents[2] / 'tools/analysis/analyze_finite_upload_capture.py')
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


def trace(device=1, frame=10, indexed=True):
    c = f'device={device} frame={frame}'
    d = c + ' index=1'
    kind = 'indexed' if indexed else 'primitive'
    args = 'base_vertex=0 min_vertex=0 num_vertices=3 start_index=0' if indexed else 'start_vertex=0'
    index_fields = ('index_required=1 index_requested=1 index_known=1 index_range_verified=1 index_exact=0 index_min=0 index_max=2 index_reason=0 index_status=00000000 index_generation=1 index_revision=5'
                    if indexed else 'index_required=0 index_requested=0 index_known=0 index_range_verified=1 index_exact=0 index_min=0 index_max=0 index_reason=1 index_status=00000001 index_generation=0 index_revision=0')
    return [
        f'frame_begin {c}',
        f'draw {d} kind={kind} topology=4 primitives=1 vs=aa ps=bb',
        f'object_context {d} scoped=1 valid=7 scope_depth=1 node=00001000 node_handle=42 camera=00002000 camera_handle=7 mesh=00003000 registry=00004000',
        f'draw_args {args}',
        f'motion_input {d} blockers=00000000 proofs=31 position_path=1 vs=aa ps=bb declaration=123 rows_hash=456 color=1 depth=2 width=64 height=64 cull=3 vb=10 vb_revision=4 ib={11 if indexed else 0} ib_revision={5 if indexed else 0} position_offset=0 position_type=2 lifetime_verified=1 vertex_finite_verified=1',
        f'motion_geometry {d} source_qualified=1 source_hash=aa source_words=32 finite_requested=1 finite_state=1 finite_reason=0 finite_status=00000000 finite_generation=1 finite_revision=4 {index_fields}',
        f'motion_lifetime {d} before_known=1 after_known=1 before_reason=0 after_reason=0 registry=00004000 observer_epoch=1 load_epoch=1 registry_epoch=1 mutation_before=9 mutation_after=9 node_serial=101 camera_serial=102 observer_epoch_after=1 load_epoch_after=1 registry_epoch_after=1 node_serial_after=101 camera_serial_after=102',
        f'draw_result {d} result=00000000',
        f'frame_end {c} draws=1 capture=1 present=00000000',
    ]


def metric(device=1, frame=10, generation=1, uploads=10, optional=True):
    values = dict(payload_bytes=64, peak_payload_bytes=128, sidecars=2, metadata_bytes=256,
                  global_payload_bytes=64, global_sidecars=2, uploads=uploads, publications=8,
                  invalidations=2, allocation_failures=0, scans=10, classified_bytes=640,
                  scan_ticks=100, queries=30, query_cache_hits=25, position_components=90)
    if optional:
        values.update(qualifier_ticks=200, query_ticks=300)
    prefix = f'finite_upload_metric device={device} frame={frame} phase=present result=00000000 status=00000000 requested=1 active=1 generation={generation}'
    return prefix + ' ' + ' '.join(f'{key}={value}' for key, value in values.items())


def modify(lines, event, old, new):
    return [line.replace(old, new) if line.startswith(event + ' ') else line for line in lines]


def candidates(report):
    return report['totals'].get('input_candidates', 0)


class FiniteUploadCaptureTests(unittest.TestCase):
    def test_indexed_conservative_bounds_and_primitive_both_qualify(self):
        report = AUDIT.analyze(trace() + trace(frame=11, indexed=False))
        self.assertEqual(candidates(report), 2)
        self.assertEqual(report['totals']['finite_finite'], 2)
        self.assertEqual(report['totals']['index_bounds_conservative'], 1)
        self.assertIn('not replay/TAA', report['candidate_meaning'])

    def test_source_qualification_and_finite_are_independent(self):
        lines = modify(trace(), 'motion_geometry', 'source_qualified=1 source_hash=aa source_words=32', 'source_qualified=0 source_hash=0 source_words=0')
        report = AUDIT.analyze(lines)
        self.assertEqual(report['totals']['source_unqualified'], 1)
        self.assertEqual(report['totals']['finite_finite'], 1)
        self.assertEqual(candidates(report), 0)

    def test_qualified_source_must_match_actual_shader(self):
        for value in ('bb', '0', 'not_hex', '10000000000000000'):
            self.assertEqual(candidates(AUDIT.analyze(modify(trace(), 'motion_geometry', 'source_hash=aa', 'source_hash=' + value))), 0)

    def test_nonfinite_and_unknown_are_distinct(self):
        lines = modify(trace(), 'motion_geometry', 'finite_state=1 finite_reason=0', 'finite_state=2 finite_reason=17')
        lines = modify(lines, 'motion_input', 'vertex_finite_verified=1', 'vertex_finite_verified=0')
        report = AUDIT.analyze(lines)
        self.assertEqual(report['totals']['finite_nonfinite'], 1)
        self.assertEqual(candidates(report), 0)
        lines = modify(trace(), 'motion_geometry', 'finite_state=1 finite_reason=0 finite_status=00000000', 'finite_state=0 finite_reason=16 finite_status=00000001')
        self.assertEqual(AUDIT.analyze(lines)['totals']['finite_unknown'], 1)

    def test_old_schema_missing_finite_records_is_unknown(self):
        lines = [line for line in trace() if not line.startswith('motion_geometry ')]
        lines = modify(lines, 'motion_input', ' vertex_finite_verified=1', '')
        report = AUDIT.analyze(lines)
        self.assertEqual(candidates(report), 0)
        self.assertEqual(report['totals']['finite_unknown'], 1)
        self.assertEqual(report['totals']['finite_flag_missing'], 1)
        self.assertEqual(report['totals']['legacy_local_gates_and_lifetime'], 1)

    def test_declared_range_does_not_override_actual_ib_bounds(self):
        for old, new in [('index_max=2', 'index_max=3'), ('index_min=0', 'index_min=9'), ('index_known=1', 'index_known=0'), ('index_requested=1', 'index_requested=0')]:
            report = AUDIT.analyze(modify(trace(), 'motion_geometry', old, new))
            self.assertEqual(candidates(report), 0)
            self.assertEqual(report['totals']['index_gate_inconsistent'], 1)

    def test_index_generation_revision_and_finite_revision_match(self):
        for old, new in [('index_generation=1', 'index_generation=2'), ('index_revision=5', 'index_revision=4'), ('finite_revision=4', 'finite_revision=0'), ('finite_generation=1', 'finite_generation=0'), ('finite_requested=1', 'finite_requested=0')]:
            self.assertEqual(candidates(AUDIT.analyze(modify(trace(), 'motion_geometry', old, new))), 0)
        lines = modify(trace(), 'motion_geometry', 'finite_revision=4', 'finite_revision=0')
        lines = modify(lines, 'motion_input', 'vb_revision=4', 'vb_revision=0')
        self.assertEqual(candidates(AUDIT.analyze(lines)), 0)

    def test_index_exact_flag_is_not_required_for_conservative_bounds(self):
        for exact in ('0', '1'):
            report = AUDIT.analyze(modify(trace(), 'motion_geometry', 'index_exact=0', 'index_exact=' + exact))
            self.assertEqual(candidates(report), 1)

    def test_negative_base_and_missing_args_reject_indexed_gate(self):
        self.assertEqual(candidates(AUDIT.analyze(modify(trace(), 'draw_args', 'base_vertex=0', 'base_vertex=-1'))), 0)
        self.assertEqual(candidates(AUDIT.analyze([line for line in trace() if not line.startswith('draw_args ')])), 0)

    def test_primitive_args_missing_duplicate_or_malformed_poison(self):
        original = trace(indexed=False)
        args = next(line for line in original if line.startswith('draw_args '))
        cases = [[line for line in original if not line.startswith('draw_args ')],
                 original[:4] + [args] + original[4:]]
        for replacement in ('start_vertex=bad', 'start_vertex=4294967296', 'start_vertex=0 start_vertex=1'):
            cases.append(modify(original, 'draw_args', 'start_vertex=0', replacement))
        for lines in cases:
            with self.subTest(lines=lines):
                report = AUDIT.analyze(lines)
                self.assertEqual(candidates(report), 0)
                self.assertEqual(report['totals']['poisoned_draw'], 1)

    def test_draw_shape_and_typed_scalar_bounds(self):
        for event, old, new in [
            ('draw', 'topology=4', 'topology=2'), ('draw', 'primitives=1', 'primitives=0'),
            ('draw', 'primitives=1', 'primitives=bad'), ('draw', 'primitives=1', 'primitives=4294967296'),
            ('draw_args', 'start_index=0', 'start_index=bad'),
            ('motion_input', 'position_path=1', 'position_path=bad'),
            ('motion_input', 'width=64', 'width=4294967296'),
            ('motion_input', 'rows_hash=456', 'rows_hash=10000000000000000'),
            ('motion_geometry', 'finite_status=00000000', 'finite_status=100000000'),
            ('motion_geometry', 'index_reason=0', 'index_reason=4294967296'),
        ]:
            with self.subTest(event=event, value=new):
                self.assertEqual(candidates(AUDIT.analyze(modify(trace(), event, old, new))), 0)
        self.assertEqual(candidates(AUDIT.analyze(modify(trace(), 'draw', 'topology=4', 'topology=5'))), 1)

    def test_metric_hresult_and_descriptor_widths_are_checked(self):
        report = AUDIT.analyze([metric().replace('status=00000000', 'status=100000000')])
        self.assertFalse(report['owner_metrics']['latest_per_owner']['1']['known'])
        detail = 'finite_upload_first_refusal device=1 frame=10 phase=present reason=9 name=native_contract type=6 format=0 pool=1 size=4294967296 usage=00000008 lock_flags=00000000'
        report = AUDIT.analyze([metric(), detail])
        self.assertFalse(report['owner_metrics']['latest_per_owner']['1']['known'])

    def test_analyzer_source_change_during_scan_rejects_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture.log'
            path.write_text('\n'.join(trace()) + '\n')
            with patch.object(Path, 'read_bytes', side_effect=[b'before', b'after']):
                with self.assertRaisesRegex(ValueError, 'analyzer source changed'):
                    AUDIT.analyze_snapshot(path)

    def test_lifetime_scope_proofs_and_finite_flag_remain_required(self):
        for event, old, new in [('motion_lifetime', 'mutation_after=9', 'mutation_after=10'), ('object_context', 'scoped=1', 'scoped=0'), ('motion_input', 'proofs=31', 'proofs=23'), ('motion_input', 'blockers=00000000', 'blockers=00000040'), ('motion_input', 'vertex_finite_verified=1', 'vertex_finite_verified=0')]:
            self.assertEqual(candidates(AUDIT.analyze(modify(trace(), event, old, new))), 0)

    def test_duplicate_records_or_scalar_keys_poison_without_overwrite(self):
        for event in ('motion_geometry', 'motion_input', 'motion_lifetime', 'object_context', 'draw_result', 'frame_end'):
            lines = trace()
            index = next(i for i, line in enumerate(lines) if line.startswith(event + ' '))
            lines.insert(index, lines[index])
            self.assertEqual(candidates(AUDIT.analyze(lines)), 0)
        lines = modify(trace(), 'motion_geometry', 'finite_state=1', 'finite_state=2 finite_state=1')
        self.assertEqual(candidates(AUDIT.analyze(lines)), 0)

    def test_out_of_frame_and_wrong_draw_coordinates_never_attach(self):
        for action in ('before', 'after', 'coordinate'):
            lines = trace()
            index = next(i for i, line in enumerate(lines) if line.startswith('motion_geometry '))
            value = lines.pop(index)
            if action == 'coordinate':
                lines.insert(index, value.replace('index=1', 'index=2'))
            elif action == 'before':
                lines.insert(0, value)
            else:
                lines.append(value)
            self.assertEqual(candidates(AUDIT.analyze(lines)), 0)

    def test_complete_successful_count_matched_frames_only(self):
        for lines in (trace()[:-1], modify(trace(), 'frame_end', 'draws=1', 'draws=2'), modify(trace(), 'frame_end', 'present=00000000', 'present=8876086c'), modify(trace(), 'draw_result', 'result=00000000', 'result=8876086c')):
            self.assertEqual(AUDIT.analyze(lines)['totals'], {})

    def test_devices_reusing_frame_index_remain_separate(self):
        report = AUDIT.analyze(trace(device=1) + trace(device=2))
        self.assertEqual(candidates(report), 2)
        self.assertEqual(report['totals']['complete_successful_frames'], 2)

    def test_latest_cumulative_metric_not_sum_and_generation_not_reset(self):
        report = AUDIT.analyze([metric(uploads=10), metric(frame=11, uploads=12), metric(frame=12, generation=2, uploads=20)])
        owner = report['owner_metrics']
        self.assertEqual(owner['latest_per_owner']['1']['values']['uploads'], 20)
        self.assertEqual(owner['latest_observed_per_generation']['1:1']['values']['uploads'], 12)
        self.assertEqual(owner['latest_observed_per_generation']['1:2']['values']['uploads'], 20)
        self.assertNotIn('total_uploads', owner)

    def test_repeated_reason_and_first_refusal_batches_are_snapshots(self):
        refusal = 'finite_upload_first_refusal device=1 frame={frame} phase=present reason=9 name=native_contract type=6 format=100 pool=1 size=4096 usage=00000008 lock_flags=00000000'
        reason = 'finite_upload_reason device=1 frame={frame} phase=present reason=9 name=native_contract count={count}'
        report = AUDIT.analyze([metric(), refusal.format(frame=10), reason.format(frame=10, count=3), metric(frame=11), refusal.format(frame=11), reason.format(frame=11, count=5)])
        latest = report['owner_metrics']['latest_per_owner']['1']
        self.assertEqual(latest['emitted_reasons']['9']['count'], 5)
        self.assertEqual(latest['first_refusal']['size'], '4096')
        self.assertFalse(latest['reasons_listing_exhaustive'])

    def test_optional_timing_missing_is_unknown_and_scans_not_whole_cost(self):
        latest = AUDIT.analyze([metric(optional=False)])['owner_metrics']['latest_per_owner']['1']
        self.assertIsNone(latest['values']['qualifier_ticks'])
        self.assertIsNone(latest['values']['query_ticks'])
        report = AUDIT.analyze([metric().replace('scans=10', 'scans=0').replace('scan_ticks=100', 'scan_ticks=0')])
        self.assertEqual(report['owner_metrics']['latest_per_owner']['1']['values']['qualifier_ticks'], 200)
        self.assertIn('not total observer overhead', report['owner_metrics']['timing_scope'])

    def test_last_malformed_metric_is_unknown_not_previous_valid_sample(self):
        report = AUDIT.analyze([metric(), metric(frame=11).replace('uploads=10', 'uploads=bad')])
        latest = report['owner_metrics']['latest_per_owner']['1']
        self.assertFalse(latest['known'])
        self.assertIsNone(latest['values'])

    def test_metric_scope_and_duplicate_reason_poison_batch(self):
        reason = 'finite_upload_reason device=1 frame=10 phase=present reason=9 name=native_contract count=3'
        for lines in ([metric(), reason, reason], [metric(), reason.replace('frame=10', 'frame=99')]):
            self.assertFalse(AUDIT.analyze(lines)['owner_metrics']['latest_per_owner']['1']['known'])

    def test_owner_unavailable_is_not_generation_zero_stability(self):
        report = AUDIT.analyze([metric(generation=0).replace('active=1', 'active=0')])
        self.assertFalse(report['owner_metrics']['latest_per_owner']['1']['known'])
        self.assertIn('1:unavailable', report['owner_metrics']['latest_observed_per_generation'])

    def test_counter_regression_reported_across_generations(self):
        report = AUDIT.analyze([metric(uploads=10), metric(frame=11, generation=2, uploads=1)])
        self.assertEqual(report['owner_metrics']['anomalies'][0]['counter'], 'uploads')

    def test_limits_and_multiple_sessions(self):
        for option in ('max_frames', 'max_draws', 'max_records'):
            with self.assertRaises(ValueError):
                AUDIT.analyze(trace(), **{option: 0})
        with self.assertRaises(ValueError):
            AUDIT.analyze(['x3-modern-renderer schema=2', 'x3-modern-renderer schema=2'])

    def test_snapshot_hash_optional_and_partial_line_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture.log'
            payload = ('\n'.join(trace()) + '\n').encode()
            path.write_bytes(payload)
            report = AUDIT.analyze_snapshot(path, hashlib.sha256(payload).hexdigest())
            self.assertEqual(candidates(report), 1)
            self.assertEqual(candidates(AUDIT.analyze_snapshot(path)), 1)
            with self.assertRaises(ValueError):
                AUDIT.analyze_snapshot(path, '0' * 64)
            path.write_bytes(payload[:-1])
            with self.assertRaises(ValueError):
                AUDIT.analyze_snapshot(path)


    def test_replaced_snapshot_inode_rejected_even_same_size_mtime(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture.log'
            path.write_text('\n'.join(trace()) + '\n')
            before = path.stat()
            replaced = SimpleNamespace(st_dev=before.st_dev, st_ino=before.st_ino + 1,
                                       st_size=before.st_size, st_mtime_ns=before.st_mtime_ns)
            with patch.object(Path, 'stat', side_effect=[before, replaced]):
                with self.assertRaises(ValueError):
                    AUDIT.analyze_snapshot(path)

    def test_missing_capture_invalidates_old_complete_output(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'missing.log'
            output = Path(directory) / 'output.json'
            output.write_text('{"analysis_complete": true}')
            result = subprocess.run([sys.executable, str(Path(AUDIT.__file__)), str(path), '--output', str(output)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(json.loads(output.read_text()), {'analysis_complete': False})

    def test_cli_hardlink_output_cannot_truncate_raw_input(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture.log'
            alias = Path(directory) / 'output.json'
            payload = ('\n'.join(trace()) + '\n').encode()
            path.write_bytes(payload)
            os.link(path, alias)
            result = subprocess.run([sys.executable, str(Path(AUDIT.__file__)), str(path), '--output', str(alias)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(path.read_bytes(), payload)


if __name__ == '__main__':
    unittest.main()
