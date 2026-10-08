"""D3D9 backend smoke probe (docs/architecture/d3d9-to-d3d11-translation.md, "DXVK D3D9 over MoltenVK:
smoke test"): the runner's stdout parser and per-program stderr attribution on synthetic transcripts,
its bottle guard without Wine, and the shape of the recorded runs. No Wine, game or build."""
import importlib.util
import json
import os
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / 'verification/probe/run_d3d9_backend_smoke.py'
RECORDS = ROOT / 'verification/results/bottle-X3/d3d9-backend-smoke'


def load():
    sys.path.insert(0, str(RUNNER.parent))
    spec = importlib.util.spec_from_file_location('run_d3d9_backend_smoke', RUNNER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


STDOUT = '\n'.join([
    'STEP load_d3d9 status=ok error=0',
    'ADAPTER hr=00000000 driver=nvd3dum.dll description=Apple_M5_Pro vendor=106b device=1a0603f1',
    'DEVICE hr=00000000 behavior=00000052 create_ms=141.7',
    'DRAW name=quad_vs30_ps30 draw_hr=00000000 read_hr=00000000 mean_r=0.0 mean_g=0.0 mean_b=0.0 coverage=0.000',
    'CHECK quad_vs30_ps30 FAIL', 'CHECK device_create PASS',
    'PRIVATEDATA resource=rt_surface unset_hr=8876086c unset_size=0 set_hr=00000000 get_hr=00000000 get_size=8 '
    'value=58334d0000000001 value_ok=1 small_hr=8876086c small_size=8',
    'SWEEP index=0 kind=ps name=ps_a.bin version=ffff0300 words=10 create_hr=00000000 draw_hr=00000000 wait_ms=1.00',
    'SWEEP index=1 kind=vs name=vs_b.bin version=fffe0300 words=10 create_hr=00000000 draw_hr=00000000 wait_ms=1.00',
    'SWEEPSUMMARY programs=2 created=2 create_failed=0', 'RESULT checks=2 failed=1 FAIL', ''])
STDERR = '\n'.join([
    'info:  DXVK: cxaddon-1.10.3', '[mvk-error] VK_ERROR_INITIALIZATION_FAILED: outside the sweep',
    'X3M-SWEEP-BEGIN 0 ps_a.bin', '[mvk-error] VK_ERROR_INITIALIZATION_FAILED: Shader library compile failed (Error code 3):',
    "program_source:1:2: error: cannot reserve 'texture' resource location at index 0", 'X3M-SWEEP-END 0',
    'X3M-SWEEP-BEGIN 1 vs_b.bin', 'info: nothing wrong', 'X3M-SWEEP-END 1', ''])


class D3D9BackendSmoke(unittest.TestCase):
    def test_parse(self):
        report = load().parse(STDOUT)
        self.assertEqual(report['adapter']['description'], 'Apple_M5_Pro')
        self.assertEqual(report['adapter']['hr'], '00000000')   # hr fields stay text
        self.assertEqual(report['device']['behavior'], '00000052')
        self.assertEqual(report['failed_checks'], ['quad_vs30_ps30'])
        self.assertEqual([row['name'] for row in report['sweep']], ['ps_a.bin', 'vs_b.bin'])
        self.assertEqual(report['result']['failed'], 1)
        self.assertEqual(report['privatedata'], [{'resource': 'rt_surface', 'unset_hr': '8876086c', 'unset_size': 0,
                                                  'set_hr': '00000000', 'get_hr': '00000000', 'get_size': 8,
                                                  'value': '58334d0000000001', 'value_ok': 1, 'small_hr': '8876086c',
                                                  'small_size': 8}])

    def test_private_data_records(self):
        """Run 131 A: the unset-GUID form per backend that ownership::private_data_not_found accepts."""
        expected = {'privatedata-wined3d': ('88760866', 8), 'privatedata-dxvk-pr20': ('8876086c', 0)}
        for name, (unset_hr, unset_size) in expected.items():
            path = RECORDS / f'{name}.json'
            if not path.is_file():
                self.skipTest(f'{name} not recorded')
            rows = json.loads(path.read_text())['report']['privatedata']
            self.assertEqual(sorted(row['resource'] for row in rows), ['managed_texture', 'rt_surface'], name)
            for row in rows:
                self.assertEqual((row['unset_hr'], row['unset_size']), (unset_hr, unset_size), name)
                self.assertEqual((row['set_hr'], row['get_hr'], row['get_size'], row['value_ok']),
                                 ('00000000', '00000000', 8, 1), name)
                self.assertNotEqual(row['small_size'], 0, name)  # too-small buffer: never the not-found form

    def test_attribution(self):
        per_program, outside = load().attribute(STDERR)
        self.assertEqual(list(per_program), [(0, 'ps_a.bin')])
        self.assertEqual(len(per_program[(0, 'ps_a.bin')]), 2)
        self.assertIn('[mvk-error] VK_ERROR_INITIALIZATION_FAILED: outside the sweep', outside)
        self.assertNotIn('info: nothing wrong', outside)

    def test_pipeline_cost_parse_and_attribution(self):
        module = load()
        report = module.parse('\n'.join([
            'PIPELINE_COST_SHADER name=ps_0123456789abcdef create_hr=00000000 create_us=812 words=1883',
            'PIPELINE_WARMUP step=e us=900 draw_hr=00000000 waited=1',
            'pipeline_cost ps=ps_0123456789abcdef step=a us=41000 vs=vs_6059306306203243 draw_hr=00000000 waited=1 ps_create_us=812',
            'pipeline_cost ps=ps_0123456789abcdef step=f us=120 vs=vs_6059306306203243 draw_hr=00000000 waited=1',
            'pipeline_cost_summary step=a n=1 median_us=41000 max_us=41000', '']))
        self.assertEqual([(r['step'], r['us']) for r in report['pipeline_cost']], [('a', 41000), ('f', 120)])
        self.assertEqual(report['pipeline_cost'][0]['ps_create_us'], 812)
        self.assertEqual(report['pipeline_cost_summary'][0]['median_us'], 41000)
        self.assertEqual(report['pipeline_warmup'][0]['step'], 'e')
        self.assertEqual(report['pipeline_cost_shaders'][0]['words'], 1883)
        per_step = module.attribute_pipeline_cost('\n'.join([
            'info: before', 'X3M-PC-BEGIN ps_x a', 'info: compiling', 'X3M-PC-END ps_x a', 'X3M-PC-BEGIN ps_x f',
            'X3M-PC-END ps_x f', 'info: after']))
        self.assertEqual(per_step, {'ps_x a': ['info: compiling'], 'ps_x f': []})

    def test_pipeline_cost_list(self):
        module = load()
        if not module.PROGRAMS.is_dir():
            self.skipTest('no local program extraction')
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            chosen, reason = module.pipeline_cost_list(Path(scratch) / 'list.txt', 8)
            if chosen is None:
                self.skipTest(reason)
            lines = (Path(scratch) / 'list.txt').read_text().splitlines()
        self.assertEqual(len(chosen), 8)
        self.assertEqual(len(lines), 8)
        self.assertTrue(all(len(line.split('\t')) == 3 for line in lines))
        sizes = [c['bytes'] for c in chosen]
        self.assertEqual(sizes, sorted(sizes, reverse=True))
        self.assertTrue(all(c['ps'].startswith('ps_') and c['vs'].startswith('vs_') and c['vs'] != c['vs2'] for c in chosen))

    def test_requires_x3_bottle(self):
        env = {k: v for k, v in os.environ.items() if k != 'X3M_FIXTURE_BOTTLE'}
        run = subprocess.run([sys.executable, str(RUNNER), '--d3d9', 'builtin'], capture_output=True, text=True, env=env, timeout=60)
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('X3M_FIXTURE_BOTTLE=X3', run.stderr)

    def test_records(self):
        records = sorted(RECORDS.glob('*.json'))
        if not records:
            self.skipTest('no recorded runs')
        for path in records:
            record = json.loads(path.read_text())
            self.assertFalse(record['game_launched'], path.name)
            self.assertEqual(record['bottle']['name'], 'X3', path.name)
            self.assertIn('exit_code', record, path.name)
            self.assertIn('report', record, path.name)


if __name__ == '__main__':
    unittest.main()
