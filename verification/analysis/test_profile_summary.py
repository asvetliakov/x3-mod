"""Synthetic sampling-profiler log tests for summarize_profile.py; no game content."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
from summarize_profile import main_module_rvas, parse, scan, summarize

FREQ = 1000  # 1 kHz QPC keeps the synthetic seconds readable


def report(qpc, elapsed_us, samples, ticks, mean, maximum, dropped=0):
    return (f'profile_report scope=delta qpc={qpc} frequency={FREQ} since_start_us=0 elapsed_us={elapsed_us} interval_us=2000 '
            f'samples={samples} threads=2 threads_unsampled=0 modules=3 ticks={ticks} tick_us_mean={mean} tick_us_max={maximum} '
            f'refresh_us=100.0 dropped={dropped} table_used=1,1,1\n')


def thread(slot, tid, samples, x3ap, ntdll):
    other = samples - x3ap - ntdll
    return (f'profile_thread scope=delta qpc=0 slot={slot} tid={tid} samples={samples} total={samples} leaf_x3ap={x3ap} leaf_ntdll={ntdll} '
            f'leaf_wine={other} leaf_d3dx=0 leaf_zlib=0 leaf_xml=0 leaf_proxy=0 leaf_other=0 stack_unknown=0 suspend_failures=0 '
            f'context_failures=0 start_module=0 start_rva=0x1000 retired=0\n')


HEADER = (f'telemetry_start schema=1 qpc_frequency={FREQ} qpc=10000 anchor=proxy_initialize cpu_only=1\n'
          'capture_event frame=1 payload=ignored\n'
          f'profile_start schema=1 enabled=1 qpc=10010 frequency={FREQ} interval_us=2000 report_s=5 sampler_tid=9 init_tid=1 '
          'main_base=0x00400000 proxy_base=0x10000000 query_thread=1 chain_limit=32 scan_dwords=1024 threads_max=32 modules_max=128 tables=65536,65536,16384\n'
          'profile_module index=0 name=X3AP.exe kind=x3ap base=0x00400000 size=0x200000 text_rva=0x1000 text_size=0x150000 ranges=1\n'
          'profile_module index=1 name=ntdll.dll kind=ntdll base=0x7bc00000 size=0x100000 text_rva=0x1000 text_size=0x80000 ranges=1\n'
          'profile_module index=2 name=d3d9.dll kind=proxy base=0x10000000 size=0x300000 text_rva=0x1000 text_size=0x200000 ranges=1\n')

# Block 1 ends at 15.0 s (qpc 25000) and covers 5 s; block 2 ends at 20.0 s; the cumulative block must be ignored.
BLOCK1 = (report(25000, 5_000_000, 300, 250, 40.5, 120.0) + thread(0, 1, 200, 150, 50) + thread(1, 2, 100, 0, 100)
          + 'profile_leaf scope=delta qpc=25000 slot=0 module=0 name=X3AP.exe rva=0xbb480 count=120\n'
          + 'profile_leaf scope=delta qpc=25000 slot=1 module=1 name=ntdll.dll rva=0x2000 count=100\n'
          + 'profile_frame scope=delta qpc=25000 slot=0 rva=0xbb480 count=150\n'
          + 'profile_frame scope=delta qpc=25000 slot=1 rva=0xe8e20 count=100\n'
          + 'profile_pair scope=delta qpc=25000 slot=1 rva=0xe8e20 caller=0xe7600 count=90\n'
          + 'profile_report_end qpc=25010 report_us=800.0\n')
BLOCK2 = (report(30000, 5_000_000, 100, 200, 20.0, 60.0, dropped=3) + thread(0, 1, 100, 100, 0)
          + 'profile_leaf scope=delta qpc=30000 slot=0 module=0 name=X3AP.exe rva=0xbb480 count=80\n'
          + 'profile_frame scope=delta qpc=30000 slot=0 rva=0xbb480 count=100\n'
          + 'profile_report_end qpc=30005 report_us=500.0\n')
CUMULATIVE = ('profile_report scope=cumulative qpc=30005 frequency=1000 since_start_us=0 samples=400 ticks=450 tick_us_mean=1 tick_us_max=1 '
              'refresh_us=1 report_us=1 dropped=3 cumulative_dropped=0 table_used=1,1,1 final=1\n'
              + 'profile_thread scope=cumulative qpc=30005 slot=0 tid=1 samples=300 total=300 leaf_x3ap=250 leaf_ntdll=50 leaf_wine=0 leaf_d3dx=0 leaf_zlib=0 leaf_xml=0 leaf_proxy=0 leaf_other=0 stack_unknown=0 suspend_failures=0 context_failures=0 start_module=0 start_rva=0x1000 retired=0\n'
              + 'profile_frame scope=cumulative qpc=30005 slot=0 rva=0xbb480 count=250\n'
              + 'profile_report_end qpc=30006 report_us=1.0\n')
SYMBOLS = {'symbols': {'0xbb480': {'function_start_rva': '0xbb470', 'function_name': 'FUN_004bb470', 'function_size': 200},
                       '0xe8e20': {'function_start_rva': '0xe8e10', 'function_name': 'FUN_004e8e10', 'function_size': 400},
                       '0xe7600': {'function_start_rva': '0xe7590', 'function_name': 'FUN_004e7590', 'function_size': 900}}}


class ProfileSummaryTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.log = Path(self.directory.name) / 'session.log'
        self.log.write_text(HEADER + BLOCK1 + BLOCK2 + CUMULATIVE)
        self.symbols = Path(self.directory.name) / 'symbols.json'
        self.symbols.write_text(json.dumps(SYMBOLS))

    def tearDown(self):
        self.directory.cleanup()

    def test_scan_keeps_only_profiler_lines(self):
        lines = scan(self.log)
        self.assertTrue(all(l.startswith(('profile_', 'telemetry_start')) for l in lines))
        self.assertFalse(any('payload' in l for l in lines))

    def test_anchor_and_blocks(self):
        parsed = parse(scan(self.log))
        self.assertEqual(parsed['anchor'], 10000)
        self.assertEqual([b['scope'] for b in parsed['blocks']], ['delta', 'delta', 'cumulative'])
        self.assertEqual(parsed['blocks'][0]['report_us'], 800.0)
        self.assertEqual(parsed['modules'][0]['kind'], 'x3ap')

    def test_whole_log_sums_deltas_only(self):
        result = summarize(self.log, [('whole', None, None)])
        w = result['windows'][0]
        self.assertEqual(w['samples'], 400)
        self.assertEqual(w['ticks'], 450)
        self.assertEqual(w['dropped'], 3)
        self.assertAlmostEqual(w['sampler_cost']['tick_us_mean'], (40.5 * 250 + 20.0 * 200) / 450)
        self.assertEqual(w['sampler_cost']['tick_us_max'], 120.0)
        self.assertEqual(w['sampler_cost']['report_us_total'], 1300.0)
        self.assertEqual(w['module_split']['leaf_x3ap'], 250)
        self.assertEqual(w['module_split']['leaf_ntdll'], 150)
        frames = {(r['slot'], r['rva']): r['count'] for r in w['top_frames']}
        self.assertEqual(frames[(0, '0xbb480')], 250)   # 150 + 100, cumulative block excluded
        self.assertEqual(frames[(1, '0xe8e20')], 100)
        self.assertEqual(w['top_pairs'][0]['count'], 90)
        self.assertEqual(result['blocks'], {'delta': 2, 'cumulative': 1})

    def test_window_selects_overlapping_blocks(self):
        result = summarize(self.log, [('gap', 12.0, 14.0), ('late', 16.0, 30.0), ('none', 40.0, 50.0)])
        gap, late, none = result['windows']
        self.assertEqual(gap['blocks'], 1)
        self.assertEqual(gap['samples'], 300)
        self.assertEqual(gap['first_report_s'], 15.0)
        self.assertEqual(late['blocks'], 1)
        self.assertEqual(late['samples'], 100)
        self.assertEqual(none['blocks'], 0)
        self.assertEqual(none['samples'], 0)
        self.assertEqual(none['sampler_cost']['tick_us_mean'], 0.0)

    def test_per_thread_rows(self):
        w = summarize(self.log, [('whole', None, None)])['windows'][0]
        rows = {(r['slot'], r['tid']): r for r in w['threads']}
        self.assertEqual(rows[(0, 1)]['samples'], 300)
        self.assertEqual(rows[(0, 1)]['leaf_x3ap'], 250)
        self.assertEqual(rows[(1, 2)]['leaf_ntdll'], 100)
        self.assertEqual(rows[(1, 2)]['start_rva'], '0x1000')

    def test_symbols_group_frames(self):
        from summarize_profile import load_symbols
        w = summarize(self.log, [('whole', None, None)], symbols=load_symbols(self.symbols))['windows'][0]
        self.assertEqual(w['top_frames'][0]['function'], 'FUN_004bb470')
        self.assertEqual(w['top_pairs'][0]['caller_function'], 'FUN_004e7590')
        functions = {(r['slot'], r['function']): r['count'] for r in w['top_functions']}
        self.assertEqual(functions[(0, 'FUN_004bb470')], 250)
        self.assertEqual(functions[(1, 'FUN_004e8e10')], 100)
        self.assertEqual(w['top_leaves'][0]['function'], 'FUN_004bb470')

    def test_main_module_rva_list(self):
        parsed = parse(scan(self.log))
        self.assertEqual([hex(r) for r in main_module_rvas(parsed)], ['0xbb480', '0xe7600', '0xe8e20'])

    def test_unpinnable_module_line_has_no_size(self):
        """A module that could not be pinned is logged `pinned=0 error=<n>` with no
        kind/size/text range (sampling-profiler.md); parsing must not fail on it."""
        unpinned = 'profile_module index=3 name=winevulkan.dll base=0x77e60000 pinned=0 error=126\n'
        self.log.write_text(HEADER + unpinned + BLOCK1)
        parsed = parse(scan(self.log))
        self.assertEqual(parsed['modules'][3]['pinned'], False)
        self.assertIsNone(parsed['modules'][3]['size'])
        self.assertTrue(parsed['modules'][0]['pinned'])
        result = summarize(self.log, [('gap', 14.9, 15.1)])
        self.assertEqual(result['windows'][0]['samples'], 300)

    def test_frame_end_gaps_on_the_anchor_clock_become_windows(self):
        frame_ends = ('frame_end device=1 frame=300 draws=1 capture=0 present=00000000 elapsed_ms=2000 dt_ms=0 qpc=12000\n'
                      'frame_end device=1 frame=600 draws=1 capture=0 present=00000000 elapsed_ms=19000 dt_ms=17000 qpc=29000\n'
                      'frame_end device=1 frame=601 draws=1 capture=1 present=00000000 elapsed_ms=19016 dt_ms=16 qpc=29016\n')
        self.log.write_text(HEADER + frame_ends + BLOCK1 + BLOCK2)
        parsed = parse(scan(self.log))
        self.assertEqual(len(parsed['frame_ends']), 3)
        from summarize_profile import frame_end_gaps
        gaps = frame_end_gaps(parsed, 2.0)
        self.assertEqual(len(gaps), 1)
        self.assertEqual(gaps[0]['clock'], 'anchor')
        self.assertAlmostEqual(gaps[0]['start_s'], 2.0)
        self.assertAlmostEqual(gaps[0]['end_s'], 19.0)
        self.assertEqual(gaps[0]['frames'], 300)
        result = summarize(self.log, [('whole', None, None)], frame_gaps=True)
        self.assertEqual(result['frame_end_lines'], 3)
        self.assertEqual([w['label'] for w in result['windows']], ['whole', 'frame_gap_1'])
        self.assertEqual(result['windows'][1]['blocks'], 2)   # block 1 [10, 15] inside; block 2 [15, 20] straddles the 19 s end
        self.assertEqual(result['windows'][1]['samples'], 400)

    def test_frame_end_gaps_without_any_anchor_are_dll_load_seconds(self):
        self.log.write_text('frame_end device=1 frame=300 draws=1 capture=0 present=00000000 elapsed_ms=2000 dt_ms=0 qpc=12000\n'
                            'frame_end device=1 frame=600 draws=1 capture=0 present=00000000 elapsed_ms=40000 dt_ms=38000 qpc=50000\n')
        result = summarize(self.log, [('whole', None, None)], frame_gaps=True)
        self.assertEqual(result['anchor_qpc'], None)
        self.assertEqual(len(result['frame_end_gaps']), 1)
        self.assertEqual(result['frame_end_gaps'][0]['clock'], 'dll_load')
        self.assertAlmostEqual(result['frame_end_gaps'][0]['end_s'], 40.0)
        self.assertEqual([w['label'] for w in result['windows']], ['whole'])   # no anchor: no window can be placed

    def test_missing_telemetry_anchor_uses_profile_start(self):
        text = HEADER.replace(f'telemetry_start schema=1 qpc_frequency={FREQ} qpc=10000 anchor=proxy_initialize cpu_only=1\n', '')
        self.log.write_text(text + BLOCK1)
        result = summarize(self.log, [('gap', 14.9, 15.1)])
        self.assertEqual(result['anchor_qpc'], 10010)
        self.assertEqual(result['windows'][0]['blocks'], 1)


if __name__ == '__main__':
    unittest.main()
