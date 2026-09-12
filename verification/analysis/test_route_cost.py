"""Synthetic tests for analyze_route_cost.py; no game content and no binaries.

The fixture log is a two-window sampling-profiler session with one capture
burst: window A has the route inactive, window B has it routed with a known
per-draw cost, so the module split, the ms/frame conversion, the burst-window
derivation and the gate regression all have exact expected values.
"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
from analyze_route_cost import (FrameClock, analyze, capture_windows, fit, parse,
                                routed_span, savings, scan)

FREQ = 1000  # 1 kHz QPC: one tick is a millisecond, so the seconds read plainly


def header():
    return (f'telemetry_start schema=1 qpc_frequency={FREQ} qpc=10000 anchor=proxy_initialize cpu_only=1\n'
            'constant device=1 index=0 payload=must_be_skipped\n'
            f'profile_start schema=1 enabled=1 qpc=10000 frequency={FREQ} interval_us=2000 report_s=5 '
            'sampler_tid=9 init_tid=1 main_base=0x00400000 proxy_base=0x10000000 query_thread=1 '
            'chain_limit=32 scan_dwords=1024 threads_max=32 modules_max=128 tables=65536,65536,16384\n'
            'profile_module index=0 name=X3AP.exe kind=x3ap base=0x00400000 size=0x200000 '
            'text_rva=0x1000 text_size=0x150000 ranges=1 pinned=1\n'
            'profile_module index=1 name=ntdll.dll kind=ntdll base=0x7bc00000 size=0x100000 '
            'text_rva=0x1000 text_size=0x80000 ranges=1 pinned=1\n'
            'profile_module index=2 name=d3d9.dll kind=proxy base=0x10000000 size=0x300000 '
            'text_rva=0x1000 text_size=0x200000 ranges=1 pinned=1\n'
            'profile_module index=3 name=wined3d.dll kind=wine base=0x78000000 size=0x300000 '
            'text_rva=0x1000 text_size=0x200000 ranges=1 pinned=1\n')


def block(end_qpc, elapsed_us, x3ap, ntdll, proxy, wine, leaves=(), pairs=()):
    samples = x3ap + ntdll + proxy + wine
    out = (f'profile_report scope=delta qpc={end_qpc} frequency={FREQ} since_start_us=0 '
           f'elapsed_us={elapsed_us} interval_us=2000 samples={samples} threads=1 threads_unsampled=0 '
           'modules=4 ticks=1 tick_us_mean=1.0 tick_us_max=1.0 refresh_us=0.0 dropped=0 table_used=1,1,1\n'
           f'profile_thread scope=delta qpc={end_qpc} slot=0 tid=1 samples={samples} total={samples} '
           f'leaf_x3ap={x3ap} leaf_ntdll={ntdll} leaf_wine={wine} leaf_d3dx=0 leaf_zlib=0 leaf_xml=0 '
           f'leaf_proxy={proxy} leaf_other=0 stack_unknown=0 suspend_failures=0 context_failures=0 '
           'start_module=0 start_rva=0x1000 retired=0\n')
    for module, rva, count in leaves:
        out += (f'profile_leaf scope=delta qpc={end_qpc} slot=0 module={module} name=m{module} '
                f'rva={rva} count={count}\n')
    for rva, caller, count in pairs:
        out += f'profile_pair scope=delta qpc={end_qpc} slot=0 rva={rva} caller={caller} count={count}\n'
    return out


def summary(qpc, frame):
    return (f'telemetry_summary device=1 frame={frame} reason=interval qpc={qpc} since_start_us=0 '
            'interval_us=0 position_suppressed=0 cursor_changes_suppressed=0\n')


def frame_line(frame, draws, routed, gate_us, route_draw_us, set_rt_us, readbacks=0):
    return (f'motion_output_frame device=1 frame={frame} draws={draws} routed={routed} '
            f'set_rt={4 * routed} lazy_flushes=0 jitter_writes={2 * routed} readbacks={readbacks} '
            f'gate_us={gate_us} route_draw_us={route_draw_us} set_rt_us={set_rt_us} lazy_flush_us=0.0 '
            'jitter_us=0.0 fill_us=10.0 taa_run_us=100.0 rt_mode=perdraw timing=cpu_qpc\n')


# Window A: 10 s (qpc 10000..20000), route off. Window B: 10 s, route on.
# Frames advance 10 per second throughout, so a 10 s window holds 100 frames.
LOG = (header()
       + ''.join(summary(10000 + 1000 * i, 10 * i) for i in range(31))
       + block(20000, 10_000_000, 80, 10, 5, 5,
               leaves=((1, '0x4ce3c', 10), (2, '0x2000', 5), (3, '0x9000', 5)))
       + block(30000, 10_000_000, 50, 40, 8, 2,
               leaves=((1, '0x4cf1c', 30), (1, '0x4ce3c', 10), (2, '0x2000', 8)),
               pairs=(('0x30000', '0xb0000', 7),))
       # A cumulative block must be ignored entirely.
       + block(30000, 10_000_000, 999, 999, 999, 999)
          .replace('scope=delta', 'scope=cumulative')
       # gate_us = 17.0 per routed draw + 0.3 per rejected draw, exactly.
       + frame_line(100, 200, 100, 1730.0, 700.0, 340.0)
       + frame_line(160, 300, 150, 2595.0, 1050.0, 510.0)
       + frame_line(220, 300, 150, 2595.0, 1050.0, 510.0, readbacks=2)
       + frame_line(221, 300, 150, 9999.0, 1050.0, 510.0, readbacks=2)
       + frame_line(280, 250, 200, 3415.0, 1400.0, 680.0)
       + frame_line(0, 200, 0, 50.0, 0.0, 0.0))


class RouteCostTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.log = Path(self.directory.name) / 'session.log'
        self.log.write_text(LOG)

    def tearDown(self):
        self.directory.cleanup()

    def test_scan_keeps_only_sparse_lines(self):
        kept = scan(self.log)
        self.assertTrue(all(not line.startswith('constant ') for line in kept))
        self.assertEqual(sum(line.startswith('profile_report ') for line in kept), 3)
        self.assertEqual(sum(line.startswith('motion_output_frame ') for line in kept), 6)

    def test_frame_clock_interpolates(self):
        parsed = parse(scan(self.log))
        clock = FrameClock(parsed)
        self.assertAlmostEqual(clock.frames_between(0.0, 10.0), 100.0, places=3)
        self.assertAlmostEqual(clock.time_of(100), 10.0, places=3)
        self.assertAlmostEqual(clock.time_of(105), 10.5, places=3)

    def test_capture_window_and_routed_span(self):
        parsed = parse(scan(self.log))
        clock = FrameClock(parsed)
        self.assertEqual(capture_windows(parsed, clock, pad=1.0),
                         [('burst1', 21.0, 23.1)])
        self.assertEqual(routed_span(parsed, clock), (10.0, 28.0))

    def test_module_split_is_exact_and_per_frame_scaled(self):
        result = analyze(self.log, extra=[('a', 0.0, 9.0), ('b', 11.0, 30.0)], pad=1.0)
        windows = {w['label']: w for w in result['windows']}
        a, b = windows['a'], windows['b']
        # Window A: 100 samples over 10 s and 100 frames -> 100 ms/frame engine time.
        self.assertEqual(a['samples'], 100)
        self.assertAlmostEqual(a['frames'], 100.0, places=3)
        self.assertAlmostEqual(a['engine_ms_per_frame'], 100.0, places=3)
        self.assertAlmostEqual(a['module_split']['x3ap']['share'], 0.80, places=5)
        self.assertAlmostEqual(a['module_split']['ntdll']['ms_per_frame'], 10.0, places=3)
        # Window B: ntdll is 40 % of the thread, so 40 ms of a 100 ms frame.
        self.assertAlmostEqual(b['module_split']['ntdll']['ms_per_frame'], 40.0, places=3)
        self.assertEqual(b['ntdll']['exact'], 40)
        self.assertEqual(b['ntdll']['listed'], 40)
        self.assertAlmostEqual(b['ntdll']['coverage'], 1.0, places=5)
        # The proxy's leaf rows are under-listed, and coverage must say so.
        self.assertEqual(b['proxy']['exact'], 8)
        self.assertEqual(b['proxy']['listed'], 8)
        self.assertEqual(b['x3ap_pairs'][0], dict(rva='0x30000', caller='0xb0000', count=7))

    def test_cumulative_blocks_are_ignored(self):
        result = analyze(self.log, extra=[('b', 20.0, 30.0)], pad=1.0)
        window = next(w for w in result['windows'] if w['label'] == 'b')
        self.assertEqual(window['samples'], 100)      # not 100 + 3996

    def test_gate_regression_separates_routed_from_rejected(self):
        result = analyze(self.log, extra=[], pad=1.0)
        t = result['telemetry']
        # The fixture's gate_us is exactly 17.0 per routed plus 0.3 per rejected
        # draw over three non-capture frames; the two capture frames (one with a
        # wild gate_us) must be excluded.
        self.assertEqual(t['records'], 3)
        self.assertAlmostEqual(t['gate']['routed'], 17.0, places=3)
        self.assertAlmostEqual(t['gate']['rejected'], 0.3, places=3)
        self.assertAlmostEqual(t['route_draw_per_routed'], 7.0, places=3)
        self.assertAlmostEqual(t['set_rt_per_call'], 0.85, places=3)
        self.assertAlmostEqual(t['exclusive_route_us'], 3640.0, places=1)
        # stamps = 2 * (2*draws + 2*routed + set_rt + jitter_writes + fill + taa phases)
        self.assertAlmostEqual(t['per_frame']['stamps'], 3416.0, places=1)

    def test_fit_recovers_known_coefficients(self):
        rows = [dict(a=2.0, b=1.0, y=2 * 3.0 + 1 * 5.0),
                dict(a=1.0, b=4.0, y=1 * 3.0 + 4 * 5.0),
                dict(a=3.0, b=2.0, y=3 * 3.0 + 2 * 5.0)]
        got = fit(rows, ('a', 'b'), 'y')
        self.assertAlmostEqual(got['a'], 3.0, places=6)
        self.assertAlmostEqual(got['b'], 5.0, places=6)

    def test_fit_refuses_a_singular_system(self):
        rows = [dict(a=1.0, b=2.0, y=1.0), dict(a=2.0, b=4.0, y=2.0)]
        self.assertIsNone(fit(rows, ('a', 'b'), 'y'))

    def test_savings_uses_named_syscalls(self):
        window = dict(
            ntdll=dict(rows=[dict(rva='0x4cf1c', function='ZwReadVirtualMemory+0xc', ms_per_frame=4.4),
                             dict(rva='0x4ce3c', function='ZwQueryPerformanceCounter+0xc', ms_per_frame=1.5),
                             dict(rva='0x4cbac', function='ZwWriteFile+0xc', ms_per_frame=0.3)]),
            telemetry=dict(per_frame=dict(stamps=5000, set_rt_us=700.0)))
        got = savings(window)
        self.assertAlmostEqual(got['read_process_memory_ms'], 4.4, places=3)
        self.assertAlmostEqual(got['frame_constant_reads_ms'], 2.0, places=3)
        self.assertAlmostEqual(got['telemetry_stamps_ms'], 1.5, places=3)
        self.assertAlmostEqual(got['us_per_qpc'], 0.3, places=3)
        self.assertAlmostEqual(got['set_rt_ms'], 0.7, places=3)

    def test_savings_tolerates_an_unsymbolized_window(self):
        got = savings(dict(ntdll=dict(rows=[dict(rva='0x4cf1c', ms_per_frame=4.4)]), telemetry=None))
        self.assertEqual(got['read_process_memory_ms'], 0.0)
        self.assertIsNone(got['us_per_qpc'])

    def test_json_is_serializable_and_states_its_limits(self):
        result = analyze(self.log, extra=[('b', 20.0, 30.0)], pad=1.0)
        json.loads(json.dumps(result))
        self.assertTrue(any('X3AP.exe addresses only' in line for line in result['limits']))
        self.assertEqual(result['engine_thread'], dict(slot='0', tid='1'))
        self.assertIsNone(result['symbolization']['proxy'])


if __name__ == '__main__':
    unittest.main()
