"""Synthetic report windows and active-prefix tests; no game content."""
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'/'analysis'))
from analyze_loading_phases import analyze,read_prefix
HEADER='telemetry_start qpc_frequency=1000 qpc=10000 anchor=proxy_initialize\nloading_trace coverage_begin=10010 frequency=1000 hooks=14\n'
def metric(op,stamp,ticks=100,count=1):
    return f'loading_metric op={op} qpc={stamp} count={count} failures=0 pending=0 ambiguous=0 bytes=4 inclusive_ticks={ticks} exclusive_ticks={ticks} max_ticks={ticks} wrapper_tail_ticks=1\n'
class LoadingPhases(unittest.TestCase):
    def test_report_boundaries_without_phase_guesses(self):
        text=HEADER+'telemetry_summary device=0 frame=0 qpc=11000\n'+metric('ReadFile',11002)
        text+='telemetry_summary device=0 frame=0 qpc=13000\n'+metric('inflate',13005)
        result=analyze(text)
        self.assertEqual(result['windows'][1]['begin_qpc'],11002)
        self.assertAlmostEqual(result['windows'][1]['report_interval_seconds'],2.003)
        self.assertEqual(result['markers'],[])
        self.assertFalse(result['completed_as_declared'])
    def test_overlapping_totals_are_not_added(self):
        text=HEADER+'telemetry_summary device=0 frame=0 qpc=11000\n'+metric('Effect',11001,800)+metric('ReadFile',11001,600)
        result=analyze(text)
        self.assertNotIn('attributed_seconds',result)
        self.assertEqual(result['windows'][0]['largest_metric'],'Effect')
    def test_unchanged_frame_requires_two_observations(self):
        text=HEADER+'telemetry_summary device=1 frame=2 qpc=11000\ntelemetry_summary device=1 frame=2 qpc=15000\ntelemetry_summary device=1 frame=3 qpc=19000\n'
        result=analyze(text);self.assertEqual(len(result['unchanged_reported_frame_runs']),1)
        self.assertEqual(result['unchanged_reported_frame_runs'][0]['observed_duration_seconds'],4)
    def test_prefix_drops_partial_line(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'trace.log';path.write_bytes(b'complete\nunfinished')
            raw,info=read_prefix(path);self.assertEqual(raw,b'complete\n');self.assertEqual(info['dropped_tail_bytes'],10)
    def test_orphan_delta_rejected(self):
        result=analyze(HEADER+metric('ReadFile',11000));self.assertTrue(result['rejected'])
    def test_zero_count_split_delta_retained(self):
        result=analyze(HEADER+'telemetry_summary device=0 frame=0 qpc=11000\n'+metric('ReadFile',11001,count=0))
        self.assertEqual(result['windows'][0]['metrics']['ReadFile']['bytes'],4)
if __name__=='__main__':unittest.main()
