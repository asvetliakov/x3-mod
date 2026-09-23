"""D3D11 post-chain feasibility probe (docs/architecture/d3d11-post-chain-feasibility.md): the
runner's transcript parser and acceptance terms on a synthetic report, its CLI refusals (bottle
guard, unbuilt fixture) without Wine, and the schema of the recorded result when one exists.
No Wine, game or DLL build."""
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / 'verification/probe/run_d3d11_interop.py'
RECORD = ROOT / 'verification/results/bottle-X3/d3d11-interop.json'

SYNTHETIC = '\n'.join([
    'MODULE name=d3d9.dll loaded=1 path=C:\\windows\\system32\\d3d9.dll image_size=200704 file_size=187968 version=10.0.0.1 product=Wine builtin=1 markers=wined3d',
    'MODULE name=d3d11.dll loaded=1 path=C:\\windows\\system32\\d3d11.dll image_size=4702208 file_size=4685376 version=1.0.0.0 product=- builtin=0 markers=DXMT,winemetal',
    'COMPILER available=1', 'STEP device_d3d9 status=ok', 'STEP device_d3d9ex status=ok', 'STEP device_d3d11 status=ok',
    'DEVICE api=d3d9 hr=00000000 flags=00000040 adapter=Apple_M1 driver=wined3d vendor=106b device=0000 ps_version=3.0 max_rts=4 queries_event=1 device_is_ex=0',
    'DEVICE api=d3d11 hr=00000000 path=adapter flags=00000020 feature_level=11_1',
    'CHECK d3d9_plain_device_created PASS',
    'SHARE dir=d3d11_to_d3d9ex format=A8R8G8B8 create_hr=00000000 handle_hr=80004001 keyed_mutex_hr=80004002 handle=00000000 open_hr=80004005 opened=0 exact=0 mismatches=0 first_x=0 first_y=0 roundtrip_us=0.0 sync_us=0.0 synced=0',
    'STEP share_d3d11_to_d3d9 status=ok', 'STEP share_d3d9_to_d3d11 status=ok',
    'DILATE api=d3d9 variant=ps3_three_passes width=1920 height=1080 iterations=8 method=event_bracket gpu_us=1234.5 empty_bracket_us=40.0 mismatches=0 synced=1',
    'CHECK dilation_d3d9_1920x1080_matches_reference PASS',
    'DILATE api=d3d11 variant=cs5_one_dispatch width=1920 height=1080 iterations=8 method=timestamp_disjoint gpu_us=300.2 cpu_bracket_us=350.0 samples=5 frequency=1000000000 disjoint=0',
    'DILATE api=d3d9 variant=ps3_three_passes width=5120 height=1440 iterations=8 method=event_bracket gpu_us=4000.0 empty_bracket_us=40.0 mismatches=0 synced=1',
    'CHECK dilation_d3d9_5120x1440_matches_reference PASS',
    'OUTPUT index=0 name=\\\\.\\DISPLAY1 desc1_hr=00000000 colour_space=RGB_FULL_G22_NONE_P709(0) bits_per_colour=8 min_nits=0.500 max_nits=270.0 max_full_frame_nits=270.0 width=2560 height=1440',
    'STEP hdr_outputs status=ok', 'SWAPCHAIN attempt=flip_discard_fp16 hr=00000000',
    'COLORSPACE space=RGB_FULL_G2084_NONE_P2020 hr=00000000 support=0 present=0 overlay=0',
    'HDR swapchain=flip_discard_fp16 swapchain3_hr=00000000 present_hr=00000000 set_colorspace_g10_hr=80004005 containing_output_hr=00000000 tearing_hr=00000000 tearing=0',
    'STEP hdr_swapchain status=ok',
    'CAPS feature_level=11_1 compute_shaders=1 compute_via_4x_hr=00000000 compute_via_4x=1 tgsm_bytes=32768 max_threads_per_group=1024',
    'FORMAT name=R16G16B16A16_FLOAT hr=00000000 texture2d=1 render_target=1 depth_stencil=0 shader_sample=1 typed_uav=1 display=1 support2_hr=00000000 uav_typed_load=1 uav_typed_store=1',
    'STEP caps status=ok',
    'RESULT checks=3 failures=0 unavailable=0 path=measured PASS', ''])


def load_runner():
    spec = importlib.util.spec_from_file_location('run_d3d11_interop', RUNNER)
    module = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(RUNNER.parent))
    try:
        spec.loader.exec_module(module)
    finally:
        sys.path.remove(str(RUNNER.parent))
    return module


class Parser(unittest.TestCase):
    def test_parse_and_accept_synthetic(self):
        runner = load_runner()
        report = runner.parse(SYNTHETIC)
        self.assertEqual(report['check_count'], 3)
        self.assertEqual(report['failed_checks'], [])
        self.assertEqual(report['result']['verdict'], 'PASS')
        self.assertEqual(report['modules'][1]['markers'], 'DXMT,winemetal')
        self.assertEqual(report['modules'][0]['builtin'], 1)
        self.assertEqual(report['step_status']['hdr_swapchain'], 'ok')
        self.assertEqual(report['shares'][0]['handle_hr'], '80004001')  # hex stays text
        self.assertEqual(report['dilations'][1]['gpu_us'], 300.2)
        self.assertEqual(report['outputs'][0]['max_nits'], 270.0)
        self.assertEqual(report['caps']['tgsm_bytes'], 32768)
        runner.accept(report)
        summary = runner.summary_of(report)
        self.assertEqual(summary['devices']['d3d11']['feature_level'], '11_1')
        json.dumps(summary)

    def test_accept_rejects_missing_step_failed_check_and_missing_size(self):
        runner = load_runner()
        with self.assertRaises(AssertionError):
            runner.accept(runner.parse(SYNTHETIC.replace('STEP caps status=ok\n', '')))
        with self.assertRaises(AssertionError):
            runner.accept(runner.parse(SYNTHETIC.replace('CHECK dilation_d3d9_5120x1440_matches_reference PASS', 'CHECK dilation_d3d9_5120x1440_matches_reference FAIL')
                                      .replace('RESULT checks=3 failures=0 unavailable=0 path=measured PASS', 'RESULT checks=3 failures=1 unavailable=0 path=measured FAIL')))
        with self.assertRaises(AssertionError):
            runner.accept(runner.parse(SYNTHETIC.replace('width=5120 height=1440', 'width=2560 height=1440')))

    def test_unavailable_steps_are_findings_not_failures(self):
        runner = load_runner()
        report = runner.parse(SYNTHETIC.replace('STEP device_d3d11 status=ok', 'STEP device_d3d11 status=unavailable reason=d3d11.dll hr=00000000'))
        self.assertEqual(report['step_status']['device_d3d11'], 'unavailable')
        self.assertEqual(report['steps'][2]['reason'], 'd3d11.dll')
        runner.accept(report)


class Cli(unittest.TestCase):
    def run_runner(self, *args, env=None):
        environment = {k: v for k, v in os.environ.items() if k != 'X3M_FIXTURE_BOTTLE'}
        environment.update(env or {})
        return subprocess.run([sys.executable, str(RUNNER), *args], capture_output=True, text=True, env=environment, cwd=ROOT)

    def test_refuses_without_the_x3_bottle(self):
        run = self.run_runner('--exe', '/nonexistent/d3d11_interop_fixture.exe')
        self.assertEqual(run.returncode, 1)
        self.assertIn('X3M_FIXTURE_BOTTLE=X3', run.stderr)

    def test_refuses_an_unbuilt_fixture_before_any_wine(self):
        with tempfile.TemporaryDirectory() as directory:
            run = self.run_runner('--exe', str(Path(directory) / 'd3d11_interop_fixture.exe'), env={'X3M_FIXTURE_BOTTLE': 'X3'})
        self.assertEqual(run.returncode, 1)
        self.assertIn('fixture not built', run.stderr)
        self.assertIn('d3d11_interop_fixture', run.stderr)


class Record(unittest.TestCase):
    def test_recorded_result_schema(self):
        if not RECORD.is_file():
            self.skipTest('no recorded result')
        record = json.loads(RECORD.read_text())
        for key in ('passed', 'game_launched', 'bottle', 'sources', 'executable_sha256', 'dll_overrides', 'command', 'exit_code', 'report'):
            self.assertIn(key, record)
        self.assertFalse(record['game_launched'])
        self.assertEqual(record['bottle']['name'], 'X3')
        self.assertEqual(record['bottle']['wine_arch'], 'arm64')
        self.assertEqual(set(record['bottle']['environment']), {'FEX_X87REDUCEDPRECISION', 'WINEMSYNC'})
        report = record['report']
        for key in ('modules', 'devices', 'shares', 'dilations', 'colour_spaces', 'outputs', 'caps', 'formats', 'steps', 'step_status', 'checks', 'result'):
            self.assertIn(key, report)
        self.assertTrue(record['passed'], report['failed_checks'])
        load_runner().accept(report)


if __name__ == '__main__':
    unittest.main()
