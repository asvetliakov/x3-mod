"""Synthetic tests for analyze_loading_profile.py: a telemetry log with loading
metrics and sampling-profiler reports built from the schema in
docs/verification/sampling-profiler.md; no game content."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
import analyze_loading_profile as alp  # noqa: E402

FREQ = 1000  # 1 kHz QPC keeps seconds readable: qpc 10000 is t = 0
BASE = 0x00400000

HEADER = (f'telemetry_start schema=1 qpc_frequency={FREQ} qpc=10000 anchor=proxy_initialize cpu_only=1\n'
          'loading_trace coverage_begin=10010 frequency=1000 hooks=16 module=main\n'
          'capture_draw frame=1 payload=ignored\n')
PROFILE_HEADER = (
    f'profile_start schema=1 enabled=1 qpc=10010 frequency={FREQ} interval_us=2000 report_s=5 sampler_tid=9 init_tid=1 '
    'main_base=0x00400000 proxy_base=0x10000000 query_thread=1 chain_limit=32 scan_dwords=1024 threads_max=32 modules_max=128 tables=65536,65536,16384\n'
    'profile_module index=0 name=X3AP.exe kind=x3ap base=0x00400000 size=0x200000 text_rva=0x1000 text_size=0x150000 ranges=1 pinned=1\n'
    'profile_module index=1 name=ntdll.dll kind=ntdll base=0x7bc00000 size=0x100000 text_rva=0x1000 text_size=0x80000 ranges=1 pinned=1\n'
    'profile_thread_seen slot=0 tid=1 teb=0x7ffde000 start_module=0 start_rva=0x1000 creation=1\n')


def summary(device, frame, stamp):
    return f'telemetry_summary device={device} frame={frame} reason=interval qpc={stamp}\n'


def frame_metric(count, total_us, maximum_us):
    return (f'telemetry_metric device=1 name=frame_normal count={count} failures=0 total_us={total_us} '
            f'min_us=0.0 max_us={maximum_us} bytes=0 buckets=0,0,0,0,0,1\n')


def metric(op, stamp, ticks=100, count=1, byte_count=4, failures=0):
    return (f'loading_metric op={op} qpc={stamp} count={count} failures={failures} pending=0 ambiguous=0 '
            f'bytes={byte_count} inclusive_ticks={ticks} exclusive_ticks={ticks} max_ticks={ticks} wrapper_tail_ticks=1\n')


def report(qpc, elapsed_us, samples, ticks, threads):
    """One delta block: `threads` is a list of (slot, tid, x3ap, ntdll, leaves, frames, pairs)."""
    text = (f'profile_report scope=delta qpc={qpc} frequency={FREQ} since_start_us=0 elapsed_us={elapsed_us} interval_us=2000 '
            f'samples={samples} threads={len(threads)} threads_unsampled=0 modules=2 ticks={ticks} tick_us_mean=300.0 tick_us_max=900.0 '
            'refresh_us=100.0 dropped=0 table_used=1,1,1\n')
    for slot, tid, x3ap, ntdll, leaves, frames, pairs in threads:
        text += (f'profile_thread scope=delta qpc={qpc} slot={slot} tid={tid} samples={x3ap + ntdll} total={x3ap + ntdll} '
                 f'leaf_x3ap={x3ap} leaf_ntdll={ntdll} leaf_wine=0 leaf_d3dx=0 leaf_zlib=0 leaf_xml=0 leaf_proxy=0 leaf_other=0 '
                 'stack_unknown=0 suspend_failures=0 context_failures=0 start_module=0 start_rva=0x1000 retired=0\n')
        for module, name, rva, n in leaves:
            text += f'profile_leaf scope=delta qpc={qpc} slot={slot} module={module} name={name} rva={rva} count={n}\n'
        for rva, n in frames:
            text += f'profile_frame scope=delta qpc={qpc} slot={slot} rva={rva} count={n}\n'
        for rva, caller, n in pairs:
            text += f'profile_pair scope=delta qpc={qpc} slot={slot} rva={rva} caller={caller} count={n}\n'
    text += f'profile_report_end qpc={qpc + 1} report_us=500.0\n'
    return text


# Timeline (seconds): device report at 1 s; a 30 s Present gap closes at 31 s
# (report at 31.5 s with 0.5 s of other intervals). Loading windows: process
# summaries at 5 s (menu vector) and 20 s. Profile blocks: [5, 10] and [10, 15]
# fully inside the gap; [28, 33] straddles the gap end; [40, 45] is outside.
MENU_METRICS = (metric(alp.ADJACENCY, 15000, ticks=3000, count=1017)
                + metric(alp.TEXTURE, 15000, ticks=2000, count=460, byte_count=299_000_000)
                + metric('inflate', 15000, ticks=1600, count=160_000))
LOADING = (summary(1, 1, 11000)
           + summary(0, 0, 15000) + MENU_METRICS
           + summary(0, 0, 30000) + metric('ReadFile', 30000, ticks=200, count=40, byte_count=4096)
           + summary(1, 4, 41500) + frame_metric(3, 30_500_000.0, 30_000_000.0))

# Two leaf RVAs inside FUN_004bb470 (0xbb480, 0xbb4a0) must aggregate to one function.
THREAD0_IN = (0, 1, 150, 50,
              [(0, 'X3AP.exe', '0xbb480', 90), (0, 'X3AP.exe', '0xbb4a0', 60), (1, 'ntdll.dll', '0x2000', 50)],
              [('0xbb480', 150), ('0xe8e20', 50)],
              [('0xbb480', '0xbd850', 140), ('0xe8e20', '0xe7600', 50)])
THREAD1_IN = (1, 2, 0, 100, [(1, 'ntdll.dll', '0x2000', 100)], [('0x0', 100)], [])
BLOCK_A = report(20000, 5_000_000, 300, 250, [THREAD0_IN, THREAD1_IN])
BLOCK_B = report(25000, 5_000_000, 300, 250, [THREAD0_IN, THREAD1_IN])
THREAD0_EDGE = (0, 1, 80, 20, [(0, 'X3AP.exe', '0xab900', 80), (1, 'ntdll.dll', '0x2000', 20)],
                [('0xab900', 80), ('0x0', 20)], [('0xab900', '0x3a00', 80)])
BLOCK_EDGE = report(43000, 5_000_000, 100, 250, [THREAD0_EDGE])
BLOCK_OUT = report(55000, 5_000_000, 100, 250, [THREAD0_EDGE])
PROFILE = PROFILE_HEADER + BLOCK_A + BLOCK_B + BLOCK_EDGE + BLOCK_OUT

SYMBOLS = {'image_base': '0x00400000', 'symbols': {
    '0xbb480': {'function_start_rva': '0xbb470', 'function_name': 'FUN_004bb470', 'function_size': 1684},
    '0xbb4a0': {'function_start_rva': '0xbb470', 'function_name': 'FUN_004bb470', 'function_size': 1684},
    '0xbd850': {'function_start_rva': '0xbd830', 'function_name': 'FUN_004bd830', 'function_size': 556},
    '0xe8e20': {'function_start_rva': '0xe8e10', 'function_name': 'FUN_004e8e10', 'function_size': 192},
    '0xe7600': {'function_start_rva': '0xe7590', 'function_name': 'FUN_004e7590', 'function_size': 4538},
    '0xab900': {'function_start_rva': '0xab880', 'function_name': 'FUN_004ab880', 'function_size': 15895},
    '0x3a00': None}}


class LoadingProfileTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.labels = alp.load_labels()

    def tearDown(self):
        self.directory.cleanup()

    def write(self, text, name='session.log'):
        path = self.root / name
        path.write_text(text)
        return path

    def run_pipeline(self, text, symbols=SYMBOLS):
        path = self.write(text)
        result = alp.analyze(path, 2.0, labels=self.labels)
        table = {int(k, 16): v for k, v in symbols['symbols'].items()} if symbols else {}
        alp.finish(result, table, 'test', self.labels)
        return result

    def test_gap_with_profile_windows_inside(self):
        result = self.run_pipeline(HEADER + LOADING + PROFILE)
        self.assertEqual(len(result['gaps']), 1)
        gap = result['gaps'][0]
        self.assertAlmostEqual(gap['gap_seconds'], 30.0)
        self.assertEqual(gap['label'], 'menu load')
        table = gap['sampled']
        # Blocks A and B are inside; the edge block straddles the gap end; the late block is outside.
        self.assertEqual(table['blocks'], 3)
        self.assertEqual(table['inside_blocks'], 2)
        self.assertEqual(table['straddling_blocks'], 1)
        self.assertEqual(table['samples'], 700)
        self.assertAlmostEqual(table['covered_seconds'], 15.0)
        self.assertAlmostEqual(table['samples_per_second'], 700 / 15.0)
        self.assertAlmostEqual(table['ticks_per_second'], 50.0)
        self.assertEqual(table['module_split']['leaf_x3ap'], 380)
        self.assertEqual(table['module_split']['leaf_ntdll'], 320)
        threads = {(row['slot'], row['tid']): row for row in table['threads']}
        self.assertEqual(threads[(0, 1)]['samples'], 500)
        self.assertEqual(threads[(1, 2)]['leaf_ntdll'], 200)
        # Hooked time next to it: the menu vector windows overlap the gap, the 20 s window too.
        self.assertEqual(gap['hooked']['operations'][alp.ADJACENCY]['count'], 1017)
        self.assertAlmostEqual(gap['hooked']['exclusive_seconds'], 6.8)
        self.assertAlmostEqual(gap['unexplained_seconds'], 23.2)

    def test_symbols_aggregate_leaf_samples_by_function(self):
        gap = self.run_pipeline(HEADER + LOADING + PROFILE)['gaps'][0]
        functions = {row['start_va']: row for row in gap['sampled']['functions']}
        mesh = functions['0x004bb470']
        # 90 + 60 leaf samples per block, two blocks: one function row, not two RVA rows.
        self.assertEqual(mesh['self_samples'], 300)
        self.assertEqual(mesh['inclusive_samples'], 300)
        self.assertAlmostEqual(mesh['self_share'], 300 / 700)
        self.assertEqual(mesh['name'], 'FUN_004bb470')
        self.assertEqual(mesh['label'], self.labels[0x004bb470]['label'])
        self.assertEqual(mesh['busiest_slot'], 0)
        self.assertAlmostEqual(mesh['busiest_slot_share'], 300 / 500)
        self.assertEqual(gap['sampled']['functions'][0]['start_va'], '0x004bb470')  # primary ranking: self samples
        # A function with no leaf samples still appears through its frame samples (waits attributed to it).
        loader = functions['0x004e8e10']
        self.assertEqual((loader['self_samples'], loader['inclusive_samples']), (0, 100))
        # Unresolved caller keeps its RVA; resolved caller carries the label.
        pairs = {(row['rva'], row['caller']): row for row in gap['sampled']['pairs']}
        self.assertEqual(pairs[('0xbb480', '0xbd850')]['caller_function'], 'FUN_004bd830')
        self.assertEqual(pairs[('0xbb480', '0xbd850')]['caller_label'], self.labels[0x004bd830]['label'])
        self.assertEqual(pairs[('0xab900', '0x3a00')]['caller_function'], '0x3a00')
        self.assertEqual(gap['sampled']['frames_without_main_module'], 220)
        self.assertEqual(result_symbols(self), (7, 6))

    def test_hooked_attribution_sits_next_to_the_sampled_function(self):
        gap = self.run_pipeline(HEADER + LOADING + PROFILE)['gaps'][0]
        functions = {row['start_va']: row for row in gap['sampled']['functions']}
        mesh_prep = functions['0x004bb470']
        # 0x004bb470 is documented as the D3DXCreateMesh caller; that API did not run here, so no row.
        self.assertEqual(mesh_prep['hooked'], [])
        text = alp.render(self.run_pipeline(HEADER + LOADING + PROFILE))
        self.assertIn('`0x004bb470`', text)
        self.assertIn('| `ID3DXMesh::GenerateAdjacency` | 1,017 |', text)
        self.assertIn('holds 42.9 % of all samples as leaf (60.0 % of slot 0', text)
        self.assertIn('it is a known routine', text)

    def test_report_crossing_the_gap_boundary_is_flagged_not_dropped(self):
        gap = self.run_pipeline(HEADER + LOADING + PROFILE)['gaps'][0]
        self.assertEqual(gap['sampled']['straddling_blocks'], 1)
        functions = {row['start_va']: row for row in gap['sampled']['functions']}
        self.assertEqual(functions['0x004ab880']['self_samples'], 80)   # from the straddling block only
        text = alp.render(self.run_pipeline(HEADER + LOADING + PROFILE))
        self.assertIn('1 of 3 delta blocks straddle the interval boundary', text)

    def test_missing_profile_lines_render_a_no_profile_section(self):
        result = self.run_pipeline(HEADER + LOADING, symbols=None)
        self.assertFalse(result['profile']['present'])
        gap = result['gaps'][0]
        self.assertIsNone(gap['sampled'])
        self.assertEqual(gap['hooked']['operations'][alp.ADJACENCY]['count'], 1017)
        text = alp.render(result)
        self.assertIn('**No profile data**', text)
        self.assertIn('contains no `profile_*` lines', text)
        self.assertIn('No sampled attribution exists for this interval.', text)
        json_path, md_path = alp.write_outputs(result, self.root / 'out')
        loaded = json.loads(json_path.read_text())
        self.assertEqual(loaded['gaps'][0]['label'], 'menu load')
        self.assertTrue(md_path.read_text().startswith('# Loading attribution:'))

    def test_profile_present_but_not_covering_the_gap(self):
        result = self.run_pipeline(HEADER + LOADING + PROFILE_HEADER + BLOCK_OUT)
        gap = result['gaps'][0]
        self.assertTrue(result['profile']['present'])
        self.assertIsNone(gap['sampled'])
        self.assertIn('**No profile data**', alp.render(result))

    def test_stall_sub_intervals_get_their_own_tables(self):
        # A 15 s report stall between the 5 s and 20 s process summaries lies inside the gap.
        result = self.run_pipeline(HEADER + LOADING + PROFILE)
        gap = result['gaps'][0]
        self.assertEqual(len(gap['stalls']), 1)
        stall = gap['stalls'][0]
        self.assertAlmostEqual(stall['start_seconds'], 5.0)
        self.assertAlmostEqual(stall['end_seconds'], 20.0)
        self.assertEqual(stall['hooked']['operations']['ReadFile']['count'], 40)
        self.assertEqual(stall['sampled']['blocks'], 2)
        self.assertEqual(stall['sampled']['straddling_blocks'], 0)
        self.assertEqual(stall['sampled']['functions'][0]['start_va'], '0x004bb470')
        self.assertIn('### Gap 1 stall 1: report stall 15.000 s', alp.render(result))

    def test_gap_labels_follow_the_hooked_evidence(self):
        save = {'gzread': dict(count=1000, failures=0), 'gzopen': dict(count=1, failures=0)}
        self.assertEqual(alp.label_gap(save, [])[0], 'save load')
        menu = {alp.ADJACENCY: dict(count=1017), alp.TEXTURE: dict(count=460, bytes=299_000_000)}
        self.assertEqual(alp.label_gap(menu, [])[0], 'menu load')
        sector = {alp.ADJACENCY: dict(count=2076), alp.TEXTURE: dict(count=54, bytes=9_000_000)}
        self.assertEqual(alp.label_gap(sector, [])[0], 'unlabelled')
        self.assertEqual(alp.label_gap(sector, ['menu load'])[0], 'sector change')
        self.assertEqual(alp.label_gap({}, ['menu load'])[0], 'unlabelled')
        self.assertEqual(alp.label_gap({'gzopen': dict(count=13, failures=13)}, [])[0], 'unlabelled')

    def test_rva_collection_and_ghidra_command(self):
        result = alp.analyze(self.write(HEADER + LOADING + PROFILE), 2.0, labels=self.labels)
        rvas = alp.collect_rvas(result['_intervals'], alp.main_module_names(result['_parsed']))
        self.assertEqual([hex(r) for r in rvas], ['0x3a00', '0xab900', '0xbb480', '0xbb4a0', '0xbd850', '0xe7600', '0xe8e20'])
        command = alp.ghidra_command('/x/rvas.txt', '/x/symbols.json', project='/p', name='N', program='X3AP.exe',
                                     script_dir='/s')
        self.assertEqual(command[:3], [alp.GHIDRA_HEADLESS, '/p', 'N'])
        self.assertIn('-readOnly', command)
        self.assertEqual(command[-3:], ['X3ProfileSymbols.java', '/x/rvas.txt', '/x/symbols.json'])

    def test_labels_table_is_well_formed(self):
        data = json.loads(alp.LABELS_PATH.read_text())
        for key, entry in data['labels'].items():
            self.assertRegex(key, r'^0x00[4-5][0-9a-f]{5}$')
            self.assertTrue(entry['label'])
            self.assertTrue(entry['source'])
        self.assertIn(0x004e7590, self.labels)
        self.assertIn('ID3DXMesh::GenerateAdjacency', self.labels[0x004bc680]['hooked_apis'])

    def test_scan_keeps_only_sparse_lines(self):
        loading_lines, profile_lines, provenance = alp.scan(self.write(HEADER + LOADING + PROFILE))
        self.assertFalse(any('payload' in line for line in loading_lines + profile_lines))
        self.assertTrue(all(line.startswith(('profile_', 'telemetry_start')) for line in profile_lines))
        self.assertEqual(provenance['bytes'], len((HEADER + LOADING + PROFILE).encode()))


def result_symbols(test):
    result = test.run_pipeline(HEADER + LOADING + PROFILE)
    return result['symbols']['requested'], result['symbols']['resolved']


if __name__ == '__main__':
    unittest.main()
