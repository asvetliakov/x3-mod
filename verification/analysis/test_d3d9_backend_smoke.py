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

    def test_attribution(self):
        per_program, outside = load().attribute(STDERR)
        self.assertEqual(list(per_program), [(0, 'ps_a.bin')])
        self.assertEqual(len(per_program[(0, 'ps_a.bin')]), 2)
        self.assertIn('[mvk-error] VK_ERROR_INITIALIZATION_FAILED: outside the sweep', outside)
        self.assertNotIn('info: nothing wrong', outside)

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
