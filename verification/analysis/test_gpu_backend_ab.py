"""GPU backend A/B fixture (docs/architecture/d3d9-to-d3d11-translation.md): the runner's transcript
parser, summary and acceptance terms on a synthetic report, its bottle guard without Wine, and the
schema of the recorded summary when one exists. No Wine, game or build."""
import importlib.util
import json
import os
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / 'verification/probe/run_gpu_backend_ab.py'
RECORD = ROOT / 'verification/results/bottle-X3/gpu-backend-ab/summary.json'


def load():
    sys.path.insert(0, str(RUNNER.parent))
    spec = importlib.util.spec_from_file_location('run_gpu_backend_ab', RUNNER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def measure(api, workload, w, h, event, submit, ts):
    return (f'MEASURE api={api} workload={workload} width={w} height={h} frames=300 event_us={event:.1f} event_p90_us={event * 1.1:.1f} submit_us={submit:.1f} submit_p90_us={submit:.1f} '
            f'timestamp_us={ts:.1f} timestamp_p90_us={ts:.1f} timestamp_samples={300 if ts else 0} timestamp_rejected=0 wall_per_frame_us={event:.1f} main_thread_cpu_us={event:.1f} '
            f'process_cpu_us={event * 2:.1f} other_threads_cpu_us={event:.1f} timeouts=0 pipelined_us={event / 2:.1f} pipelined_submit_us={submit:.1f} pipelined_synced=1')


def synthetic(cover11=1.0):
    lines = ['RENDERER key_open=2 renderer=absent', 'STEP d3d9 status=ok', 'STEP d3d11 status=ok',
             measure('d3d9', 'empty', 0, 0, 20, 1, 0), measure('d3d11', 'empty', 0, 0, 40, 1, 30)]
    for workload, w, h in load().EXPECTED:
        lines += [measure('d3d9', workload, w, h, 400, 100, 0), measure('d3d11', workload, w, h, 240, 50, 200)]
        lines += [f'VERIFY api=d3d9 workload={workload} width={w} height={h} coverage=1.000000 mean_r=0.2 mean_g=0.3 mean_b=0.4',
                  f'VERIFY api=d3d11 workload={workload} width={w} height={h} coverage={cover11:.6f} mean_r=0.2 mean_g=0.3 mean_b=0.4']
    return '\n'.join(lines + ['CHECK d3d9_scene_460_1920x1080_synced PASS', 'RESULT checks=1 failures=0 PASS', ''])


class GpuBackendAb(unittest.TestCase):
    def test_summary_and_acceptance(self):
        runner = load()
        report = runner.parse(synthetic())
        summary = runner.summarise(report)
        runner.accept(report, summary)
        entry = summary['workloads']['scene_460']['5120x1440']
        self.assertEqual(entry['ratio_d3d9_over_d3d11'], round(400 / 240, 3))
        self.assertEqual(entry['ratio_net_of_empty_bracket'], round(380 / 200, 3))
        self.assertEqual(entry['d3d11_event_over_timestamp'], 1.2)
        self.assertIsNone(entry['d3d9_timestamp_us'])
        self.assertEqual(entry['cpu_submit_us'], {'d3d9': 100.0, 'd3d11': 50.0})
        self.assertEqual(summary['empty_bracket_us'], {'d3d9': 20.0, 'd3d11': 40.0})
        self.assertEqual(entry['ratio_pipelined'], round(200 / 120, 3))

    def test_rejects_disagreeing_readback_and_missing_workload(self):
        runner = load()
        report = runner.parse(synthetic(cover11=0.9))
        with self.assertRaises(AssertionError):
            runner.accept(report, runner.summarise(report))
        report = runner.parse(synthetic().replace('coverage=1.000000', 'coverage=0.000100'))
        with self.assertRaises(AssertionError):  # both APIs agree, but drew nothing
            runner.accept(report, runner.summarise(report))
        report = runner.parse('\n'.join(line for line in synthetic().splitlines() if 'scene_460_cpu' not in line))
        with self.assertRaises(AssertionError):
            runner.accept(report, runner.summarise(report))

    def test_bottle_guard(self):
        env = {k: v for k, v in os.environ.items() if k != 'X3M_FIXTURE_BOTTLE'}
        run = subprocess.run([sys.executable, str(RUNNER), '--exe', '/nonexistent.exe'], capture_output=True, text=True, env=env, cwd=ROOT)
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('X3M_FIXTURE_BOTTLE=X3', run.stderr)

    @unittest.skipUnless(RECORD.exists(), 'no recorded run')
    def test_recorded_summary_schema(self):
        record = json.loads(RECORD.read_text())
        self.assertFalse(record['game_launched'])
        self.assertEqual(record['bottle']['name'], 'X3')
        if record['passed']:
            for workload, w, h in load().EXPECTED:
                entry = record['workloads'][workload][f'{w}x{h}']
                for key in ('d3d9_event_us', 'd3d11_event_us', 'd3d11_timestamp_us', 'cpu_submit_us', 'ratio_d3d9_over_d3d11'):
                    self.assertIn(key, entry)


if __name__ == '__main__':
    unittest.main()
