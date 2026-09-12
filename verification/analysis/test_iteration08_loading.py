"""Synthetic loading-gap tests for the iteration-08 analyzer; no game content."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
from analyze_iteration08_loading import analyze, combine, gaps, parse, repeats, trim

HEADER = ('telemetry_start schema=1 qpc_frequency=1000 qpc=10000 anchor=proxy_initialize\n'
          'loading_trace coverage_begin=10010 frequency=1000 hooks=16 module=main\n'
          'mesh_hook table=0 installed=1 owned_slots=3\n')


def summary(device, frame, stamp):
    return f'telemetry_summary device={device} frame={frame} reason=interval qpc={stamp}\n'


def frame_metric(count, total_us, maximum_us, minimum_us=0.0):
    return (f'telemetry_metric device=1 name=frame_normal count={count} failures=0 '
            f'total_us={total_us} min_us={minimum_us} max_us={maximum_us} bytes=0 '
            'buckets=0,0,0,0,0,1\n')


def metric(op, stamp, ticks=100, count=1, byte_count=4):
    return (f'loading_metric op={op} qpc={stamp} count={count} failures=0 pending=0 '
            f'ambiguous=0 bytes={byte_count} inclusive_ticks={ticks} exclusive_ticks={ticks} '
            f'max_ticks={ticks} wrapper_tail_ticks=1\n')


def write(text):
    directory = tempfile.mkdtemp()
    path = Path(directory) / 'trace.log'
    path.write_text(text)
    return path


class Iteration08Loading(unittest.TestCase):
    def test_gap_is_bounded_by_the_surrounding_reports(self):
        # Reports follow proxy activity, so the closing Present lies between them.
        text = (HEADER + summary(1, 1, 30000)
                + summary(1, 4, 41000) + frame_metric(3, 30500000.0, 30000000.0))
        found = gaps(parse(text.splitlines())[2], 2.0)
        self.assertEqual(len(found), 1)
        self.assertAlmostEqual(found[0]['gap_seconds'], 30.0)
        # 41000 ticks is 31 s after the clock start, less the 0.5 s of other intervals.
        self.assertAlmostEqual(found[0]['end_latest_seconds'], 30.5)
        self.assertAlmostEqual(found[0]['end_earliest_seconds'], 20.0)
        self.assertAlmostEqual(found[0]['start_earliest_seconds'], -10.0)
        self.assertEqual(found[0]['window_frames'], 3)

    def test_short_gaps_are_excluded_by_the_threshold(self):
        text = HEADER + summary(1, 1, 11000) + summary(1, 2, 12000) + frame_metric(1, 900.0, 900.0)
        self.assertEqual(gaps(parse(text.splitlines())[2], 2.0), [])

    def test_accounting_keeps_the_unexplained_remainder(self):
        text = (HEADER + summary(0, 0, 15000) + metric('CreateFileA', 15000, ticks=1000)
                + summary(1, 1, 30000)
                + summary(0, 0, 38000) + metric('inflate', 38000, ticks=4000, count=200)
                + summary(1, 2, 41000) + frame_metric(1, 10000000.0, 10000000.0))
        path = write(text)
        result = analyze(path, threshold=2.0)
        self.assertEqual(len(result['gap_accounting']), 1)
        accounting = result['gap_accounting'][0]
        self.assertAlmostEqual(accounting['gap_seconds'], 10.0)
        # Only the 4.0 s inflate delta lands inside the gap; the rest is unattributed.
        self.assertAlmostEqual(accounting['instrumented_exclusive_seconds'], 4.0)
        self.assertAlmostEqual(accounting['unexplained_seconds'], 6.0)
        self.assertEqual(accounting['operations']['inflate']['count'], 200)
        self.assertAlmostEqual(accounting['operations']['inflate']['mean_ms'], 20.0)

    def test_report_stall_is_detected_inside_the_gap(self):
        text = (HEADER + summary(0, 0, 10500) + summary(1, 1, 11000)
                + summary(0, 0, 30000) + metric('gzread', 30000, ticks=50, count=1000)
                + summary(1, 2, 41000) + frame_metric(1, 30000000.0, 30000000.0))
        accounting = analyze(write(text), threshold=2.0)['gap_accounting'][0]
        self.assertEqual(len(accounting['report_stalls']), 1)
        stall = accounting['report_stalls'][0]
        self.assertAlmostEqual(stall['interval_seconds'], 19.5)
        self.assertEqual(stall['top_operations'][0]['op'], 'gzread')
        self.assertAlmostEqual(stall['top_operations'][0]['mean_us'], 50.0)

    def test_identical_work_vectors_match_across_runs(self):
        text = (HEADER + summary(0, 0, 10500) + summary(1, 1, 11000)
                + summary(0, 0, 30000) + metric('inflate', 30000, ticks=4000, count=200)
                + summary(1, 2, 41000) + frame_metric(1, 30000000.0, 30000000.0))
        # The same counts with different durations must still match.
        slower = text.replace('inclusive_ticks=4000 exclusive_ticks=4000',
                              'inclusive_ticks=9000 exclusive_ticks=9000')
        runs = {'a': trim(analyze(write(text), 2.0)), 'b': trim(analyze(write(slower), 2.0))}
        merged = combine(runs)
        self.assertEqual(len(merged['identical_gap_work_vectors']), 1)
        grouped = next(iter(merged['identical_gap_work_vectors'].values()))
        self.assertEqual(sorted(item['run'] for item in grouped), ['a', 'b'])
        self.assertNotAlmostEqual(grouped[0]['instrumented_exclusive_seconds'],
                                  grouped[1]['instrumented_exclusive_seconds'])
        json.dumps(merged)

    def test_trivial_windows_are_not_reported_as_repeated_work(self):
        windows = [dict(begin_seconds=float(index), report_seconds=index + 1.0,
                        metrics={'ReadFile': dict(count=1, failures=0, pending=0, ambiguous=0,
                                                  bytes=8, inclusive_seconds=0.0,
                                                  exclusive_seconds=0.0, max_seconds=0.0,
                                                  wrapper_tail_seconds=0.0)})
                   for index in range(20)]
        self.assertEqual(repeats(windows), [])

    def test_substantial_blocks_repeat(self):
        def window(index):
            return dict(begin_seconds=float(index), report_seconds=index + 1.0,
                        metrics={'ID3DXMesh::GenerateAdjacency': dict(
                            count=100, failures=0, pending=0, ambiguous=0, bytes=0,
                            inclusive_seconds=0.5, exclusive_seconds=0.5, max_seconds=0.1,
                            wrapper_tail_seconds=0.0)})
        found = repeats([window(index) for index in range(12)])
        self.assertTrue(found)
        self.assertGreaterEqual(len(found[0]['occurrences']), 2)
        self.assertEqual(found[0]['occurrences'][0]['adjacency_calls'], 500)

    def test_orphan_loading_delta_is_rejected(self):
        with self.assertRaises(ValueError):
            parse((HEADER + metric('ReadFile', 11000)).splitlines())

    def test_missing_clock_is_rejected(self):
        with self.assertRaises(ValueError):
            parse(['telemetry_summary device=0 frame=0 qpc=1'])

    def test_source_digest_matches_the_file(self):
        import hashlib
        text = HEADER + summary(1, 1, 11000)
        path = write(text)
        result = analyze(path)
        self.assertEqual(result['source']['sha256'], hashlib.sha256(text.encode()).hexdigest())
        self.assertEqual(result['source']['bytes'], len(text.encode()))


if __name__ == '__main__':
    unittest.main()
