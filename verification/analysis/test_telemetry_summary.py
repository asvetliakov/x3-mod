"""CPU span aggregation and malformed diagnostic tests."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/analysis'))
from summarize_telemetry import summarize

class TelemetryTests(unittest.TestCase):
    def metric(self,**overrides):
        f=dict(device='1',name='present_normal',count='2',failures='0',total_us='10',min_us='3',max_us='7',bytes='0',buckets='2,0,0,0,0,0')
        f.update(overrides)
        return 'telemetry_metric '+' '.join(k+'='+v for k,v in f.items())
    def test_windows_merge_but_devices_and_capture_stay_separate(self):
        r=summarize('\n'.join([self.metric(),self.metric(),self.metric(device='2'),self.metric(name='present_capture')]))
        self.assertEqual(len(r['metrics']),3)
        m=r['metrics']['1:present_normal'];self.assertEqual(m['count'],4);self.assertEqual(m['mean_us'],5)
        self.assertEqual(m['buckets'],[4,0,0,0,0,0]);self.assertEqual(m['windows'],2)
    def test_invalid_counts_and_nan_rejected(self):
        for change in ({'buckets':'1,0,0,0,0,0'},{'total_us':'nan'},{'failures':'3'},{'min_us':'8'}):
            r=summarize(self.metric(**change));self.assertEqual(len(r['rejected']),1);self.assertFalse(r['metrics'])
    def test_qpc_span_and_marker(self):
        r=summarize('telemetry_start qpc_frequency=1000 qpc=500 anchor=proxy_initialize\ntelemetry_span name=backend_load qpc_begin=500 qpc_end=525 thread=3 success=1\ntelemetry_marker device=1 frame=2 coverage=present_poll')
        self.assertEqual(r['spans'][0]['duration_us'],25000);self.assertEqual(len(r['markers']),1)
    def test_span_without_clock_and_reversed(self):
        r=summarize('telemetry_span name=load qpc_begin=1 qpc_end=3\ntelemetry_span name=load qpc_begin=5 qpc_end=2')
        self.assertNotIn('duration_us',r['spans'][0]);self.assertEqual(len(r['rejected']),1)
    def test_loading_deltas_may_straddle_counts(self):
        r=summarize('loading_trace frequency=1000 coverage_begin=10\nloading_metric op=ReadFile qpc=20 count=0 failures=0 pending=0 ambiguous=0 bytes=10 inclusive_ticks=2 exclusive_ticks=1 max_ticks=2 wrapper_tail_ticks=1\nloading_metric op=ReadFile qpc=30 count=1 failures=0 pending=0 ambiguous=0 bytes=0 inclusive_ticks=0 exclusive_ticks=0 max_ticks=0 wrapper_tail_ticks=0')
        m=r['loading_metrics']['ReadFile'];self.assertEqual(m['count'],1);self.assertEqual(m['bytes'],10)
        self.assertEqual(m['inclusive_us'],2000);self.assertFalse(r['rejected'])

    def frame(self,**overrides):
        f=dict(device='1',frame='60',latched='1',filled='1',draws='300',routed='120',matched='100',depth_routed='120',jittered='250',
               rt_mode='perdraw',timing='cpu_qpc',set_rt='480',lazy_flushes='0',jitter_writes='500',readbacks='0',
               gate_us='900.5',route_draw_us='1200.0',set_rt_us='400.0',lazy_flush_us='0.0',jitter_us='300.0',fill_us='50.0',
               taa_run_us='700.0',taa_capture_us='100.0',taa_copy_color_us='150.0',taa_copy_depth_us='100.0',taa_draw_us='250.0',
               taa_apply_us='80.0',taa_copy_back_us='60.0',readback_us='0.0')
        f.update(overrides)
        return 'motion_output_frame '+' '.join(k+'='+v for k,v in f.items())
    def test_route_metrics_and_frame_costs_are_grouped(self):
        from summarize_telemetry import route_report
        trace='\n'.join([self.metric(name='route_draw',count='2',total_us='10',min_us='3',max_us='7'),
                         self.metric(name='taa_run',count='1',total_us='700',min_us='700',max_us='700',buckets='0,0,1,0,0,0'),
                         self.frame(),self.frame(frame='120',route_draw_us='800.0'),
                         self.frame(frame='121',readbacks='4',readback_us='9000.0',rt_mode='lazy',lazy_flushes='40')])
        r=summarize(trace)
        self.assertEqual(sorted(r['route_costs']['metrics']),['1:route_draw','1:taa_run'])
        normal=r['route_costs']['frames']['1:perdraw:normal'];capture=r['route_costs']['frames']['1:lazy:capture']
        self.assertEqual(normal['frames'],2);self.assertEqual(normal['mean_us']['route_draw_us'],1000);self.assertEqual(normal['max_us']['route_draw_us'],1200)
        self.assertEqual(normal['counts']['set_rt'],960);self.assertTrue(capture['capture']);self.assertEqual(capture['counts']['lazy_flushes'],40)
        self.assertFalse(r['rejected'])
        text='\n'.join(route_report(r))
        self.assertIn('1:route_draw',text);self.assertIn('1:perdraw:normal frames=2',text);self.assertIn('readback=9000.0',text)
    def test_frame_line_without_cost_fields_is_ignored_and_bad_cost_rejected(self):
        r=summarize('motion_output_frame device=1 frame=60 draws=3 routed=2')
        self.assertFalse(r['route_costs']['frames']);self.assertFalse(r['rejected'])
        r=summarize(self.frame(fill_us='-1'))
        self.assertEqual(len(r['rejected']),1);self.assertFalse(r['route_costs']['frames'])
        self.assertEqual(summarize('').get('route_costs'),dict(metrics={},frames={}))
        from summarize_telemetry import route_report
        self.assertEqual(route_report(summarize('')),[])

if __name__=='__main__': unittest.main()
