"""Negative witnesses for the consume-only live GPU evidence checker."""
import copy
import unittest
from run_linear_material_live import compare_cases, validate_case, ELIGIBLE, PIXEL_PROGRAMS


def cases():
    result = {}
    for owner in (0, 1):
        for taa in (0, 1):
            for material in (0, 1):
                result[f'ownership{owner}-taa{taa}-material{material}'] = {
                    'temporal_hashes': [['motion', 'depth']] * 12,
                    'held_references': 20 + material * 3,
                    'pixel_programs': list(PIXEL_PROGRAMS),
                    'rgba': [[1.877 if material and frame in ELIGIBLE else 1.] * 3 + [.75] for frame in range(12)],
                }
    return result


def sample_report():
    output = ['RESULT PASS checks=1 restorations=1 frames=12 depth_written=12 taa_reference_frames=12 motion_pixels=12 matched_pixels=10']
    trace = ['linear_material_variant kind=vs original=53a0a641107ed76c transform=0 create=00000000',
             'linear_material_variant kind=ps original=8759c7838bbc86c2 transform=0 create=00000000',
             'linear_material_variant kind=ps original=3b94320087e81945 transform=0 create=00000000',
             'motion_output_release held=23 released=1']
    for frame in range(12):
        eligible = int(frame in ELIGIBLE)
        output += [f'LINEAR_LIVE frame={frame} combined={eligible} ps={PIXEL_PROGRAMS[frame]} rgba=1,1,1,0.75',
                   f'MOTION_HASH frame={frame} motion=1234 depth=5678']
        trace += [f'motion_output_frame frame={frame} routed=1 depth_routed=1 matched={int(frame not in (0, 10))} apply_failures=0 restore_failures=0 taa_resolved=1',
                  f'linear_material_frame frame={frame} routed={eligible} refused={1-eligible} bind_failures=0']
    return '\n'.join(output), trace


class LiveMaterialReportTests(unittest.TestCase):
    def test_valid_control_twins(self):
        compare_cases(cases())
        output, trace = sample_report()
        self.assertEqual(validate_case(output, trace, True, True)['pixel_programs'], PIXEL_PROGRAMS)

    def test_rejects_lost_temporal_color_alpha_or_references(self):
        base = cases()
        for failure in ('temporal', 'refcount', 'fallback', 'alpha', 'activation', 'shared_activation', 'shared_schedule', 'reset'):
            changed = copy.deepcopy(base)
            item = changed['ownership0-taa1-material1']
            if failure == 'temporal': item['temporal_hashes'][3] = ['wrong', 'depth']
            elif failure == 'refcount': item['held_references'] -= 1
            elif failure == 'fallback': item['rgba'][9][0] += .1
            elif failure == 'alpha': item['rgba'][0][3] += .1
            elif failure == 'activation': item['rgba'][0][0] = 1.
            elif failure == 'shared_activation': item['rgba'][1][0] = 1.
            elif failure == 'shared_schedule': item['pixel_programs'][1] = '462342e3e5781384'
            else: item['rgba'][10][0] = 1.
            with self.subTest(failure=failure), self.assertRaises(AssertionError):
                compare_cases(changed)
        output, trace = sample_report()
        for failure in ('missing_shared_variant', 'split_variant', 'split_admitted'):
            changed = list(trace)
            if failure == 'missing_shared_variant': del changed[2]
            elif failure == 'split_variant': changed[2] = changed[2].replace('3b94320087e81945', '462342e3e5781384')
            else: changed = [line.replace('routed=0 refused=1', 'routed=1 refused=0') if line.startswith('linear_material_frame frame=9 ') else line for line in changed]
            with self.subTest(failure=failure), self.assertRaises(AssertionError):
                validate_case(output, changed, True, True)


if __name__ == '__main__':
    unittest.main()
