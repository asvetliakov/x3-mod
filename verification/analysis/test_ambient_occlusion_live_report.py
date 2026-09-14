"""The live ambient occlusion runner's parsers and per-twin verdict on transcript excerpts.

Exercises run_ambient_occlusion_live.parse_fixture / parse_trace / validate_case:
the multiply twin, the fault twin (attach refused, frames bit-identical), the
off twin (no AO lines) and the grammar of the ambient_occlusion_frame line.
No Wine, no build.
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'probe'))
import run_ambient_occlusion_live as runner  # noqa: E402

FIXTURE = '''HOOK installed=1 status=active ao=1 fault=0 debug=0 strength=0.500 hdr=0
RESET PASS
AO_DEPTH frame=3 covered=2688 min=0.116177 max=0.983427 mean=0.591933 max_column=11
AO_CREASE frame=3 law=multiply darkened=1097 sentinel=1408 violations=0 max_drop=15 centre_mean=5.300 outer_mean=0.000 changed=1097
AO_CREASE frame=4 law=multiply changed=1100 pixel_check=0
AO_CREASE frame=5 law=multiply changed=1115 pixel_check=0
RESET PASS
AO_CREASE frame=6 law=multiply darkened=1148 sentinel=1408 violations=0 max_drop=17 centre_mean=4.951 outer_mean=0.000 changed=1148
AO_CREASE frame=7 law=multiply changed=1143 pixel_check=0
RESULT PASS checks=111 restorations=0 frames=8 motion_pixels=0
'''


def trace(attached=1, ran=1, reason='ok', device_reason='ok', frames=8, applied=1, gpu=-1.0, unavailable=True, debug=0):
    lines = ['ambient_occlusion_mode requested=1 enabled=1 motion_output=1 taa=1 radius_m=2 strength=0.5 debug=0 timing=1',
             f'ambient_occlusion_device device=1 attached={attached} reason={device_reason} result=00000000 target_format=21 adapter_format=22 slots=397 radius_m=2.000 strength=0.500 debug=0 timing=1 taa_references=6']
    if unavailable:
        lines.append('ambient_occlusion_timing device=1 queries=unavailable result=8876086a')
    for f in range(frames):
        lines.append(f'ambient_occlusion_frame device=1 frame={f} attached={attached} ran={ran} reason={reason} gpu_us={gpu} cpu_us={150 + f} width=64 height=64 radius_px=2.13 gpu_frame=0 applied={applied} result=00000000 restore=00000000 stage=0 debug={debug}')
        if f in (1, 2):
            lines.append(f'scene_end_marker device=1 frame={f} draw_index=3')
    return '\n'.join(lines) + '\n'


class AmbientOcclusionLiveReportTests(unittest.TestCase):
    def test_parse_fixture(self):
        report = runner.parse_fixture(FIXTURE)
        self.assertEqual(report['result'], 'PASS')
        self.assertEqual((report['checks'], report['restorations']), (111, 0))
        self.assertEqual(report['hook']['installed'], 1)
        self.assertEqual([c['frame'] for c in report['crease']], [3, 4, 5, 6, 7])
        self.assertEqual([c['frame'] for c in report['crease'] if 'darkened' in c], [3, 6])
        self.assertEqual(report['violations'], [])

    def test_parse_trace_grammar(self):
        report = runner.parse_trace(trace())
        self.assertEqual(len(report['frames']), 8)
        line = report['frames'][0]
        for key in ('frame', 'attached', 'ran', 'reason', 'gpu_us', 'cpu_us', 'width', 'height', 'radius_px', 'gpu_frame'):
            self.assertIn(key, line)
        self.assertEqual((line['gpu_us'], line['cpu_us'], line['width'], line['radius_px']), (-1.0, 150, 64, 2.13))
        self.assertTrue(report['timing_unavailable'])
        self.assertEqual([m['draw_index'] for m in report['markers']], [3, 3])
        self.assertEqual(report['device'][0]['slots'], 397)

    def test_multiply_twin_passes(self):
        summary = runner.validate_case({'name': 'ao-on', 'ao': 1}, runner.parse_fixture(FIXTURE), runner.parse_trace(trace()))
        self.assertEqual((summary['law'], summary['ao_lines'], summary['ran'], summary['applied'], summary['gpu_timing']), ('multiply', 8, 8, 8, 'unavailable'))
        self.assertEqual(summary['cpu_us_median'], 154)
        self.assertEqual([p['frame'] for p in summary['pixel_frames']], [3, 6])

    def test_multiply_twin_needs_a_completed_pair_when_queries_exist(self):
        with self.assertRaises(AssertionError):
            runner.validate_case({'name': 'ao-on', 'ao': 1}, runner.parse_fixture(FIXTURE), runner.parse_trace(trace(unavailable=False)))
        summary = runner.validate_case({'name': 'ao-on', 'ao': 1}, runner.parse_fixture(FIXTURE), runner.parse_trace(trace(unavailable=False, gpu=310.5)))
        self.assertEqual((summary['gpu_timing'], summary['gpu_us_median']), ('timestamp', 310.5))

    def test_fault_twin(self):
        text = FIXTURE.replace('law=multiply darkened=1097 sentinel=1408 violations=0 max_drop=15 centre_mean=5.300 outer_mean=0.000 changed=1097',
                               'law=identity darkened=0 sentinel=1408 violations=0 max_drop=0 centre_mean=0.000 outer_mean=0.000 changed=0')
        text = text.replace('law=multiply darkened=1148 sentinel=1408 violations=0 max_drop=17 centre_mean=4.951 outer_mean=0.000 changed=1148',
                            'law=identity darkened=0 sentinel=1408 violations=0 max_drop=0 centre_mean=0.000 outer_mean=0.000 changed=0').replace('law=multiply', 'law=identity')
        fault = trace(attached=0, ran=0, reason='attach', device_reason='ps_3_0', applied=0, unavailable=False)
        summary = runner.validate_case({'name': 'ao-fault', 'ao': 1, 'fault': True}, runner.parse_fixture(text), runner.parse_trace(fault))
        self.assertEqual((summary['law'], summary['attach_reason'], summary['ran']), ('identity', 'ps_3_0', 0))
        # The same transcript is not a passing multiply twin: the chain did not run.
        with self.assertRaises(AssertionError):
            runner.validate_case({'name': 'ao-on', 'ao': 1}, runner.parse_fixture(FIXTURE), runner.parse_trace(fault))

    def test_off_twin_rejects_ao_lines(self):
        text = FIXTURE.replace('law=multiply darkened=1097 sentinel=1408 violations=0 max_drop=15 centre_mean=5.300 outer_mean=0.000 changed=1097',
                               'law=identity darkened=0 sentinel=1408 violations=0 max_drop=0 centre_mean=0.000 outer_mean=0.000 changed=0')
        text = text.replace('law=multiply darkened=1148 sentinel=1408 violations=0 max_drop=17 centre_mean=4.951 outer_mean=0.000 changed=1148',
                            'law=identity darkened=0 sentinel=1408 violations=0 max_drop=0 centre_mean=0.000 outer_mean=0.000 changed=0').replace('law=multiply', 'law=identity')
        summary = runner.validate_case({'name': 'ao-off', 'ao': 0}, runner.parse_fixture(text), runner.parse_trace(''))
        self.assertEqual(summary['ao_lines'], 0)
        with self.assertRaises(AssertionError):
            runner.validate_case({'name': 'ao-off', 'ao': 0}, runner.parse_fixture(text), runner.parse_trace(trace()))


if __name__ == '__main__':
    unittest.main()
