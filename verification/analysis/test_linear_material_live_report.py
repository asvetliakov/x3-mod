"""Negative witnesses for the consume-only live GPU evidence checker."""
import copy
import unittest
from run_linear_material_live import compare_cases, ELIGIBLE


def cases():
    result = {}
    for owner in (0, 1):
        for taa in (0, 1):
            for material in (0, 1):
                result[f'ownership{owner}-taa{taa}-material{material}'] = {
                    'temporal_hashes': [['motion', 'depth']] * 12,
                    'held_references': 20 + material * 2,
                    'rgba': [[1.877 if material and frame in ELIGIBLE else 1.] * 3 + [.75] for frame in range(12)],
                }
    return result


class LiveMaterialReportTests(unittest.TestCase):
    def test_valid_control_twins(self):
        compare_cases(cases())

    def test_rejects_lost_temporal_color_alpha_or_references(self):
        base = cases()
        for failure in ('temporal', 'refcount', 'fallback', 'alpha', 'activation', 'reset'):
            changed = copy.deepcopy(base)
            item = changed['ownership0-taa1-material1']
            if failure == 'temporal': item['temporal_hashes'][3] = ['wrong', 'depth']
            elif failure == 'refcount': item['held_references'] -= 1
            elif failure == 'fallback': item['rgba'][9][0] += .1
            elif failure == 'alpha': item['rgba'][0][3] += .1
            elif failure == 'activation': item['rgba'][0][0] = 1.
            else: item['rgba'][10][0] = 1.
            with self.subTest(failure=failure), self.assertRaises(AssertionError):
                compare_cases(changed)


if __name__ == '__main__':
    unittest.main()
