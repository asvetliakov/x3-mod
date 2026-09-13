"""Negative witnesses for the consume-only live GPU evidence checker."""
import copy
import unittest
from run_linear_material_live import compare_cases, validate_case, ELIGIBLE, PIXEL_PROGRAMS, VERTEX_PROGRAMS, BUMP_FRAMES, MATCHED, FRAME_COUNT


def cases():
    result = {}
    for owner in (0, 1):
        for taa in (0, 1):
            for material in (0, 1):
                result[f'ownership{owner}-taa{taa}-material{material}'] = {
                    'temporal_hashes': [['motion', 'depth']] * FRAME_COUNT,
                    'held_references': 20 + material * 5,
                    'pixel_programs': list(PIXEL_PROGRAMS),
                    'vertex_programs': list(VERTEX_PROGRAMS),
                    'rgba': [[1.877 if material and frame in ELIGIBLE else 1.] * 3 + [.75] for frame in range(FRAME_COUNT)],
                }
    return result


def sample_report():
    output = ['RESULT PASS checks=1 restorations=1 frames=24 depth_written=24 taa_reference_frames=24 motion_pixels=24 matched_pixels=17']
    trace = ['linear_material_variant kind=vs original=53a0a641107ed76c transform=0 create=00000000',
             'linear_material_variant kind=ps original=8759c7838bbc86c2 transform=0 create=00000000',
             'linear_material_variant kind=ps original=3b94320087e81945 transform=0 create=00000000',
             'linear_material_variant kind=vs original=4944d81dfe531b37 transform=0 create=00000000',
             'linear_material_variant kind=ps original=ca6bfa4a6cca7e2a transform=0 create=00000000',
             'motion_output_release held=25 released=1']
    for frame in range(FRAME_COUNT):
        eligible = int(frame in ELIGIBLE)
        output += [f'LINEAR_LIVE frame={frame} combined={eligible} vs={VERTEX_PROGRAMS[frame]} ps={PIXEL_PROGRAMS[frame]} rgba=1,1,1,0.75',
                   f'MOTION_HASH frame={frame} motion=1234 depth=5678']
        trace += [f'motion_output_frame frame={frame} routed=1 depth_routed=1 matched={int(frame in MATCHED)} apply_failures=0 restore_failures=0 taa_resolved=1',
                  f'linear_material_frame frame={frame} routed={eligible} refused={1-eligible} bind_failures=0 bump_routed={int(eligible and frame in BUMP_FRAMES)}']
    return '\n'.join(output), trace


class LiveMaterialReportTests(unittest.TestCase):
    def test_valid_control_twins(self):
        compare_cases(cases())
        output, trace = sample_report()
        self.assertEqual(validate_case(output, trace, True, True)['pixel_programs'], PIXEL_PROGRAMS)

    def test_rejects_lost_temporal_color_alpha_or_references(self):
        base = cases()
        for failure in ('temporal', 'refcount', 'fallback', 'alpha', 'activation', 'shared_activation', 'shared_schedule', 'bump_activation', 'bump_refusal', 'bump_schedule', 'bump_reset', 'reset'):
            changed = copy.deepcopy(base)
            item = changed['ownership0-taa1-material1']
            if failure == 'temporal': item['temporal_hashes'][3] = ['wrong', 'depth']
            elif failure == 'refcount': item['held_references'] -= 1
            elif failure == 'fallback': item['rgba'][9][0] += .1
            elif failure == 'alpha': item['rgba'][0][3] += .1
            elif failure == 'activation': item['rgba'][0][0] = 1.
            elif failure == 'shared_activation': item['rgba'][1][0] = 1.
            elif failure == 'shared_schedule': item['pixel_programs'][1] = '462342e3e5781384'
            elif failure == 'bump_activation': item['rgba'][12][0] = 1.
            elif failure == 'bump_refusal': item['rgba'][13][0] += .1
            elif failure == 'bump_schedule': item['vertex_programs'][12] = '53a0a641107ed76c'
            elif failure == 'bump_reset': item['rgba'][21][0] = 1.
            else: item['rgba'][10][0] = 1.
            with self.subTest(failure=failure), self.assertRaises(AssertionError):
                compare_cases(changed)
        output, trace = sample_report()
        for failure in ('missing_shared_variant', 'split_variant', 'split_admitted', 'bump_counter', 'bump_negative_admitted', 'missing_bump_variant'):
            changed = list(trace)
            if failure == 'missing_shared_variant': del changed[2]
            elif failure == 'split_variant': changed[2] = changed[2].replace('3b94320087e81945', '462342e3e5781384')
            elif failure == 'split_admitted': changed = [line.replace('routed=0 refused=1', 'routed=1 refused=0') if line.startswith('linear_material_frame frame=9 ') else line for line in changed]
            elif failure == 'bump_counter': changed = [line.replace('bump_routed=1', 'bump_routed=0') if line.startswith('linear_material_frame frame=12 ') else line for line in changed]
            elif failure == 'bump_negative_admitted': changed = [line.replace('routed=0 refused=1', 'routed=1 refused=0') if line.startswith('linear_material_frame frame=19 ') else line for line in changed]
            else: del changed[4]
            with self.subTest(failure=failure), self.assertRaises(AssertionError):
                validate_case(output, changed, True, True)


if __name__ == '__main__':
    unittest.main()
